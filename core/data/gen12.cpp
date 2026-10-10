#include "core/data/gen12.h"

#include <cstring>

namespace pktool::data {
namespace {

constexpr uint16_t kNone = 0xFFFF;

}  // namespace

const PersonalGb* personalGb1(uint16_t species, bool yellow) {
    if (species == 0 || species > kMaxSpeciesGen1) return nullptr;
    return &(yellow ? generated::kPersonalY : generated::kPersonalRB)[species];
}

const PersonalGb* personalGb2(uint16_t species, bool crystal) {
    if (species == 0 || species > kMaxSpeciesGen2) return nullptr;
    return &(crystal ? generated::kPersonalC : generated::kPersonalGS)[species];
}

const PersonalGb* personalGb(uint8_t format, uint16_t species, bool yellowOrCrystal) {
    return format == 1 ? personalGb1(species, yellowOrCrystal) : personalGb2(species, yellowOrCrystal);
}

uint16_t speciesFromGen1(uint8_t internal) {
    return generated::kSpecies1ToNational[internal];
}

uint8_t speciesToGen1(uint16_t national) {
    return national <= kMaxSpeciesGen1 ? generated::kSpeciesToInternal1[national] : 0;
}

uint16_t itemFromGen2(uint8_t item2) {
    uint16_t item4 = generated::kItem2To4[item2];
    return item4 == kNone ? 0 : item4;
}

uint8_t itemToGen2(uint16_t item4) {
    if (item4 == 0) return 0;
    for (int i = 1; i < 256; ++i) {
        if (generated::kItem2To4[i] == item4) return static_cast<uint8_t>(i);
    }
    return 0;
}

bool isHeldItem2(uint8_t item2) {
    for (int i = 0; i < generated::kHeldItems2Count; ++i) {
        if (generated::kHeldItems2[i] == item2) return true;
    }
    return false;
}

bool catchRateIsItem(uint8_t rate) {
    return (generated::kCatchRateIsItem[rate >> 3] >> (rate & 7)) & 1;
}

// pokecrystal data/items/catch_rate_items.asm: the few items that have their own
// catch rate value; every other item travels as its own ID.
uint8_t catchRateFromItem2(uint8_t item2) {
    switch (item2) {
        case 0x92: return 0x19;  // Leftovers
        case 0x53: return 0x2D;  // Bitter Berry
        case 0xAE: return 0x32;  // Gold Berry
        case 0xAD: return 0x5A;  // Berry
        default: return item2;
    }
}

uint8_t itemFromCatchRate(uint8_t rate) {
    switch (rate) {
        case 0x19: return 0x92;
        case 0x2D: return 0x53;
        case 0x32: return 0xAE;
        case 0x5A: case 0x64: case 0x78: case 0x87: case 0xBE: case 0xC3: case 0xDC: case 0xFA: case 0xFF:
            return 0xAD;
        default: return rate;
    }
}

uint8_t basePpGb(uint8_t format, uint16_t move) {
    if (format == 1) return move < generated::kPp1Count ? generated::kPp1[move] : 0;
    return move < generated::kPp2Count ? generated::kPp2[move] : 0;
}

uint8_t maxPpGb(uint8_t format, uint16_t move, uint8_t ppUps) {
    uint8_t base = basePpGb(format, move);
    if (base == 0) return 0;
    if (ppUps > 3) ppUps = 3;
    unsigned bonus = ppUps * (base / 5 < 7 ? base / 5 : 7);
    return static_cast<uint8_t>(base + bonus);
}

int machineCountGb(uint8_t format) {
    return format == 1 ? generated::kMachines1Count : generated::kMachines2Count;
}

uint16_t machineMoveGb(uint8_t format, int index) {
    if (index < 0 || index >= machineCountGb(format)) return 0;
    return format == 1 ? generated::kMachines1[index] : generated::kMachines2[index];
}

bool learnsMachineGb(const PersonalGb& personal, uint8_t format, uint16_t move) {
    const int count = machineCountGb(format);
    for (int i = 0; i < count; ++i) {
        if (machineMoveGb(format, i) == move) return (personal.machines[i >> 3] >> (i & 7)) & 1;
    }
    return false;
}

uint8_t evolutionStage1(uint16_t species) {
    return species <= kMaxSpeciesGen1 ? generated::kEvoStage1[species] : 0;
}

const char* typeNameGb(uint8_t type) {
    static const char* const kNames[] = {"Normal", "Fighting", "Flying", "Poison", "Ground", "Rock", "Bird", "Bug", "Ghost", "Steel"};
    static const char* const kElemental[] = {"Fire", "Water", "Grass", "Electric", "Psychic", "Ice", "Dragon", "Dark"};
    if (type < 10) return kNames[type];
    if (type >= 20 && type < 28) return kElemental[type - 20];
    return "???";
}

const char* itemNameGen2(uint8_t item2) {
    return item2 < generated::kItemNames2Count ? generated::kItemNames2[item2] : "???";
}

const char* itemNameGen1(uint8_t item1) {
    return item1 < generated::kItemNames1Count ? generated::kItemNames1[item1] : "???";
}

const char* locationName2(uint16_t location) {
    if (location < generated::kLocations2Count && generated::kLocations2[location][0] != '\0') {
        return generated::kLocations2[location];
    }
    return "???";
}

uint16_t charGb(uint8_t format, uint8_t language, uint8_t byte) {
    if (format == 1) return (language == 1 ? generated::kGen1CharsJp : generated::kGen1CharsEn)[byte];
    switch (language) {
        case 1: return generated::kGen2CharsJp[byte];
        case 3:
        case 5: return generated::kGen2CharsFre[byte];
        case 4:
        case 7: return generated::kGen2CharsIta[byte];
        default: return generated::kGen2CharsEn[byte];
    }
}

}  // namespace pktool::data
