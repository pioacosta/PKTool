#pragma once

#include <cstdint>

// Colosseum / XD Shadow Pokemon team locks and Poke Spot areas, generated from
// PKHeX (Encounters3ColoShadow, Encounters3XDShadow, Encounters3*Teams,
// Encounters3XD slots) by tools/gen_data.py --only locks3.
//
// Before a Shadow Pokemon the game generates the NPC trainer's party; each
// member is locked to a nature and gender, which constrains the RNG frame of
// the shadow (see core/legality/shadow_locks.h).
namespace pktool::data {

struct NpcLock3 {
    uint8_t nature;
    uint8_t gender;  // 0 male, 1 female, 2 any
    uint8_t ratio;   // gender ratio byte of the species
    uint8_t state;   // 0 plain member, 1 Shadow member, 2 Shadow member already seen
};

// One possible party before the shadow, in generation order.
struct TeamLock3 {
    uint16_t firstLock;
    uint8_t lockCount;
};

// The parties an encounter can have (the encounter is valid with any of them);
// teamCount 0 means the shadow has no locks.
struct LockSet3 {
    uint16_t firstTeam;
    uint8_t teamCount;
};

enum ShadowGame3 : uint8_t { kShadowColo, kShadowXd, kShadowEReader };

struct ShadowEnc3 {
    uint16_t species;
    uint8_t level;
    uint8_t location;
    uint8_t game;  // ShadowGame3
    uint8_t set;   // index into the lock sets
};

// XD Poke Spot area: the three slots (50 / 35 / 15 %).
struct PokeSpotArea3 {
    uint8_t location;
    uint16_t species[3];
    uint8_t level[3];
};

namespace generated {
extern const NpcLock3 kNpcLocks3[];
extern const TeamLock3 kTeamLocks3[];
extern const LockSet3 kLockSets3[];
extern const ShadowEnc3 kShadowEnc3[];
extern const int kShadowEnc3Count;
extern const PokeSpotArea3 kPokeSpotAreas3[];
extern const int kPokeSpotAreas3Count;
}  // namespace generated

inline int shadowEnc3Count() { return generated::kShadowEnc3Count; }
inline const ShadowEnc3& shadowEnc3(int index) { return generated::kShadowEnc3[index]; }
inline int pokeSpotArea3Count() { return generated::kPokeSpotAreas3Count; }
inline const PokeSpotArea3& pokeSpotArea3(int index) { return generated::kPokeSpotAreas3[index]; }
inline const LockSet3& lockSet3(int index) { return generated::kLockSets3[index]; }
inline const TeamLock3& teamLock3(int index) { return generated::kTeamLocks3[index]; }
inline const NpcLock3& npcLock3(int index) { return generated::kNpcLocks3[index]; }

}  // namespace pktool::data
