#pragma once

#include <cstdint>

#include "core/data/events4.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/pk4.h"
#include "core/pokemon/pokemon.h"

// Receiving Gen IV Mystery Gift Pokémon (Wonder Cards), the way the games
// turn the card's template into a Pokémon (mirrors PKHeX's PGT.ConvertToPKM).
namespace pktool::pokemon::gift4 {

// Fills `out` (decrypted stored PK4) with the Pokémon of `card`, received on
// `today`. Random parts come from `seed`:
//   - PID: kept when the card fixes it, otherwise a random PID that is never
//     shiny for the card's trainer and keeps the card's gender;
//   - IVs: kept when the card fixes them, otherwise random;
//   - met location: the card's egg location + 3000;
//   - met date: a random day of the card's distribution periods, `today`
//     when they are unknown.
// The card's trainer (OT, IDs), moves, ball, ribbons and fateful flag stay.
Pokemon receive(const data::Event4& card, const Date& today, uint32_t seed, uint8_t out[pk4::kStoredSize]);

// The Pokemon Ranger Manaphy egg (PKHeX PGT "Manaphy egg" gift): not a card
// with a template but a game-made egg. It is an unhatched fateful egg from the
// Ranger (egg location 3001) with Tail Glow, Bubble and Water Sport, in the
// trainer's Gen IV game (Diamond if the save is not Gen IV) and language
// (English if the trainer's is unusable for it). PID and IVs come from one
// Method 1 frame of `seed`; the egg is never shiny for its trainer.
Pokemon receiveManaphyEgg(const create4::Trainer& trainer, uint32_t seed, uint8_t out[pk4::kStoredSize]);

// A random day (from `seed`) of `periods`, or `fallback` when there are none.
Date eventDate(data::EventPeriods periods, uint32_t seed, const Date& fallback);

// Event card that could have produced `p` (same species line, trainer IDs
// and OT name), or nullptr.
const data::Event4* matchingEvent(const Pokemon& p);

}  // namespace pktool::pokemon::gift4
