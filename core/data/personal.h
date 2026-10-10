#pragma once

#include <cstdint>

// Per-species game data ("personal info" in the games' own terminology).
namespace pktool::data {

enum class GrowthRate : uint8_t {
    MediumFast = 0,
    Erratic = 1,
    Fluctuating = 2,
    MediumSlow = 3,
    Fast = 4,
    Slow = 5,
};

// Gender ratio sentinel values stored in PersonalInfo::genderRatio.
constexpr uint8_t kGenderRatioMaleOnly = 0;
constexpr uint8_t kGenderRatioFemaleOnly = 254;
constexpr uint8_t kGenderRatioGenderless = 255;

struct PersonalInfo {
    uint8_t baseStats[6];  // HP, Atk, Def, Spe, SpA, SpD
    uint8_t genderRatio;   // PID gender threshold, or one of the sentinels above
    uint8_t growthRate;    // GrowthRate
    uint8_t abilities[2];
    uint8_t eggGroups[2];
    uint8_t hiddenAbility;  // Gen V Dream World ability (0 in the Gen IV table)
    uint8_t hatchCycles;    // egg cycles (an unhatched egg's friendship counts them down)
};

// Egg group of species that cannot breed (legendaries, babies' "No Eggs").
constexpr uint8_t kEggGroupUndiscovered = 15;

// Base-form Gen IV data. Returns nullptr for species outside 1-493.
const PersonalInfo* personal4(uint16_t species);

// Base-form Gen V data (Black 2/White 2). nullptr outside 1-649.
const PersonalInfo* personal5(uint16_t species);

// Personal data for the given generation (4 or 5).
const PersonalInfo* personalFor(uint8_t generation, uint16_t species);

// Total EXP required to reach `level` (1-100) with the given growth rate.
uint32_t expForLevel(GrowthRate growth, uint8_t level);

// Level reached with `exp` points (1-100).
uint8_t levelForExp(GrowthRate growth, uint32_t exp);

namespace generated {
extern const PersonalInfo kPersonal4[];
extern const int kPersonal4Count;
extern const PersonalInfo kPersonal5[];
extern const int kPersonal5Count;
extern const uint32_t kExpTable[6][100];
}  // namespace generated

}  // namespace pktool::data
