#include "core/pokemon/edit4.h"

#include "core/data/gen3.h"
#include "core/data/language.h"
#include "core/data/moves.h"
#include "core/data/names.h"
#include "core/data/personal.h"
#include "core/util/lcrng.h"

namespace pktool::pokemon::edit4 {
namespace {

// Upper bound on PID candidates; a valid PID is normally found in < 1000.
constexpr uint32_t kMaxPidAttempts = 1u << 22;

constexpr uint16_t kMaxEvTotal = 510;

// Gen IV data for species 1-493, Gen V data for the Unova species.
const data::PersonalInfo* personalAny(uint16_t species) {
    const data::PersonalInfo* info = data::personal4(species);
    return info ? info : data::personal5(species);
}

uint8_t genderRatio(uint16_t species) {
    const data::PersonalInfo* info = personalAny(species);
    return info ? info->genderRatio : data::kGenderRatioGenderless;
}

}  // namespace

bool wildPidCorrelation5(uint32_t pid, uint16_t tid, uint16_t sid) {
    return (((pid >> 31) ^ (pid & 1) ^ ((tid ^ sid) & 1)) & 1) == 0;
}

bool findPid5(uint16_t species, uint16_t tid, uint16_t sid, bool shiny, Gender gender, uint8_t abilitySlot,
              uint32_t seed, uint32_t& pidOut, bool wildCorrelation) {
    if (!canHaveGender(species, gender)) return false;
    uint32_t state = seed ^ 0xA5A5A5A5u;
    for (uint32_t attempt = 0; attempt < kMaxPidAttempts; ++attempt) {
        state = util::lcrngNext(state);
        uint32_t high = state >> 16;
        state = util::lcrngNext(state);
        uint32_t low = state >> 16;
        uint32_t pid = (high << 16) | low;
        if (shiny) pid = (((low ^ tid ^ sid ^ (high & 7)) & 0xFFFF) << 16) | low;
        if (wildCorrelation && !wildPidCorrelation5(pid, tid, sid)) pid ^= 0x80000000u;
        if (isShiny(pid, tid, sid) != shiny) continue;
        if (((pid >> 16) & 1) != (abilitySlot & 1u) || genderFromPid(species, pid) != gender) continue;
        pidOut = pid;
        return true;
    }
    return false;
}

namespace {

uint8_t currentSlot(const Pokemon& p) {
    return static_cast<uint8_t>(p.format >= 5 ? (p.pid >> 16) & 1 : p.pid & 1);
}

Result applyTraits(Pokemon& p, uint8_t nature, bool shiny, Gender gender, uint8_t slot) {
    uint32_t pid;
    if (p.format >= 5) {
        bool correlated = wildPidCorrelation5(p.pid, p.trainer.tid, p.trainer.sid);  // keep it if present
        if (!findPid5(p.species, p.trainer.tid, p.trainer.sid, shiny, gender, slot, p.pid, pid, correlated)) {
            return Result::Impossible;
        }
        p.pid = pid;
        p.nature = nature;
        if (!p.hiddenAbility) p.ability = abilityForSlot(p.species, slot);
        refreshDerived(p);
        return Result::Ok;
    }
    if (!findPid(p.species, p.trainer.tid, p.trainer.sid, nature, shiny, gender, slot, p.pid, pid)) {
        return Result::Impossible;
    }
    p.pid = pid;
    p.ability = abilityForSlot(p, slot);
    refreshDerived(p);
    return Result::Ok;
}

}  // namespace

Gender genderFromPid(uint16_t species, uint32_t pid) {
    uint8_t ratio = genderRatio(species);
    switch (ratio) {
        case data::kGenderRatioMaleOnly:
            return Gender::Male;
        case data::kGenderRatioFemaleOnly:
            return Gender::Female;
        case data::kGenderRatioGenderless:
            return Gender::Genderless;
        default:
            return (pid & 0xFF) < ratio ? Gender::Female : Gender::Male;
    }
}

bool canHaveGender(uint16_t species, Gender gender) {
    uint8_t ratio = genderRatio(species);
    switch (ratio) {
        case data::kGenderRatioMaleOnly:
            return gender == Gender::Male;
        case data::kGenderRatioFemaleOnly:
            return gender == Gender::Female;
        case data::kGenderRatioGenderless:
            return gender == Gender::Genderless;
        default:
            return gender != Gender::Genderless;
    }
}

uint8_t abilityCount(uint16_t species) {
    const data::PersonalInfo* info = personalAny(species);
    if (info == nullptr) {
        return 1;
    }
    return info->abilities[1] != 0 && info->abilities[1] != info->abilities[0] ? 2 : 1;
}

uint16_t abilityForSlot(uint16_t species, uint8_t slot) {
    const data::PersonalInfo* info = personalAny(species);
    if (info == nullptr) {
        return 0;
    }
    return abilityCount(species) == 2 ? info->abilities[slot & 1] : info->abilities[0];
}

uint8_t abilityCountFor(const Pokemon& p) {
    if (p.format != 3) return abilityCount(p.species);
    return data::ability3(p.species, 0) != data::ability3(p.species, 1) ? 2 : 1;
}

uint16_t abilityForSlot(const Pokemon& p, uint8_t slot) {
    return p.format == 3 ? data::ability3(p.species, slot) : abilityForSlot(p.species, slot);
}

bool findPid(uint16_t species, uint16_t tid, uint16_t sid, uint8_t nature, bool shiny, Gender gender,
             uint8_t abilitySlot, uint32_t seed, uint32_t& pidOut) {
    if (nature >= 25 || !canHaveGender(species, gender)) {
        return false;
    }
    uint32_t state = seed ^ 0x5A5A5A5Au;
    for (uint32_t attempt = 0; attempt < kMaxPidAttempts; ++attempt) {
        state = util::lcrngNext(state);
        uint32_t high = state >> 16;
        state = util::lcrngNext(state);
        uint32_t low = state >> 16;

        uint32_t pid;
        if (shiny) {
            // Choose the high half so that TID ^ SID ^ high ^ low is 0-7.
            uint32_t shinyXor = high & 7;
            pid = (((low ^ tid ^ sid ^ shinyXor) & 0xFFFF) << 16) | low;
        } else {
            pid = (high << 16) | low;
            if (isShiny(pid, tid, sid)) {
                continue;
            }
        }
        if (pid % 25 != nature || (pid & 1) != (abilitySlot & 1u) || genderFromPid(species, pid) != gender) {
            continue;
        }
        pidOut = pid;
        return true;
    }
    return false;
}

Result setNature(Pokemon& p, uint8_t nature) {
    if (p.format >= 5) {  // stored on its own in Gen V
        if (nature >= 25) return Result::Impossible;
        p.nature = nature;
        return Result::Ok;
    }
    return applyTraits(p, nature, p.shiny, p.gender, currentSlot(p));
}

Result setShiny(Pokemon& p, bool shiny) {
    return applyTraits(p, p.nature, shiny, p.gender, currentSlot(p));
}

Result setGender(Pokemon& p, Gender gender) {
    return applyTraits(p, p.nature, p.shiny, gender, currentSlot(p));
}

Result setAbilitySlot(Pokemon& p, uint8_t slot) {
    if (p.format >= 5 && slot == 2) {
        const data::PersonalInfo* info = data::personal5(p.species);  // the Gen IV table has no hidden abilities
        // The Gen V table repeats the first ability for species without a hidden one.
        if (info == nullptr || info->hiddenAbility == 0 || info->hiddenAbility == info->abilities[0]) {
            return Result::Impossible;
        }
        p.hiddenAbility = true;
        p.ability = info->hiddenAbility;
        return Result::Ok;
    }
    p.hiddenAbility = false;
    return applyTraits(p, p.nature, p.shiny, p.gender, slot & 1);
}

uint8_t abilitySlot(const Pokemon& p) {
    return p.format >= 5 && p.hiddenAbility ? 2 : currentSlot(p);
}

Result setSpecies(Pokemon& p, uint16_t species) {
    const data::PersonalInfo* info = p.format >= 5 ? data::personal5(species) : data::personal4(species);
    if (info == nullptr || (p.format == 3 && species > data::kMaxSpeciesGen3)) {
        return Result::Impossible;
    }
    uint8_t level = p.level;
    p.species = species;
    p.form = 0;
    setLevel(p, level);
    p.ability = p.hiddenAbility && info->hiddenAbility ? info->hiddenAbility : abilityForSlot(p, currentSlot(p));
    if (!p.hiddenAbility || !info->hiddenAbility) p.hiddenAbility = false;
    if (!p.isNicknamed) p.nickname = data::defaultNickname(species, p.language, p.format);
    refreshDerived(p);
    return Result::Ok;
}

void setLevel(Pokemon& p, uint8_t level) {
    if (level < 1) {
        level = 1;
    } else if (level > 100) {
        level = 100;
    }
    const data::PersonalInfo* info = personalAny(p.species);
    auto growth = info ? static_cast<data::GrowthRate>(info->growthRate) : data::GrowthRate::MediumFast;
    p.exp = data::expForLevel(growth, level);
    p.level = level;
}

Result setMove(Pokemon& p, int index, uint16_t move) {
    if (index < 0 || index >= 4 || move > data::maxMove(p.format)) {
        return Result::Impossible;
    }
    for (int i = 0; i < 4; ++i) {
        if (i != index && move != 0 && p.moves[i] == move) {
            return Result::Impossible;
        }
    }
    p.moves[index] = move;
    if (move == 0) {
        p.ppUps[index] = 0;
    }
    p.pp[index] = data::maxPpFor(p.format, move, p.ppUps[index]);
    return Result::Ok;
}

void setIv(Pokemon& p, Stat stat, uint8_t value) {
    p.ivs[stat] = value > 31 ? 31 : value;
}

void setEv(Pokemon& p, Stat stat, uint8_t value) {
    uint16_t others = 0;
    for (int s = 0; s < kStatCount; ++s) {
        if (s != stat) {
            others = static_cast<uint16_t>(others + p.evs[s]);
        }
    }
    uint16_t room = others >= kMaxEvTotal ? 0 : static_cast<uint16_t>(kMaxEvTotal - others);
    p.evs[stat] = static_cast<uint8_t>(value < room ? value : room);
}

void refreshDerived(Pokemon& p) {
    if (p.format < 5) p.nature = natureFromPid(p.pid);
    p.shiny = isShiny(p.pid, p.trainer.tid, p.trainer.sid);
    p.gender = genderFromPid(p.species, p.pid);
}

}  // namespace pktool::pokemon::edit4
