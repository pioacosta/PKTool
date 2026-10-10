#pragma once

#include <cstdint>

#include "core/pokemon/pokemon.h"

// Roaming legendaries (Gen III Latias/Latios/beasts, Gen IV Mesprit, Cresselia,
// the Pt birds, Raikou, Entei, HG/SS Latias/Latios, Gen V Tornadus/Thundurus),
// after PKHeX's EncounterStatic3/4/5 `IsRoaming`:
//   - they can be met anywhere inside their roaming area, so the met location
//     is checked against PKHeX's permitted sets instead of one fixed place;
//   - Gen III R/S/FR/LG roamers have the games' IV bug: only one byte of the
//     IV word is kept (5 bits HP, 3 bits Attack, everything else 0), the PID
//     is Method 1 and that byte is the low 8 bits of the first IV call.
//     Emerald fixed the bug (plain Method 1);
//   - Gen IV roamers are plain Method 1 (PKHeX has no IV quirk for them) and
//     must be met on grass or water tiles (the Route 45 water tile is not a
//     real encounter);
//   - Gen V roamers have no wild PID rule.
namespace pktool::legality::roamer {

// Gen III: met location inside the roaming area of the origin game
// (Hoenn routes 101-138 for R/S/E, Kanto routes 1-25 for FR/LG).
bool metOk3(uint8_t originGame, uint16_t location);
// A location to use when LEGALIZE builds a roamer encounter.
uint16_t defaultLocation3(uint8_t originGame);

// Gen IV: met location and ground tile (the PK4 "encounter type" byte) for the
// roamer `species` (the region follows from the species: Raikou/Entei roam
// Johto, HG/SS Latias/Latios Kanto, everything else Sinnoh).
bool metOk4(uint16_t species, uint16_t location, uint8_t groundTile);
uint16_t defaultLocation4(uint16_t species);
constexpr uint8_t kGroundGrass = 2;
constexpr uint8_t kGroundWater = 7;

// Gen V: the routes the roamers can be met on.
bool metOk5(uint16_t location);
constexpr uint16_t kDefaultLocation5 = 15;

// Gen III PID/IV rule of a roamer met in `originGame`.
bool ivsMatch3(uint8_t originGame, uint32_t pid, const uint8_t ivs[pokemon::kStatCount]);

// Gen III roamer frame: a Method 1 PID with the requested traits and the
// closest IVs the truncation allows.
struct Request3 {
    uint16_t species = 0;
    uint16_t tid = 0;
    uint16_t sid = 0;
    uint8_t nature = 0;
    bool shiny = false;
    pokemon::Gender gender = pokemon::Gender::Male;
    uint8_t abilitySlot = 0;
    uint8_t targetIvs[pokemon::kStatCount] = {};
};
// `salt` varies the search; false if no frame was found within the budget.
bool generate3(const Request3& req, uint32_t salt, uint32_t* pid, uint8_t ivs[pokemon::kStatCount]);

}  // namespace pktool::legality::roamer
