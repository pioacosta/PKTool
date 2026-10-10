#pragma once

#include <cstddef>
#include <cstdint>

#include "core/pokemon/pokemon.h"

// Format-independent access to raw entities (PK1/PK2 blobs of Game Boy saves,
// PK3/PK4/PK5).
namespace pktool::pokemon::entity {

constexpr size_t kStoredSize = 136;  // buffer size that fits any stored entity (PK3 uses 80)
constexpr size_t kMaxPartySize = 236;  // PK4; PK5 party data is 220 bytes

size_t partySize(uint8_t format);
// Bytes a stored (box) entity of the format occupies in a buffer.
size_t storedSize(uint8_t format);

// Decrypted data -> model for the given format (1-5). For PK3, `size`
// is the party size (100) for party data; any other size means stored data.
Pokemon decode(uint8_t format, const uint8_t* decrypted, size_t size);

// Applies `p` onto decrypted data in p.format.
void apply(const Pokemon& p, uint8_t* decrypted);

// Recomputes party level and stats of decrypted party data.
void refreshParty(uint8_t format, uint8_t* decrypted);

}  // namespace pktool::pokemon::entity
