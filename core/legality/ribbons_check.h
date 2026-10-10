#pragma once

#include <string>
#include <vector>

#include "core/legality/legality.h"
#include "core/pokemon/pokemon.h"

// Ribbon legality for Gen III-V Pokémon (port of PKHeX's RibbonVerifier for
// the ribbons these generations have).
//
// Rules (PKHeX RibbonRules / RibbonVerifier*): contest ribbons only from games
// with contests (Gen III contests need a Gen III origin, Gen IV ones a Gen IV
// visit; Ditto, Unown and the spiky-eared Pichu cannot enter), a contest rank
// needs the lower ranks, Hoenn Champion/Artist need Gen III, Effort needs Gen
// III or IV, Ability ribbons need Gen IV and a species the Battle Frontier
// accepts, Winning/Victory need Gen III and a species the Battle Tower
// accepts (Winning also met level <= 50 in Gen III saves), Sinnoh Champion,
// Legend, the daily-life ribbons, Gorgeous/Royal and Footprint need Gen IV
// (Footprint also Gen III), Record is unobtainable, eggs have no ribbons, and
// the event ribbons (Gen III Country/World/Champion..., Gen IV Classic,
// Premier, Event, Birthday...) only come with an event: they must equal the
// Wonder Card's when the card is known, are warnings for a fateful Pokémon
// whose event is not in the database and Invalid otherwise.
//
// A Pokémon "visited" the generations of its origin game up to its format
// (a Gen V Pokémon with a Gen IV origin visited Gen IV, a Black one did not).
// Uncertain cases (unknown events, the Colosseum/XD ribbons) are warnings.
namespace pktool::legality {

struct RibbonIssue {
    int index;     // ribbon index of ribbons.h for p.format
    bool invalid;  // false = warning only
    bool missing;  // the ribbon should be present (a prerequisite or the card's)
    const char* why;
};

// Every problem of the ribbons of `p` (none when p has no ribbon snapshot).
std::vector<RibbonIssue> analyzeRibbons(const pokemon::Pokemon& p);

// Adds the findings of analyzeRibbons to `r` (nothing when all is fine).
void checkRibbons(const pokemon::Pokemon& p, Report& r);

// LEGALIZE: removes the impossible ribbons and adds the missing prerequisites
// (and the known Wonder Card's ribbons); each change is appended to `changes`.
// Warnings are left alone. Returns true if something changed.
bool fixRibbons(pokemon::Pokemon& p, std::vector<std::string>& changes);

}  // namespace pktool::legality
