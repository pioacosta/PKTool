#pragma once

#include <cstdint>

#include "core/data/event_period.h"

// Gen IV Pokemon Wonder Cards (PCD), generated from PKHeX's Mystery Gift
// database. Each card carries the Pokemon template the game hands out.
namespace pktool::data {

struct Event4 {
    uint16_t cardId;
    uint16_t species;
    uint8_t language;
    const char* title;          // card title and language, e.g. "Pokemon Ranger Manaphy (ENG)"
    uint16_t periodFirst;       // distribution periods: kEventPeriods4[periodFirst, +periodCount)
    uint8_t periodCount;        // 0 = unknown
    uint8_t templatePk4[136];   // decrypted stored PK4 (PID <= 1 = random, IVs 0 = random)
};

int events4Count();
const Event4& event4(int index);  // sorted by species, then card ID

// Index of the first card for `species` at or after `from`, -1 if none.
int findEvent4(uint16_t species, int from = 0);

// The card's distribution periods (none when unknown).
EventPeriods eventPeriods(const Event4& card);

namespace generated {
extern const Event4 kEvents4[];
extern const int kEvents4Count;
extern const EventPeriod kEventPeriods4[];
}  // namespace generated

}  // namespace pktool::data
