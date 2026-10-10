#pragma once

#include <cstdint>

#include "core/data/events5.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/pk5.h"
#include "core/pokemon/pokemon.h"

// Receiving Gen V Mystery Gift Pokémon (Wonder Cards), the way the games turn
// the card into a Pokémon (mirrors PKHeX's PGF.ConvertToPKM).
namespace pktool::pokemon::gift5 {

// Game (version ID 20-23) the card's Pokémon comes from when `trainer`
// receives it: the card's origin game, else the trainer's game if the card
// allows it, else the first game that can receive it.
uint8_t receivingGame(const data::Event5& card, uint8_t trainerVersion);

// Fills `out` (decrypted stored PK5) with the Pokémon of `card` received by
// `trainer`. Random parts come from `seed`:
//   - PID: kept when the card fixes it, otherwise random with the card's
//     gender, ability slot and shininess rule (never / random / always);
//   - nature, gender, ability, IVs and level: the card's, random when open;
//   - met data: the card's locations and level; met date (egg date for egg
//     cards) a random day of the card's distribution periods, else the
//     card's date, else today.
// The card's trainer (OT, IDs), nickname, moves, ball, ribbons, contest
// stats and the fateful flag are kept. Egg cards give an unhatched egg that
// belongs to `trainer`.
Pokemon receive(const data::Event5& card, const create4::Trainer& trainer, uint32_t seed,
                uint8_t out[pk5::kStoredSize]);

// Event card that could have produced `p`, or nullptr: same species line and,
// for Pokémon cards, the card's trainer, met location and met level; for egg
// cards, the card's egg location.
const data::Event5* matchingEvent(const Pokemon& p);

// True if the card's fixed moves include `move`.
bool cardHasMove(const data::Event5& card, uint16_t move);

}  // namespace pktool::pokemon::gift5
