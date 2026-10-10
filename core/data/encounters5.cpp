#include "core/data/encounters5.h"

#include <algorithm>

namespace pktool::data {

uint8_t versionBit5(uint8_t gameVersion) {
    switch (gameVersion) {
        case 21: return kVerBlack;
        case 20: return kVerWhite;
        case 23: return kVerBlack2;
        case 22: return kVerWhite2;
        default: return 0;
    }
}

void encounters5(uint16_t species, const Encounter4** begin, const Encounter4** end) {
    const Encounter4* first = generated::kEncounters5;
    const Encounter4* last = first + generated::kEncounters5Count;
    *begin = std::lower_bound(first, last, species, [](const Encounter4& e, uint16_t s) { return e.species < s; });
    *end = std::upper_bound(*begin, last, species, [](uint16_t s, const Encounter4& e) { return s < e.species; });
}

const char* locationName5(uint16_t location) {
    const LocationName* first = generated::kLocations5;
    const LocationName* last = first + generated::kLocations5Count;
    const LocationName* it =
        std::lower_bound(first, last, location, [](const LocationName& n, uint16_t id) { return n.id < id; });
    return (it != last && it->id == location) ? it->name : "Unknown location";
}

}  // namespace pktool::data
