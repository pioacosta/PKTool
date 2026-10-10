#pragma once

#include <cstdint>

#include "core/data/learn4.h"

// Gen III move learnability (R/S, E and FR/LG combined: level-up, egg moves,
// TM/HM, the FR/LG and Emerald tutors) and move PP, generated from PKHeX.
// Evolution data is shared with Gen IV (preEvolution4, evolveLevel4).
namespace pktool::data {

// How `species` itself can learn `move` in Gen III (no pre-evolutions).
LearnHit findLearn3(uint16_t species, uint16_t move);

// Maximum PP of a Gen III move with `ppUps` PP Ups (some moves had more PP
// before Gen IV, e.g. Recover).
uint8_t maxPp3(uint16_t move, uint8_t ppUps);

namespace generated {
extern const uint8_t kLearn3[];
extern const uint32_t kLearn3Index[];
extern const uint8_t kPp3[];
extern const int kPp3Count;
}  // namespace generated

}  // namespace pktool::data
