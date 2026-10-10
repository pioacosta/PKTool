#include "core/data/learn5.h"

namespace pktool::data {

LearnHit findLearn5(uint16_t species, uint16_t move) {
    if (species == 0 || species > kMaxSpeciesGen5) return LearnHit();
    return decodeLearn(generated::kLearn5 + generated::kLearn5Index[species],
                       generated::kLearn5 + generated::kLearn5Index[species + 1], move);
}

uint16_t preEvolution5(uint16_t species) {
    return species <= kMaxSpeciesGen5 ? generated::kPreEvolution5[species] : 0;
}

uint8_t evolveLevel5(uint16_t species) {
    return species <= kMaxSpeciesGen5 ? generated::kEvolveLevel5[species] : 0;
}

uint16_t baseSpecies5(uint16_t species) {
    for (int guard = 0; guard < 4; ++guard) {
        uint16_t pre = preEvolution5(species);
        if (pre == 0) break;
        species = pre;
    }
    return species;
}

}  // namespace pktool::data
