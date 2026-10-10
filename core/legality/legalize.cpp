#include "core/legality/legalize.h"

#include <algorithm>

#include "core/data/encounters4.h"
#include "core/data/encounters3.h"
#include "core/data/learn4.h"
#include "core/data/learn5.h"
#include "core/data/moves.h"
#include "core/data/names.h"
#include "core/data/personal.h"
#include "core/legality/pid_iv.h"
#include "core/legality/roamer.h"
#include "core/legality/pokewalker.h"
#include "core/legality/rng4.h"
#include "core/legality/trade_check.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/edit4.h"

namespace pktool::legality {
namespace {

using pokemon::Pokemon;

constexpr uint16_t kLocationNpcTrade = 2001;
constexpr uint16_t kMaxEvTotal = 510;

constexpr uint16_t kLocationDayCare = 2000;   // egg received from the Day-Care couple
constexpr uint16_t kLocationSolaceon = 4;     // D/P/Pt Day-Care town
constexpr uint16_t kLocationRoute34 = 182;    // HG/SS Day-Care route
constexpr uint16_t kLocationPalPark = 55;     // Gen III Pokémon moved to D/P/Pt
constexpr uint8_t kMaxBallGen3 = 12;          // Premier Ball

// Representative game version for a version bit.
uint8_t versionForBit(uint8_t bit) {
    switch (bit) {
        case data::kVerDiamond: return version::kDiamond;
        case data::kVerPearl: return version::kPearl;
        case data::kVerPlatinum: return version::kPlatinum;
        case data::kVerHeartGold: return version::kHeartGold;
        default: return version::kSoulSilver;
    }
}

const char* gameName(uint8_t version) {
    switch (version) {
        case version::kDiamond: return "Diamond";
        case version::kPearl: return "Pearl";
        case version::kPlatinum: return "Platinum";
        case version::kHeartGold: return "HeartGold";
        case version::kSoulSilver: return "SoulSilver";
        default: return "?";
    }
}

bool canBreed(uint16_t species) {
    const data::PersonalInfo* info = data::personal4(species);
    return info && (info->eggGroups[0] != data::kEggGroupUndiscovered ||
                    info->eggGroups[1] != data::kEggGroupUndiscovered);
}

// True if the species' evolution line can come from an egg. Babies (such as
// Pichu) cannot breed themselves but hatch from their evolutions' eggs.
bool canHatch(uint16_t species) {
    for (uint16_t s = species; s != 0; s = data::preEvolution4(s)) {
        if (canBreed(s)) return true;
    }
    for (uint16_t s = 1; s <= data::kMaxSpeciesGen4; ++s) {  // evolutions of a baby
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

// Moves the Pokémon's origin to a Gen III encounter brought over with Pal
// Park (met level = level at transfer). Returns false if none fits.
bool palParkEncounter(Pokemon& p, LegalizeResult& out) {
    const uint8_t originBit = data::versionBit3(p.originGame);
    const uint8_t evolveLevel = data::evolveLevel4(p.species);
    const data::Encounter4* best = nullptr;
    uint16_t bestSpecies = 0;
    int bestScore = 1 << 30;
    for (uint16_t s = p.species; s != 0; s = data::preEvolution4(s)) {
        if (s != p.species && p.level < evolveLevel) break;
        const data::Encounter4* begin;
        const data::Encounter4* end;
        data::encounters3(s, &begin, &end);
        for (const data::Encounter4* e = begin; e != end; ++e) {
            if (e->kind == data::kEncounterRoamer || e->minLevel > p.level) continue;
            int score = (e->versions & originBit) ? 0 : 1000;
            score += s == p.species ? 0 : 100;
            score += e->kind == data::kEncounterStatic ? 0 : 10;
            if (score < bestScore) {
                bestScore = score;
                best = e;
                bestSpecies = s;
            }
        }
    }
    if (best == nullptr) return false;
    if (!(best->versions & originBit)) {
        uint8_t bit = 1;
        while (!(best->versions & bit)) bit = static_cast<uint8_t>(bit << 1);
        p.originGame = versionForBit3(bit);
    }
    p.met.metLocation = kLocationPalPark;
    p.met.metLevel = p.level;
    p.met.eggLocation = 0;
    if (p.met.ball == 0 || p.met.ball > kMaxBallGen3) p.met.ball = data::kBallPoke;
    out.changes.push_back(std::string("From ") + gameName3(p.originGame) + " via Pal Park (" +
                          data::locationName3(best->location) + ", as " + data::speciesName(bestSpecies) + ")");
    return true;
}

// The ability slot the user asked for: the PID's, unless the ability field
// names the other slot.
uint8_t requestedAbilitySlot(const Pokemon& p) {
    uint8_t slot = static_cast<uint8_t>(p.pid & 1);
    if (p.ability != pokemon::edit4::abilityForSlot(p.species, slot) &&
        p.ability == pokemon::edit4::abilityForSlot(p.species, static_cast<uint8_t>(1 - slot))) {
        slot = static_cast<uint8_t>(1 - slot);  // the user picked the other ability
    }
    return slot;
}

bool sameAbility(uint16_t species, uint8_t slotA, uint8_t slotB) {
    return pokemon::edit4::abilityForSlot(species, slotA) == pokemon::edit4::abilityForSlot(species, slotB);
}

// Gives the Pokémon a real encounter in a Gen IV game when its current met
// data matches none: a wild/static encounter (preferring its origin game and
// its own species), or hatching from an egg for breedable species.
void fixEncounter(Pokemon& p, LegalizeResult& out) {
    EncounterStatus status = findEncounter4(p);
    const bool walker = claimsPokewalker(p);
    if (walker && findPokewalker(p) == EncounterStatus::Found) {
        if (p.originGame != version::kHeartGold && p.originGame != version::kSoulSilver) {
            p.originGame = version::kHeartGold;  // the Pokewalker only exists for HG/SS
            out.changes.push_back("Origin game set to HeartGold");
        }
        return;
    }
    if (isGen3Origin(p.originGame) && p.met.metLocation == kLocationPalPark) {
        if (findEncounter3(p) != EncounterStatus::NotFound) return;
        if (palParkEncounter(p, out)) return;  // keep a Gen III origin when possible
    } else if (status != EncounterStatus::NotFound && !walker) {
        return;
    }

    const uint8_t originBit = data::versionBit4(p.originGame);
    const uint8_t evolveLevel = data::evolveLevel4(p.species);
    const data::Encounter4* best = nullptr;
    uint16_t bestSpecies = 0;
    int bestScore = 1 << 30;
    for (uint16_t s = p.species; s != 0; s = data::preEvolution4(s)) {
        if (s != p.species && p.level < evolveLevel) break;  // could not have evolved yet
        const data::Encounter4* begin;
        const data::Encounter4* end;
        data::encounters4(s, &begin, &end);
        for (const data::Encounter4* e = begin; e != end; ++e) {
            if (e->minLevel > p.level) continue;
            int score = (e->versions & originBit) ? 0 : 1000;
            score += s == p.species ? 0 : 100;
            if (e->kind == data::kEncounterRoamer) score += 200;  // wild and static encounters first
            score += p.level - std::min<int>(e->maxLevel, p.level);  // prefer met levels close to the current one
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
            p.originGame = versionForBit(bit);
            out.changes.push_back(std::string("Origin game set to ") + gameName(p.originGame));
        }
        const bool roaming = best->kind == data::kEncounterRoamer;
        p.met.metLocation = roaming ? roamer::defaultLocation4(bestSpecies) : best->location;
        if (roaming) p.met.encounterType = roamer::kGroundGrass;  // roamers are met on grass or water tiles
        p.met.metLevel = static_cast<uint8_t>(std::min<int>(best->maxLevel, p.level));
        p.met.eggLocation = 0;
        out.changes.push_back(std::string(roaming ? "Met roaming at " : "Met at ") +
                              data::locationName4(p.met.metLocation) + ", Lv. " + std::to_string(p.met.metLevel) +
                              " (as " + data::speciesName(bestSpecies) + ")");
        return;
    }

    if (canHatch(p.species)) {
        if (originBit == 0) p.originGame = version::kPlatinum;
        bool hgss = p.originGame == version::kHeartGold || p.originGame == version::kSoulSilver;
        p.met.eggLocation = kLocationDayCare;
        p.met.metLocation = hgss ? kLocationRoute34 : kLocationSolaceon;
        p.met.metLevel = 0;
        p.met.eggDate = p.met.metDate;
        p.met.ball = data::kBallPoke;
        out.changes.push_back(std::string("Hatched from a Day-Care egg in ") + data::locationName4(p.met.metLocation));
        return;
    }
    if (palParkEncounter(p, out)) return;
    if (applyPokewalkerOrigin(p, requestedAbilitySlot(p), out.changes)) return;  // the only way left: HG/SS Pokewalker
    const data::Encounter4* lowest4 = nullptr;
    const data::Encounter4* lowest3 = nullptr;
    lowestEncounter4(p.species, &lowest4);
    lowestEncounter3(p.species, &lowest3);
    std::string name = data::speciesName(p.species);
    if (lowest4 == nullptr && lowest3 == nullptr) {
        out.unresolved.push_back(name + " has no Gen III/IV encounter: use its event (Add > Event)");
        return;
    }
    bool useGen3 = lowest4 == nullptr || (lowest3 != nullptr && lowest3->minLevel < lowest4->minLevel);
    const data::Encounter4* lowest = useGen3 ? lowest3 : lowest4;
    uint8_t bit = 1;
    while (!(lowest->versions & bit)) bit = static_cast<uint8_t>(bit << 1);
    std::string where = lowest->kind == data::kEncounterRoamer ? std::string("roaming")
                        : useGen3 ? data::locationName3(lowest->location)
                                  : data::locationName4(lowest->location);
    std::string game = useGen3 ? gameName3(versionForBit3(bit)) : gameName(versionForBit(bit));
    out.unresolved.push_back(name + " is only found from Lv. " + std::to_string(lowest->minLevel) + " (" + where +
                             ", " + game + "): raise its level");
}

std::string ivText(const uint8_t ivs[pokemon::kStatCount]) {
    std::string s;
    for (int i = 0; i < pokemon::kStatCount; ++i) {
        if (i) s += "/";
        s += std::to_string(ivs[i]);
    }
    return s;
}

}  // namespace

namespace {

uint8_t lowestIn(void (*table)(uint16_t, const data::Encounter4**, const data::Encounter4**), uint16_t species,
                 bool ownOnly, const data::Encounter4** match) {
    const data::Encounter4* best = nullptr;
    for (uint16_t s = species; s != 0; s = ownOnly ? 0 : data::preEvolution4(s)) {
        const data::Encounter4* begin;
        const data::Encounter4* end;
        table(s, &begin, &end);
        for (const data::Encounter4* e = begin; e != end; ++e) {
            if (best == nullptr || e->minLevel < best->minLevel) best = e;
        }
    }
    if (match) *match = best;
    return best ? best->minLevel : 0;
}

// Lowest level a species can have when met through `table`, counting the
// evolution level when it is met as a pre-evolution.
uint8_t minLevelIn(void (*table)(uint16_t, const data::Encounter4**, const data::Encounter4**), uint16_t species) {
    uint8_t any = lowestIn(table, species, false, nullptr);
    if (any == 0) return 0;
    uint8_t own = lowestIn(table, species, true, nullptr);
    uint8_t viaEvolution = std::max(any, data::evolveLevel4(species));
    return own ? std::min(own, viaEvolution) : viaEvolution;
}

}  // namespace

uint8_t lowestEncounter4(uint16_t species, const data::Encounter4** match) {
    return lowestIn(data::encounters4, species, false, match);
}

uint8_t lowestEncounter3(uint16_t species, const data::Encounter4** match) {
    return lowestIn(data::encounters3, species, false, match);
}

uint8_t minCreateLevel4(uint16_t species) {
    if (canHatch(species)) return 1;
    uint8_t gen4 = minLevelIn(data::encounters4, species);
    uint8_t gen3 = minLevelIn(data::encounters3, species);
    if (gen4 == 0) return gen3;
    if (gen3 == 0) return gen4;
    return std::min(gen4, gen3);
}

void fixEgg(Pokemon& p, std::vector<std::string>& changes) {
    const bool gen5 = p.format >= 5;
    uint16_t base = gen5 ? data::baseSpecies5(p.species) : data::baseSpecies4(p.species);
    if (base != p.species && !p.met.fatefulEncounter) {
        pokemon::edit4::setSpecies(p, base);
        changes.push_back(std::string("Species set to ") + data::speciesName(base) + " (what hatches)");
    }
    if (p.level != 1) {
        pokemon::edit4::setLevel(p, 1);
        changes.push_back("Level set to 1");
    }
    if (p.met.metLevel != 0 || p.met.metLocation != 0) {
        p.met.metLevel = 0;
        p.met.metLocation = 0;
        changes.push_back("Met data cleared (not hatched yet)");
    }
    if (p.met.eggLocation == 0) {
        p.met.eggLocation = gen5 ? 60002 : 2000;
        changes.push_back("Egg received from the Day-Care");
    }
    if (p.met.ball != data::kBallPoke && !p.met.fatefulEncounter) {
        p.met.ball = data::kBallPoke;
        changes.push_back("Ball set to Poke Ball");
    }
    std::string name = pokemon::create4::eggName(p.language, p.format);
    if (p.nickname != name || p.isNicknamed != gen5) {
        p.nickname = name;
        p.isNicknamed = gen5;
        changes.push_back("Egg name set to " + name);
    }
    const data::PersonalInfo* info = data::personalFor(p.format, p.species);
    if (info && p.friendship > info->hatchCycles) {
        p.friendship = info->hatchCycles;
        changes.push_back("Egg cycles set to " + std::to_string(info->hatchCycles));
    }
}

bool needsCorrelatedPid(const Pokemon& p) {
    if (p.format == 3) {  // Gen III save: hatched Pokémon have met level 0 (no egg location)
        return !p.isEgg && p.met.metLevel != 0 && !p.met.fatefulEncounter && p.met.metLocation < location3::kGiftEgg &&
               p.originGame != version::kColosseumXD;
    }
    return !p.isEgg && p.met.eggLocation == 0 && !p.met.fatefulEncounter && !isNpcTrade(p) &&
           p.originGame != version::kColosseumXD && (isGen3Origin(p.originGame) || isGen4Origin(p.originGame));
}

namespace {

// Wild Gen IV encounters: the player can lead with Synchronize or Cute Charm.
bool leadsAllowed(const Pokemon& p) {
    if (p.format != 4 || !isGen4Origin(p.originGame)) return false;
    const data::Encounter4* match = nullptr;
    if (findEncounter4(p, &match) != EncounterStatus::Found || match->kind >= data::kEncounterStatic) return false;
    return match->kind != 8;  // Bug-Catching Contest: no lead abilities
}

// Pokewalker catch: the PID follows from the IDs, nature and gender; only the
// IVs (any two consecutive RNG outputs) are free.
bool rerollWalker(Pokemon& p, uint8_t nature, bool shiny, pokemon::Gender gender, uint8_t abilitySlot, int effort) {
    if (shiny || nature >= 24) return false;
    const uint32_t pid = pokewalkerPid(p.trainer.tid, p.trainer.sid, nature, static_cast<uint8_t>(gender),
                                       pokewalkerGenderRatio(p.species));
    if (pid % 25 != nature || !sameAbility(p.species, static_cast<uint8_t>(pid & 1), abilitySlot) ||
        pokemon::edit4::genderFromPid(p.species, pid) != gender || pokemon::isShiny(pid, p.trainer.tid, p.trainer.sid)) {
        return false;
    }
    if (!ivsFromConsecutiveCalls(p.ivs)) {
        PidIvResult ivs = generateConsecutiveIvs(p.ivs, effort);
        if (!ivs.found) return false;
        for (int s = 0; s < pokemon::kStatCount; ++s) p.ivs[s] = ivs.ivs[s];
    }
    p.pid = pid;
    return true;
}

}  // namespace

bool rerollPidGen4(Pokemon& p, uint8_t nature, bool shiny, pokemon::Gender gender, uint8_t abilitySlot, int effort,
                   PidSource* source) {
    if (source != nullptr) *source = PidSource::Normal;
    if (needsCorrelatedPid(p) && claimsPokewalker(p)) {
        if (!rerollWalker(p, nature, shiny, gender, abilitySlot, effort)) return false;
        p.ability = pokemon::edit4::abilityForSlot(p, abilitySlot);
        pokemon::edit4::refreshDerived(p);
        return true;
    }
    if (!needsCorrelatedPid(p)) {
        uint32_t pid;
        if (!pokemon::edit4::findPid(p.species, p.trainer.tid, p.trainer.sid, nature, shiny, gender, abilitySlot, p.pid,
                                     pid)) {
            return false;
        }
        p.pid = pid;
    } else {
        PidIvRequest req;
        req.species = p.species;
        req.tid = p.trainer.tid;
        req.sid = p.trainer.sid;
        req.nature = nature;
        req.shiny = shiny;
        req.gender = gender;
        req.abilitySlot = abilitySlot;
        for (int s = 0; s < pokemon::kStatCount; ++s) req.targetIvs[s] = p.ivs[s];
        req.method = methodForOrigin(p.originGame);
        req.allowSync = req.allowCuteCharm = leadsAllowed(p);
        PidIvResult result = generatePidIv(req, effort, p.pid);
        if (!result.found) {
            return false;
        }
        if (source != nullptr) *source = result.source;
        p.pid = result.pid;
        for (int s = 0; s < pokemon::kStatCount; ++s) p.ivs[s] = result.ivs[s];
    }
    p.ability = pokemon::edit4::abilityForSlot(p, abilitySlot);
    pokemon::edit4::refreshDerived(p);
    return true;
}

LegalizeResult legalizeGen4(const Pokemon& requested, int effort) {
    LegalizeResult out;
    Pokemon& p = out.pokemon;
    p = requested;
    const data::PersonalInfo* info = data::personal4(p.species);
    if (info == nullptr) {
        out.unresolved.push_back("Species does not exist in Gen IV");
        out.report = checkGen4(p);
        return out;
    }

    // EXP and met level.
    auto growth = static_cast<data::GrowthRate>(info->growthRate);
    if (p.exp > data::expForLevel(growth, 100)) {
        p.exp = data::expForLevel(growth, 100);
        out.changes.push_back("EXP capped at level 100");
    }
    p.level = data::levelForExp(growth, p.exp);
    if (p.isEgg) fixEgg(p, out.changes);
    if (!p.isEgg && !p.met.fatefulEncounter) {
        fixEncounter(p, out);
    }
    if (!p.isEgg && p.met.metLevel > p.level) {
        p.met.metLevel = p.level;
        out.changes.push_back("Met level lowered to " + std::to_string(p.level));
    }

    // Ball.
    bool fromHgss = p.originGame == version::kHeartGold || p.originGame == version::kSoulSilver;
    bool hatched = p.met.eggLocation != 0;
    uint8_t ball = p.met.ball;
    if (ball == 0 || ball > data::kBallSport || (ball > data::kBallCherish && !fromHgss) ||
        (isGen3Origin(p.originGame) && ball > kMaxBallGen3) ||
        (ball == data::kBallCherish && !p.met.fatefulEncounter) ||
        (hatched && isGen4Origin(p.originGame) && !p.met.fatefulEncounter && ball != data::kBallPoke) ||
        (claimsPokewalker(p) && ball != data::kBallPoke)) {
        p.met.ball = data::kBallPoke;
        out.changes.push_back("Ball set to Poke Ball");
    }

    // EVs: trim the largest values until the total is 510.
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

    // Moves: remove duplicates, fix PP. Unlearnable moves are reported, not removed.
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < i; ++j) {
            if (p.moves[i] != 0 && p.moves[i] == p.moves[j]) {
                out.changes.push_back(std::string("Removed duplicate ") + data::moveName(p.moves[i]));
                p.moves[i] = 0;
                p.pp[i] = p.ppUps[i] = 0;
            }
        }
        if (p.ppUps[i] > 3) p.ppUps[i] = 3;
        uint8_t maxPp = data::maxPp4(p.moves[i], p.ppUps[i]);
        if (p.pp[i] > maxPp) p.pp[i] = maxPp;
    }

