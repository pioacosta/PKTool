#include "core/legality/legality.h"

#include "core/data/encounters3.h"
#include "core/data/learn3.h"
#include "core/data/learn4.h"
#include "core/data/moves.h"
#include "core/data/names.h"
#include "core/data/personal.h"
#include "core/legality/events3_check.h"
#include "core/legality/pid_iv.h"
#include "core/legality/ribbons_check.h"
#include "core/legality/trainer_check.h"
#include "core/legality/trade_check.h"
#include "core/legality/pokewalker.h"
#include "core/legality/roamer.h"
#include "core/data/learn5.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/edit4.h"
#include "core/pokemon/gift4.h"
#include "core/util/bytes.h"

namespace pktool::legality {
namespace {

using pokemon::Pokemon;

constexpr uint16_t kShedinja = 292;
constexpr uint16_t kRotom = 479;

// Moves Rotom learns when changing into an appliance form (Pt, HG/SS); they
// stay when the form changes back.
bool isRotomFormMove(uint16_t move) {
    return move == 315 || move == 56 || move == 59 || move == 403 || move == 437;
}
constexpr uint16_t kMaxEvTotal = 510;

// Met location of Pokémon received in in-game (NPC) trades; they have fixed
// PIDs and IVs defined by the game.
constexpr uint16_t kLocationNpcTrade = 2001;

void add(Report& r, Severity s, std::string text) {
    r.findings.push_back({s, std::move(text)});
}

bool hatched(const Pokemon& p) {
    return p.met.eggLocation != 0;
}

// True if the card's template knows `move`.
bool eventHasMove(const data::Event4* card, uint16_t move) {
    if (card == nullptr) return false;
    for (int i = 0; i < 4; ++i) {
        if (util::readU16(card->templatePk4 + pokemon::pk4::offset::kMoves + i * 2) == move) return true;
    }
    return false;
}

// True if the species line has a static (fateful in-game) encounter.
bool hasStaticEncounter(uint16_t species) {
    for (uint16_t s = species; s != 0; s = data::preEvolution4(s)) {
        const data::Encounter4* begin;
        const data::Encounter4* end;
        data::encounters4(s, &begin, &end);
        for (const data::Encounter4* e = begin; e != end; ++e) {
            if (e->kind == data::kEncounterStatic) return true;
        }
    }
    return false;
}

void checkEvent(const Pokemon& p, const data::Event4* card, Report& r) {
    if (!p.met.fatefulEncounter || p.isEgg) return;
    if (card == nullptr) {
        std::string gen3 = isGen3Origin(p.originGame) ? describeGen3Origin(p) : std::string();
        if (!gen3.empty()) {
            add(r, Severity::Valid, "Gen III origin, Pal Park: " + gen3);
        } else if (!hasStaticEncounter(p.species)) {
            add(r, Severity::Warning, "Event not in the Wonder Card database");
        }
        return;
    }
    add(r, Severity::Valid, std::string("Event: ") + card->title);
    uint32_t templatePid = util::readU32(card->templatePk4 + pokemon::pk4::offset::kPid);
    if (templatePid <= 1 && p.shiny) {
        add(r, Severity::Invalid, "This event is never shiny");
    }
}

// The Pokemon Ranger Manaphy egg (PKHeX: PGT IsRangerManaphy): a fateful
// Manaphy egg from the Ranger (or traded as an egg, then marked as a link
// trade), either still an egg or hatched.
constexpr uint16_t kManaphy = 490;
constexpr uint16_t kLocationRanger = 3001;
constexpr uint16_t kLocationLinkTrade = 2002;

bool isRangerManaphy(const Pokemon& p) {
    if (p.species != kManaphy || !p.met.fatefulEncounter || p.format != 4) return false;
    if (!p.isEgg) return p.met.eggLocation == kLocationRanger || p.met.eggLocation == kLocationLinkTrade;
    return p.met.eggLocation == kLocationRanger &&
           (p.met.metLocation == kLocationLinkTrade || p.met.metLocation == 0);
}

uint32_t shinyValue(uint32_t pid) {
    return ((pid >> 16) ^ (pid & 0xFFFF)) >> 3;
}

// The game re-rolls a Manaphy PID that is shiny for its first trainer with the
// ARNG (up to 3 times). Undo it: the PID before must be a Method 1 frame whose
// shiny value is the current trainer's, unless the egg was traded.
bool isManaphyAntiShinyPid(const Pokemon& p, bool tradedEgg) {
    constexpr uint32_t kMult = 0x9638806Du;  // reverse ARNG
    constexpr uint32_t kAdd = 0x69C77F93u;
    const uint32_t current = shinyValue(p.pid);
    uint32_t pid = p.pid * kMult + kAdd;
    const uint32_t original = shinyValue(pid);
    if (original == current) return false;
    for (int i = 0; i < 3; ++i) {
        if (findPidIvMethod(pid, p.ivs) == PidIvMethod::Method1) {
            return tradedEgg || original == static_cast<uint32_t>((p.trainer.tid ^ p.trainer.sid) >> 3);
        }
        uint32_t earlier = pid * kMult + kAdd;
        if (shinyValue(earlier) != original) break;
        pid = earlier;
    }
    return false;
}

void checkManaphy(const Pokemon& p, Report& r) {
    add(r, Severity::Valid, "Event: Pokemon Ranger Manaphy egg");
    if (p.language == 0 || p.language == 6 || p.language >= 8) add(r, Severity::Invalid, "Language impossible for the Manaphy egg");
    if (!isGen4Origin(p.originGame)) add(r, Severity::Invalid, "The Manaphy egg is a Gen IV gift");
    if (p.met.ball != data::kBallPoke) add(r, Severity::Invalid, "The Manaphy egg is in a Poke Ball");
    const bool tradedEgg = !p.isEgg && p.met.eggLocation == kLocationLinkTrade;
    if (findPidIvMethod(p.pid, p.ivs) == PidIvMethod::Method1) {
        if (p.shiny && !tradedEgg) {
            add(r, Severity::Invalid, "The Manaphy egg is never shiny for its first trainer");
        } else {
            add(r, Severity::Valid, "PID/IV: Method 1");
        }
    } else if (isManaphyAntiShinyPid(p, tradedEgg)) {
        add(r, Severity::Valid, "PID/IV: anti-shiny re-roll");
    } else {
        add(r, Severity::Invalid, "PID/IVs not from the Manaphy egg's RNG");
    }
}

void checkMoves(const Pokemon& p, const data::Event4* card, const data::WalkerSlot* walker, Report& r) {
    int known = 0;
    for (int i = 0; i < 4; ++i) {
        uint16_t move = p.moves[i];
        if (move == 0) {
            continue;
        }
        ++known;
        if (move > data::kMaxMoveGen4) {
            add(r, Severity::Invalid, "Move " + std::to_string(i + 1) + " does not exist in Gen IV");
            continue;
        }
        for (int j = 0; j < i; ++j) {
            if (p.moves[j] == move) {
                add(r, Severity::Invalid, std::string("Duplicate move: ") + data::moveName(move));
            }
        }
        if (p.ppUps[i] > 3) {
            add(r, Severity::Invalid, std::string("Too many PP Ups on ") + data::moveName(move));
        }
        if (p.pp[i] > data::maxPp4(move, p.ppUps[i])) {
            add(r, Severity::Invalid, std::string("PP above maximum on ") + data::moveName(move));
        }

        // Search the species and its pre-evolutions. A Pokémon that hatched
        // can also have inherited any level-up move of its base species from
        // its father, whatever its level.
        bool learnable = false;
        bool levelTooLow = false;
        const bool bred = p.isEgg || hatched(p);
        const uint16_t base = data::baseSpecies4(p.species);
        for (uint16_t s = p.species; s != 0 && !learnable; s = data::preEvolution4(s)) {
            const data::LearnHit e = data::findLearn4(s, move);
            if (e == nullptr) {
                continue;
            }
            if (e->sources & (data::kLearnMachine | data::kLearnTutor | data::kLearnEgg)) {
                learnable = true;
            } else if (e->minLevel <= p.level || (bred && s == base)) {
                learnable = true;
            } else {
                levelTooLow = true;
            }
        }
        // Pokémon from Gen III (Pal Park) keep what they learned there,
        // including the FR/LG and Emerald tutors.
        for (uint16_t s = p.species; isGen3Origin(p.originGame) && s != 0 && !learnable; s = data::preEvolution4(s)) {
            const data::LearnHit e = data::findLearn3(s, move);
            // Level-up moves: by the level it had when it left Gen III (the met level).
            learnable = e && (e->sources != data::kLearnLevelUp || e->minLevel <= p.met.metLevel || bred);
        }
        if (learnable || (p.species == kRotom && isRotomFormMove(move)) || eventHasMove(card, move) ||
            (isGen3Origin(p.originGame) && gen3OriginHasMove(p, move)) ||
            walkerSlotHasMove(walker, move)) {
            continue;
        }
        std::string name = data::moveName(move);
        if (p.met.fatefulEncounter || isGen3Origin(p.originGame)) {
            add(r, Severity::Warning, name + ": event/Gen III move, unchecked");
        } else if (levelTooLow) {
            add(r, Severity::Invalid, name + " is learned at a higher level");
        } else {
            add(r, Severity::Invalid, std::string(data::speciesName(p.species)) + " cannot learn " + name);
        }
    }
    if (known == 0 && !p.isEgg) {
        add(r, Severity::Invalid, "No moves");
    }
}

void checkBall(const Pokemon& p, Report& r) {
    uint8_t ball = p.met.ball;
    if (ball == 0 || ball > data::kBallSport) {
        add(r, Severity::Invalid, "Invalid ball");
        return;
    }
    bool fromHgss = p.originGame == version::kHeartGold || p.originGame == version::kSoulSilver;
    if (ball > data::kBallCherish && !fromHgss) {
        add(r, Severity::Invalid, std::string(data::ballName(ball)) + " only exists in HG/SS");
    }
    if (ball == data::kBallCherish && !p.met.fatefulEncounter) {
        add(r, Severity::Invalid, "Cherish Ball outside an event");
    }
    if (hatched(p) && isGen4Origin(p.originGame) && ball != data::kBallPoke && !p.met.fatefulEncounter) {
        add(r, Severity::Invalid, "Hatched eggs must be in a Poke Ball");
    }
}

// Poke Radar chain shinies exist in D/P/Pt grass, except the Great Marsh. The
// location is enough: unusual levels are reported by the encounter check.
constexpr uint16_t kLocationGreatMarsh = 52;

bool radarEncounterPossible(const Pokemon& p) {
    const uint8_t bit = data::versionBit4(p.originGame);
    if ((bit & (data::kVerDiamond | data::kVerPearl | data::kVerPlatinum)) == 0) return false;
    for (uint16_t s = p.species; s != 0; s = data::preEvolution4(s)) {
        const data::Encounter4* begin;
        const data::Encounter4* end;
        data::encounters4(s, &begin, &end);
        for (const data::Encounter4* e = begin; e != end; ++e) {
            if ((e->versions & bit) && e->kind == 0 && e->location == p.met.metLocation &&
                e->location != kLocationGreatMarsh) {
                return true;
            }
        }
    }
    return false;
}

// The Gyarados of the Lake of Rage (HG/SS) is always shiny, with a chain shiny PID.
bool isRedGyarados(const Pokemon& p, const data::Encounter4* encounter) {
    return encounter != nullptr && encounter->kind == data::kEncounterStatic && encounter->species == 130 &&
           encounter->location == 135 && (p.originGame == version::kHeartGold || p.originGame == version::kSoulSilver);
}

void checkPidIv(const Pokemon& p, const data::Encounter4* encounter, Report& r) {
    if (p.isEgg || hatched(p) || p.met.fatefulEncounter || p.originGame == version::kColosseumXD) {
        return;  // eggs and events do not use the correlated RNG methods
    }
    if (isNpcTrade(p)) {
        checkNpcTrade(p, r);
        return;
    }
    if (isGen4Origin(p.originGame) && p.met.metLocation == kLocationPokewalker) {
        return;  // checked by checkPokewalker
    }
    PidIvMethod method = findPidIvMethod(p.pid, p.ivs);
    if (isGen4Origin(p.originGame)) {
        const bool redGyarados = isRedGyarados(p, encounter);
        if (redGyarados && !p.shiny) {
            add(r, Severity::Invalid, "The Lake of Rage Gyarados is always shiny");
        }
        if (method == PidIvMethod::Method1) {
            add(r, Severity::Valid, std::string("PID/IV: ") + methodName(method));
        } else if (method == PidIvMethod::CuteCharm) {
            if (isCuteCharmPid(p.species, p.gender, p.pid)) {
                add(r, Severity::Valid, "PID/IV: Cute Charm");
            } else {
                add(r, Severity::Invalid, "PID below 0x100 does not follow the Cute Charm rule");
            }
        } else if (p.shiny && isChainShinyFrame(p.pid, p.ivs, p.trainer.tid, p.trainer.sid)) {
            if (redGyarados || radarEncounterPossible(p)) {
                add(r, Severity::Valid, redGyarados ? "PID/IV: chain shiny (fixed shiny)" : "PID/IV: Poke Radar chain shiny");
            } else {
                add(r, Severity::Invalid, "Poke Radar chain shinies only exist in D/P/Pt grass");
            }
        } else if (encounter != nullptr && encounter->kind == data::kEncounterRoamer) {
            add(r, Severity::Invalid, "Roamers have Method 1 PID/IVs");
        } else {
            add(r, Severity::Invalid, p.shiny ? "Shiny PID/IVs not from the game's RNG or a Poke Radar chain"
                                              : "PID/IVs not from the game's RNG");
        }
    } else if (isGen3Origin(p.originGame)) {
        if (method != PidIvMethod::None) {
            add(r, Severity::Valid, std::string("PID/IV: ") + methodName(method));
        } else {
            add(r, Severity::Warning, "PID/IVs only valid for a Gen III egg");
        }
    }
}

// Roamers can be met anywhere in their area, on grass or water tiles.
bool roamsAt4(const Pokemon& p, uint16_t species) {
    return roamer::metOk4(species, p.met.metLocation, p.met.encounterType);
}

// True if the species (or a pre-evolution) appears at the met location in the
// origin game at any level.
bool speciesMetAtLocation(const Pokemon& p) {
    uint8_t bit = data::versionBit4(p.originGame);
    for (uint16_t s = p.species; s != 0; s = data::preEvolution4(s)) {
        const data::Encounter4* begin;
        const data::Encounter4* end;
        data::encounters4(s, &begin, &end);
        for (const data::Encounter4* e = begin; e != end; ++e) {
            if ((e->versions & bit) &&
                (e->kind == data::kEncounterRoamer ? roamsAt4(p, s) : e->location == p.met.metLocation)) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace

Severity Report::overall() const {
    Severity worst = Severity::Valid;
    for (const Finding& f : findings) {
        if (f.severity > worst) worst = f.severity;
    }
    return worst;
}

bool isGen3Origin(uint8_t game) {
    return (game >= version::kSapphire && game <= version::kLeafGreen) || game == version::kColosseumXD;
}

bool isGen4Origin(uint8_t game) {
    return game == version::kHeartGold || game == version::kSoulSilver || game == version::kDiamond ||
           game == version::kPearl || game == version::kPlatinum;
}

EncounterStatus findEncounter4(const Pokemon& p, const data::Encounter4** match, uint16_t* matchedSpecies) {
    constexpr uint16_t kLocationPalPark = 55;
    uint8_t bit = data::versionBit4(p.originGame);
    if (bit == 0 || p.isEgg || hatched(p) || p.met.fatefulEncounter || isNpcTrade(p) ||
        p.met.metLocation == kLocationPokewalker || p.met.metLocation == kLocationPalPark) {
        return EncounterStatus::NotApplicable;
    }
    for (uint16_t s = p.species; s != 0; s = data::preEvolution4(s)) {
        const data::Encounter4* begin;
        const data::Encounter4* end;
        data::encounters4(s, &begin, &end);
        for (const data::Encounter4* e = begin; e != end; ++e) {
            if (!(e->versions & bit)) continue;
            if (e->kind == data::kEncounterRoamer ? !roamer::metOk4(s, p.met.metLocation, p.met.encounterType)
                                                  : e->location != p.met.metLocation) {
                continue;
            }
            if (p.met.metLevel < e->minLevel || p.met.metLevel > e->maxLevel) continue;
            if (match) *match = e;
            if (matchedSpecies) *matchedSpecies = s;
            return EncounterStatus::Found;
        }
    }
    return EncounterStatus::NotFound;
}

EncounterStatus findEncounter3(const Pokemon& p, const data::Encounter4** match, uint16_t* matchedSpecies) {
    constexpr uint16_t kLocationPalPark = 55;
    uint8_t bit = data::versionBit3(p.originGame);
    if (bit == 0 || p.isEgg || p.met.fatefulEncounter || p.met.metLocation != kLocationPalPark) {
        return EncounterStatus::NotApplicable;
    }
    for (uint16_t s = p.species; s != 0; s = data::preEvolution4(s)) {
        const data::Encounter4* begin;
        const data::Encounter4* end;
        data::encounters3(s, &begin, &end);
        for (const data::Encounter4* e = begin; e != end; ++e) {
            if (!(e->versions & bit) || e->minLevel > p.met.metLevel) continue;
            if (match) *match = e;
            if (matchedSpecies) *matchedSpecies = s;
            return EncounterStatus::Found;
        }
    }
    return EncounterStatus::NotFound;
}

Report checkGen4(const Pokemon& p) {
    Report r;
    const data::PersonalInfo* info = data::personal4(p.species);
    if (info == nullptr) {
        add(r, Severity::Invalid, "Species does not exist in Gen IV");
        return r;
    }
    if (!isGen3Origin(p.originGame) && !isGen4Origin(p.originGame)) {
        add(r, Severity::Invalid, "Unknown origin game");
    }
    if (p.isEgg) checkEgg(p, r);

    // Level and experience.
    auto growth = static_cast<data::GrowthRate>(info->growthRate);
    if (p.exp > data::expForLevel(growth, 100)) {
        add(r, Severity::Invalid, "EXP above the level 100 maximum");
    }
    if (!p.isEgg && p.met.metLevel > p.level) {
        add(r, Severity::Invalid, "Met level is above the current level");
    }
    // Encounter: where and at which level it was met.
    const data::Encounter4* encounter = nullptr;
    uint16_t encounterSpecies = 0;
    EncounterStatus status = findEncounter4(p, &encounter, &encounterSpecies);
    if (status == EncounterStatus::Found) {
        std::string where = data::locationName4(p.met.metLocation);
        if (encounter->kind == data::kEncounterRoamer) where = "roaming";
        add(r, Severity::Valid, std::string("Met: ") + where + " (" +
                                    (encounter->kind >= data::kEncounterStatic ? "static" : "wild") + ", " +
                                    data::speciesName(encounterSpecies) + ")");
    } else if (status == EncounterStatus::NotFound) {
        std::string where = data::locationName4(p.met.metLocation);
        if (speciesMetAtLocation(p)) {
            add(r, Severity::Warning, "Unusual met level " + std::to_string(p.met.metLevel) + " at " + where);
        } else {
            add(r, Severity::Invalid, std::string(data::speciesName(p.species)) + " cannot be found at " + where);
        }
    }

    // HG/SS Pokewalker: course slot, Poke Ball, never shiny, Pokewalker PID.
    WalkerMatch walker;
    if (claimsPokewalker(p)) checkPokewalker(p, r, &walker);

    // Pal Park: Gen III origin. Eggs, trades and events of Gen III are not in
    // the tables, so a missing encounter is only a warning.
    const data::Encounter4* encounter3 = nullptr;
    uint16_t species3 = 0;
    EncounterStatus status3 = findEncounter3(p, &encounter3, &species3);
    if (status3 == EncounterStatus::Found) {
        add(r, Severity::Valid, std::string("Met in Gen III: ") + data::locationName3(encounter3->location) + " (" +
                                    data::speciesName(species3) + "), Pal Park");
    } else if (status3 == EncounterStatus::NotFound) {
        std::string gen3 = describeGen3Origin(p);
        if (!gen3.empty()) {
            add(r, Severity::Valid, "Gen III origin, Pal Park: " + gen3);
        } else {
            add(r, Severity::Warning, "No Gen III encounter at Lv. " + std::to_string(p.met.metLevel) +
                                          " (only valid as an egg, trade or event)");
        }
    } else if (p.originGame == version::kColosseumXD && !p.met.fatefulEncounter) {
        std::string gen3 = describeGen3Origin(p);
        if (!gen3.empty()) add(r, Severity::Valid, "Colosseum/XD origin, Pal Park: " + gen3);
    }
    if (isGen3Origin(p.originGame) && p.originGame != version::kColosseumXD && p.met.ball > 12) {
        add(r, Severity::Invalid, std::string(data::ballName(p.met.ball)) + " does not exist in Gen III");
    }

    // Evolution level: certain for hatched Pokémon and for those caught as
    // a pre-evolution; otherwise it may have been caught already evolved.
    uint8_t evolveLevel = data::evolveLevel4(p.species);
    if (!p.isEgg && p.level < evolveLevel && walker.slot == nullptr) {  // walker: see checkPokewalker
        std::string name = data::speciesName(p.species);
        bool caughtEvolved = status == EncounterStatus::Found && encounterSpecies == p.species;
        bool mustHaveEvolved = (hatched(p) && !p.met.fatefulEncounter) ||
                               (status == EncounterStatus::Found && encounterSpecies != p.species);
        if (mustHaveEvolved) {
            add(r, Severity::Invalid, name + " only evolves at level " + std::to_string(evolveLevel));
        } else if (!caughtEvolved) {
            add(r, Severity::Warning, name + " below level " + std::to_string(evolveLevel) +
                                          ": only valid if caught already evolved");
        }
    }
    if (!p.isEgg && !hatched(p) && isGen4Origin(p.originGame) && p.met.metLevel == 0) {
        add(r, Severity::Invalid, "Met level 0 without being hatched");
    }

    // Traits derived from the PID.
    if (p.species != kShedinja) {
        if (!pokemon::edit4::canHaveGender(p.species, p.gender)) {
            add(r, Severity::Invalid, "Gender impossible for this species");
        } else if (pokemon::edit4::genderFromPid(p.species, p.pid) != p.gender) {
            add(r, Severity::Invalid, "Gender does not match the PID");
        }
    }
    // The PID's low bit picks the ability. Pokémon transferred from Gen III
    // keep their Gen III ability even if their species gained a second one in
    // Gen IV, so either of the species' abilities is accepted for them. Event
    // cards set the ability directly, whatever the PID.
    const data::Event4* card = p.met.fatefulEncounter ? pokemon::gift4::matchingEvent(p) : nullptr;
    uint16_t expected = pokemon::edit4::abilityForSlot(p.species, static_cast<uint8_t>(p.pid & 1));
    bool gen3AbilityOk = isGen3Origin(p.originGame) && (p.ability == info->abilities[0] || p.ability == info->abilities[1]);
    bool eventAbilityOk = card != nullptr && (p.ability == info->abilities[0] || p.ability == info->abilities[1]);
    if (p.ability != expected && !gen3AbilityOk && !eventAbilityOk) {
        add(r, Severity::Invalid, std::string("Ability should be ") + data::abilityName(expected));
    }

    // Stats.
    int evTotal = 0;
    for (int s = 0; s < pokemon::kStatCount; ++s) evTotal += p.evs[s];
    if (evTotal > kMaxEvTotal) {
        add(r, Severity::Invalid, "EV total above 510");
    }
    if (p.heldItem > data::kMaxItemGen4) {
        add(r, Severity::Invalid, "Held item does not exist in Gen IV");
    }

    if (isRangerManaphy(p)) {
        checkManaphy(p, r);
    } else {
        checkEvent(p, card, r);
    }
    checkMoves(p, card, walker.slot, r);
    checkBall(p, r);
    checkPidIv(p, encounter, r);
    checkRibbons(p, r);
    checkTrainer(p, r);

    if (r.overall() == Severity::Valid && r.findings.empty()) {
        add(r, Severity::Valid, "No problems found");
    }
    return r;
}

void checkEgg(const Pokemon& p, Report& r) {
    const bool gen5 = p.format >= 5;
    const data::PersonalInfo* info = data::personalFor(p.format, p.species);
    if (p.level != 1) add(r, Severity::Invalid, "Eggs are level 1");
    if (p.met.metLevel != 0) add(r, Severity::Invalid, "Unhatched eggs have met level 0");
    if (p.met.eggLocation == 0) add(r, Severity::Invalid, "Egg without an egg location");
    uint16_t pre = gen5 ? data::preEvolution5(p.species) : data::preEvolution4(p.species);
    bool breedable = info && (info->eggGroups[0] != data::kEggGroupUndiscovered ||
                              info->eggGroups[1] != data::kEggGroupUndiscovered);
    // Babies (Pichu...) cannot breed but hatch from their evolutions' eggs.
    for (uint16_t s = 1; !breedable && s <= (gen5 ? 649 : 493); ++s) {
        uint16_t sPre = gen5 ? data::preEvolution5(s) : data::preEvolution4(s);
        const data::PersonalInfo* evo = data::personalFor(p.format, s);
        breedable = sPre == p.species && evo && evo->eggGroups[0] != data::kEggGroupUndiscovered;
    }
    if (!p.met.fatefulEncounter) {
        if (pre != 0) {
            add(r, Severity::Invalid, std::string("Eggs hatch into ") + data::speciesName(gen5 ? data::baseSpecies5(p.species)
                                                                                                 : data::baseSpecies4(p.species)));
        } else if (!breedable) {
            add(r, Severity::Invalid, std::string(data::speciesName(p.species)) + " cannot come from an egg");
        }
    }
    if (info && p.friendship > info->hatchCycles) add(r, Severity::Invalid, "Egg cycles above the species' count");
    std::string expected = pokemon::create4::eggName(p.language, p.format);
    if (p.nickname != expected) add(r, Severity::Invalid, "Egg name should be " + expected);
    if (p.isNicknamed != gen5) add(r, Severity::Invalid, gen5 ? "Gen V eggs have the nickname flag" : "Gen IV eggs have no nickname flag");
    if (r.overall() != Severity::Invalid) add(r, Severity::Valid, "Egg");
}

bool isGen5Origin(uint8_t game) {
    return game >= version::kWhite && game <= version::kBlack2;
}

Report check(const Pokemon& p) {
    if (p.format == 1 || p.format == 2) return checkGb(p);
    if (p.format == 3) return checkGen3(p);
    return p.format >= 5 ? checkGen5(p) : checkGen4(p);
}

}  // namespace pktool::legality
