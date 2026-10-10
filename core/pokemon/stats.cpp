#include "core/pokemon/stats.h"

namespace pktool::pokemon {

namespace {
constexpr uint16_t kShedinja = 292;
}

uint8_t natureModifier(uint8_t nature, Stat stat) {
    if (stat == kHp || nature >= 25) {
        return 10;
    }
    int increased = nature / 5 + 1;  // +1 skips HP
    int decreased = nature % 5 + 1;
    if (increased == decreased) {
        return 10;
    }
    if (stat == increased) {
        return 11;
    }
    if (stat == decreased) {
        return 9;
    }
    return 10;
}

void calcStats(uint16_t species, const uint8_t baseStats[kStatCount], const uint8_t ivs[kStatCount],
               const uint8_t evs[kStatCount], uint8_t level, uint8_t nature, uint16_t out[kStatCount]) {
    for (int s = 0; s < kStatCount; ++s) {
        uint32_t core = (2u * baseStats[s] + ivs[s] + evs[s] / 4u) * level / 100u;
        if (s == kHp) {
            out[s] = static_cast<uint16_t>(species == kShedinja ? 1 : core + level + 10);
        } else {
            out[s] = static_cast<uint16_t>((core + 5) * natureModifier(nature, static_cast<Stat>(s)) / 10);
        }
    }
}

}  // namespace pktool::pokemon
