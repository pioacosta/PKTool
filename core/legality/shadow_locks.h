#pragma once

#include <cstdint>

#include "core/data/locks3.h"

// Colosseum / XD Shadow Pokemon team locks, ported from PKHeX (LockFinder,
// TeamLockResult, NPCLock).
//
// The games generate the opponent's party before a Shadow Pokemon; every
// member re-rolls its PID until it satisfies a nature/gender lock and is not
// shiny. That consumes a varying number of RNG calls, so only some origin
// seeds (the RNG state before the shadow's IVs) can have produced the shadow.
// The search runs the party generation backwards from the shadow's origin.
namespace pktool::legality::shadow {

constexpr uint32_t kNoTsv = 0xFFFFFFFFu;

// True if some party of `set` can be generated before a shadow whose frame has
// origin seed `origin`. `tsv` is the player's trainer shiny value for XD (no
// shiny shadows there), kNoTsv for Colosseum. A set without teams is always
// satisfied. The search is bounded: when it gives up it reports false.
bool locksSatisfied(const data::LockSet3& set, uint32_t origin, uint32_t tsv);

// The lock set of an encounter row.
inline const data::LockSet3& setOf(const data::ShadowEnc3& enc) { return data::lockSet3(enc.set); }

}  // namespace pktool::legality::shadow
