#pragma once

#include <cstdint>

// Gen I/II move learnability, generated from PKHeX: level-up moves of Red/Blue,
// Yellow, Gold/Silver and Crystal, and the Gen II egg moves. TMs/HMs are in
// PersonalGb::machines (gen12.h).
namespace pktool::data {

struct LevelMoveGb {
    uint8_t move;
    uint8_t level;
};

// Which game's tables to use.
enum class GbGame : uint8_t { RedBlue, Yellow, GoldSilver, Crystal };

// Level-up moves of `species` in the game, as [begin, end) (ascending levels).
void levelUpGb(GbGame game, uint16_t species, const LevelMoveGb** begin, const LevelMoveGb** end);
// Lowest level at which the species itself learns the move by level-up (0 if
// it does not).
uint8_t levelUpLevelGb(GbGame game, uint16_t species, uint16_t move);
// Egg moves of `species` in Gold/Silver or Crystal ([begin, end) of move IDs).
void eggMovesGb(GbGame game, uint16_t species, const uint8_t** begin, const uint8_t** end);
bool isEggMoveGb(GbGame game, uint16_t species, uint16_t move);

namespace generated {
extern const LevelMoveGb kLevelUpRB[];
extern const uint16_t kLevelUpRBIndex[];
extern const LevelMoveGb kLevelUpY[];
extern const uint16_t kLevelUpYIndex[];
extern const LevelMoveGb kLevelUpGS[];
extern const uint16_t kLevelUpGSIndex[];
extern const LevelMoveGb kLevelUpC[];
extern const uint16_t kLevelUpCIndex[];
extern const uint8_t kEggMovesGS[];
extern const uint16_t kEggMovesGSIndex[];
extern const uint8_t kEggMovesC[];
extern const uint16_t kEggMovesCIndex[];
}  // namespace generated

}  // namespace pktool::data
