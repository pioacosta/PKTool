#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/data/events3.h"
#include "core/legality/legality.h"
#include "core/pokemon/pokemon.h"

// Legality of Gen III event gifts and of Colosseum/XD Pokemon (the tables of
// core/data/events3.h, PID rules of core/legality/rng3.h).
//
// A Pokemon is "identified" with an event when its species line, trainer IDs
// and OT name match one; from then on every fixed field of the event (origin
// game, language, met data, ball, fateful flag, shininess, OT gender, PID/IV
// method) must fit, or the Pokemon is Invalid. A Pokemon no event identifies
// is left to the caller (a warning: it may be a distribution PKTool lacks).
namespace pktool::legality {

// Checks a Gen III entity (p.format == 3, met location 255 or fateful flag)
// against the event gifts. Returns false, adding nothing, if none identifies
// it. `moves` receives the matched event's moves (they are always legal).
bool checkEvent3(const pokemon::Pokemon& p, Report& r, const uint16_t** moves);

// Same for a Colosseum/XD Pokemon (origin game 15) against gifts, starters,
// trades, Shadow Pokemon and Poke Spot slots.
bool checkGc3(const pokemon::Pokemon& p, Report& r, const uint16_t** moves);

// A Colosseum Pokemon whose gift has a female OT (the bonus-disc Celebi): the
// generic "Colosseum/XD trainers are male" rule does not apply to it.
bool gcFemaleOtGift(const pokemon::Pokemon& p);

// Pal Park (Gen IV entities with a Gen III origin): does an event, Colosseum
// or XD encounter explain the Pokemon? The Gen IV entity keeps the trainer
// IDs, language, ball and fateful flag but loses the met location and level.
// Returns a description ("Event: ...") or an empty string.
std::string describeGen3Origin(const pokemon::Pokemon& p);

// Pal Park: is `move` one of the moves of the event or Colosseum/XD encounter
// describeGen3Origin recognises?
bool gen3OriginHasMove(const pokemon::Pokemon& p, uint16_t move);

// Moves of the event eggs (Pokemon Box, PCJP, Wish, PokePark) a Gen III egg or
// a Pokemon hatched from one could come from: species line, origin game, OT
// and trainer ID fit. Appends to `out`.
void eventEggMoves3(const pokemon::Pokemon& p, std::vector<uint16_t>& out);

// The Gen III event gift (core/data/events3.h) whose species line, trainer IDs,
// language and game fit `p` (Gen III entity or Pal Park Gen IV/V entity), the
// fateful flag aside; nullptr if none. Used for the ribbons an event gives.
const data::Event3* findGen3Event(const pokemon::Pokemon& p);

}  // namespace pktool::legality
