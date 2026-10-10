#include "core/data/moves.h"

#include "core/data/gen12.h"
#include "core/data/learn3.h"

namespace pktool::data {

uint8_t basePp4(uint16_t move) {
    return move < generated::kMovePP4Count ? generated::kMovePP4[move] : 0;
}

uint8_t maxPp4(uint16_t move, uint8_t ppUps) {
    if (ppUps > 3) {
        ppUps = 3;
    }
    return static_cast<uint8_t>(basePp4(move) * (5 + ppUps) / 5);
}

uint8_t basePp5(uint16_t move) {
    return move < generated::kMovePP5Count ? generated::kMovePP5[move] : 0;
}

uint8_t maxPp5(uint16_t move, uint8_t ppUps) {
    if (ppUps > 3) {
        ppUps = 3;
    }
    return static_cast<uint8_t>(basePp5(move) * (5 + ppUps) / 5);
}

uint8_t maxPpFor(uint8_t generation, uint16_t move, uint8_t ppUps) {
    if (generation <= 2) return maxPpGb(generation, move, ppUps);
    if (generation == 3) return maxPp3(move, ppUps);
    return generation >= 5 ? maxPp5(move, ppUps) : maxPp4(move, ppUps);
}

}  // namespace pktool::data
