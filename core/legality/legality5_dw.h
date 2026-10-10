#pragma once

#include <cstdint>

#include "core/data/dreamworld5.h"
#include "core/data/personal.h"
#include "core/legality/legality.h"
#include "core/pokemon/pokemon.h"

// Gen V Dream World (Entree Forest) and N's Pokémon checks, used by
// checkGen5 and by LEGALIZE (legalize5.cpp). Rules follow PKHeX's
// EncounterStatic5Entree and EncounterStatic5N.
namespace pktool::legality {

// Gen V species with a hidden ability of their own (the table repeats the
// first ability for species without one).
inline bool hasHiddenAbility5(const data::PersonalInfo& info) {
    return info.hiddenAbility != 0 && info.hiddenAbility != info.abilities[0];
}

// The special moves a Dream World capture can come with. Only one of them can
// be on a Pokémon unless it also learns the others normally.
struct SpecialMoves {
    uint16_t moves[48];
    int count = 0;

    bool contains(uint16_t move) const {
        for (int i = 0; i < count; ++i) {
            if (moves[i] == move) return true;
        }
        return false;
    }
};

// Balls a Dream World capture can be in (wild balls and the Dream Ball).
bool isDreamWorldBall5(uint8_t ball);

// Checks a Pokémon met at the Entree Forest against the Dream World table
// (species line, level, games, form, gender, language, never shiny, hidden
// ability, ball) and fills `special` with the special moves it may have.
void checkDreamWorld5(const pokemon::Pokemon& p, Report& r, SpecialMoves* special);

// The Dream World capture LEGALIZE can build `p` from: its species line at a
// level it has reached, a gender and language it allows, with the hidden
// ability (p.hiddenAbility must be set and p not shiny). Prefers the entry
// whose special moves cover most of p's moves. Null if none fits.
// `encounterSpecies` is the species it is caught as (a pre-evolution maybe).
const data::DreamWorld5* findDreamWorldFor5(const pokemon::Pokemon& p, bool anyGame, uint16_t* encounterSpecies);

// Dream Radar (PKHeX EncounterStatic5Radar, Black 2 / White 2 only): met at
// "Dream Radar" (30015) at a level that is a multiple of 5 between 5 and 40, in
// a Dream Ball, never shiny, with the hidden ability (Rotom: its regular ones),
// the form the radar gives (Therian Tornadus, Thundurus and Landorus), no
// fateful encounter and no egg. Moves are learned normally.
void checkDreamRadar5(const pokemon::Pokemon& p, Report& r);

// The Dream Radar entry whose species line contains `p` (the radar species
// itself or one it evolves into), or null.
const data::DreamRadar5* findDreamRadarFor5(const pokemon::Pokemon& p);

// Therian Tornadus, Thundurus and Landorus only exist through the Dream Radar.
bool isTherianForme5(const pokemon::Pokemon& p);

constexpr uint16_t kLocationDreamRadar5 = 30015;

// The hidden ability a Pokémon would have: the Therian Formes have a single
// ability of their own (Regenerator, Volt Absorb, Intimidate) as their hidden one.
uint16_t hiddenAbilityOf5(const pokemon::Pokemon& p, const data::PersonalInfo& info);

// The N's Pokémon (Black 2 / White 2) with this PID and species line, or null.
const data::NPokemon5* findNPokemon5(const pokemon::Pokemon& p);

// Everything N's Pokémon fix: sparkle flag, OT, IDs, IVs, location, level,
// nature, ability, gender, ball.
void checkNPokemon5(const pokemon::Pokemon& p, const data::NPokemon5& n, Report& r);

}  // namespace pktool::legality
