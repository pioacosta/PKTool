#pragma once

#include <cstdint>

#include "core/data/encounters4.h"

// Gen III encounter tables (R/S/E/FR/LG), generated from PKHeX. Used for
// Pokémon moved to Gen IV with Pal Park, which keeps their Gen III origin.
namespace pktool::data {

enum VersionBit3 : uint8_t {
    kVerSapphire = 1,
    kVerRuby = 2,
    kVerEmerald = 4,
    kVerFireRed = 8,
    kVerLeafGreen = 16,
};

// Version bit for a Gen III game version ID (0 if not a GBA game).
uint8_t versionBit3(uint8_t gameVersion);

// Encounters of one species: [begin, end). Same layout as the Gen IV table;
// kinds are PKHeX's SlotType3 values, kEncounterStatic or kEncounterRoamer.
void encounters3(uint16_t species, const Encounter4** begin, const Encounter4** end);

// Display name of a Gen III location ID.
const char* locationName3(uint16_t location);

namespace generated {
extern const Encounter4 kEncounters3[];
extern const int kEncounters3Count;
extern const char* const kLocations3[];
extern const int kLocations3Count;
}  // namespace generated

}  // namespace pktool::data
