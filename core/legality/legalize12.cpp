#include <algorithm>
#include <string>

#include "core/data/gen12.h"
#include "core/data/language.h"
#include "core/data/moves.h"
#include "core/data/names.h"
#include "core/legality/encounters_gb.h"
#include "core/legality/legalize.h"
#include "core/pokemon/edit_gb.h"
#include "core/pokemon/gb_common.h"

// Auto-Legalize for Generation I and II (formats 1 and 2), encounter driven
// like the later generations: the requested species, level, moves and
// shininess/gender are kept, an encounter that can produce them is chosen
// (wild, static, Day-Care egg, NPC trade, event gift, in that order) and the
// data the games derive from it is rebuilt: types and catch rate (Gen I),
// held item, Crystal's caught data, fixed DVs, EXP, PP and names. What cannot
// be reconciled (moves nothing can teach, a level below every encounter) is
// reported instead of changed.
namespace pktool::legality {
namespace {

namespace gbn = pktool::legality::gb;
using pokemon::Pokemon;
namespace editgb = pokemon::editgb;

size_t utf8Length(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) n += (c & 0xC0) != 0x80 ? 1 : 0;
    return n;
}

std::string cut(const std::string& s, size_t chars) {
    size_t count = 0;
    size_t end = 0;
    while (end < s.size() && count < chars) {
        ++end;
        while (end < s.size() && (static_cast<unsigned char>(s[end]) & 0xC0) == 0x80) ++end;
        ++count;
    }
    return s.substr(0, end);
}

// A time of day (1 morning, 2 day, 3 night) the encounter allows, keeping `current` if it does.
uint8_t timeFor(const gbn::Candidate& c, uint8_t current) {
    if (current >= 1 && current <= 3 && (c.time == 0 || (c.time & (1 << current)))) return current;
    for (uint8_t t = 1; t <= 3; ++t) {
        if (c.time == 0 || (c.time & (1 << t))) return t;
    }
    return 2;
}

}  // namespace

uint8_t minCreateLevelGb(uint8_t format, uint16_t species) {
    const uint8_t lowest = gbn::lowestEncounterLevel(format, species, gbn::versionMask(format, 0, true));
    return lowest == 0 ? 5 : lowest;
}

