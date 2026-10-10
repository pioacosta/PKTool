#pragma once

#include <cstdint>

namespace pktool::data {

constexpr uint16_t kMaxMoveGen4 = 467;
constexpr uint16_t kMaxMoveGen5 = 559;

// Base PP of a Gen IV move (0 for "no move" or unknown IDs).
uint8_t basePp4(uint16_t move);

// PP of a move with `ppUps` PP Ups applied (0-3): base * (5 + ppUps) / 5.
uint8_t maxPp4(uint16_t move, uint8_t ppUps);

// Gen V equivalents (moves up to 559).
uint8_t basePp5(uint16_t move);
uint8_t maxPp5(uint16_t move, uint8_t ppUps);

// By generation (4 or 5).
inline uint16_t maxMove(uint8_t generation) {
    // 354 = Psycho Boost (Gen III), 165 / 251 the last moves of Gen I / II.
    return generation >= 5 ? kMaxMoveGen5 : generation == 3 ? 354 : generation == 2 ? 251 : generation == 1 ? 165 : kMaxMoveGen4;
}
uint8_t maxPpFor(uint8_t generation, uint16_t move, uint8_t ppUps);

namespace generated {
extern const uint8_t kMovePP4[];
extern const int kMovePP4Count;
extern const uint8_t kMovePP5[];
extern const int kMovePP5Count;
}  // namespace generated

}  // namespace pktool::data