    // PID-derived traits: keep the requested nature, shininess, gender and
    // ability; regenerate the PID (and IVs for RNG-generated encounters).
    uint8_t slot = requestedAbilitySlot(p);
    pokemon::Gender gender = p.gender;
    if (!pokemon::edit4::canHaveGender(p.species, gender)) {
        gender = pokemon::edit4::genderFromPid(p.species, p.pid);
        if (!pokemon::edit4::canHaveGender(p.species, gender)) {
            gender = pokemon::edit4::canHaveGender(p.species, pokemon::Gender::Genderless) ? pokemon::Gender::Genderless
                     : pokemon::edit4::canHaveGender(p.species, pokemon::Gender::Female) ? pokemon::Gender::Female
                                                                                          : pokemon::Gender::Male;
        }
        out.changes.push_back(std::string("Gender set to the species' only gender"));
    }
    bool pidOk = pokemon::edit4::genderFromPid(p.species, p.pid) == gender &&
                 p.ability == pokemon::edit4::abilityForSlot(p.species, slot) && (p.pid & 1) == slot &&
                 (!needsCorrelatedPid(p) ||
                  (claimsPokewalker(p) ? isPokewalkerPid(p) && ivsFromConsecutiveCalls(p.ivs)
                                       : findPidIvMethod(p.pid, p.ivs) != PidIvMethod::None));
    if (!pidOk) {
        uint8_t before[pokemon::kStatCount];
        std::copy(p.ivs, p.ivs + pokemon::kStatCount, before);
        PidSource source = PidSource::Normal;
        if (rerollPidGen4(p, p.nature, p.shiny, gender, slot, effort, &source)) {
            out.changes.push_back("New PID for the requested nature/shininess/gender/ability");
            if (source == PidSource::Synchronize) {
                out.changes.push_back(std::string("Lead with a Pokemon with Synchronize and the ") +
                                      data::natureName(p.nature) + " nature");
            } else if (source == PidSource::CuteCharm) {
                out.changes.push_back(std::string("Lead with a Cute Charm Pokemon of the ") +
                                      (p.gender == pokemon::Gender::Female ? "male" : "female") + " gender");
            }
            if (!std::equal(before, before + pokemon::kStatCount, p.ivs)) {
                out.changes.push_back("IVs " + ivText(before) + " -> " + ivText(p.ivs));
            }
        } else {
            out.unresolved.push_back("No valid PID for this nature/shiny/gender/ability combination");
        }
    }

    out.report = checkGen4(p);
    for (const Finding& f : out.report.findings) {
        if (f.severity == Severity::Invalid) out.unresolved.push_back(f.text);
    }
    return out;
}

}  // namespace pktool::legality
