#pragma once

#include <cstddef>
#include <cstdint>

#include "core/pokemon/pokemon.h"

// Generation I entity ("PK1"), as stored in Red/Blue/Yellow saves.
//
// The games store a Pokemon as a 44-byte body (33 bytes in a box), big endian,
// with the OT name and nickname in separate string lists. PKTool keeps one
// self-contained blob per Pokemon, the way PKHeX's single-slot lists do:
//
//   0..43   body (the party layout; a box Pokemon has the last 11 bytes zero
//           until refreshed)
//   44..54  OT name (11 bytes; Japanese games use the first 6)
//   55..65  nickname (same)
//   66      language: 1 Japanese, 2 international (3-7 for the western languages)
//
// There is no checksum, PID, nature, ability or held item; the catch rate
// byte becomes the held item when the Pokemon goes through the Time Capsule.
namespace pktool::pokemon::pk1 {

constexpr size_t kPartyBody = 44;
constexpr size_t kStoredBody = 33;
constexpr size_t kStringBytes = 11;
constexpr size_t kJapaneseStringBytes = 6;
constexpr size_t kOtOffset = kPartyBody;
constexpr size_t kNicknameOffset = kOtOffset + kStringBytes;
constexpr size_t kLanguageOffset = kNicknameOffset + kStringBytes;
constexpr size_t kEntitySize = kLanguageOffset + 1;
constexpr size_t kPartySize = kEntitySize;
constexpr size_t kStoredSize = kEntitySize;

constexpr size_t kNicknameChars = 10;
constexpr size_t kOtNameChars = 7;
constexpr size_t kJapaneseNameChars = 5;

namespace offset {
constexpr size_t kSpecies = 0x00;     // internal species ID
constexpr size_t kHp = 0x01;          // current HP (16 bit)
constexpr size_t kBoxLevel = 0x03;
constexpr size_t kStatus = 0x04;
constexpr size_t kType1 = 0x05;
constexpr size_t kType2 = 0x06;
constexpr size_t kCatchRate = 0x07;
constexpr size_t kMoves = 0x08;       // 4 bytes
constexpr size_t kTid = 0x0C;
constexpr size_t kExp = 0x0E;         // 24 bit
constexpr size_t kStatExp = 0x11;     // HP, Atk, Def, Spe, Spc: 16 bit each
constexpr size_t kDvs = 0x1B;
constexpr size_t kPp = 0x1D;          // 4 bytes: PP in bits 0-5, PP Ups in bits 6-7
constexpr size_t kLevel = 0x21;       // party only
constexpr size_t kMaxHp = 0x22;       // party only: HP, Atk, Def, Spe, Spc (16 bit)
}  // namespace offset

// Decoded entity -> model (format = 1). The level is derived from EXP.
Pokemon toPokemon(const uint8_t* entity, size_t size);

// Applies the model onto the entity, changing only what differs from what the
// entity decodes to (so values PKTool cannot represent survive). Types are set
// from the species when it changed.
void applyPokemon(const Pokemon& p, uint8_t* entity);

// Writes the party-only part (level and battle stats) from the stored fields,
// leaving the stored 33 bytes alone. Used when a box Pokemon is read.
void fillPartyArea(uint8_t* entity);

// Same, and also clamps the current HP to the new maximum (a Pokemon moved
// into the party or edited there).
void refreshPartyStats(uint8_t* entity);

// A fresh entity of `species` (internal blob of kEntitySize bytes): the personal
// data's types, catch rate and starting moves at level 5, all PP full.
void clear(uint8_t* entity, bool japanese);

}  // namespace pktool::pokemon::pk1
