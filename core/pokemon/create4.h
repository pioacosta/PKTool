#pragma once

#include <cstdint>
#include <string>

#include "core/pokemon/pk4.h"
#include "core/pokemon/pokemon.h"

// Creation of new Gen IV Pokémon for a given trainer.
namespace pktool::pokemon::create4 {

// The trainer receiving the Pokémon (from the save).
struct Trainer {
    std::string name;
    uint16_t tid = 0;
    uint16_t sid = 0;
    Gender gender = Gender::Male;
    uint8_t language = 2;   // English
    uint8_t version = 12;   // Platinum (20-23: Gen V)
    Date today;
};

// Entity format of Pokémon created for a game version (3, 4 or 5).
uint8_t formatFor(uint8_t gameVersion);

// A new Pokémon caught by `trainer` (PK4 model, PK5 when trainer.version is a
// Gen V game, PK3 for a Gen III one): default moves (the last level-up moves
// up to `level`), full PP, Poke Ball, base friendship, random IVs and PID
// from `seed`. Not legalized yet: run legality::legalizeGen4() on it.
Pokemon newPokemon(const Trainer& trainer, uint16_t species, uint8_t level, uint32_t seed);

// An unhatched egg of `species` (or of its base species) received from the
// Day-Care: level 1, egg moves = level-1 moves, egg cycles as friendship,
// the game's "Egg" name for the trainer's language.
Pokemon newEgg(const Trainer& trainer, uint16_t species, uint32_t seed);

// Name the games give to eggs: Gen IV title case ("Egg", "Huevo", "Oeuf"),
// Gen V "Egg", "Huevo", "\u0152uf".
std::string eggName(uint8_t language, uint8_t format);

// Encodes a model into a fresh decrypted stored entity in p.format (136
// bytes, or 80 for PK3).
void encode(const Pokemon& p, uint8_t out[pk4::kStoredSize]);

// Default nickname for a species in a game language, as that game stores it
// (see data::defaultNickname: capitals for Gen III/IV, the language's own name).
std::string defaultNickname(uint16_t species, uint8_t language = 2, uint8_t format = 4);

}  // namespace pktool::pokemon::create4
