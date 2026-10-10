#pragma once

#include <string>
#include <vector>

#include "core/data/encounters4.h"
#include "core/legality/legality.h"
#include "core/legality/rng4.h"
#include "core/pokemon/pokemon.h"

// Auto-Legalize, first version (Gen IV).
//
// Keeps what the user asked for (species, level, nature, shininess, gender,
// ability, moves, held item, as close as possible IVs) and fixes the data the
// game derives from it: PID/IV frames for the origin game's RNG method,
// ability slot, ball rules, met level, PP, EV total. Anything it cannot fix
// without changing a requested trait is reported instead of silently changed.
// Encounter-based legalization (met location, wild levels) needs the
// encounter tables and comes later.
namespace pktool::legality {

struct LegalizeResult {
    pokemon::Pokemon pokemon;
    std::vector<std::string> changes;     // what was modified
    std::vector<std::string> unresolved;  // problems that could not be fixed
    Report report;                        // legality after the changes
};

// `effort` bounds the PID/IV search time (see generatePidIv).
LegalizeResult legalizeGen4(const pokemon::Pokemon& requested, int effort);

// Gen V version: real B/W/B2/W2 encounter (or Day-Care egg, or Poke
// Transfer for Gen III/IV origins), Gen V balls, hidden ability rules and a
// PID with the wild bit-31 rule where the encounter needs it. IVs are kept
// (Gen V IVs are not tied to the PID).
LegalizeResult legalizeGen5(const pokemon::Pokemon& requested, int effort);

// Gen III version (Pokémon in R/S/E/FR/LG saves): real Gen III encounter or
// a Day-Care egg, Method 1 PID/IVs, Gen III balls, items and PP.
LegalizeResult legalizeGen3(const pokemon::Pokemon& requested, int effort);

// Lowest level at which a new Gen III Pokémon of this species can be legal
// (its evolution level if it hatches from eggs, 0 if there is no Gen III
// encounter).
uint8_t minCreateLevel3(uint16_t species);

// Game Boy version (legalize12.cpp, formats 1 and 2): picks a wild, static,
// Day-Care, trade or event encounter that fits the requested species, level,
// moves, shininess and gender, then rebuilds what the games derive from it
// (types and catch rate, held item, Crystal's caught data, fixed DVs, EXP, PP).
LegalizeResult legalizeGb(const pokemon::Pokemon& requested, int effort);
// Lowest level at which a new Gen I/II Pokemon of the species is an ordinary
// wild, static or Day-Care Pokemon (5 if the species has only trades or events).
uint8_t minCreateLevelGb(uint8_t format, uint16_t species);

// legalizeGb, legalizeGen3, legalizeGen4 or legalizeGen5 depending on requested.format.
LegalizeResult legalize(const pokemon::Pokemon& requested, int effort);

// Lowest level at which a new Gen V Pokémon of this species can be legal
// (1 if it hatches from eggs, 0 if there is no Gen V encounter).
uint8_t minCreateLevel5(uint16_t species);

// Re-rolls the PID (and IVs, if the origin needs an RNG-consistent frame)
// for new PID-derived traits, keeping IVs as close as possible. Returns
// false if no frame exists. Used by the editor when nature, shininess,
// gender or ability change. Wild Gen IV encounters may use a lead with
// Synchronize or Cute Charm (reported through `source`); Pokewalker catches
// get the Pokewalker PID.
bool rerollPidGen4(pokemon::Pokemon& p, uint8_t nature, bool shiny, pokemon::Gender gender, uint8_t abilitySlot,
                   int effort, PidSource* source = nullptr);

// Lowest-level Gen IV encounter (wild, static or roaming) of the species or
// a pre-evolution; returns its minimum level, 0 if there is none.
uint8_t lowestEncounter4(uint16_t species, const data::Encounter4** match = nullptr);
// Same for Gen III (R/S/E/FR/LG) encounters, reachable with Pal Park.
uint8_t lowestEncounter3(uint16_t species, const data::Encounter4** match = nullptr);

// Lowest level at which a new Pokémon of this species can be legal in Gen IV
// or through Pal Park (1 if it can hatch from an egg, 0 if it only comes
// from events).
uint8_t minCreateLevel4(uint16_t species);

// Makes an unhatched egg follow the egg rules (see checkEgg): first-stage
// species, level 1, met level 0, Day-Care egg location, Poke Ball, the egg
// name and flag, egg cycles within range. Appends what changed.
void fixEgg(pokemon::Pokemon& p, std::vector<std::string>& changes);

// True if this Pokémon's PID and IVs must come from the RNG (wild, static and
// gift Pokémon). Eggs, hatched Pokémon, events and in-game trades are exempt.
bool needsCorrelatedPid(const pokemon::Pokemon& p);

}  // namespace pktool::legality
