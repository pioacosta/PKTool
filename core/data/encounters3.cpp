#include "core/data/encounters3.h"

#include <algorithm>

namespace pktool::data {

uint8_t versionBit3(uint8_t gameVersion) {
    switch (gameVersion) {
        case 1: return kVerSapphire;
        case 2: return kVerRuby;
        case 3: return kVerEmerald;
        case 4: return kVerFireRed;
        case 5: return kVerLeafGreen;
        default: return 0;
    }
}

void encounters3(uint16_t species, const Encounter4** begin, const Encounter4** end) {
    const Encounter4* first = generated::kEncounters3;
    const Encounter4* last = first + generated::kEncounters3Count;
    *begin = std::lower_bound(first, last, species, [](const Encounter4& e, uint16_t s) { return e.species < s; });
    *end = std::upper_bound(*begin, last, species, [](uint16_t s, const Encounter4& e) { return s < e.species; });
}

const char* locationName3(uint16_t location) {
    if (location < generated::kLocations3Count && generated::kLocations3[location][0] != '\0') {
        return generated::kLocations3[location];
    }
    return "Unknown location";
}

}  // namespace pktool::data
