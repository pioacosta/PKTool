#include "core/data/gen3.h"

namespace pktool::data {
namespace {
constexpr uint16_t kItemNone = 0xFFFF;
}  // namespace

uint16_t speciesFromGen3(uint16_t internal) {
    return internal < generated::kSpecies3ToNationalCount ? generated::kSpecies3ToNational[internal] : 0;
}

uint16_t speciesToGen3(uint16_t national) {
    if (national == 0 || national > kMaxSpeciesGen3) return 0;
    if (national < 252) return national;  // the first 251 are aligned
    for (int i = 252; i < generated::kSpecies3ToNationalCount; ++i) {
        if (generated::kSpecies3ToNational[i] == national) return static_cast<uint16_t>(i);
    }
    return 0;
}

uint16_t itemFromGen3(uint16_t item3) {
    if (item3 >= generated::kItem3To4Count) return 0;
    uint16_t item4 = generated::kItem3To4[item3];
    return item4 == kItemNone ? 0 : item4;
}

uint16_t itemToGen3(uint16_t item4) {
    if (item4 == 0) return 0;
    for (int i = 1; i < generated::kItem3To4Count; ++i) {
        if (generated::kItem3To4[i] == item4) return static_cast<uint16_t>(i);
    }
    return 0;
}

uint16_t ability3(uint16_t species, uint8_t slot) {
    if (species == 0 || species > kMaxSpeciesGen3) return 0;
    const uint8_t* a = generated::kAbilities3[species];
    return (slot & 1) && a[1] != 0 ? a[1] : a[0];
}

}  // namespace pktool::data
