#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/pokemon/pokemon.h"

// Pokemon Showdown set text import (the format used by Showdown's teambuilder
// "Import/Export"), e.g.:
//
//   Garchomp @ Choice Scarf
//   Ability: Sand Veil
//   Level: 50
//   Shiny: Yes
//   EVs: 4 HP / 252 Atk / 252 Spe
//   Jolly Nature
//   IVs: 0 SpA
//   - Earthquake
//   - Dragon Claw
namespace pktool::showdown {

struct Set {
    std::string title;  // first line, for display
    uint16_t species = 0;
    std::string nickname;  // empty when none
    uint16_t item = 0;
    uint16_t ability = 0;       // 0 = not given
    uint8_t level = 100;
    bool shiny = false;
    int gender = -1;            // -1 = not given, else pokemon::Gender
    int nature = -1;            // -1 = not given
    uint8_t evs[pokemon::kStatCount] = {};
    uint8_t ivs[pokemon::kStatCount] = {31, 31, 31, 31, 31, 31};
    uint16_t moves[4] = {};
    uint8_t friendship = 255;
    std::vector<std::string> warnings;  // unknown names, ignored lines
};

// Parses every set in `text` (sets are separated by blank lines).
std::vector<Set> parse(const std::string& text);

// Showdown set text for a Pokémon (the format parse() reads back).
std::string format(const pokemon::Pokemon& p);

// Name lookups used by the parser (case- and punctuation-insensitive).
// Return 0 when not found.
uint16_t findSpecies(const std::string& name);
uint16_t findMove(const std::string& name);
uint16_t findItem(const std::string& name);
uint16_t findAbility(const std::string& name);
int findNature(const std::string& name);  // -1 when not found

}  // namespace pktool::showdown
