#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/data/walker4.h"
#include "core/legality/legality.h"
#include "core/pokemon/pokemon.h"

// HG/SS Pokewalker encounters (PKHeX: EncounterStatic4Pokewalker and
// PokewalkerRNG).
//
// A Pokemon caught on a stroll has the level, gender and moves of a course
// slot, the Pokewalker met location and a Poke Ball. Its PID is not drawn from
// the RNG: it is derived from the trainer IDs, the nature and the gender, and
// can never be shiny. Its IVs are two consecutive RNG outputs.
namespace pktool::legality {

// Met location of Pokewalker catches (HG/SS location list).
constexpr uint16_t kLocationPokewalker = 233;

// The PID the game builds for a Pokewalker Pokemon (`gender`: 0 male, 1
// female, 2 genderless; `genderRatio` as in the personal data). `nature` 24
// (Quirky) cannot be produced and is treated as 0, like the game does.
uint32_t pokewalkerPid(uint16_t tid, uint16_t sid, uint32_t nature, uint8_t gender, uint8_t genderRatio);

// Gender ratio the game uses for a Pokewalker PID of `species` (Froslass uses
// its pre-evolution's 50:50 ratio).
uint8_t pokewalkerGenderRatio(uint16_t species);

// True if `p.pid` is the Pokewalker PID for its trainer IDs, nature and
// gender (the Azurill and Froslass gender ratio quirks included).
bool isPokewalkerPid(const pokemon::Pokemon& p);

// A course slot of the species line with the Pokemon's met level and gender.
struct WalkerMatch {
    int course = -1;
    const data::WalkerSlot* slot = nullptr;
    uint16_t species = 0;  // the slot's species (the Pokemon may have evolved)
};
EncounterStatus findPokewalker(const pokemon::Pokemon& p, WalkerMatch* match = nullptr);

// True if the Pokemon claims a Pokewalker origin: Gen IV origin with the
// Pokewalker met location, not an egg and not hatched.
bool claimsPokewalker(const pokemon::Pokemon& p);

// Gen IV checker part for Pokewalker Pokemon (appends to `r`). Fills `match`
// with the slot found, if any.
void checkPokewalker(const pokemon::Pokemon& p, Report& r, WalkerMatch* match);

// LEGALIZE: moves `p` to a Pokewalker catch. Picks a course slot of its
// species line at or below its level, with its gender, whose Pokewalker PID
// (never shiny, no Quirky, ability tied to nature and gender) carries the
// requested nature and ability slot; sets origin (HeartGold if it was not
// HG/SS), met location and level, Poke Ball. Returns false (and changes
// nothing) if no slot fits. The PID and IVs are not touched: reroll them with
// rerollPidGen4.
bool applyPokewalkerOrigin(pokemon::Pokemon& p, uint8_t abilitySlot, std::vector<std::string>& changes);

// True if the slot knows `move` as one of its fixed moves.
bool walkerSlotHasMove(const data::WalkerSlot* slot, uint16_t move);

}  // namespace pktool::legality
