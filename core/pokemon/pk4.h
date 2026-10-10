#pragma once

#include <cstddef>
#include <cstdint>

#include "core/pokemon/pokemon.h"

// Generation IV entity format ("PK4"), as stored in D/P/Pt/HG/SS saves.
//
// Layout (decrypted): 8-byte header (PID, flags, checksum) followed by four
// 32-byte blocks A-D, plus 100 bytes of battle stats for party members.
// At rest the blocks are shuffled by the PID and encrypted with the LCRNG
// seeded by the checksum; party stats are encrypted with the PID as seed.
// Gen V (PK5) uses the same scheme, so the crypto helpers are shared.
namespace pktool::pokemon::pk4 {

constexpr size_t kStoredSize = 136;
constexpr size_t kPartySize = 236;
constexpr size_t kPk5PartySize = 220;

// In-place conversion between the encrypted (at rest) and decrypted forms.
// `size` must be kStoredSize, kPartySize or kPk5PartySize.
void decrypt(uint8_t* data, size_t size);
void encrypt(uint8_t* data, size_t size);

// 16-bit sum of the decrypted words at 0x08-0x87.
uint16_t calcChecksum(const uint8_t* decrypted);
uint16_t storedChecksum(const uint8_t* decrypted);
void refreshChecksum(uint8_t* decrypted);

// True when an encrypted slot contains no Pokémon (the games zero empty slots).
bool isEmptySlot(const uint8_t* encrypted, size_t size);

// Decrypted-data view -> common model. The level is derived from EXP.
Pokemon toPokemon(const uint8_t* decrypted, size_t size);

// Applies the directly-stored fields of `pkm` onto decrypted data, leaving
// every other byte untouched, then refreshes the checksum.
//
// Derived values are not written: nature and shininess follow from the PID
// (change `pid` instead), and level follows from EXP. The ball is only
// rewritten when it changed, following the D/P/Pt vs HG/SS rules; so are the
// met and egg locations (D/P field plus the Pt/HG/SS extended field).
//
// For party Pokémon call refreshPartyStats() afterwards.
void applyPokemon(const Pokemon& pkm, uint8_t* decrypted);

// Recomputes the party level and battle stats from the stored data (species,
// EXP, IVs, EVs, nature), keeping a full-HP Pokémon at full HP.
// `decrypted` must be kPartySize bytes. Refreshes the checksum.
void refreshPartyStats(uint8_t* decrypted);

// Field offsets inside decrypted data.
namespace offset {
constexpr size_t kPid = 0x00;
constexpr size_t kChecksum = 0x06;
constexpr size_t kSpecies = 0x08;
constexpr size_t kHeldItem = 0x0A;
constexpr size_t kTid = 0x0C;
constexpr size_t kSid = 0x0E;
constexpr size_t kExp = 0x10;
constexpr size_t kFriendship = 0x14;
constexpr size_t kAbility = 0x15;
constexpr size_t kLanguage = 0x17;
constexpr size_t kEvs = 0x18;
constexpr size_t kMoves = 0x28;
constexpr size_t kPp = 0x30;
constexpr size_t kPpUps = 0x34;
constexpr size_t kIv32 = 0x38;
constexpr size_t kFlags40 = 0x40;  // fateful (bit 0), gender (bits 1-2), form (bits 3-7)
constexpr size_t kEggLocationExt = 0x44;
constexpr size_t kMetLocationExt = 0x46;
constexpr size_t kNickname = 0x48;  // 11 glyphs
constexpr size_t kOriginGame = 0x5F;
constexpr size_t kOtName = 0x68;  // 8 glyphs
constexpr size_t kEggDate = 0x78;
constexpr size_t kMetDate = 0x7B;
constexpr size_t kEggLocationDp = 0x7E;
constexpr size_t kMetLocationDp = 0x80;
constexpr size_t kPokerus = 0x82;
constexpr size_t kBallDpPt = 0x83;
constexpr size_t kMetLevelOtGender = 0x84;
constexpr size_t kEncounterType = 0x85;
constexpr size_t kBallHgss = 0x86;
constexpr size_t kPartyLevel = 0x8C;
constexpr size_t kPartyStats = 0x8E;  // current HP, then max HP, Atk, Def, Spe, SpA, SpD
}  // namespace offset

constexpr size_t kNicknameChars = 11;
constexpr size_t kNicknameMaxLength = 10;
constexpr size_t kOtNameChars = 8;
constexpr size_t kOtNameMaxLength = 7;

}  // namespace pktool::pokemon::pk4
