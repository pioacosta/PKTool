#pragma once

#include <cstddef>
#include <cstdint>

#include "core/pokemon/pk4.h"
#include "core/pokemon/pokemon.h"

// Generation V entity format ("PK5"), as stored in B/W/B2/W2 saves.
//
// Same block shuffle, encryption and checksum as PK4 (use pk4::decrypt /
// pk4::encrypt with kPartySize = 220). Differences from PK4: the nature is a
// field of its own (0x41), 0x42 holds the hidden-ability and N's Pokémon
// flags, strings are UTF-16, locations are single 16-bit fields and there is
// one ball field.
namespace pktool::pokemon::pk5 {

constexpr size_t kStoredSize = 136;
constexpr size_t kPartySize = pk4::kPk5PartySize;  // 220

namespace offset {
constexpr size_t kNature = 0x41;
constexpr size_t kFlags42 = 0x42;  // hidden ability (bit 0), N's Pokémon (bit 1)
constexpr size_t kEggLocation = 0x7E;
constexpr size_t kMetLocation = 0x80;
constexpr size_t kBall = 0x83;
constexpr size_t kPokeStarFame = 0x87;
}  // namespace offset

// Decrypted data -> model (format = 5). The level is derived from EXP.
Pokemon toPokemon(const uint8_t* decrypted, size_t size);

// Applies the stored fields of `pkm` onto decrypted data, leaving unmodelled
// bytes untouched, then refreshes the checksum.
void applyPokemon(const Pokemon& pkm, uint8_t* decrypted);

// Recomputes party level and battle stats (`decrypted` is 220 bytes).
void refreshPartyStats(uint8_t* decrypted);

}  // namespace pktool::pokemon::pk5
