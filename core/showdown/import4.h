#pragma once

#include <string>
#include <vector>

#include "core/legality/legality.h"
#include "core/pokemon/create4.h"
#include "core/showdown/showdown.h"

// Builds a legal Gen IV Pokémon from a Showdown set.
namespace pktool::showdown {

struct Imported {
    pokemon::Pokemon pokemon;
    std::vector<std::string> warnings;  // parts of the set that could not be applied as written
    std::vector<std::string> changes;   // what LEGALIZE changed (met data, ball...)
    legality::Report report;
};

// Creates the set's Pokémon for `trainer`, applies everything the set asks
// for (moves, item, ability, nature, gender, shininess, EVs, IVs as close as
// the RNG allows) and legalizes it. `effort` bounds the PID/IV search.
Imported buildGen4(const Set& set, const pokemon::create4::Trainer& trainer, uint32_t seed, int effort);

// Game Boy version (Gen I/II trainers): IVs become DVs (IV / 2), EVs Stat Exp
// (EV squared), there is no ability or nature; Gen II items, shininess and
// gender are applied through the DVs. buildGen4 forwards here for those games.
Imported buildGb(const Set& set, const pokemon::create4::Trainer& trainer, uint32_t seed, int effort);

}  // namespace pktool::showdown
