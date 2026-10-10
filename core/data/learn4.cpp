#include "core/data/learn4.h"

#include "core/data/names.h"

namespace pktool::data {

LearnHit decodeLearn(const uint8_t* p, const uint8_t* end, uint16_t move) {
    unsigned current = 0;
    while (p < end) {
        const unsigned head = *p++;
        unsigned delta = head >> 4;
        const uint8_t sources = head & 0x0F;
        if (delta == 0) {
            delta = *p++ + 16u;
            if (delta == 255 + 16u) {
                delta = p[0] | (p[1] << 8);
                p += 2;
            }
        }
        const uint8_t level = (sources & kLearnLevelUp) ? *p++ : 0;
        current += delta;
        if (current == move) return LearnHit(LearnEntry{move, sources, level});
        if (current > move) break;
    }
    return LearnHit();
}

LearnHit findLearn4(uint16_t species, uint16_t move) {
    if (species == 0 || species > kMaxSpeciesGen4) {
        return LearnHit();
    }
    return decodeLearn(generated::kLearn4 + generated::kLearn4Index[species],
                       generated::kLearn4 + generated::kLearn4Index[species + 1], move);
}

uint16_t preEvolution4(uint16_t species) {
    return species <= kMaxSpeciesGen4 ? generated::kPreEvolution4[species] : 0;
}

uint8_t evolveLevel4(uint16_t species) {
    return species <= kMaxSpeciesGen4 ? generated::kEvolveLevel4[species] : 0;
}

uint16_t baseSpecies4(uint16_t species) {
    for (int guard = 0; guard < 4; ++guard) {
        uint16_t pre = preEvolution4(species);
        if (pre == 0) break;
        species = pre;
    }
    return species;
}

}  // namespace pktool::data
