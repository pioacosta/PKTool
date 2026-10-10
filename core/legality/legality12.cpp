#include <algorithm>
#include <string>
#include <vector>

#include "core/data/gen12.h"
#include "core/data/learn12.h"
#include "core/data/moves.h"
#include "core/data/names.h"
#include "core/legality/encounters_gb.h"
#include "core/legality/legality.h"
#include "core/pokemon/gb_common.h"
#include "core/text/gen12_text.h"

// Legality checks for Generation I and II Pokemon (PKHeX LegalityAnalysis for
// formats 1 and 2, simplified): species, level and EXP, types and catch rate
// (Gen I), held item (Gen II), the encounter (wild, static, trade, event gift,
// Day-Care egg), Crystal's caught data, moves, PP and names.
//
// Conservative like the other checkers: a finding is Invalid only when no rule
// of the game or of PKHeX allows the data; anything PKTool cannot verify
// (e.g. Pokérus values) is a Warning.
namespace pktool::legality {
namespace {

using pokemon::Pokemon;
namespace gbn = pktool::legality::gb;

void add(Report& r, Severity s, std::string text) {
    r.findings.push_back({s, std::move(text)});
}

const char* sourceName(gbn::Source source) {
    switch (source) {
        case gbn::Source::Wild: return "Wild encounter";
        case gbn::Source::Static: return "Static encounter";
        case gbn::Source::Trade: return "In-game trade";
        case gbn::Source::Gift: return "Event gift";
        case gbn::Source::Egg: return "Day-Care egg";
    }
    return "Encounter";
}

size_t utf8Length(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) n += (c & 0xC0) != 0x80 ? 1 : 0;
    return n;
}

}  // namespace

