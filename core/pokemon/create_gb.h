#pragma once

#include <cstdint>

#include "core/pokemon/create4.h"
#include "core/pokemon/pokemon.h"

// Creation of new Gen I and Gen II Pokemon (formats 1 and 2) for a trainer.
// create4::newPokemon / newEgg / encode / formatFor forward here for Game Boy
// versions (35-41).
namespace pktool::pokemon::creategb {

// A new Pokemon caught by `trainer`: default moves (what a wild one of that
// level knows), full PP, random DVs from `seed`, base friendship. In Crystal
// the caught data (time, level, location) comes from a real encounter.
Pokemon newPokemon(const create4::Trainer& trainer, uint16_t species, uint8_t level, uint32_t seed);

// A Gen II Day-Care egg (level 5, egg cycles as friendship). Gen I has none:
// the result is a normal level 5 Pokemon.
Pokemon newEgg(const create4::Trainer& trainer, uint16_t species, uint32_t seed);

// Encodes the model into a fresh entity blob (pk1::kEntitySize / pk2::kEntitySize bytes).
void encode(const Pokemon& p, uint8_t* out);

}  // namespace pktool::pokemon::creategb
