#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/pokemon/pokemon.h"

// Editing rules for the "origin" data of a Pokémon: original trainer, met /
// egg data and Pokérus. They work on the common model for Gen III, IV and V
// and only expose what each format really stores:
//
//  * Gen III has no met date, egg location, egg date nor encounter type; its
//    met location is one byte.
//  * Gen IV/V store met and egg dates (year since 2000, month, day).
//  * Gen IV games before Platinum can only hold the locations of Diamond/Pearl
//    (anything newer is stored as "Faraway place"), see canStoreLocation().
//
// Changing the trainer IDs keeps the PID, so the Pokémon may become shiny or
// stop being shiny (the PID is never silently re-rolled).
namespace pktool::pokemon::editorigin {

// ----- Original trainer ---------------------------------------------------------

// Characters of the OT name in every supported format.
size_t otNameMaxLength(uint8_t format);

// Same for this Pok�mon (Japanese Game Boy games have 5-character names).
size_t otNameMaxLengthFor(const Pokemon& p);

// Sets the OT name (cut to the format's length). An empty name is rejected, and
// so is a name the Game Boy character set cannot show (formats 1 and 2).
bool setOtName(Pokemon& p, const std::string& name);

// Changing the TID/SID updates the derived shininess.
void setTid(Pokemon& p, uint16_t tid);
void setSid(Pokemon& p, uint16_t sid);

// ----- Met and egg data ------------------------------------------------------------

// Whether the format stores a met date, an egg location and an egg date.
bool hasMetDate(uint8_t format);
bool hasEggData(uint8_t format);

// Whether `location` can be stored in `p` (and read back unchanged): Gen IV
// games before Platinum only keep the locations known to Diamond/Pearl.
bool canStoreLocation(const Pokemon& p, uint16_t location);

// Location with a real name in the format's table.
bool isNamedLocation(uint8_t format, uint16_t location);

// Name shown for a location ("Location N" when the table has none).
std::string locationText(uint8_t format, uint16_t location);

// Moves `steps` (negative = backwards) named, storable locations from
// `current`, in ID order, stopping at the ends of the table.
uint16_t stepLocation(const Pokemon& p, uint16_t current, int steps);

// Met level, 1-100 (1-63 in Gen II, whose caught data has 6 bits).
void setMetLevel(Pokemon& p, int level);

// Dates. An all-zero date means "not set" (an egg that never hatched, ...).
bool isDateSet(const Date& d);
bool isValidDate(const Date& d);  // unset, or a real calendar date in 2000-2099
std::string dateText(const Date& d);
// Steps `days` / `months` (negative = backwards); the result is always a real
// date. Stepping an unset date gives `fallback`; stepping back from
// 2000-01-01 unsets the date. Past 2099-12-31 it stays there.
Date stepDay(const Date& d, int days, const Date& fallback);
Date stepMonth(const Date& d, int months, const Date& fallback);

// ----- Pokérus ----------------------------------------------------------------------
//
// Stored as one byte: strain in the high nibble, days left in the low one.
// Game rules: strain 0 means "no Pokérus" (days must be 0); a new infection
// lasts (strain & 3) + 1 days at most; 0 days with a strain is "cured".

uint8_t pokerusStrain(uint8_t value);
uint8_t pokerusDays(uint8_t value);
uint8_t pokerusMaxDays(uint8_t strain);

// Strain 0-15. Becoming infected sets the longest duration; a cured Pokémon
// stays cured; days are cut to the new maximum.
void setPokerusStrain(Pokemon& p, int strain);
// Days 0-max for the strain. Returns false when there is no strain to set
// days on.
bool setPokerusDays(Pokemon& p, int days);

// "None", "Infected (strain 5, 3 days)" or "Cured (strain 5)".
std::string pokerusText(uint8_t value);

}  // namespace pktool::pokemon::editorigin
