#pragma once

#include <cstdint>

// Gen I/II encounters, generated from PKHeX: wild slots, static encounters,
// gifts and NPC trades.
namespace pktool::data {

// Game bit masks. Gen I: Red, Blue (and Green), Blue (Japan), Yellow.
// Gen II: Gold, Silver, Crystal.
enum GbVersion : uint8_t {
    kGbRed = 1,
    kGbBlue = 2,
    kGbBlueJp = 4,
    kGbYellow = 8,
    kGbGold = 1,
    kGbSilver = 2,
    kGbCrystal = 4,
};

// PKHeX slot types: grass, surf, old/good/super rod; Gen II adds rock smash,
// headbutt (normal and special trees) and the bug-catching contest.
enum GbSlotKind : uint8_t {
    kSlotGrass,
    kSlotSurf,
    kSlotOldRod,
    kSlotGoodRod,
    kSlotSuperRod,
    kSlotRockSmash,
    kSlotHeadbutt,
    kSlotHeadbuttSpecial,
    kSlotBugContest
};

struct WildGb {
    uint8_t species;
    uint8_t levelMin;
    uint8_t levelMax;
    uint8_t versions;  // GbVersion bits
    uint8_t kind;      // GbSlotKind
    uint8_t location;  // Gen II location ID (0 in Gen I)
    uint8_t time;      // Gen II: 0 = any, else bits 2 (morning) 4 (day) 8 (night)
};

enum StaticFlag : uint8_t { kStaticEgg = 1, kStaticShinyAlways = 2, kStaticShinyNever = 4 };

struct StaticGb {
    uint8_t species;
    uint8_t level;
    uint8_t versions;
    uint8_t location;  // Gen II location ID
    uint8_t flags;     // StaticFlag
    uint8_t gender;    // 0 male, 1 female, 2 genderless, 255 random
    uint16_t dvs;      // fixed DVs (Atk, Def, Spe, Spc), 0xFFFF = random
    uint8_t moves[4];  // fixed moves (all 0 = the level-up moves)
};

struct TradeGb1 {
    uint8_t species;
    uint8_t versions;
    uint8_t levelRby;  // lowest level in Red/Blue/Yellow
    uint8_t levelGsc;  // lowest level when sent back with the Time Capsule
    uint8_t evolveOnTrade;
    const char* nicknames[7];  // en, ja, fr, de, it, es, ko
};

struct TradeGb2 {
    uint8_t species;
    uint8_t level;
    uint16_t tid;
    uint8_t gender;  // 0 male, 1 female, 2 genderless
    uint8_t otGender;
    uint8_t shiny;  // 0 random, 1 always, 2 never
    uint16_t dvs;
    const char* nicknames[7];
    const char* otNames[7];
};

struct GiftGb1 {
    uint8_t species;
    uint8_t level;
    uint8_t moves[4];
    uint8_t language;  // 0 any, 1 Japanese, 2 international
    uint8_t trainer;   // 0 recipient, 1 VC Mew, 2 Stadium, 3 Europe tour, 4 Japan tour
};

struct GiftGb2 {
    uint8_t species;
    uint8_t level;
    uint8_t moves[4];
    uint8_t location;
    uint8_t currentLevel;
    uint8_t shinyAlways;
    uint8_t egg;
    uint8_t language;  // 0 international, 1 Japanese, 2 English, 3 other international
    uint8_t trainer;   // 0 recipient, 1-3 Stadium (JP/EN/INT), 4 Pokemon Center New York
};

namespace generated {
extern const WildGb kWild1[];
extern const int kWild1Count;
extern const WildGb kWild2[];
extern const int kWild2Count;
extern const StaticGb kStatic1[];
extern const int kStatic1Count;
extern const StaticGb kStatic2[];
extern const int kStatic2Count;
extern const TradeGb1 kTrades1[];
extern const int kTrades1Count;
extern const TradeGb2 kTrades2[];
extern const int kTrades2Count;
extern const GiftGb1 kGifts1[];
extern const int kGifts1Count;
extern const GiftGb2 kGifts2[];
extern const int kGifts2Count;
// Headbutt trees per Gen II location: bit (trainer ID % 10) of the mask is a tree
// of the normal kind, bit (trainer ID % 10 + 12) of the special kind.
extern const uint8_t kHeadbuttLocations[];
extern const uint32_t kHeadbuttMasks[];
extern const int kHeadbuttCount;
}  // namespace generated

}  // namespace pktool::data
