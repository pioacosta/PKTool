#pragma once

#include <cstdint>

#include "core/pokemon/pokemon.h"

// Editing operations for Generation I and II Pokemon (formats 1 and 2),
// applied to the common model. There is no PID: Gen II shininess and gender
// are decided by the DVs, so changing one picks DVs that keep the other.
namespace pktool::pokemon::editgb {

enum class Result : uint8_t {
    Ok,
    Impossible,  // e.g. a female-only species cannot be male, a species of a later generation
};

// Whether the species exists in the Pokemon's format (151 / 251).
bool validSpecies(const Pokemon& p, uint16_t species);
// Whether `species` can have `gender` at all in Gen II (Gen I has no gender).
bool canHaveGender(uint16_t species, Gender gender);

// Changes the species keeping the level, EXP, DVs and moves; an un-nicknamed
// Pokemon gets the new name and a Gen I Pokemon the catch rate of the species.
Result setSpecies(Pokemon& p, uint16_t species);
// Sets EXP to the minimum for `level` (1-100) with the species' growth rate.
void setLevel(Pokemon& p, uint8_t level);
// Sets a move and refills its PP (keeping PP Ups). Duplicate moves are
// rejected, except for "no move" (0).
Result setMove(Pokemon& p, int index, uint16_t move);
// DVs are 0-15 (the HP DV is derived and cannot be set; the Special DV is
// kSpA, kSpD follows). Stat Exp is 0-65535 (kSpA/kSpD share it).
void setDv(Pokemon& p, Stat stat, uint8_t value);
void setStatExp(Pokemon& p, Stat stat, uint16_t value);
// Gen II: DVs that make the Pokemon shiny (Atk 2/3/6/7/10/11/14/15 and 10 in
// the other DVs) or not, keeping the gender where the species allows it.
Result setShiny(Pokemon& p, bool shiny);
// Gen II: an Attack DV on the other side of the species' gender threshold,
// keeping shininess where possible.
Result setGender(Pokemon& p, Gender gender);

// Refreshes what the DVs and EXP decide: HP DV, shininess, gender, Unown
// letter and level.
void refreshDerived(Pokemon& p);

}  // namespace pktool::pokemon::editgb