Report checkGb(const Pokemon& p) {
    Report r;
    const uint8_t format = p.format;
    const uint16_t maxSpecies = format == 1 ? data::kMaxSpeciesGen1 : data::kMaxSpeciesGen2;
    if (p.species == 0 || p.species > maxSpecies) {
        add(r, Severity::Invalid, format == 1 ? "Not a Generation I species" : "Not a Generation II species");
        return r;
    }
    const bool japanese = p.language == 1;

    // ----- Level and EXP
    if (p.level < 1 || p.level > 100) {
        add(r, Severity::Invalid, "Level is out of range");
    } else if (pokemon::gb::levelFor(format, p.species, p.exp) != p.level) {
        add(r, Severity::Invalid, "Level does not match EXP");
    } else {
        add(r, Severity::Valid, "Level matches EXP");
    }
    if (p.isEgg && p.level != 5) add(r, Severity::Invalid, "An egg is level 5");

    gbn::Analysis a;
    gbn::analyse(p, a);

    // ----- Generation I: types and catch rate; Generation II: held item
    if (format == 1) {
        const data::PersonalGb* rb = data::personalGb1(p.species, false);
        const data::PersonalGb* y = data::personalGb1(p.species, true);
        const bool typesOk = (rb && p.types[0] == rb->types[0] && p.types[1] == rb->types[1]) ||
                             (y && p.types[0] == y->types[0] && p.types[1] == y->types[1]);
        add(r, typesOk ? Severity::Valid : Severity::Invalid,
            typesOk ? "Types match the species" : "Types do not match the species");
        // Yellow's starter Pikachu (Light Ball) and its evolution keep catch rate 163.
        bool lightBall = false;
        if (p.catchRate == 0xA3) {
            for (const gbn::Candidate& c : a.found) {
                if (c.source == gbn::Source::Static && c.species == 25 && c.levelMin == 5 && (c.versions & data::kGbYellow)) {
                    lightBall = true;
                }
            }
        }
        if (gbn::catchRateFits(p, a.line) || lightBall) {
            add(r, Severity::Valid, "Catch rate matches the species line");
        } else if (a.tradeback) {
            add(r, Severity::Valid, p.catchRate == 0 ? "No held item: went through the Time Capsule"
                                                     : "Catch rate is a Gen II held item: went through the Time Capsule");
        } else {
            add(r, Severity::Invalid, "Catch rate does not match the species line");
        }
    } else if (p.heldItem != 0) {
        const uint8_t item = data::itemToGen2(p.heldItem);
        if (item == 0 || !data::isHeldItem2(item)) {
            add(r, Severity::Invalid, "That item cannot be held in Generation II");
        } else {
            add(r, Severity::Valid, "Held item can be held");
        }
    }

    // ----- Names
    const size_t nameLimit = japanese ? 5 : 7;
    const size_t nickLimit = japanese ? 5 : 10;
    if (p.trainer.name.empty()) {
        add(r, Severity::Invalid, "The original trainer name is empty");
    } else if (!a.tradeOt && utf8Length(p.trainer.name) > nameLimit) {
        add(r, Severity::Invalid, "The original trainer name is too long");
    }
    if (utf8Length(p.nickname) > nickLimit) add(r, Severity::Invalid, "The nickname is too long");

    // ----- Moves: each must have a source; a candidate may add its own moves
    int moveCount = 0;
    for (int i = 0; i < 4; ++i) moveCount += p.moves[i] != 0 ? 1 : 0;
    const gbn::Candidate* best = nullptr;
    for (const gbn::Candidate* c : a.ordered) {
        bool all = true;
        for (int i = 0; i < 4 && all; ++i) all = gbn::moveLearnable(p, a, p.moves[i], c);
        if (all) {
            best = c;
            break;
        }
    }
    bool movesLegalSomewhere = best != nullptr;
    if (a.ordered.empty()) {  // no encounter to blame: judge the moves alone
        movesLegalSomewhere = true;
        for (int i = 0; i < 4; ++i) movesLegalSomewhere = movesLegalSomewhere && gbn::moveLearnable(p, a, p.moves[i], nullptr);
    }
    if (best == nullptr && !a.ordered.empty()) best = a.ordered.front();

    // ----- Report the encounter
    if (a.ordered.empty()) {
        if (a.found.empty()) {
            add(r, Severity::Invalid, "No encounter exists for this Pok\xC3\xA9mon in the games");
        } else if (a.levelTooLow && a.lowest > 0 && a.usable.empty()) {
            add(r, Severity::Invalid, "Level is below the lowest encounter (" + std::to_string(a.lowest) + ")");
        } else {
            add(r, Severity::Invalid, "No encounter matches its data (trade OT, fixed DVs, shininess or gender)");
        }
    } else if (best->source == gbn::Source::Gift) {
        add(r, Severity::Valid, "Event gift (trainer, language and DVs match)");
    } else if (best->treeBad) {
        add(r, Severity::Invalid, "Headbutt tree not available for this Trainer ID");
    } else {
        std::string text = sourceName(best->source);
        if (a.crystalData && best->location != 0 && best->source != gbn::Source::Trade) {
            text += " at " + std::string(data::locationName2(best->location));
        }
        add(r, Severity::Valid, text);
        if (a.dataMismatch) add(r, Severity::Warning, "Caught data (location, level or time) does not match an encounter");
    }
    if (a.crystalData) {
        if (p.met.metLevel == 0 || p.met.metLevel > p.level) {
            if (!(p.met.metLevel == 1 && p.isEgg)) add(r, Severity::Invalid, "Met level is above the level");
        }
        if (p.met.encounterType < 1) add(r, Severity::Invalid, "Caught data has no time of day");
        if (p.met.metLocation == 0 && !p.isEgg) add(r, Severity::Warning, "Caught data has no location");
    }
    if (p.isEgg) {
        const data::PersonalGb* info = data::personalGb2(p.species, true);
        if (info && p.friendship > info->hatchCycles) add(r, Severity::Invalid, "Egg cycles exceed the species'");
        if (!gbn::canHatchGb(p.species)) add(r, Severity::Invalid, "This species does not hatch from eggs");
    }

    // ----- Move findings
    if (moveCount == 0) add(r, Severity::Invalid, "A Pok\xC3\xA9mon knows at least one move");
    bool movesOk = true;
    for (int i = 0; i < 4; ++i) {
        const uint16_t move = p.moves[i];
        if (move == 0) continue;
        for (int j = 0; j < i; ++j) {
            if (p.moves[j] == move) {
                add(r, Severity::Invalid, std::string("Duplicate move ") + data::moveName(move));
                movesOk = false;
            }
        }
        if (move > data::maxMove(format)) {
            add(r, Severity::Invalid, std::string("Move ") + data::moveName(move) + " does not exist in this generation");
            movesOk = false;
            continue;
        }
        bool eggOnly = false;
        if (!movesLegalSomewhere && !gbn::moveLearnable(p, a, move, best, &eggOnly)) {
            add(r, Severity::Invalid,
                std::string("Cannot learn ") + data::moveName(move) + (eggOnly ? " (egg move on a non-egg Pok\xC3\xA9mon)" : ""));
            movesOk = false;
        }
        if (p.ppUps[i] > 3) {
            add(r, Severity::Invalid, "PP Ups above 3");
            movesOk = false;
        } else if (p.pp[i] > data::maxPpGb(format, move, p.ppUps[i])) {
            add(r, Severity::Invalid, std::string("PP of ") + data::moveName(move) + " is above its maximum");
            movesOk = false;
        }
    }
    if (movesOk && moveCount > 0) add(r, Severity::Valid, "Moves can be learned");

    if (format == 2 && !p.isEgg && p.friendship == 0) add(r, Severity::Warning, "Friendship is 0");
    return r;
}

Report checkGen1(const Pokemon& p) {
    return checkGb(p);
}

Report checkGen2(const Pokemon& p) {
    return checkGb(p);
}

}  // namespace pktool::legality
