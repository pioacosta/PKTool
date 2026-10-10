#pragma once

#include <cstdint>

// Bag pockets of every supported game, generated from PKHeX's ItemStorage*
// and PlayerBag* classes: which items each pocket may hold, where its slots
// are stored and its quantity limits. Gen III saves use Gen III item IDs
// (their names come from itemNameGen3), Gen IV and V use their own IDs
// (names from itemName).
namespace pktool::data {

enum class PocketKind : uint8_t { Items, Medicine, Balls, TMsHMs, Berries, Mail, BattleItems, KeyItems };

struct PocketSpec {
    const char* name;
    PocketKind kind;
    uint16_t offset;      // first slot, relative to the start of the bag
    uint16_t capacity;    // number of slots
    uint16_t maxCount;    // quantity limit of one item
    uint16_t singleFrom;  // items singleFrom..singleTo (HMs): PKHeX legality only, not enforced
    uint16_t singleTo;
    const uint16_t* items;  // every item the pocket may hold
    uint16_t itemCount;
};

struct BagSpec {
    const PocketSpec* pockets;
    int pocketCount;
};

// One layout per game family, in the order of the generated tables.
enum class BagLayout : uint8_t {
    RubySapphire, Emerald, FireRedLeafGreen, DiamondPearl, Platinum, HeartGoldSoulSilver, BlackWhite, Black2White2,
    Gen1, Gen2GoldSilver, Gen2Crystal,  // Game Boy list bags (slot offsets live in the save classes)
    None
};

BagSpec bagSpec(BagLayout layout);

// Whether `item` may be stored in the pocket.
bool pocketAllows(const PocketSpec& pocket, uint16_t item);
// Quantity limit of `item` in the pocket: the game's per-slot limit (maxCount).
uint16_t pocketMaxCount(const PocketSpec& pocket, uint16_t item);

// English name of a Gen III item ID ("???" when out of range).
const char* itemNameGen3(uint16_t item);

namespace generated {
extern const PocketSpec kBagRS[];
extern const int kBagRSCount;
extern const PocketSpec kBagE[];
extern const int kBagECount;
extern const PocketSpec kBagFRLG[];
extern const int kBagFRLGCount;
extern const PocketSpec kBagDP[];
extern const int kBagDPCount;
extern const PocketSpec kBagPt[];
extern const int kBagPtCount;
extern const PocketSpec kBagHGSS[];
extern const int kBagHGSSCount;
extern const PocketSpec kBagBW[];
extern const int kBagBWCount;
extern const PocketSpec kBagB2W2[];
extern const int kBagB2W2Count;
extern const PocketSpec kBagGen1[];
extern const int kBagGen1Count;
extern const PocketSpec kBagGen2GS[];
extern const int kBagGen2GSCount;
extern const PocketSpec kBagGen2C[];
extern const int kBagGen2CCount;
extern const char* const kItemNames3[];
extern const int kItemNames3Count;
}  // namespace generated

}  // namespace pktool::data
