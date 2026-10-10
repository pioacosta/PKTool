#pragma once

#include <cstdint>

// When an event Pokémon was handed out. The periods come from Bulbapedia's
// distribution lists (tools/fetch_event_dates.py) and are compiled next to
// the Wonder Cards (events4.h, events5.h).
namespace pktool::data {

struct EventPeriod {
    uint8_t first[3];  // year - 2000, month, day
    uint8_t last[3];   // inclusive
};

struct EventPeriods {
    const EventPeriod* items = nullptr;
    int count = 0;
};

// A day of `periods` chosen uniformly with `random` (each day of every period
// is equally likely), written to `out` as year - 2000, month, day. False (and
// `out` untouched) when there are no periods.
bool pickEventDay(EventPeriods periods, uint32_t random, uint8_t out[3]);

// True if the day (year - 2000, month, day) falls in one of the periods.
bool inEventPeriods(EventPeriods periods, const uint8_t day[3]);

}  // namespace pktool::data
