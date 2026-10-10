#pragma once

#include <cstddef>
#include <cstdint>

#include "core/pokemon/pokemon.h"

// Ribbons of Gen III-V entities, read and written directly on the decrypted
// raw bytes (the raw entity stays the source of truth; nothing else moves).
//
// Each format has a fixed table of "ribbons", numbered 0..count(format)-1:
//   - a flag has the value 0 or 1;
//   - a Gen III contest ribbon is a rank 0..4 (None, Normal, Super, Hyper,
//     Master) kept in 3 bits per contest category.
// Gen IV and Gen V share the layout: the Sinnoh set at 0x24 (28 bits), the
// Hoenn set at 0x3C (32 bits, contest ranks are one bit each) and the Sinnoh
// contest set at 0x60 (20 bits). Gen III keeps everything in the 32-bit word
// at 0x4C (bits 27-30 are unused, bit 31 is the fateful-encounter flag).
//
// Only the game's own limits apply: PKHeX's legality rules for ribbons are not
// enforced here.
namespace pktool::pokemon::ribbons {

// Number of ribbons in the table of `format` (3, 4 or 5); 0 for other formats.
int count(uint8_t format);

const char* name(uint8_t format, int index);

// True for the Gen III contest ranks (value 0..4), false for flags.
bool isRank(uint8_t format, int index);

// Highest value of a ribbon: 1 for a flag, 4 for a Gen III rank.
int maxValue(uint8_t format, int index);

// Label of a Gen III rank value ("None" ... "Master").
const char* rankName(int value);

// Value of a ribbon in decrypted data (0 when `index` is out of range). A
// Gen III rank of 5-7 (only possible in edited saves) is returned as stored.
int get(uint8_t format, const uint8_t* decrypted, int index);

// Sets a ribbon (values above maxValue are clamped), then refreshes the
// entity's checksum. Every other bit is left alone. Returns false for an
// out-of-range index.
bool set(uint8_t format, uint8_t* decrypted, int index, int value);

// How many ribbons the Pokémon wears: every set flag, plus each Gen III rank
// counted as its value (Master = 4 ribbons).
int total(uint8_t format, const uint8_t* decrypted);

// ----- Snapshot in the Pokemon view ------------------------------------------------
// Pokemon::ribbonWords holds the raw ribbon words (Gen III: the word at 0x4C
// without the fateful bit; Gen IV/V: the words at 0x24, 0x3C and 0x60), so the
// legality checker and LEGALIZE can read and change ribbons without the raw
// bytes. `ribbons::kWordMask*` are the bits that really are ribbons.
int getWords(uint8_t format, const uint32_t* words, int index);
void setWords(uint8_t format, uint32_t* words, int index, int value);
int totalWords(uint8_t format, const uint32_t* words);

// Fills p.ribbonFormat/ribbonWords from decrypted data of p.format.
void snapshot(const uint8_t* decrypted, Pokemon& p);
// Writes the snapshot back (no-op when p.ribbonFormat does not match p.format;
// does not refresh the checksum, the caller's apply function does).
void restore(const Pokemon& p, uint8_t* decrypted);

}  // namespace pktool::pokemon::ribbons
