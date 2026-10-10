#pragma once

#include <cstdint>

#include "core/data/gen12.h"
#include "core/pokemon/pokemon.h"

// Rules shared by the Gen I (PK1) and Gen II (PK2) entities: Determinant
// Values (DVs) instead of IVs, Stat Exp instead of EVs, no PID. Shininess and
// gender (Gen II) come from the DVs, the HP DV from the low bits of the others.
//
// In the model (Pokemon): ivs[] holds the DVs 0-15 (kHp derived, kAtk, kDef,
// kSpe, and the Special DV in both kSpA and kSpD), statExp[] the Stat Exp
// (kSpA is the Special Stat Exp, kSpD mirrors it), evs[] stays zero.
namespace pktool::pokemon::gb {

constexpr int kDvMax = 15;
constexpr uint16_t kStatExpMax = 65535;

// Game Boy entities are big endian.
inline uint16_t readBe16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
inline void writeBe16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
}
inline uint32_t readBe24(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[2];
}
inline void writeBe24(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 16);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v);
}

// DV word as stored: Atk 12-15, Def 8-11, Spe 4-7, Spc 0-3.
uint16_t packDvs(const uint8_t ivs[kStatCount]);
// Fills ivs[] from the DV word (HP derived, Special in kSpA and kSpD).
void unpackDvs(uint16_t dv16, uint8_t ivs[kStatCount]);
uint8_t hpDv(const uint8_t ivs[kStatCount]);

// Gen II: Atk DV 2/3/6/7/10/11/14/15 with Def, Spe and Spc DVs of 10.
bool isShinyDvs(const uint8_t ivs[kStatCount]);
// Gen II gender from the Attack DV and the species' ratio (Genderless for Gen I
// and genderless species).
Gender genderFromDvs(uint16_t species, const uint8_t ivs[kStatCount]);
// Unown letter (0-25) from the DVs.
uint8_t unownForm(const uint8_t ivs[kStatCount]);

// Battle stats at `level` (HP, Atk, Def, Spe, SpA, SpD). Gen I's Special is
// used for both special stats.
void calcStats(uint8_t format, uint16_t species, bool yellowOrCrystal, const uint8_t ivs[kStatCount],
               const uint16_t statExp[kStatCount], uint8_t level, uint16_t out[kStatCount]);

// Level of an EXP total for the species' growth rate (1-100).
uint8_t levelFor(uint8_t format, uint16_t species, uint32_t exp);
uint32_t expFor(uint8_t format, uint16_t species, uint8_t level);

}  // namespace pktool::pokemon::gb