LegalizeResult legalizeGb(const Pokemon& requested, int /*effort*/) {
    LegalizeResult out;
    Pokemon& p = out.pokemon;
    p = requested;
    const uint8_t format = p.format;
    auto change = [&](const std::string& text) { out.changes.push_back(text); };
    const uint16_t maxSpecies = format == 1 ? data::kMaxSpeciesGen1 : data::kMaxSpeciesGen2;
    if (p.species == 0 || p.species > maxSpecies) {
        out.unresolved.push_back(format == 1 ? "Species does not exist in Gen I" : "Species does not exist in Gen II");
        out.report = check(p);
        return out;
    }

    // ----- Names and EXP
    const bool japanese = p.language == 1;
    if (utf8Length(p.trainer.name) > (japanese ? 5u : 7u) && p.trainer.name != "*") {
        p.trainer.name = cut(p.trainer.name, japanese ? 5 : 7);
        change("OT name shortened");
    }
    if (utf8Length(p.nickname) > (japanese ? 5u : 10u)) {
        p.nickname = cut(p.nickname, japanese ? 5 : 10);
        change("Nickname shortened");
    }
    if (pokemon::gb::levelFor(format, p.species, p.exp) != p.level) {
        p.exp = pokemon::gb::expFor(format, p.species, p.level);
        change("EXP set to match level " + std::to_string(p.level));
    }
    if (p.isEgg && p.level != 5) {
        editgb::setLevel(p, 5);
        change("An egg is level 5");
    }

    // ----- Species-dependent fields
    if (format == 1) {
        const data::PersonalGb* rb = data::personalGb1(p.species, false);
        const data::PersonalGb* y = data::personalGb1(p.species, true);
        const bool typesOk = (p.types[0] == rb->types[0] && p.types[1] == rb->types[1]) ||
                             (p.types[0] == y->types[0] && p.types[1] == y->types[1]);
        if (!typesOk) {
            p.types[0] = rb->types[0];
            p.types[1] = rb->types[1];
            change("Types set to the species'");
        }
        gbn::Analysis probe;
        gbn::analyse(p, probe);
        if (!gbn::catchRateFits(p, probe.line) && !probe.tradeback && !(p.species == 25 && p.catchRate == 0xA3)) {
            p.catchRate = rb->catchRate;
            change("Catch rate set to " + std::to_string(p.catchRate));
        }
    } else if (p.heldItem != 0) {
        const uint8_t item = data::itemToGen2(p.heldItem);
        if (item == 0 || !data::isHeldItem2(item)) {
            p.heldItem = 0;
            change("Removed an item that cannot be held");
        }
    }

    // ----- Moves: no duplicates, PP within limits
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < i; ++j) {
            if (p.moves[i] != 0 && p.moves[i] == p.moves[j]) {
                change(std::string("Removed the duplicate ") + data::moveName(p.moves[i]));
                p.moves[i] = 0;
                p.pp[i] = 0;
                p.ppUps[i] = 0;
            }
        }
        if (p.moves[i] == 0) continue;
        if (p.ppUps[i] > 3) {
            p.ppUps[i] = 3;
            change("PP Ups limited to 3");
        }
        const uint8_t max = data::maxPpGb(format, p.moves[i], p.ppUps[i]);
        if (p.pp[i] > max) p.pp[i] = max;
    }
    bool anyMove = false;
    for (int i = 0; i < 4; ++i) anyMove = anyMove || p.moves[i] != 0;
    if (!anyMove) {
        uint16_t moves[4];
        gbn::defaultMoves(format, p.species, p.level, p.originGame == 38 || p.originGame == 41, moves);
        for (int i = 0; i < 4; ++i) editgb::setMove(p, i, moves[i]);
        change("Gave it the moves a wild one of that level knows");
    }

    // ----- Choose the encounter
    gbn::Analysis a;
    gbn::analyse(p, a);
    const gbn::Candidate* chosen = nullptr;
    for (const gbn::Candidate* c : a.ordered) {
        bool all = true;
        for (int i = 0; i < 4 && all; ++i) all = gbn::moveLearnable(p, a, p.moves[i], c);
        if (all) {
            chosen = c;
            break;
        }
    }
    if (chosen == nullptr) {
        if (a.ordered.empty()) {
            if (a.found.empty()) {
                out.unresolved.push_back("No encounter exists for this Pok\xC3\xA9mon in the games");
            } else if (a.levelTooLow && a.lowest > 0) {
                out.unresolved.push_back("Needs at least level " + std::to_string(a.lowest) + " to exist");
            } else {
                out.unresolved.push_back("No encounter fits its trade OT, DVs, shininess or gender");
            }
        } else {
            for (int i = 0; i < 4; ++i) {
                if (!gbn::moveLearnable(p, a, p.moves[i], a.ordered.front())) {
                    out.unresolved.push_back(std::string("Cannot learn ") + data::moveName(p.moves[i]));
                }
            }
        }
        out.report = check(p);
        return out;
    }

    // ----- Apply it
    if (chosen->source == gbn::Source::Trade) {
        if (p.trainer.name != "*") {
            p.trainer.name = "*";
            change("OT set to the in-game trade's");
        }
        if (format == 2 && chosen->row >= 0) {
            const data::TradeGb2& t = data::generated::kTrades2[chosen->row];
            if (p.trainer.tid != t.tid) {
                p.trainer.tid = t.tid;
                change("Trainer ID set to the trade's");
            }
            const char* nick = t.nicknames[p.language == 1 ? 1 : 0];
            if (nick[0] != '\0' && p.nickname != nick) {
                p.nickname = nick;
                p.isNicknamed = true;
                change("Nickname set to the trade's");
            }
        }
    }
    if (format == 2) {
        if (chosen->dvs != 0xFFFF && gbn::dvWord(p) != chosen->dvs) {
            pokemon::gb::unpackDvs(chosen->dvs, p.ivs);
            editgb::refreshDerived(p);
            change("DVs set to the encounter's");
        }
        if ((chosen->flags & data::kStaticShinyAlways) && !p.shiny && editgb::setShiny(p, true) == editgb::Result::Ok) {
            change("Made it shiny, as this encounter always is");
        }
        if ((chosen->flags & data::kStaticShinyNever) && p.shiny && editgb::setShiny(p, false) == editgb::Result::Ok) {
            change("Made it not shiny, as this encounter never is");
        }
        if (chosen->gender != 255 && static_cast<uint8_t>(p.gender) != chosen->gender &&
            editgb::setGender(p, static_cast<pokemon::Gender>(chosen->gender)) == editgb::Result::Ok) {
            change("Gender set to the encounter's");
        }
        const bool crystal = p.originGame == 41 || a.crystalData;
        if (crystal && !p.isEgg) {
            uint8_t level = p.met.metLevel;
            uint8_t location = p.met.metLocation;
            uint8_t time = p.met.encounterType;
            switch (chosen->source) {
                case gbn::Source::Wild:
                case gbn::Source::Static:
                    if (level < chosen->levelMin || level > chosen->levelMax || level > p.level) {
                        level = std::min<uint8_t>(std::max<uint8_t>(chosen->levelMin, 1), std::min(p.level, chosen->levelMax));
                    }
                    location = chosen->location != 0 ? chosen->location : location;
                    time = timeFor(*chosen, time);
                    break;
                case gbn::Source::Egg:
                    level = 1;
                    location = gbn::kHatchLocationCrystal;
                    time = timeFor(*chosen, time);
                    break;
                case gbn::Source::Trade:
                    location = gbn::kTradeLocationCrystal;
                    level = std::min<uint8_t>(std::max<uint8_t>(level, 1), p.level);
                    time = timeFor(*chosen, time);
                    break;
                case gbn::Source::Gift:
                    location = chosen->location != 0 ? chosen->location : location;
                    level = std::min<uint8_t>(std::max<uint8_t>(chosen->levelMin, 1), p.level);
                    time = timeFor(*chosen, time);
                    break;
            }
            if (level != p.met.metLevel || location != p.met.metLocation || time != p.met.encounterType) {
                p.met.metLevel = level;
                p.met.metLocation = location;
                p.met.encounterType = time;
                change("Caught data set from the encounter (" + std::string(data::locationName2(location)) + ")");
            }
        }
    }
    out.report = check(p);
    for (const Finding& f : out.report.findings) {
        if (f.severity == Severity::Invalid) out.unresolved.push_back(f.text);
    }
    return out;
}

}  // namespace pktool::legality
