#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Packs of whole Wonder Cards in the NitroFS assets (tools/gen_data.py --only
// cards): a 4-byte magic, u16 card count, u16 card size, then the cards. The
// compiled tables keep only the Pokémon part of each card; a save's Mystery
// Gift album needs all of it.
namespace pktool::data {

// Finds the first card of `pack` (a path relative to each of `dirs`, searched
// in order) for which `match` is true and copies its `size` bytes to `out`.
// False if no pack has such a card.
bool findInCardPack(const std::vector<std::string>& dirs, const char* pack, const char* magic, size_t size,
                    const std::function<bool(const uint8_t*)>& match, uint8_t* out);

}  // namespace pktool::data
