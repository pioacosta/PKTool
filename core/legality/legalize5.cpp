// Auto-Legalize for Gen V (B/W/B2/W2) Pokémon.

#include <algorithm>

#include "core/data/encounters5.h"
#include "core/data/learn5.h"
#include "core/data/moves.h"
#include "core/data/names.h"
#include "core/data/personal.h"
#include "core/legality/legality5_dw.h"
#include "core/legality/legalize.h"
#include "core/legality/roamer.h"
#include "core/legality/ribbons_check.h"
#include "core/pokemon/edit4.h"
#include "core/pokemon/pk4.h"
#include "core/pokemon/pk5.h"
#include "core/pokemon/transfer45.h"

#include <cstring>

namespace pktool::legality {
namespace {

using pokemon::Pokemon;

constexpr uint16_t kMaxEvTotal = 510;
constexpr uint8_t kBallDream = 25;
constexpr uint16_t kLocationTransfer4 = 30001;
constexpr uint16_t kLocationDaycare5 = 60002;
constexpr uint16_t kLocationHatch5 = 64;  // default hatch location (as PKHeX uses)
constexpr uint16_t kLocationEntree5 = 75; // Entree Forest (Dream World)

bool canBreed5(uint16_t species) {
    const data::PersonalInfo* info = data::personal5(species);
    return info && (info->eggGroups[0] != data::kEggGroupUndiscovered ||
                    info->eggGroups[1] != data::kEggGroupUndiscovered);
}

bool canHatch5(uint16_t species) {
    for (uint16_t s = species; s != 0; s = data::preEvolution5(s)) {
        if (canBreed5(s)) return true;
    }
    for (uint16_t s = 1; s <= data::kMaxSpeciesGen5; ++s) {  // babies hatch from their evolutions' eggs
        if (data::preEvolution5(s) == species && canBreed5(s)) return true;
    }
    return false;
}

uint8_t versionForBit5(uint8_t bit) {
    switch (bit) {
        case data::kVerBlack: return version::kBlack;
        case data::kVerWhite: return version::kWhite;
        case data::kVerBlack2: return version::kBlack2;
        default: return version::kWhite2;
    }
}

bool allowsHidden(const data::Encounter4* e) {
    return e->kind == data::kSlotHiddenGrotto || e->kind == data::kEncounterStaticHidden;
}

// Wild slots (not Hidden Grottos) and plain static encounters use the wild
// PID rule; gifts, roamers and fixed-shiny encounters do not.
bool correlated(const data::Encounter4* e) {
    return e->kind < data::kEncounterStatic ? e->kind != data::kSlotHiddenGrotto : e->kind == data::kEncounterStatic;
}

// Moves p to the Entree Forest capture `e` (caught as `species`).
void useDreamWorld5(Pokemon& p, const data::DreamWorld5& e, uint16_t species, LegalizeResult& out) {
    const uint8_t originBit = data::versionBit5(p.originGame);
    if (!(e.versions & originBit)) {
        uint8_t bit = 1;
        while (!(e.versions & bit)) bit = static_cast<uint8_t>(bit << 1);
        p.originGame = versionForBit5(bit);
        out.changes.push_back("Origin game set to a game that can receive it from the Dream World");
    }
    p.met.metLocation = kLocationEntree5;
    p.met.metLevel = e.level;
    p.met.eggLocation = 0;
    p.met.fatefulEncounter = false;
    out.changes.push_back(std::string("Met at the Entree Forest (Dream World), Lv. ") + std::to_string(e.level) +
                          " (as " + data::speciesName(species) + ", hidden ability)");
}

// Moves p to the Dream Radar (B2/W2): a species of the radar's line at the
// highest level (a multiple of 5, up to 40) it has reached, in a Dream Ball,
// with the hidden ability. False if the radar cannot produce p.
bool useDreamRadar5(Pokemon& p, LegalizeResult& out) {
    constexpr uint16_t kRotom = 479;
    if (p.shiny || p.level < 5 || p.met.fatefulEncounter) return false;
    const data::DreamRadar5* e = findDreamRadarFor5(p);
    if (e == nullptr) return false;
    if (e->species != p.species && p.level < data::evolveLevel5(p.species)) return false;
    if (e->species != kRotom && e->form != p.form) return false;
    const data::PersonalInfo* info = data::personal5(p.species);
    if (info == nullptr || (e->anyAbility ? p.hiddenAbility : !hasHiddenAbility5(*info))) return false;

    if (p.originGame != version::kBlack2 && p.originGame != version::kWhite2) {
        p.originGame = version::kBlack2;
        out.changes.push_back("Origin game set to Black 2 (the Dream Radar needs Black 2 / White 2)");
    }
    if (!e->anyAbility && !p.hiddenAbility) {
        p.hiddenAbility = true;
        out.changes.push_back("Hidden ability set (the Dream Radar always gives it)");
    }
    const uint8_t level = static_cast<uint8_t>(std::min<int>(p.level, 40) / 5 * 5);
    if (p.met.metLocation != kLocationDreamRadar5 || p.met.metLevel != level || p.met.eggLocation != 0) {
        out.changes.push_back(std::string("Met at the Dream Radar, Lv. ") + std::to_string(level) + " (as " +
                              data::speciesName(e->species) + ")");
    }
    p.met.metLocation = kLocationDreamRadar5;
    p.met.metLevel = level;
    p.met.eggLocation = 0;
    p.met.fatefulEncounter = false;
    return true;
}

const data::Encounter4* fixEncounter5(Pokemon& p, LegalizeResult& out, bool useDreamWorld,
                                      const data::DreamWorld5** dreamWorld) {
    const data::Encounter4* found = nullptr;
    // The Therian Formes and Pokémon already at the Dream Radar come from it.
    if ((isTherianForme5(p) || p.met.metLocation == kLocationDreamRadar5) && !p.isEgg && !p.met.fatefulEncounter) {
        if (useDreamRadar5(p, out)) return nullptr;
        if (isTherianForme5(p)) {
            out.unresolved.push_back(p.shiny ? "The Therian Forme comes from the Dream Radar and cannot be shiny"
                                             : "The Therian Forme needs the Dream Radar: level 5 or more");
            return nullptr;
        }
    }
    if (findEncounter5(p, &found) == EncounterStatus::Found) return found;
    if (findEncounter5(p) == EncounterStatus::NotApplicable) return nullptr;

    const uint8_t originBit = data::versionBit5(p.originGame);
    const uint8_t evolveLevel = data::evolveLevel5(p.species);
    const data::Encounter4* best = nullptr;
    uint16_t bestSpecies = 0;
    int bestScore = 1 << 30;
    for (uint16_t s = p.species; s != 0; s = data::preEvolution5(s)) {
        if (s != p.species && p.level < evolveLevel) break;
        const data::Encounter4* begin;
        const data::Encounter4* end;
        data::encounters5(s, &begin, &end);
        for (const data::Encounter4* e = begin; e != end; ++e) {
            if (e->minLevel > p.level) continue;
            if (allowsHidden(e) != p.hiddenAbility) continue;
            if (e->kind == data::kSlotHiddenGrotto && p.shiny) continue;
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
            p.originGame = versionForBit5(bit);
            out.changes.push_back(std::string("Origin game set to ") +
                                  (p.originGame == version::kBlack   ? "Black"
                                   : p.originGame == version::kWhite ? "White"
                                   : p.originGame == version::kBlack2 ? "Black 2"
                                                                      : "White 2"));
        }
        const bool roaming = best->kind == data::kEncounterRoamer;
        p.met.metLocation = roaming ? roamer::kDefaultLocation5 : best->location;
        p.met.metLevel = static_cast<uint8_t>(std::min<int>(best->maxLevel, p.level));
        p.met.eggLocation = 0;
        out.changes.push_back(std::string(roaming ? "Met roaming at " : "Met at ") +
                              data::locationName5(p.met.metLocation) + ", Lv. " + std::to_string(p.met.metLevel) +
                              " (as " + data::speciesName(bestSpecies) + ")");
        return best;
    }
    // A hidden ability without a Hidden Grotto or static capture: Dream World.
    if (useDreamWorld && p.hiddenAbility) {
        uint16_t dwSpecies = 0;
        if (const data::DreamWorld5* dw = findDreamWorldFor5(p, true, &dwSpecies)) {
            useDreamWorld5(p, *dw, dwSpecies, out);
            *dreamWorld = dw;
            return nullptr;
        }
    }
    // Else the Dream Radar (B2/W2), which always gives the hidden ability.
    if (useDreamWorld && p.hiddenAbility && useDreamRadar5(p, out)) return nullptr;
    if (canHatch5(p.species)) {
        p.met.eggLocation = kLocationDaycare5;
        p.met.metLocation = kLocationHatch5;
        p.met.metLevel = 1;
        p.met.eggDate = p.met.metDate;
        p.met.ball = data::kBallPoke;
        out.changes.push_back("Hatched from a Day-Care egg");
        return nullptr;
    }
    // Nothing fits: say what would.
    const data::Encounter4* lowest = nullptr;
    for (uint16_t s = p.species; s != 0; s = data::preEvolution5(s)) {
        const data::Encounter4* begin;
        const data::Encounter4* end;
        data::encounters5(s, &begin, &end);
        for (const data::Encounter4* e = begin; e != end; ++e) {
            if (!lowest || e->minLevel < lowest->minLevel) lowest = e;
        }
    }
    std::string name = data::speciesName(p.species);
    if (lowest == nullptr) {
        out.unresolved.push_back(name + " has no Gen V encounter (event or transfer only)");
    } else if (p.hiddenAbility) {
        out.unresolved.push_back(name + ": no encounter with its hidden ability at this level");
    } else {
        out.unresolved.push_back(name + " is only found from Lv. " + std::to_string(lowest->minLevel) + " (" +
                                 data::locationName5(lowest->location) + "): raise its level");
    }
    return nullptr;
}

}  // namespace

uint8_t minCreateLevel5(uint16_t species) {
    if (canHatch5(species)) return 1;
    uint8_t gen4 = minCreateLevel4(species);  // Poke Transfer from Gen IV (or Gen III through it)
    uint8_t lowest = 0;
    for (uint16_t s = species; s != 0; s = data::preEvolution5(s)) {
        const data::Encounter4* begin;
        const data::Encounter4* end;
        data::encounters5(s, &begin, &end);
        for (const data::Encounter4* e = begin; e != end; ++e) {
            if (e->kind == data::kEncounterStaticHidden) continue;
            uint8_t level = s == species ? e->minLevel : std::max(e->minLevel, data::evolveLevel5(species));
            if (lowest == 0 || level < lowest) lowest = level;
        }
    }
    if (lowest == 0) return gen4;
    return gen4 != 0 ? std::min(lowest, gen4) : lowest;
}

LegalizeResult legalizeGen5Native(const Pokemon& requested, int effort);

namespace {

// For species with no Gen V encounter (e.g. Kyogre): legalize it as a Gen IV
// Pokémon and bring it over with Poke Transfer. Returns false if that does
// not give a legal Pokémon either.
bool viaPokeTransfer(const Pokemon& requested, int effort, LegalizeResult& out) {
    if (requested.species > data::kMaxSpeciesGen4 || requested.hiddenAbility) return false;
    Pokemon p4 = requested;
    p4.format = 4;
    p4.originGame = version::kHeartGold;
    p4.met.metLocation = 0;
    p4.met.eggLocation = 0;
    p4.met.fatefulEncounter = false;
    uint8_t slot = static_cast<uint8_t>((requested.pid >> 16) & 1);
    if (!rerollPidGen4(p4, requested.nature, requested.shiny, requested.gender, slot, effort)) return false;
    LegalizeResult gen4 = legalizeGen4(p4, effort);
    if (!gen4.unresolved.empty()) return false;
    uint8_t raw4[pokemon::pk4::kStoredSize] = {};
    pokemon::pk4::applyPokemon(gen4.pokemon, raw4);
    uint8_t raw5[pokemon::pk4::kStoredSize];
    std::vector<std::string> notes;
    pokemon::transfer45::convert(raw4, raw5, &notes);
    Pokemon p5 = pokemon::pk5::toPokemon(raw5, sizeof raw5);
    Report report = checkGen5(p5);
    if (!report.legal()) return false;
    out = LegalizeResult();
    out.pokemon = p5;
    for (const std::string& c : gen4.changes) out.changes.push_back(c);
    out.changes.push_back("Brought from Gen IV with Poke Transfer");
    for (const std::string& n : notes) out.changes.push_back(n);
    out.report = report;
    return true;
}

}  // namespace

LegalizeResult legalizeGen5(const Pokemon& requested, int effort) {
    LegalizeResult first = legalizeGen5Native(requested, effort);
    if (first.unresolved.empty()) return first;
    // No Gen V encounter fits: try a Gen IV origin moved with Poke Transfer.
    bool noEncounter = false;
    for (const std::string& u : first.unresolved) {
        noEncounter = noEncounter || u.find("no Gen V encounter") != std::string::npos ||
                      u.find("is only found from") != std::string::npos || u.find("cannot be found") != std::string::npos;
    }
    LegalizeResult transferred;
    if (noEncounter && !isGen3Origin(requested.originGame) && !isGen4Origin(requested.originGame) &&
        viaPokeTransfer(requested, effort, transferred)) {
        return transferred;
    }
    return first;
}

static LegalizeResult legalizeGen5Pass(const Pokemon& requested, bool useDreamWorld);

// The Dream World is tried first for hidden abilities with no Hidden Grotto or
// static capture; if that does not end legal (the moves may need an egg), the
// Day-Care route is tried as well.
LegalizeResult legalizeGen5Native(const Pokemon& requested, int /*effort*/) {
    LegalizeResult first = legalizeGen5Pass(requested, true);
    const uint16_t location = first.pokemon.met.metLocation;
    bool usedDreamWorld = (location == kLocationEntree5 && requested.met.metLocation != kLocationEntree5) ||
                          (location == kLocationDreamRadar5 && requested.met.metLocation != kLocationDreamRadar5);
    if (first.unresolved.empty() || !usedDreamWorld) return first;
    LegalizeResult second = legalizeGen5Pass(requested, false);
    return second.unresolved.size() <= first.unresolved.size() ? second : first;
}

static LegalizeResult legalizeGen5Pass(const Pokemon& requested, bool useDreamWorld) {
    LegalizeResult out;
    Pokemon& p = out.pokemon;
    p = requested;
    const data::PersonalInfo* info = data::personal5(p.species);
    if (info == nullptr) {
        out.unresolved.push_back("Species does not exist in Gen V");
        out.report = checkGen5(p);
        return out;
    }

    auto growth = static_cast<data::GrowthRate>(info->growthRate);
    if (p.exp > data::expForLevel(growth, 100)) {
        p.exp = data::expForLevel(growth, 100);
        out.changes.push_back("EXP capped at level 100");
    }
    p.level = data::levelForExp(growth, p.exp);
    if (p.isEgg) fixEgg(p, out.changes);

    const bool transferred = isGen3Origin(p.originGame) || isGen4Origin(p.originGame);
    const data::Encounter4* encounter = nullptr;
    const data::DreamWorld5* dreamWorld = nullptr;
    if (transferred) {
        if (p.met.metLocation < kLocationTransfer4 || p.met.metLocation > 30013) {
            p.met.metLocation = kLocationTransfer4;
            out.changes.push_back("Met location set to Poke Transfer");
        }
        if (p.hiddenAbility) {
            p.hiddenAbility = false;
            out.changes.push_back("Hidden ability removed (not possible after a transfer)");
        }
    } else if (!p.isEgg && !p.met.fatefulEncounter) {
        encounter = fixEncounter5(p, out, useDreamWorld, &dreamWorld);
    }
    if (p.nSparkle && findNPokemon5(p) == nullptr) {
        p.nSparkle = false;
        out.changes.push_back("N's Pokemon flag removed (not one of N's Pokemon)");
    }
    if (p.pokeStarFame != 0 && (p.isEgg || p.species == 132 || (p.pokeStarFame % 25 != 0 && p.pokeStarFame % 25 != 5))) {
        p.pokeStarFame = 0;
        out.changes.push_back("Pokestar Studios fame reset");
    }
    if (!p.isEgg && p.met.metLevel > p.level) {
        p.met.metLevel = p.level;
        out.changes.push_back("Met level lowered to " + std::to_string(p.level));
    }

    // Ball.
    bool hatched = p.met.eggLocation != 0;
    uint8_t ball = p.met.ball;
    bool dreamWorldLocation = p.met.metLocation == 75 || p.met.metLocation == 30015;
    bool gift = encounter && encounter->kind == data::kEncounterGift5;
    if (ball == 0 || ball > kBallDream || (dreamWorld && !isDreamWorldBall5(ball)) || (ball == data::kBallCherish && !p.met.fatefulEncounter) ||
        (ball == kBallDream && !dreamWorldLocation && !p.met.fatefulEncounter) ||
        ((hatched || gift) && !p.met.fatefulEncounter && ball != data::kBallPoke)) {
        p.met.ball = data::kBallPoke;
        out.changes.push_back("Ball set to Poke Ball");
    }
    if (p.met.metLocation == kLocationDreamRadar5 && !p.met.fatefulEncounter && p.met.ball != kBallDream) {
        p.met.ball = kBallDream;  // the Dream Radar always gives a Dream Ball
        out.changes.push_back("Ball set to Dream Ball");
    }

    // EVs and moves.
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
        uint8_t maxPp = data::maxPp5(p.moves[i], p.ppUps[i]);
        if (p.pp[i] > maxPp) p.pp[i] = maxPp;
    }

