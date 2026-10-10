#pragma once

#include <cstdint>

#include "core/data/events3.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/pk3.h"
#include "core/pokemon/pokemon.h"

// Receiving Gen III event gifts (Wishmaker Jirachi, Pokemon Channel Jirachi,
// MYSTRY Mew, the 10th Anniversary legendaries...), the way PKHeX's
// EncounterGift3 generates them: fixed trainer, origin game, met data and the
// event's own PID/IV method (see core/legality/rng3.h).
namespace pktool::pokemon::gift3 {

// Traits the caller would like (-1 = no preference). The PID comes from the
// event's RNG, so only some combinations exist: `satisfied` reports it.
struct Criteria {
    int8_t nature = -1;
    int8_t shiny = -1;   // 0 / 1
    int8_t gender = -1;  // Gender::Male = 0 / Female = 1
};

// Fills `out` (decrypted stored PK3) with the gift for `recipient` and
// returns its model. `seed` varies the random parts.
Pokemon receive(const data::Event3& event, const create4::Trainer& recipient, uint32_t seed, const Criteria& wanted,
                uint8_t out[pk3::kStoredSize], bool* satisfied = nullptr);

// True if the recipient's game can receive the event (R/S/E/FR/LG saves may
// receive any gift: the Pokemon is stamped with the event's own game).
bool canReceive(const data::Event3& event, uint8_t gameVersion);

}  // namespace pktool::pokemon::gift3
