#include "core/data/learn3.h"

#include "core/data/gen3.h"

namespace pktool::data {

LearnHit findLearn3(uint16_t species, uint16_t move) {
    if (species == 0 || species > kMaxSpeciesGen3) return LearnHit();
    return decodeLearn(generated::kLearn3 + generated::kLearn3Index[species],
                       generated::kLearn3 + generated::kLearn3Index[species + 1], move);
}

uint8_t maxPp3(uint16_t move, uint8_t ppUps) {
    if (move >= generated::kPp3Count) return 0;
    if (ppUps > 3) ppUps = 3;
    return static_cast<uint8_t>(generated::kPp3[move] * (5 + ppUps) / 5);
}

}  // namespace pktool::data