    // Ability: a regular slot must exist; the hidden one only where allowed.
    if (p.hiddenAbility && !hasHiddenAbility5(*info)) {
        p.hiddenAbility = false;
        out.changes.push_back("This species has no hidden ability");
    }
    if (p.hiddenAbility) {
        p.ability = hiddenAbilityOf5(p, *info);
    }

    // PID: gender, ability slot, shininess and (for wild encounters) bit 31.
    pokemon::Gender gender = p.gender;
    if (!pokemon::edit4::canHaveGender(p.species, gender)) {
        gender = pokemon::edit4::genderFromPid(p.species, p.pid);
    }
    uint8_t slot = static_cast<uint8_t>(transferred ? p.pid & 1 : (p.pid >> 16) & 1);
    if (!p.hiddenAbility && p.ability != pokemon::edit4::abilityForSlot(p.species, slot) &&
        p.ability == pokemon::edit4::abilityForSlot(p.species, static_cast<uint8_t>(1 - slot))) {
        slot = static_cast<uint8_t>(1 - slot);
    }
    bool needCorrelation = encounter && correlated(encounter);
    // A Dream World capture with a forced gender cannot have the extreme gender values.
    auto dwPidBad = [&](uint32_t pid) {
        return dreamWorld && dreamWorld->gender < 2 &&
               (dreamWorld->gender == 0 ? (pid & 0xFF) >= 254 : (pid & 0xFF) == 0);
    };
    bool pidOk = !dwPidBad(p.pid) && pokemon::edit4::genderFromPid(p.species, p.pid) == gender &&
                 (transferred ? (p.pid & 1) == slot && pokemon::natureFromPid(p.pid) == p.nature
                              : ((p.pid >> 16) & 1) == slot) &&
                 (!needCorrelation || pokemon::edit4::wildPidCorrelation5(p.pid, p.trainer.tid, p.trainer.sid));
    if (!pidOk) {
        uint32_t pid = 0;
        bool found = transferred ? pokemon::edit4::findPid(p.species, p.trainer.tid, p.trainer.sid, p.nature, p.shiny,
                                                           gender, slot, p.pid, pid)
                                 : pokemon::edit4::findPid5(p.species, p.trainer.tid, p.trainer.sid, p.shiny, gender,
                                                            slot, p.pid, pid, needCorrelation);
        for (uint32_t seed = p.pid + 1; found && dwPidBad(pid) && seed < p.pid + 64; ++seed) {
            found = pokemon::edit4::findPid5(p.species, p.trainer.tid, p.trainer.sid, p.shiny, gender, slot, seed, pid,
                                             needCorrelation);
        }
        if (found && dwPidBad(pid)) found = false;
        if (found) {
            p.pid = pid;
            p.gender = gender;
            if (!p.hiddenAbility) p.ability = pokemon::edit4::abilityForSlot(p.species, slot);
            out.changes.push_back("New PID for the requested gender/ability/shininess");
        } else {
            out.unresolved.push_back("No valid PID for this gender/ability/shiny combination");
        }
    }

    out.report = checkGen5(p);
    for (const Finding& f : out.report.findings) {
        if (f.severity == Severity::Invalid) out.unresolved.push_back(f.text);
    }
    return out;
}

LegalizeResult legalize(const Pokemon& requested, int effort) {
    LegalizeResult out = requested.format <= 2   ? legalizeGb(requested, effort)
                         : requested.format == 3 ? legalizeGen3(requested, effort)
                         : requested.format >= 5 ? legalizeGen5(requested, effort)
                                                 : legalizeGen4(requested, effort);
    // Ribbons depend on the final origin: drop the impossible ones.
    if (fixRibbons(out.pokemon, out.changes)) out.report = check(out.pokemon);
    return out;
}

}  // namespace pktool::legality
