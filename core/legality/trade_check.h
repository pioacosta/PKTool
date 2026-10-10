#pragma once
#include "core/legality/legality.h"
#include "core/pokemon/pokemon.h"

// In-game (NPC) trades of Gen III, IV and V (PKHeX EncounterTrade3 /
// EncounterTrade4PID / EncounterTrade5BW / EncounterTrade5B2W2, data in
// core/data/trades.h).
//
// A Pokemon met at the generation's NPC trade location must be one of the
// known trades: species line, level, original trainer ID (and SID), OT
// gender, Poke Ball, and where the game fixes them the PID (nature, gender),
// IVs and nature. The OT name is compared with every language of the trade
// (the language quirks of D/P, the Magikarp and Pikachu trades are not
// modelled): a name of another language is a warning, no match an error.
// Not checked: the ability, the contest stats and the moves. Unknown trades
// (no row of the species line in the game) are a warning.
namespace pktool::legality {

// Met at the NPC trade location of its generation (Gen III 254, Gen IV 2001,
// Gen V 30002), or at the fixed met location of an HG/SS trade (Webster's
// Spearow, Kirk's Shuckle).
bool isNpcTrade(const pokemon::Pokemon& p);

// Checks an NPC-trade Pokemon (isNpcTrade) and adds the findings.
void checkNpcTrade(const pokemon::Pokemon& p, Report& r);

}  // namespace pktool::legality
