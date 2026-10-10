#pragma once

#include <cstdint>

#include "core/data/encounters4.h"

// Gen V encounter tables (B/W/B2/W2), generated from PKHeX. Same entry
// layout as the Gen IV table; kinds are PKHeX's SlotType5 values (5 = Hidden
// Grotto), kEncounterStatic, kEncounterRoamer or kEncounterStaticHidden.
namespace pktool::data {

enum VersionBit5 : uint8_t {
    kVerBlack = 1,
    kVerWhite = 2,
    kVerBlack2 = 4,
    kVerWhite2 = 8,
};

constexpr uint8_t kSlotHiddenGrotto = 5;
constexpr uint8_t kEncounterStaticHidden = 102;     // static encounter with its hidden ability
constexpr uint8_t kEncounterGift5 = 103;            // gift in a Poke Ball
constexpr uint8_t kEncounterStaticFixedShiny = 104; // always / never shiny

uint8_t versionBit5(uint8_t gameVersion);
void encounters5(uint16_t species, const Encounter4** begin, const Encounter4** end);
const char* locationName5(uint16_t location);

namespace generated {
extern const Encounter4 kEncounters5[];
extern const int kEncounters5Count;
extern const LocationName kLocations5[];
extern const int kLocations5Count;
}  // namespace generated

}  // namespace pktool::data
