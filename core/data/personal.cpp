#include "core/data/personal.h"

namespace pktool::data {
namespace {

const uint32_t* expTable(GrowthRate growth) {
    auto index = static_cast<uint8_t>(growth);
    if (index > 5) {
        index = 0;
    }
    return generated::kExpTable[index];
}

}  // namespace

const PersonalInfo* personal4(uint16_t species) {
    if (species == 0 || species >= generated::kPersonal4Count) {
        return nullptr;
    }
    return &generated::kPersonal4[species];
}

const PersonalInfo* personal5(uint16_t species) {
    if (species == 0 || species >= generated::kPersonal5Count) {
        return nullptr;
    }
    return &generated::kPersonal5[species];
}

const PersonalInfo* personalFor(uint8_t generation, uint16_t species) {
    return generation >= 5 ? personal5(species) : personal4(species);
}

uint32_t expForLevel(GrowthRate growth, uint8_t level) {
    if (level < 1) {
        level = 1;
    } else if (level > 100) {
        level = 100;
    }
    return expTable(growth)[level - 1];
}

uint8_t levelForExp(GrowthRate growth, uint32_t exp) {
    const uint32_t* table = expTable(growth);
    if (exp >= table[99]) {
        return 100;
    }
    uint8_t level = 1;
    while (exp >= table[level]) {
        ++level;
    }
    return level;
}

}  // namespace pktool::data
