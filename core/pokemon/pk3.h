#pragma once

#include <cstddef>
#include <cstdint>

#include "core/pokemon/pokemon.h"

// Generation III entity format ("PK3"), as stored in R/S/E/FR/LG saves.
//
// Layout (decrypted): a 32-byte header (PID, trainer IDs, nickname,
// language, OT name, checksum) followed by four 12-byte substructures
// (growth, attacks, EVs/contest, misc), plus 20 bytes of battle stats for
// party members. At rest the substructures are shuffled by PID % 24 and
// XOR-encrypted with PID ^ trainer ID; the party stats are not encrypted.
// Species and items use Gen III's own IDs, converted to National Dex /
// Gen IV IDs in the model (see core/data/gen3.h).
namespace pktool::pokemon::pk3 {

constexpr size_t kStoredSize = 80;
constexpr size_t kPartySize = 100;

// In-place conversion between the encrypted (at rest) and decrypted forms.
// Only the first 80 bytes are affected.
void decrypt(uint8_t* data);
void encrypt(uint8_t* data);

// 16-bit sum of the decrypted words at 0x20-0x4F.
uint16_t calcChecksum(const uint8_t* decrypted);
uint16_t storedChecksum(const uint8_t* decrypted);
void refreshChecksum(uint8_t* decrypted);

// True when a slot holds no Pokémon (the "has species" flag is clear).
bool isEmptySlot(const uint8_t* data);

// Decrypted data -> model (format = 3). The level is derived from EXP.
Pokemon toPokemon(const uint8_t* decrypted, size_t size);

// Applies the stored fields of `pkm` onto decrypted data, leaving unmodelled
// bytes untouched, then refreshes the checksum. Names, species and item are
// only rewritten when they changed (so unconvertible values survive).
void applyPokemon(const Pokemon& pkm, uint8_t* decrypted);

// Recomputes party level and battle stats (`decrypted` is kPartySize bytes).
void refreshPartyStats(uint8_t* decrypted);

namespace offset {
constexpr size_t kPid = 0x00;
constexpr size_t kTid = 0x04;
constexpr size_t kSid = 0x06;
constexpr size_t kNickname = 0x08;  // 10 characters
constexpr size_t kLanguage = 0x12;
constexpr size_t kFlags = 0x13;     // bad egg (bit 0), has species (bit 1), egg (bit 2)
constexpr size_t kOtName = 0x14;    // 7 characters
constexpr size_t kChecksum = 0x1C;
constexpr size_t kSpecies = 0x20;   // Gen III internal ID
constexpr size_t kHeldItem = 0x22;  // Gen III item ID
constexpr size_t kExp = 0x24;
constexpr size_t kPpUps = 0x28;     // 2 bits per move
constexpr size_t kFriendship = 0x29;
constexpr size_t kMoves = 0x2C;
constexpr size_t kPp = 0x34;
constexpr size_t kEvs = 0x38;
constexpr size_t kContest = 0x3E;
constexpr size_t kPokerus = 0x44;
constexpr size_t kMetLocation = 0x45;
constexpr size_t kOrigins = 0x46;   // met level (7 bits), game (4), ball (4), OT gender (1)
constexpr size_t kIv32 = 0x48;      // IVs, egg (bit 30), ability slot (bit 31)
constexpr size_t kRibbons = 0x4C;   // fateful encounter = bit 31
constexpr size_t kPartyLevel = 0x54;
constexpr size_t kPartyStats = 0x56;  // current HP, then max HP, Atk, Def, Spe, SpA, SpD
}  // namespace offset

constexpr size_t kNicknameChars = 10;
constexpr size_t kOtNameChars = 7;

}  // namespace pktool::pokemon::pk3
