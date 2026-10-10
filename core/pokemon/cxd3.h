#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/data/events3.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/gift3.h"
#include "core/pokemon/pk3.h"
#include "core/pokemon/pokemon.h"

// Generating Pokemon of Pokemon Colosseum and XD for a Gen III save (the ones
// that reach a GBA game through the Colosseum/XD link). Their PID/IVs come from
// the XD RNG (core/legality/rng3.h) and depend on the player's trainer IDs:
//   - the Colosseum starters (Espeon, Umbreon) and XD's Eevee are drawn right
//     after the IDs, so only some IDs have a frame at all;
//   - Shadow Pokemon need a frame the NPC party's nature/gender locks allow,
//     searched with the save's IDs (core/legality/shadow_locks.h);
//   - gifts, trades and Poke Spot Pokemon use plain frames / the Poke Spot rules.
// The search is bounded (a fraction of a second on the DS).
namespace pktool::pokemon::cxd3 {

// One entry per (species, kind, level) of data::Gc3 that can be created
// (everything but the Japanese e-Reader shadows); indices into data::gc3().
std::vector<int> menuEntries();

// "Entei [Colosseum Shadow Lv.40]"
std::string title(const data::Gc3& e);

// The Pokemon `gc3(index)` gives `recipient` (trainer IDs, language and game
// version of the save). `out` receives the stored PK3. Returns a Pokemon with
// species 0 and fills `error` when no frame exists (e.g. starters for trainer
// IDs the games cannot draw). `satisfied` reports whether the wanted traits
// were found (the other rules always hold).
Pokemon create(int index, const create4::Trainer& recipient, uint32_t seed, const gift3::Criteria& wanted,
               uint8_t out[pk3::kStoredSize], bool* satisfied = nullptr, std::string* error = nullptr);

}  // namespace pktool::pokemon::cxd3
