// LEGALIZE for Pokémon in Gen III saves (p.format == 3): a real R/S/E/FR/LG
// encounter (or hatching from a Day-Care egg), a Method 1 PID/IV frame that
// keeps the requested nature, shininess, gender and ability, and the Gen III
// rules for balls, items, EVs and PP.

#include <algorithm>

#include "core/data/encounters3.h"
#include "core/data/events3.h"
#include "core/data/gen3.h"
#include "core/data/learn3.h"
#include "core/data/learn4.h"
#include "core/data/moves.h"
#include "core/data/names.h"
#include "core/data/personal.h"
#include "core/legality/events3_check.h"
#include "core/legality/legalize.h"
#include "core/legality/pid_iv.h"
#include "core/legality/roamer.h"
#include "core/legality/rng4.h"
#include "core/pokemon/edit4.h"
#include "core/pokemon/cxd3.h"
#include "core/pokemon/gift3.h"

namespace pktool::legality {
namespace {

using pokemon::Pokemon;

constexpr uint8_t kMaxBallGen3 = 12;
constexpr int kMaxEvTotal = 510;
constexpr uint8_t kLanguageJapanese = 1;
const char* const kEggName3 = "\xE3\x82\xBF\xE3\x83\x9E\xE3\x82\xB4";  // タマゴ: every Gen III egg

bool canBreed(uint16_t species) {
    const data::PersonalInfo* info = data::personal4(species);
    return info && (info->eggGroups[0] != data::kEggGroupUndiscovered ||
                    info->eggGroups[1] != data::kEggGroupUndiscovered);
}

// First stage of the species line that exists in Gen III (Gen IV babies such
// as Munchlax did not exist yet: Snorlax eggs hatch Snorlax).
uint16_t hatchSpecies3(uint16_t species) {
    uint16_t s = species;
    for (uint16_t pre = data::preEvolution4(s); pre != 0 && pre <= data::kMaxSpeciesGen3; pre = data::preEvolution4(s)) s = pre;
    return s;
}

bool canHatch3(uint16_t species) {
    for (uint16_t s = species; s != 0 && s <= data::kMaxSpeciesGen3; s = data::preEvolution4(s)) {
        if (canBreed(s)) return true;
    }
    for (uint16_t s = 1; s <= data::kMaxSpeciesGen3; ++s) {  // babies (Pichu, Azurill...) hatch from their evolutions
        if (data::preEvolution4(s) == species && canBreed(s)) return true;
    }
    return false;
}

uint8_t versionForBit3(uint8_t bit) {
    switch (bit) {
        case data::kVerSapphire: return version::kSapphire;
        case data::kVerRuby: return version::kRuby;
        case data::kVerEmerald: return version::kEmerald;
        case data::kVerFireRed: return version::kFireRed;
        default: return version::kLeafGreen;
    }
}

const char* gameName3(uint8_t v) {
    switch (v) {
        case version::kSapphire: return "Sapphire";
        case version::kRuby: return "Ruby";
        case version::kEmerald: return "Emerald";
        case version::kFireRed: return "FireRed";
        default: return "LeafGreen";
    }
}

bool isFrlg(uint8_t v) {
    return v == version::kFireRed || v == version::kLeafGreen;
}

bool hatched3(const Pokemon& p) {
    return !p.isEgg && p.met.metLevel == 0 && p.met.metLocation != location3::kFateful;
}

bool needsCorrelatedPid3(const Pokemon& p) {
    return !p.isEgg && !hatched3(p) && !p.met.fatefulEncounter && p.met.metLocation < location3::kGiftEgg &&
           p.originGame != version::kColosseumXD;
}

// True if the Pokémon is a roamer of its origin game (its PID/IVs follow the
// roamer rules, with the IV bug outside Emerald).
bool roamsGen3(const Pokemon& p) {
    const data::Encounter4* e = nullptr;
    return needsCorrelatedPid3(p) && findEncounter3Native(p, &e) == EncounterStatus::Found &&
           e->kind == data::kEncounterRoamer;
}

void hatchFromDayCare(Pokemon& p, LegalizeResult& out) {
    if (!isGen3Origin(p.originGame) || p.originGame == version::kColosseumXD) p.originGame = version::kEmerald;
    p.met.metLevel = 0;
    p.met.metLocation = isFrlg(p.originGame) ? location3::kFourIsland : location3::kRoute117;
    p.met.ball = data::kBallPoke;
    out.changes.push_back(std::string("Hatched from a Day-Care egg in ") + data::locationName3(p.met.metLocation));
}

// True if `move` is fine on `p` (no finding of the checker mentions it).
bool moveFits3(const Pokemon& p, uint16_t move) {
    Report report = checkGen3(p);
    const std::string name = data::moveName(move);
    for (const Finding& f : report.findings) {
        if (f.severity == Severity::Invalid && f.text.find(name) != std::string::npos) return false;
    }
    return true;
}

// The Pokémon comes from a Gen III event when nothing else can produce its
// species (Mew, Celebi, Jirachi, ...): the event's trainer, origin, met data and
// PID/IV method replace the requested ones; the requested level, nature,
// gender, shininess, moves, EVs and nickname are kept where the event allows.
bool useEvent3(Pokemon& p, LegalizeResult& out) {
    // The species' own event, else that of a pre-evolution; an event in the
    // requested language first, then one open to any language.
    int index = -1;
    uint16_t base = 0;
    for (uint16_t s = p.species; s != 0 && index < 0; s = data::preEvolution4(s)) {
        int bestScore = 3;
        for (int i = data::findEvent3(s); i >= 0; i = data::findEvent3(s, i + 1)) {
            const data::Event3& candidate = data::event3(i);
            if (candidate.flags & data::kEvEgg) continue;  // eggs are not a way to get a hatched species
            int score = candidate.language == p.language ? 0 : candidate.language == 0 ? 1 : 2;
            if (score < bestScore) {
                bestScore = score;
                index = i;
                base = s;
            }
        }
    }
    if (index < 0) return false;
    const data::Event3& e = data::event3(index);
    uint8_t level = std::max(p.level, e.level);
    if (base != p.species) level = std::max(level, data::evolveLevel4(p.species));

    pokemon::create4::Trainer trainer;
    trainer.name = p.trainer.name;
    trainer.tid = p.trainer.tid;
    trainer.sid = p.trainer.sid;
    trainer.gender = p.trainer.gender;
    trainer.language = p.language;
    trainer.version = isGen3Origin(p.originGame) && p.originGame != version::kColosseumXD ? p.originGame : version::kEmerald;

    // Relax the wishes one by one until the event's RNG can satisfy them.
    pokemon::gift3::Criteria want;
    want.nature = static_cast<int8_t>(p.nature);
    want.shiny = p.shiny ? 1 : 0;
    want.gender = p.gender == pokemon::Gender::Genderless ? int8_t{-1} : static_cast<int8_t>(p.gender);
    uint8_t raw[pokemon::pk3::kStoredSize];
    bool satisfied = false;
    Pokemon g = pokemon::gift3::receive(e, trainer, p.pid, want, raw, &satisfied);
    if (!satisfied) {
        want.gender = -1;
        g = pokemon::gift3::receive(e, trainer, p.pid, want, raw, &satisfied);
    }
    if (!satisfied && p.shiny) {
        want.shiny = -1;
        g = pokemon::gift3::receive(e, trainer, p.pid, want, raw, &satisfied);
        if (satisfied) out.unresolved.push_back(std::string(e.title) + " cannot be shiny");
    }
    if (!satisfied) {
        want.nature = -1;
        g = pokemon::gift3::receive(e, trainer, p.pid, want, raw, &satisfied);
        out.unresolved.push_back(std::string("The event's RNG has no ") + data::natureName(p.nature) + " " +
                                 (p.shiny ? "shiny " : "") + "frame");
    }

    Pokemon requested = p;
    p = g;
    if (base != requested.species) {  // a later stage of the event's species
        pokemon::edit4::setSpecies(p, requested.species);
    }
    pokemon::edit4::setLevel(p, level);
    for (int s = 0; s < pokemon::kStatCount; ++s) p.evs[s] = requested.evs[s];
    if (p.heldItem == 0) p.heldItem = requested.heldItem;
    if (requested.isNicknamed) {
        p.nickname = requested.nickname;
        p.isNicknamed = true;
    }
    // Requested moves first (when this Pokémon could know them), then the event's own.
    for (int i = 0; i < 4; ++i) p.moves[i] = p.pp[i] = p.ppUps[i] = 0;
    int slot = 0;
    auto place = [&](uint16_t move) {
        if (slot >= 4 || move == 0) return;
        for (int i = 0; i < slot; ++i) {
            if (p.moves[i] == move) return;
        }
        pokemon::Pokemon trial = p;
        pokemon::edit4::setMove(trial, slot, move);
        if (!moveFits3(trial, move)) return;
        pokemon::edit4::setMove(p, slot++, move);
    };
    for (int i = 0; i < 4; ++i) place(requested.moves[i]);
    for (int i = 0; i < 4; ++i) place(e.moves[i]);
    out.changes.push_back(std::string("Received as the event ") + e.title + ": trainer, origin, PID and IVs come from it");
    if (level > requested.level) out.changes.push_back("Level raised to " + std::to_string(level) + " (the event's level)");
    return true;
}

// Fallback after the events: a Colosseum/XD Pokémon (starters, gifts, trades,
// Poké Spots, Shadow Pokémon) of the species line, generated with the save's
// trainer IDs so that its XD-RNG frame and the shadow locks hold.
bool useColoXd3(Pokemon& p, LegalizeResult& out, int preferLocation = -1) {
    static const uint8_t kOrder[] = {data::kGcColoStarter, data::kGcColoGift, data::kGcXdGift, data::kGcXdTrade,
                                     data::kGcXdPokeSpot, data::kGcXdShadow, data::kGcColoShadow};
    pokemon::create4::Trainer trainer;
    trainer.name = p.trainer.name;
    trainer.tid = p.trainer.tid;
    trainer.sid = p.trainer.sid;
    trainer.gender = p.trainer.gender;
    trainer.language = p.language;
    trainer.version = isGen3Origin(p.originGame) && p.originGame != version::kColosseumXD ? p.originGame : version::kEmerald;

    for (int pass = preferLocation >= 0 ? 0 : 1; pass < 2; ++pass) {
      for (uint8_t kind : kOrder) {
        for (uint16_t s = p.species; s != 0; s = data::preEvolution4(s)) {
            for (int i = 0; i < data::gc3Count(); ++i) {
                const data::Gc3& e = data::gc3(i);
                if (e.species != s || e.kind != kind) continue;
                if (pass == 0 && e.location != preferLocation) continue;  // the Pokémon's own encounter first
                uint8_t level = std::max(p.level, e.level);
                if (s != p.species) level = std::max(level, data::evolveLevel4(p.species));
                // Relax the wishes one by one until the frames can satisfy them.
                pokemon::gift3::Criteria want;
                want.nature = static_cast<int8_t>(p.nature);
                want.shiny = p.shiny ? 1 : 0;
                want.gender = p.gender == pokemon::Gender::Genderless ? int8_t{-1} : static_cast<int8_t>(p.gender);
                uint8_t raw[pokemon::pk3::kStoredSize];
                bool satisfied = false;
                Pokemon g = pokemon::cxd3::create(i, trainer, p.pid, want, raw, &satisfied);
                if (g.species == 0) continue;
                if (!satisfied) {
                    want.gender = -1;
                    g = pokemon::cxd3::create(i, trainer, p.pid, want, raw, &satisfied);
                }
                if (!satisfied && p.shiny) {
                    want.shiny = -1;
                    g = pokemon::cxd3::create(i, trainer, p.pid, want, raw, &satisfied);
                    if (satisfied) out.unresolved.push_back(pokemon::cxd3::title(e) + " cannot be shiny");
                }
                if (!satisfied) {
                    want.nature = -1;
                    g = pokemon::cxd3::create(i, trainer, p.pid, want, raw, &satisfied);
                    out.unresolved.push_back(std::string("No ") + data::natureName(p.nature) + " " + (p.shiny ? "shiny " : "") +
                                             "frame for this Colosseum/XD Pokémon");
                }
                Pokemon requested = p;
                p = g;
                if (s != requested.species) pokemon::edit4::setSpecies(p, requested.species);
                pokemon::edit4::setLevel(p, level);
                for (int k = 0; k < pokemon::kStatCount; ++k) p.evs[k] = requested.evs[k];
                if (requested.isNicknamed) {
                    p.nickname = requested.nickname;
                    p.isNicknamed = true;
                }
                // Requested moves first (when this Pokémon could know them), then the encounter's own.
                for (int k = 0; k < 4; ++k) p.moves[k] = p.pp[k] = p.ppUps[k] = 0;
                int slot = 0;
                auto place = [&](uint16_t move) {
                    if (slot >= 4 || move == 0) return;
                    for (int k = 0; k < slot; ++k) {
                        if (p.moves[k] == move) return;
                    }
                    pokemon::Pokemon trial = p;
                    pokemon::edit4::setMove(trial, slot, move);
                    if (!moveFits3(trial, move)) return;
                    pokemon::edit4::setMove(p, slot++, move);
                };
                for (int k = 0; k < 4; ++k) place(requested.moves[k]);
                for (int k = 0; k < 4; ++k) place(e.moves[k]);
                out.changes.push_back("Received as " + pokemon::cxd3::title(e) + ": trainer data, origin and PID/IVs come from " +
                                      "the Colosseum/XD RNG");
                if (level > requested.level) out.changes.push_back("Level raised to " + std::to_string(level));
                return true;
            }
        }
      }
    }
    return false;
}

// Gives the Pokémon a real Gen III encounter when its met data matches none.
// `event` is set when the Pokémon had to come from an event instead.
void fixEncounter3(Pokemon& p, LegalizeResult& out, bool& event) {
    if (p.originGame == version::kColosseumXD) {
        // A Colosseum/XD Pokémon whose frame or data the checker rejects is regenerated from its own encounter.
        if (!checkGen3(p).legal()) event = useColoXd3(p, out, p.met.metLocation);
        return;
    }
    if (p.met.fatefulEncounter || p.met.metLocation >= location3::kGiftEgg) return;
    if (hatched3(p)) {
        if (canHatch3(p.species)) return;
    } else if (findEncounter3Native(p) == EncounterStatus::Found) {
        return;
    }
    if (!isGen3Origin(p.originGame)) p.originGame = version::kEmerald;

    const uint8_t originBit = data::versionBit3(p.originGame);
    const uint8_t evolveLevel = data::evolveLevel4(p.species);
    const data::Encounter4* best = nullptr;
    uint16_t bestSpecies = 0;
    int bestScore = 1 << 30;
    for (uint16_t s = p.species; s != 0; s = data::preEvolution4(s)) {
        if (s != p.species && p.level < evolveLevel) break;  // could not have evolved yet
        const data::Encounter4* begin;
        const data::Encounter4* end;
        data::encounters3(s, &begin, &end);
        for (const data::Encounter4* e = begin; e != end; ++e) {
            if (e->minLevel > p.level) continue;
            int score = (e->versions & originBit) ? 0 : 1000;
            score += s == p.species ? 0 : 100;
            if (e->kind == data::kEncounterRoamer) score += 200;  // wild and static encounters first
            score += p.level - std::min<int>(e->maxLevel, p.level);
            if (score < bestScore) {
                bestScore = score;
                best = e;
                bestSpecies = s;
            }
        }
    }
    if (best != nullptr) {
        if (!(best->versions & originBit)) {
            uint8_t bit = 1;
            while (!(best->versions & bit)) bit = static_cast<uint8_t>(bit << 1);
            p.originGame = versionForBit3(bit);
            out.changes.push_back(std::string("Origin game set to ") + gameName3(p.originGame));
        }
        const bool roaming = best->kind == data::kEncounterRoamer;
        p.met.metLocation = roaming ? roamer::defaultLocation3(p.originGame) : best->location;
        p.met.metLevel = static_cast<uint8_t>(std::min<int>(best->maxLevel, p.level));
        out.changes.push_back(std::string(roaming ? "Met roaming at " : "Met at ") +
                              data::locationName3(p.met.metLocation) + ", Lv. " + std::to_string(p.met.metLevel) +
                              " (as " + data::speciesName(bestSpecies) + ")");
        return;
    }
    if (canHatch3(p.species)) {
        hatchFromDayCare(p, out);
        return;
    }
    const data::Encounter4* lowest = nullptr;
    lowestEncounter3(p.species, &lowest);
    std::string name = data::speciesName(p.species);
    if (lowest == nullptr) {
        event = useEvent3(p, out) || useColoXd3(p, out);
        if (!event) out.unresolved.push_back(name + " has no Gen III encounter (only from events)");
        return;
    }
    uint8_t bit = 1;
    while (!(lowest->versions & bit)) bit = static_cast<uint8_t>(bit << 1);
    std::string where = lowest->kind == data::kEncounterRoamer ? std::string("roaming") : data::locationName3(lowest->location);
    out.unresolved.push_back(name + " is only found from Lv. " + std::to_string(lowest->minLevel) + " (" + where + ", " +
                             gameName3(versionForBit3(bit)) + "): raise its level");
}

void fixEgg3(Pokemon& p, std::vector<std::string>& changes) {
    uint16_t species = hatchSpecies3(p.species);
    if (species != p.species && !p.met.fatefulEncounter) {
        pokemon::edit4::setSpecies(p, species);
        changes.push_back(std::string("Species set to ") + data::speciesName(species) + " (what hatches)");
    }
    if (p.level != 5) {  // Gen III eggs are level 5
        pokemon::edit4::setLevel(p, 5);
        changes.push_back("Level set to 5");
    }
    if (p.met.metLevel != 0) {
        p.met.metLevel = 0;
        changes.push_back("Met level set to 0 (not hatched yet)");
    }
    if (p.met.ball != data::kBallPoke) {
        p.met.ball = data::kBallPoke;
        changes.push_back("Ball set to Poke Ball");
    }
    if (p.nickname != kEggName3 || p.language != kLanguageJapanese) {
        p.nickname = kEggName3;  // the game stores every egg with the Japanese name
        p.language = kLanguageJapanese;
        p.isNicknamed = false;
        changes.push_back("Egg name set");
    }
    const data::PersonalInfo* info = data::personal4(p.species);
    if (info && p.friendship > info->hatchCycles) {
        p.friendship = info->hatchCycles;
        changes.push_back("Egg cycles set to " + std::to_string(info->hatchCycles));
    }
}

std::string ivText(const uint8_t ivs[pokemon::kStatCount]) {
    std::string s;
    for (int i = 0; i < pokemon::kStatCount; ++i) s += (i ? "/" : "") + std::to_string(ivs[i]);
    return s;
}

// A PID with the requested traits: a Method 1 frame (IVs as close as
// possible) for wild/static Pokémon, any PID for eggs and events.
bool rerollPid3(Pokemon& p, pokemon::Gender gender, uint8_t slot, int effort, PidIvMethod* method) {
    if (roamsGen3(p) && p.originGame != version::kEmerald) {  // truncated roamer IVs
        roamer::Request3 req;
        req.species = p.species;
        req.tid = p.trainer.tid;
        req.sid = p.trainer.sid;
        req.nature = p.nature;
        req.shiny = p.shiny;
        req.gender = gender;
        req.abilitySlot = slot;
        for (int s = 0; s < pokemon::kStatCount; ++s) req.targetIvs[s] = p.ivs[s];
        uint32_t pid = 0;
        uint8_t ivs[pokemon::kStatCount];
        if (!roamer::generate3(req, p.pid, &pid, ivs)) return false;
        p.pid = pid;
        for (int s = 0; s < pokemon::kStatCount; ++s) p.ivs[s] = ivs[s];
        *method = PidIvMethod::Method1;
    } else if (needsCorrelatedPid3(p)) {
        PidIvRequest req;
        req.species = p.species;
        req.tid = p.trainer.tid;
        req.sid = p.trainer.sid;
        req.nature = p.nature;
        req.shiny = p.shiny;
        req.gender = gender;
        req.abilitySlot = slot;
        for (int s = 0; s < pokemon::kStatCount; ++s) req.targetIvs[s] = p.ivs[s];
        // Method 1 first; Methods 2/4 only when they come closer to the IVs.
        // Static encounters (starters, legendaries, gifts) only use Method 1.
        const data::Encounter4* enc = nullptr;
        const bool wild = findEncounter3Native(p, &enc) == EncounterStatus::Found && enc->kind < data::kEncounterStatic;
        PidIvResult3 result = generatePidIv3(req, effort, p.pid, wild);
        if (!result.frame.found) return false;
        p.pid = result.frame.pid;
        for (int s = 0; s < pokemon::kStatCount; ++s) p.ivs[s] = result.frame.ivs[s];
        *method = result.method;
    } else {
        uint32_t pid;
        if (!pokemon::edit4::findPid(p.species, p.trainer.tid, p.trainer.sid, p.nature, p.shiny, gender, slot, p.pid, pid)) {
            return false;
        }
        p.pid = pid;
    }
    p.ability = data::ability3(p.species, slot);
    pokemon::edit4::refreshDerived(p);
    return true;
}

}  // namespace

uint8_t minCreateLevel3(uint16_t species) {
    if (species == 0 || species > data::kMaxSpeciesGen3) return 0;
    // From an egg it still has to reach its evolution level (Dragonite: 55).
    if (canHatch3(species)) return std::max<uint8_t>(1, data::evolveLevel4(species));
    uint8_t best = 0;
    for (uint16_t s = species; s != 0; s = data::preEvolution4(s)) {
        const data::Encounter4* begin;
        const data::Encounter4* end;
        data::encounters3(s, &begin, &end);
        for (const data::Encounter4* e = begin; e != end; ++e) {
            uint8_t level = s == species ? e->minLevel : std::max(e->minLevel, data::evolveLevel4(species));
            if (best == 0 || level < best) best = level;
        }
    }
    if (best == 0) {  // only from an event (Mew, Celebi, Jirachi...)
        for (uint16_t s = species; s != 0; s = data::preEvolution4(s)) {
            int event = data::findEvent3(s);
            if (event < 0) continue;
            return s == species ? data::event3(event).level : std::max(data::event3(event).level, data::evolveLevel4(species));
        }
        for (uint16_t s = species; s != 0 && best == 0; s = data::preEvolution4(s)) {  // Colosseum/XD only
            for (int i = 0; i < data::gc3Count(); ++i) {
                const data::Gc3& e = data::gc3(i);
                if (e.species != s || e.kind == data::kGcColoEReader) continue;
                uint8_t level = s == species ? e.level : std::max(e.level, data::evolveLevel4(species));
                if (best == 0 || level < best) best = level;
            }
        }
    }
    return best;
}

LegalizeResult legalizeGen3(const Pokemon& requested, int effort) {
    LegalizeResult out;
    Pokemon& p = out.pokemon;
    p = requested;
    const data::PersonalInfo* info = data::personal4(p.species);
    if (info == nullptr || p.species > data::kMaxSpeciesGen3) {
        out.unresolved.push_back("Species does not exist in Gen III");
        out.report = checkGen3(p);
        return out;
    }

    auto growth = static_cast<data::GrowthRate>(info->growthRate);
    if (p.exp > data::expForLevel(growth, 100)) {
        p.exp = data::expForLevel(growth, 100);
        out.changes.push_back("EXP capped at level 100");
    }
    p.level = data::levelForExp(growth, p.exp);
    bool fromEvent = false;  // the PID/IVs then come from the event's own RNG: never re-rolled
    if (p.isEgg) {
        fixEgg3(p, out.changes);
    } else {
        fixEncounter3(p, out, fromEvent);
        if (fromEvent) p.level = data::levelForExp(growth, p.exp);
        if (p.met.metLevel > p.level) {
            p.met.metLevel = p.level;
            out.changes.push_back("Met level lowered to " + std::to_string(p.level));
        }
    }

    if (p.met.ball == 0 || p.met.ball > kMaxBallGen3 || ((p.isEgg || hatched3(p)) && p.met.ball != data::kBallPoke)) {
        p.met.ball = data::kBallPoke;
        out.changes.push_back("Ball set to Poke Ball");
    }
    if (p.heldItem != 0 && data::itemToGen3(p.heldItem) == 0) {
        out.changes.push_back(std::string("Removed ") + data::itemName(p.heldItem) + " (not in Gen III)");
        p.heldItem = 0;
    }

    int total = 0;
    for (int s = 0; s < pokemon::kStatCount; ++s) total += p.evs[s];
    if (total > kMaxEvTotal) {
        while (total > kMaxEvTotal) {
            int maxIndex = static_cast<int>(std::max_element(p.evs, p.evs + pokemon::kStatCount) - p.evs);
            int cut = std::min<int>(total - kMaxEvTotal, p.evs[maxIndex]);
            p.evs[maxIndex] = static_cast<uint8_t>(p.evs[maxIndex] - cut);
            total -= cut;
        }
        out.changes.push_back("EVs trimmed to a total of 510");
    }
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < i; ++j) {
            if (p.moves[i] != 0 && p.moves[i] == p.moves[j]) {
                out.changes.push_back(std::string("Removed duplicate ") + data::moveName(p.moves[i]));
                p.moves[i] = 0;
                p.pp[i] = p.ppUps[i] = 0;
            }
        }
        if (p.ppUps[i] > 3) p.ppUps[i] = 3;
        uint8_t maxPp = data::maxPp3(p.moves[i], p.ppUps[i]);
        if (p.pp[i] > maxPp) p.pp[i] = maxPp;
    }

    // PID-derived traits: keep the requested ones.
    uint8_t slot = static_cast<uint8_t>(p.pid & 1);
    if (p.ability != data::ability3(p.species, slot) && p.ability == data::ability3(p.species, static_cast<uint8_t>(1 - slot))) {
        slot = static_cast<uint8_t>(1 - slot);
    }
    pokemon::Gender gender = p.gender;
    if (!pokemon::edit4::canHaveGender(p.species, gender)) gender = pokemon::edit4::genderFromPid(p.species, p.pid);
    bool pidOk = pokemon::edit4::genderFromPid(p.species, p.pid) == gender && (p.pid & 1) == slot &&
                 p.ability == data::ability3(p.species, slot) &&
                 (!needsCorrelatedPid3(p) ||
                  (roamsGen3(p) ? roamer::ivsMatch3(p.originGame, p.pid, p.ivs) : findPidIvMethod3(p.pid, p.ivs, p.species) != PidIvMethod::None));
    // A Colosseum/XD Pokemon that already passes its own check keeps its PID: its
    // ability bit is stored apart from the PID and is not a request to change it.
    if (!pidOk && !fromEvent && p.originGame == version::kColosseumXD) {
        Report gcReport;
        const uint16_t* gcMoves = nullptr;
        pidOk = checkGc3(p, gcReport, &gcMoves) && gcReport.legal();
    }
    if (!pidOk && !fromEvent) {
        uint8_t before[pokemon::kStatCount];
        std::copy(p.ivs, p.ivs + pokemon::kStatCount, before);
        PidIvMethod method = PidIvMethod::Method1;
        if (rerollPid3(p, gender, slot, effort, &method)) {
            out.changes.push_back("New PID for the requested nature/shininess/gender/ability");
            if (method == PidIvMethod::Method2 || method == PidIvMethod::Method4) {
                out.changes.push_back(std::string("PID/IVs from ") + methodName(method) + " (closer to the requested IVs)");
            }
            if (!std::equal(before, before + pokemon::kStatCount, p.ivs)) {
                out.changes.push_back("IVs " + ivText(before) + " -> " + ivText(p.ivs));
            }
        } else {
            out.unresolved.push_back("No valid PID for this nature/shiny/gender/ability combination");
        }
    }

    out.report = checkGen3(p);
    for (const Finding& f : out.report.findings) {
        if (f.severity == Severity::Invalid) out.unresolved.push_back(f.text);
    }
    return out;
}

}  // namespace pktool::legality
