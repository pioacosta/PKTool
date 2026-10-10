#pragma once

#include <cstdint>

#include "core/pokemon/pokemon.h"

// Editing operations for Generation IV Pokémon, applied to the common model.
// They also handle Gen V entities (p.format == 5): there the nature is a
// separate field, the ability slot is PID bit 16 and there is a hidden
// ability; the Unova species use the Gen V data.
//
// In Gen III/IV the PID determines nature (PID % 25), gender (low byte vs the
// species' gender ratio), ability slot (bit 0) and shininess (together with
// TID/SID). Changing any of these picks a new PID that keeps the others, so
// the Pokémon stays internally consistent. A new PID no longer matches the
// RNG method of the original encounter, which a future legality checker will
// report until the Pokémon is legalized.
namespace pktool::pokemon::edit4 {

enum class Result : uint8_t {
    Ok,
    Impossible,  // e.g. a female-only species cannot be male
};

// ----- Rules ---------------------------------------------------------------

// Gender implied by the PID for `species` (Gen IV base-form data).
Gender genderFromPid(uint16_t species, uint32_t pid);

// Whether `species` can have `gender` at all.
bool canHaveGender(uint16_t species, Gender gender);

// Number of distinct abilities (1 or 2) and the ability in each slot.
uint8_t abilityCount(uint16_t species);
uint16_t abilityForSlot(uint16_t species, uint8_t slot);
// Same for the Pokémon's format: Gen III entities (format 3) use the Gen III
// abilities (some species only gained their second ability in Gen IV).
uint8_t abilityCountFor(const Pokemon& p);
uint16_t abilityForSlot(const Pokemon& p, uint8_t slot);

// Finds a PID for `species` with the requested traits, deterministically from
// `seed`. Returns false if the combination is impossible.
bool findPid(uint16_t species, uint16_t tid, uint16_t sid, uint8_t nature, bool shiny, Gender gender,
             uint8_t abilitySlot, uint32_t seed, uint32_t& pidOut);

// Gen V: a PID with the requested shininess, gender and ability slot (bit 16).
// With `wildCorrelation`, bit 31 also follows the wild-encounter rule below.
bool findPid5(uint16_t species, uint16_t tid, uint16_t sid, bool shiny, Gender gender, uint8_t abilitySlot,
              uint32_t seed, uint32_t& pidOut, bool wildCorrelation);

// Gen V wild and most static encounters: PID bit 31 ^ bit 0 ^ (TID ^ SID) bit 0 == 0.
bool wildPidCorrelation5(uint32_t pid, uint16_t tid, uint16_t sid);

// ----- Edits on the model -----------------------------------------------------

// PID-derived traits. Each keeps the other three unchanged.
Result setNature(Pokemon& p, uint8_t nature);
Result setShiny(Pokemon& p, bool shiny);
Result setGender(Pokemon& p, Gender gender);
// Slot 0/1; in Gen V also 2 = hidden ability (Dream World).
Result setAbilitySlot(Pokemon& p, uint8_t slot);
uint8_t abilitySlot(const Pokemon& p);

// Changes species keeping level, PID and nickname rules: ability follows the
// PID's slot, gender follows the PID (or the species' fixed gender), and an
// un-nicknamed Pokémon gets the new species name.
Result setSpecies(Pokemon& p, uint16_t species);

// Sets EXP to the minimum for `level` (1-100) with the species' growth rate.
void setLevel(Pokemon& p, uint8_t level);

// Sets a move and refills its PP (keeping PP Ups). Duplicate moves are
// rejected, except for "no move" (0).
Result setMove(Pokemon& p, int index, uint16_t move);

// IVs are clamped to 31. EVs are clamped to 255 and to a total of 510.
void setIv(Pokemon& p, Stat stat, uint8_t value);
void setEv(Pokemon& p, Stat stat, uint8_t value);

// Current PID-derived values, refreshed from `p.pid` (nature, gender, shiny,
// ability). Call after changing the PID or trainer IDs directly.
void refreshDerived(Pokemon& p);

}  // namespace pktool::pokemon::edit4
