#pragma once

#include <cstdint>

// Gen IV encounter tables (D/P/Pt/HG/SS), generated from PKHeX.
namespace pktool::data {

enum VersionBit : uint8_t {
    kVerDiamond = 1,
    kVerPearl = 2,
    kVerPlatinum = 4,
    kVerHeartGold = 8,
    kVerSoulSilver = 16,
};

// Encounter kinds: 0-99 are PKHeX's SlotType4 wild slot types (grass, surf,
// rods, Rock Smash, Headbutt, Bug Contest, honey trees, Safari, Poke Radar,
// swarms...), then static encounters and roamers.
constexpr uint8_t kEncounterStatic = 100;
constexpr uint8_t kEncounterRoamer = 101;  // location 0: met anywhere they roam

struct Encounter4 {
    uint16_t species;
    uint16_t location;
    uint8_t minLevel;
    uint8_t maxLevel;
    uint8_t versions;  // VersionBit mask
    uint8_t kind;
};

struct LocationName {
    uint16_t id;
    const char* name;
};

// Version bit for a Gen IV game version ID (0 if not Gen IV).
uint8_t versionBit4(uint8_t gameVersion);

// Encounters of one species: [begin, end).
void encounters4(uint16_t species, const Encounter4** begin, const Encounter4** end);

// Display name of a Gen IV location ID ("Location 123" style fallback).
const char* locationName4(uint16_t location);

namespace generated {
extern const Encounter4 kEncounters4[];
extern const int kEncounters4Count;
extern const LocationName kLocations4[];
extern const int kLocations4Count;
}  // namespace generated

}  // namespace pktool::data
