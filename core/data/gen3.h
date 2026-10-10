#pragma once

#include <cstdint>

// Generation III (Ruby, Sapphire, Emerald, FireRed, LeafGreen) conversion
// tables, generated from PKHeX: Gen III stores its own species and item IDs,
// which PKTool converts to the National Dex / Gen IV IDs its model uses.
namespace pktool::data {

constexpr uint16_t kMaxSpeciesGen3 = 386;
constexpr uint16_t kMaxMoveGen3 = 354;

// Internal Gen III species ID -> National Dex number (0 if unused).
uint16_t speciesFromGen3(uint16_t internal);
// National Dex number -> internal Gen III species ID (0 above 386).
uint16_t speciesToGen3(uint16_t national);

// Gen III item ID -> Gen IV item ID, 0 if the item has no Gen IV equivalent.
uint16_t itemFromGen3(uint16_t item3);
// Gen IV item ID -> Gen III item ID, 0 if the item did not exist in Gen III.
uint16_t itemToGen3(uint16_t item4);

// Ability in slot 0/1 in Gen III (some species gained a second ability in
// Gen IV). A species with one ability returns it for both slots.
uint16_t ability3(uint16_t species, uint8_t slot);

namespace generated {
extern const uint16_t kGen3CharsInt[256];
extern const uint16_t kGen3CharsJpn[256];
extern const uint16_t kSpecies3ToNational[];
extern const int kSpecies3ToNationalCount;
extern const uint16_t kItem3To4[];
extern const int kItem3To4Count;
extern const uint8_t kAbilities3[][2];
}  // namespace generated

}  // namespace pktool::data
