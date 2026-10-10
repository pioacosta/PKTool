#pragma once

#include <cstddef>
#include <cstdint>

#include "core/pokemon/pokemon.h"

// Generation II entity ("PK2"), as stored in Gold/Silver/Crystal saves.
//
// Like PK1 the games keep a 48-byte body (32 bytes in a box), big endian, and
// separate string lists; PKTool keeps one blob per Pokemon:
//
//   0..47   body (the party layout; a box Pokemon has the last 16 bytes zero
//           until refreshed)
//   48..58  OT name (11 bytes; Japanese games use the first 6)
//   59..69  nickname (same)
//   70      language: 1 Japanese, 2 international (3-7 western languages)
//   71      egg flag (the games keep it in the list header, not in the body)
//
// Held item IDs are the Gen II ones; the model holds them as Gen IV IDs.
namespace pktool::pokemon::pk2 {

constexpr size_t kPartyBody = 48;
constexpr size_t kStoredBody = 32;
constexpr size_t kStringBytes = 11;
constexpr size_t kJapaneseStringBytes = 6;
constexpr size_t kOtOffset = kPartyBody;
constexpr size_t kNicknameOffset = kOtOffset + kStringBytes;
constexpr size_t kLanguageOffset = kNicknameOffset + kStringBytes;
constexpr size_t kEggOffset = kLanguageOffset + 1;
constexpr size_t kEntitySize = kEggOffset + 1;
constexpr size_t kPartySize = kEntitySize;
constexpr size_t kStoredSize = kEntitySize;

constexpr size_t kNicknameChars = 10;
constexpr size_t kOtNameChars = 7;
constexpr size_t kJapaneseNameChars = 5;

namespace offset {
constexpr size_t kSpecies = 0x00;
constexpr size_t kHeldItem = 0x01;  // Gen II item ID
constexpr size_t kMoves = 0x02;     // 4 bytes
constexpr size_t kTid = 0x06;
constexpr size_t kExp = 0x08;       // 24 bit
constexpr size_t kStatExp = 0x0B;   // HP, Atk, Def, Spe, Spc: 16 bit each
constexpr size_t kDvs = 0x15;
constexpr size_t kPp = 0x17;        // 4 bytes: PP in bits 0-5, PP Ups in bits 6-7
constexpr size_t kFriendship = 0x1B;  // egg cycles for an egg
constexpr size_t kPokerus = 0x1C;
constexpr size_t kCaughtData = 0x1D;  // Crystal: time of day, met level, OT gender, location
constexpr size_t kLevel = 0x1F;
constexpr size_t kStatus = 0x20;      // party only from here
constexpr size_t kHp = 0x22;
constexpr size_t kMaxHp = 0x24;       // HP, Atk, Def, Spe, SpA, SpD (16 bit)
}  // namespace offset

Pokemon toPokemon(const uint8_t* entity, size_t size);

// Applies the model onto the entity, changing only what differs from what it
// decodes to. The held item is rewritten only when it changed.
void applyPokemon(const Pokemon& p, uint8_t* entity);

// Level (stored) and party-only stats from the stored fields.
void fillPartyArea(uint8_t* entity);
void refreshPartyStats(uint8_t* entity);

// A blank entity (zeroed, terminated strings, language set).
void clear(uint8_t* entity, bool japanese);

}  // namespace pktool::pokemon::pk2
