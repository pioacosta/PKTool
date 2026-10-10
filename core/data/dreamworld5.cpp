#include "core/data/dreamworld5.h"

#include <algorithm>

namespace pktool::data {

void dreamWorld5(uint16_t species, const DreamWorld5** begin, const DreamWorld5** end) {
    const DreamWorld5* first = generated::kDreamWorld5;
    const DreamWorld5* last = first + generated::kDreamWorld5Count;
    *begin = std::lower_bound(first, last, species, [](const DreamWorld5& e, uint16_t s) { return e.species < s; });
    *end = std::upper_bound(*begin, last, species, [](uint16_t s, const DreamWorld5& e) { return s < e.species; });
}

const DreamRadar5* dreamRadarTable5(int* count) {
    *count = generated::kDreamRadar5Count;
    return generated::kDreamRadar5;
}

const DreamRadar5* dreamRadar5(uint16_t species) {
    for (int i = 0; i < generated::kDreamRadar5Count; ++i) {
        if (generated::kDreamRadar5[i].species == species) return &generated::kDreamRadar5[i];
    }
    return nullptr;
}

const NPokemon5* nPokemon5(int* count) {
    *count = generated::kNPokemon5Count;
    return generated::kNPokemon5;
}

}  // namespace pktool::data
