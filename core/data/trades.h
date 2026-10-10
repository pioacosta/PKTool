#pragma once
#include <cstdint>

// In-game (NPC) trades of Gen III-V, generated from PKHeX (EncounterTrade3,
// EncounterTrade4PID, EncounterTrade5BW, EncounterTrade5B2W2): the Pokemon
// the player receives, with a fixed trainer, ID and, in most games, fixed
// PID and IVs. Rebuild with tools/gen_data.py --only trades345.
namespace pktool::data {

enum : uint8_t {
    kTradeNickFixed = 1,  // the received Pokemon carries the NPC's nickname
    kTradeHasPid = 2,     // fixed PID (the nature and gender come from it in Gen III/IV)
    kTradeAnyGender = 4,  // gender not fixed (Gen V Yancy / Curtis trades)
};

struct Trade345 {
    uint8_t gen;          // 3, 4 or 5
    uint16_t species;     // the species received
    uint8_t level;
    uint8_t form;
    uint32_t versions;    // bit mask: 1 << PKTool version id of the games with this trade
    uint32_t pid;         // valid when kTradeHasPid
    uint16_t tid;
    uint16_t sid;
    uint8_t otGender;     // 0 male, 1 female
    uint8_t gender;       // 0 male, 1 female, 2 genderless, 3 any
    uint8_t nature;       // 255 = any
    uint8_t flags;        // kTrade*
    uint8_t metLocation;  // fixed met location (HG/SS Webster / Kirk), 0 = the NPC trade location
    uint8_t ivs[6];       // HP, Atk, Def, Spe, SpA, SpD; 255 = any
    uint16_t moves[4];    // fixed moves (HG/SS), 0 = the level-up moves
    const char* nickname[9];  // UTF-8, by language ID (empty = none)
    const char* ot[9];
};

int trades345Count();
const Trade345& trade345(int index);

namespace generated {
extern const Trade345 kTrades345[];
extern const int kTrades345Count;
}  // namespace generated

}  // namespace pktool::data
