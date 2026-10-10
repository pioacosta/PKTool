#pragma once

#include <cstdint>

#include "core/data/learn4.h"
#include "core/data/names.h"

// Gen V move learnability (B/W and B2/W2 combined) and evolution data,
// generated from PKHeX. Same entry layout as the Gen IV tables.
namespace pktool::data {

// kMaxSpeciesGen5 comes from core/data/names.h.

// How `species` itself can learn `move` in Gen V (no pre-evolutions).
LearnHit findLearn5(uint16_t species, uint16_t move);
uint16_t preEvolution5(uint16_t species);
uint8_t evolveLevel5(uint16_t species);
uint16_t baseSpecies5(uint16_t species);

namespace generated {
extern const uint8_t kLearn5[];
extern const uint32_t kLearn5Index[];
extern const uint16_t kPreEvolution5[];
extern const uint8_t kEvolveLevel5[];
}  // namespace generated

}  // namespace pktool::data
