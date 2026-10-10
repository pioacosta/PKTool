#pragma once

#include <cstdint>

#include "core/legality/legality.h"
#include "core/pokemon/pokemon.h"

// Original-trainer checks for Gen III-V Pokémon (port of PKHeX's
// TrainerNameVerifier, TrainerIDVerifier, LanguageVerifier and the Colosseum/XD
// OT gender rule).
//
// What PKHeX checks and this ports:
//  - the OT name of a player trainer is not empty and not longer than
//    12 characters (6 for Japanese/Korean) in Gen III-V; names starting with
//    "PKHeX" are suspicious (warning);
//  - TID/SID of a player trainer: both 0, TID = SID, TID 0, SID 0 and the
//    well-known test pairs 12345/54321 and 15040/18831 are suspicious (warning);
//  - Ruby/Sapphire trainer IDs are two consecutive RNG outputs, so a pair no
//    RNG state produces is impossible (Invalid; a warning for hatched eggs,
//    which may have been traded to Emerald);
//  - the language is 1-5 or 7 for a Gen III origin and also 8 (Korean) for
//    Gen IV and V (0 and 6 never exist);
//  - Colosseum/XD trainers cannot be female.
// Pokémon with a fixed trainer (events, in-game trades, N's Pokémon, Colosseum
// and XD) are exempt from the name and ID rules, as in PKHeX. PKHeX does not
// check the OT gender of the other games, Gen III-V ones all have a girl.
namespace pktool::legality {

// Whether some RNG state produces `sid` then `tid` as consecutive 16-bit
// outputs (the Ruby/Sapphire new-game trainer ID, PKHeX MethodH).
bool rsTrainerIdPossible(uint16_t tid, uint16_t sid);

void checkTrainer(const pokemon::Pokemon& p, Report& r);

}  // namespace pktool::legality
