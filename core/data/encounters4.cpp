#include "core/data/encounters4.h"

#include <algorithm>

namespace pktool::data {

uint8_t versionBit4(uint8_t gameVersion) {
    switch (gameVersion) {
        case 10: return kVerDiamond;
        case 11: return kVerPearl;
        case 12: return kVerPlatinum;
        case 7: return kVerHeartGold;
        case 8: return kVerSoulSilver;
        default: return 0;
    }
}

void encounters4(uint16_t species, const Encounter4** begin, const Encounter4** end) {
    const Encounter4* first = generated::kEncounters4;
    const Encounter4* last = first + generated::kEncounters4Count;
    auto range = std::equal_range(first, last, species, [](const auto& a, const auto& b) {
        if constexpr (std::is_same_v<std::decay_t<decltype(a)>, Encounter4>) {
            return a.species < b;
        } else {
            return a < b.species;
        }
    });
    *begin = range.first;
    *end = range.second;
}

const char* locationName4(uint16_t location) {
    const LocationName* first = generated::kLocations4;
    const LocationName* last = first + generated::kLocations4Count;
    const LocationName* it =
        std::lower_bound(first, last, location, [](const LocationName& n, uint16_t id) { return n.id < id; });
    return (it != last && it->id == location) ? it->name : "Unknown location";
}

}  // namespace pktool::data
