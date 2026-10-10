#include "core/data/items.h"

#include "core/data/language.h"

namespace pktool::data {

BagSpec bagSpec(BagLayout layout) {
    switch (layout) {
        case BagLayout::RubySapphire: return {generated::kBagRS, generated::kBagRSCount};
        case BagLayout::Emerald: return {generated::kBagE, generated::kBagECount};
        case BagLayout::FireRedLeafGreen: return {generated::kBagFRLG, generated::kBagFRLGCount};
        case BagLayout::DiamondPearl: return {generated::kBagDP, generated::kBagDPCount};
        case BagLayout::Platinum: return {generated::kBagPt, generated::kBagPtCount};
        case BagLayout::HeartGoldSoulSilver: return {generated::kBagHGSS, generated::kBagHGSSCount};
        case BagLayout::BlackWhite: return {generated::kBagBW, generated::kBagBWCount};
        case BagLayout::Black2White2: return {generated::kBagB2W2, generated::kBagB2W2Count};
        case BagLayout::Gen1: return {generated::kBagGen1, generated::kBagGen1Count};
        case BagLayout::Gen2GoldSilver: return {generated::kBagGen2GS, generated::kBagGen2GSCount};
        case BagLayout::Gen2Crystal: return {generated::kBagGen2C, generated::kBagGen2CCount};
        case BagLayout::None: return {nullptr, 0};
    }
    return {nullptr, 0};
}

bool pocketAllows(const PocketSpec& pocket, uint16_t item) {
    for (int i = 0; i < pocket.itemCount; ++i) {
        if (pocket.items[i] == item) return true;
    }
    return false;
}

uint16_t pocketMaxCount(const PocketSpec& pocket, uint16_t) {
    // Only the game's own per-slot limit counts: PKHeX's rule that HMs hold
    // one (singleFrom..singleTo) is a legality matter and is not enforced.
    return pocket.maxCount;
}

const char* itemNameGen3(uint16_t item) {
    if (const char* local = packName(NameTable::ItemGen3, item)) return local;
    return item < generated::kItemNames3Count ? generated::kItemNames3[item] : "???";
}

}  // namespace pktool::data
