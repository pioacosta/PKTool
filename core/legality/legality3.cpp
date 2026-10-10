// Gen III (R/S/E/FR/LG) legality checks, for Pokémon in Gen III saves. Same
// conservative approach as the other checkers: only what is impossible under
// every known rule is Invalid.

#include <algorithm>
#include <vector>

#include "core/data/encounters3.h"
#include "core/data/gen3.h"
#include "core/data/learn3.h"
#include "core/data/learn4.h"
#include "core/data/moves.h"
#include "core/data/names.h"
#include "core/data/personal.h"
#include "core/legality/events3_check.h"
#include "core/legality/legality.h"
#include "core/legality/pid_iv.h"
#include "core/legality/roamer.h"
#include "core/legality/ribbons_check.h"
#include "core/legality/trainer_check.h"
#include "core/legality/trade_check.h"
#include "core/pokemon/edit4.h"

namespace pktool::legality {
namespace {

using pokemon::Pokemon;

constexpr uint16_t kShedinja = 292;
constexpr int kMaxEvTotal = 510;
constexpr uint8_t kMaxBallGen3 = 12;  // Premier Ball

void add(Report& r, Severity s, std::string text) {
    r.findings.push_back({s, std::move(text)});
}

// Gen III has no egg location: hatched Pokémon have met level 0.
bool hatched3(const Pokemon& p) {
    return !p.isEgg && p.met.metLevel == 0 && p.met.metLocation != location3::kFateful;
}

// Without an incense, Marill and Wobbuffet eggs hatch as Marill and Wobbuffet
// in Gen III (Azurill and Wynaut need Sea/Lax Incense), so they can hatch
// below their evolution level.
bool hatchesEvolved3(uint16_t species) {
    return species == 183 || species == 202;
}

// Roaming legendaries: the games' IV bug leaves them without a PID/IV match.
bool isRoamer3(uint16_t species) {
    return species == 243 || species == 244 || species == 245 || species == 380 || species == 381;
}

// True if the species line appears at the met location in the origin game.
bool speciesAtLocation3(const Pokemon& p) {
    uint8_t bit = data::versionBit3(p.originGame);
    for (uint16_t s = p.species; s != 0; s = data::preEvolution4(s)) {
        const data::Encounter4* begin;
        const data::Encounter4* end;
        data::encounters3(s, &begin, &end);
        for (const data::Encounter4* e = begin; e != end; ++e) {
            if ((e->versions & bit) && (e->kind == data::kEncounterRoamer ? roamer::metOk3(p.originGame, p.met.metLocation) : e->location == p.met.metLocation)) return true;
        }
    }
    return false;
}

bool isEventMove(const uint16_t* eventMoves, uint16_t move) {
    for (int i = 0; eventMoves != nullptr && i < 4; ++i) {
        if (eventMoves[i] == move) return true;
    }
    return false;
}

// A static encounter (legendaries and gifts carry the fateful encounter
// flag) of the origin game at the met location and level.
bool isStatic3(const Pokemon& p) {
    uint8_t bit = data::versionBit3(p.originGame);
    if (bit == 0 || p.met.metLocation >= location3::kGiftEgg) return false;
    for (uint16_t s = p.species; s != 0; s = data::preEvolution4(s)) {
        const data::Encounter4* begin;
        const data::Encounter4* end;
        data::encounters3(s, &begin, &end);
        for (const data::Encounter4* e = begin; e != end; ++e) {
            if (e->kind != data::kEncounterStatic || !(e->versions & bit) || e->location != p.met.metLocation) continue;
            if (p.met.metLevel >= e->minLevel && p.met.metLevel <= e->maxLevel) return true;
        }
    }
    return false;
}

// `eventMoves`: the four moves of the event or Colosseum/XD encounter the
// Pokemon was identified with (legal by definition), or null.
// `uncertainOrigin`: an event or Colosseum/XD Pokemon PKTool could not
// identify, whose moves may be ones it does not know (a warning, not an error).
void checkMoves3(const Pokemon& p, const uint16_t* eventMoves, const std::vector<uint16_t>& extraMoves, bool uncertainOrigin,
                 Report& r) {
    int known = 0;
    const bool bred = p.isEgg || hatched3(p) || p.met.metLocation == location3::kGiftEgg;
    const uint16_t base = data::baseSpecies4(p.species);
    for (int i = 0; i < 4; ++i) {
        uint16_t move = p.moves[i];
        if (move == 0) continue;
        ++known;
        if (move > data::kMaxMoveGen3) {
            add(r, Severity::Invalid, "Move " + std::to_string(i + 1) + " does not exist in Gen III");
            continue;
        }
        for (int j = 0; j < i; ++j) {
            if (p.moves[j] == move) add(r, Severity::Invalid, std::string("Duplicate move: ") + data::moveName(move));
        }
        if (p.ppUps[i] > 3) add(r, Severity::Invalid, std::string("Too many PP Ups on ") + data::moveName(move));
        if (p.pp[i] > data::maxPp3(move, p.ppUps[i])) {
            add(r, Severity::Invalid, std::string("PP above maximum on ") + data::moveName(move));
        }
        // Smeargle copies any move with Sketch, except Struggle.
        constexpr uint16_t kSmeargle = 235;
        constexpr uint16_t kStruggle = 165;
        if (p.species == kSmeargle && move != kStruggle) continue;
        if (isEventMove(eventMoves, move)) continue;
        if (std::find(extraMoves.begin(), extraMoves.end(), move) != extraMoves.end()) continue;
        // The species and its pre-evolutions; hatched Pokémon may also have
        // inherited any level-up move of their base species. TM/HM and tutor
        // moves (FR/LG and Emerald, which every GBA game can reach by trading)
        // are open to all; egg moves only to Pokémon that came from an egg.
        bool learnable = false;
        bool levelTooLow = false;
        bool eggMoveOnly = false;
        for (uint16_t s = p.species; s != 0 && !learnable; s = data::preEvolution4(s)) {
            const data::LearnHit e = data::findLearn3(s, move);
            if (e == nullptr) continue;
            if (e->sources & (data::kLearnMachine | data::kLearnTutor)) {
                learnable = true;
            } else if ((e->sources & data::kLearnEgg) && bred) {
                learnable = true;
            } else if ((e->sources & data::kLearnLevelUp) && (e->minLevel <= p.level || (bred && s == base))) {
                learnable = true;
            } else if (e->sources & data::kLearnLevelUp) {
                levelTooLow = true;
            } else {
                eggMoveOnly = true;
            }
        }
        // Shedinja keeps what the Ninjask it was split from learned on evolving (Lv. 20 and up).
        constexpr uint16_t kNinjask = 291;
        if (!learnable && p.species == kShedinja) {
            const data::LearnHit e = data::findLearn3(kNinjask, move);
            learnable = e && (e->sources & data::kLearnLevelUp) && e->minLevel >= 20 && e->minLevel <= p.level;
        }
        if (learnable) continue;
        std::string name = data::moveName(move);
        if (uncertainOrigin) {
            add(r, Severity::Warning, name + ": event/Colosseum move of an unknown origin, unchecked");
        } else if (eggMoveOnly) {
            add(r, Severity::Invalid, name + " is an egg move, but this Pokemon did not hatch from an egg");
        } else if (levelTooLow) {
            add(r, Severity::Invalid, name + " is learned at a higher level");
        } else {
            add(r, Severity::Invalid, std::string(data::speciesName(p.species)) + " cannot learn " + name);
        }
    }
    if (known == 0 && !p.isEgg) add(r, Severity::Invalid, "No moves");
}

}  // namespace

EncounterStatus findEncounter3Native(const Pokemon& p, const data::Encounter4** match, uint16_t* matchedSpecies) {
    uint8_t bit = data::versionBit3(p.originGame);
    if (bit == 0 || p.isEgg || hatched3(p) || p.met.fatefulEncounter || p.met.metLocation >= location3::kGiftEgg) {
        return EncounterStatus::NotApplicable;
    }
    for (uint16_t s = p.species; s != 0; s = data::preEvolution4(s)) {
        const data::Encounter4* begin;
        const data::Encounter4* end;
        data::encounters3(s, &begin, &end);
        for (const data::Encounter4* e = begin; e != end; ++e) {
            if (!(e->versions & bit)) continue;
            if (e->kind == data::kEncounterRoamer ? !roamer::metOk3(p.originGame, p.met.metLocation) : e->location != p.met.metLocation) continue;
            if (p.met.metLevel < e->minLevel || p.met.metLevel > e->maxLevel) continue;
            if (match) *match = e;
            if (matchedSpecies) *matchedSpecies = s;
            return EncounterStatus::Found;
        }
    }
    return EncounterStatus::NotFound;
}

Report checkGen3(const Pokemon& p) {
    Report r;
    const data::PersonalInfo* info = data::personal4(p.species);
    if (info == nullptr || p.species > data::kMaxSpeciesGen3) {
        add(r, Severity::Invalid, "Species does not exist in Gen III");
        return r;
    }
    if (!isGen3Origin(p.originGame)) add(r, Severity::Invalid, "Unknown origin game");

    auto growth = static_cast<data::GrowthRate>(info->growthRate);
    if (p.exp > data::expForLevel(growth, 100)) add(r, Severity::Invalid, "EXP above the level 100 maximum");
    if (!p.isEgg && p.met.metLevel > p.level) add(r, Severity::Invalid, "Met level is above the current level");

    // Where it comes from.
    const uint16_t* eventMoves = nullptr;
    const data::Encounter4* encounter = nullptr;
    uint16_t encounterSpecies = 0;
    EncounterStatus status = EncounterStatus::NotApplicable;
    bool uncertainOrigin = false;
    std::vector<uint16_t> extraMoves;  // moves of event eggs this Pokemon could come from
    if (p.isEgg && p.met.metLocation == location3::kFateful) {  // an event egg (Pokemon Box, PCJP, Wish...)
        if (!checkEvent3(p, r, &eventMoves)) {
            add(r, Severity::Warning, "Gen III event egg not in the PKTool database, unchecked");
            uncertainOrigin = true;
        }
    } else if (p.isEgg) {
        if (p.met.metLevel != 0) add(r, Severity::Invalid, "Unhatched eggs have met level 0");
        if (p.friendship > info->hatchCycles) add(r, Severity::Invalid, "Egg cycles above the species' count");
    } else if (p.originGame == version::kColosseumXD) {
        if (!checkGc3(p, r, &eventMoves)) {
            add(r, Severity::Warning, "Colosseum/XD Pokemon matching no known encounter, unchecked");
            uncertainOrigin = true;
        }
    } else if (p.met.fatefulEncounter || p.met.metLocation == location3::kFateful) {
        if (!checkEvent3(p, r, &eventMoves)) {
            if (isStatic3(p)) {
                add(r, Severity::Valid, "Met: " + std::string(data::locationName3(p.met.metLocation)) + " (static)");
            } else {
                add(r, Severity::Warning, "Gen III event not in the PKTool database, unchecked");
                uncertainOrigin = true;
            }
        }
    } else if (isNpcTrade(p)) {
        checkNpcTrade(p, r);
    } else if (hatched3(p) || p.met.metLocation == location3::kGiftEgg) {
        add(r, Severity::Valid, "Hatched from an egg");
        eventEggMoves3(p, extraMoves);
        if (p.met.ball != data::kBallPoke) add(r, Severity::Invalid, "Hatched eggs must be in a Poke Ball");
    } else {
        status = findEncounter3Native(p, &encounter, &encounterSpecies);
        std::string where = data::locationName3(p.met.metLocation);
        if (status == EncounterStatus::Found) {
            add(r, Severity::Valid, "Met: " + (encounter->kind == data::kEncounterRoamer ? std::string("roaming") : where) +
                                        " (" + (encounter->kind >= data::kEncounterStatic ? "static" : "wild") + ", " +
                                        data::speciesName(encounterSpecies) + ")");
        } else if (speciesAtLocation3(p)) {
            add(r, Severity::Warning, "Unusual met level " + std::to_string(p.met.metLevel) + " at " + where);
        } else {
            add(r, Severity::Invalid, std::string(data::speciesName(p.species)) + " cannot be found at " + where);
        }
    }

    // Evolution level: certain for hatched Pokémon and those met as a pre-evolution.
    uint8_t evolveLevel = data::evolveLevel4(p.species);
    if (!p.isEgg && p.level < evolveLevel &&
        ((hatched3(p) && !hatchesEvolved3(p.species)) || (status == EncounterStatus::Found && encounterSpecies != p.species))) {
        add(r, Severity::Invalid, std::string(data::speciesName(p.species)) + " only evolves at level " +
                                      std::to_string(evolveLevel));
    }

    // Traits derived from the PID. The ability slot is its own bit in Gen III
    // but always equals the PID's low bit.
    if (p.species != kShedinja) {
        if (!pokemon::edit4::canHaveGender(p.species, p.gender)) {
            add(r, Severity::Invalid, "Gender impossible for this species");
        } else if (pokemon::edit4::genderFromPid(p.species, p.pid) != p.gender) {
            add(r, Severity::Invalid, "Gender does not match the PID");
        }
    }
    // (Colosseum/XD Pokemon pick their ability from the RNG, whatever the PID.)
    uint16_t expected = data::ability3(p.species, static_cast<uint8_t>(p.pid & 1));
    if (p.ability != expected && p.originGame != version::kColosseumXD) {
        add(r, Severity::Invalid, std::string("Ability should be ") + data::abilityName(expected));
    }

    // PID/IVs: wild and static Pokémon come from Methods 1, 2 or 4.
    bool correlated = !p.isEgg && !hatched3(p) && !p.met.fatefulEncounter && p.met.metLocation < location3::kGiftEgg &&
                      p.originGame != version::kColosseumXD;
    if (correlated) {
        PidIvMethod method = findPidIvMethod3(p.pid, p.ivs, p.species);
        if (encounter != nullptr && encounter->kind == data::kEncounterRoamer) {
            // Roamers: Method 1, with the games' truncated IVs outside Emerald.
            if (roamer::ivsMatch3(p.originGame, p.pid, p.ivs)) {
                add(r, Severity::Valid, p.originGame == version::kEmerald ? "PID/IV: Method 1 (roamer)"
                                                                          : "PID/IV: Method 1 roamer (8-bit IVs)");
            } else {
                add(r, Severity::Invalid, p.originGame == version::kEmerald
                                              ? "Roamers have Method 1 PID/IVs"
                                              : "Roamer PID/IVs: Method 1, and only the first IV byte outside Emerald");
            }
        } else if (method != PidIvMethod::None && method != PidIvMethod::CuteCharm) {
            add(r, Severity::Valid, std::string("PID/IV: ") + methodName(method));
        } else if (isRoamer3(p.species)) {
            add(r, Severity::Warning, "PID/IVs unchecked (roamer)");
        } else {
            add(r, Severity::Invalid, "PID/IVs not from the game's RNG");
        }
    }

    int evTotal = 0;
    for (int s = 0; s < pokemon::kStatCount; ++s) evTotal += p.evs[s];
    if (evTotal > kMaxEvTotal) add(r, Severity::Invalid, "EV total above 510");
    if (p.heldItem != 0 && data::itemToGen3(p.heldItem) == 0) add(r, Severity::Invalid, "Held item does not exist in Gen III");
    if (p.met.ball == 0 || p.met.ball > kMaxBallGen3) add(r, Severity::Invalid, "Invalid ball for Gen III");

    checkMoves3(p, eventMoves, extraMoves, uncertainOrigin, r);
    checkRibbons(p, r);
    checkTrainer(p, r);

    if (r.overall() == Severity::Valid && r.findings.empty()) add(r, Severity::Valid, "No problems found");
    return r;
}

}  // namespace pktool::legality
