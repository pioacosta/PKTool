#include "core/data/event_period.h"

namespace pktool::data {
namespace {

// Days since 2000-01-01 (proleptic Gregorian; years 2000-2255).
int32_t dayNumber(const uint8_t d[3]) {
    int32_t y = 2000 + d[0];
    int32_t m = d[1];
    if (m <= 2) {
        y -= 1;
        m += 12;
    }
    int32_t days = 365 * y + y / 4 - y / 100 + y / 400 + (153 * (m - 3) + 2) / 5 + d[2];
    return days - 730426;  // 2000-01-01 -> 0
}

void fromDayNumber(int32_t n, uint8_t out[3]) {
    // civil_from_days (H. Hinnant).
    int32_t z = n + 730425;  // days since 0000-03-01
    int32_t era = z / 146097;
    int32_t doe = z - era * 146097;
    int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int32_t mp = (5 * doy + 2) / 153;
    int32_t day = doy - (153 * mp + 2) / 5 + 1;
    int32_t month = mp < 10 ? mp + 3 : mp - 9;
    int32_t year = era * 400 + yoe + (month <= 2 ? 1 : 0);
    out[0] = static_cast<uint8_t>(year - 2000);
    out[1] = static_cast<uint8_t>(month);
    out[2] = static_cast<uint8_t>(day);
}

}  // namespace

bool pickEventDay(EventPeriods periods, uint32_t random, uint8_t out[3]) {
    uint32_t total = 0;
    for (int i = 0; i < periods.count; ++i) {
        total += static_cast<uint32_t>(dayNumber(periods.items[i].last) - dayNumber(periods.items[i].first) + 1);
    }
    if (total == 0) return false;
    uint32_t pick = random % total;
    for (int i = 0; i < periods.count; ++i) {
        int32_t first = dayNumber(periods.items[i].first);
        uint32_t length = static_cast<uint32_t>(dayNumber(periods.items[i].last) - first + 1);
        if (pick < length) {
            fromDayNumber(first + static_cast<int32_t>(pick), out);
            return true;
        }
        pick -= length;
    }
    return false;
}

bool inEventPeriods(EventPeriods periods, const uint8_t day[3]) {
    int32_t n = dayNumber(day);
    for (int i = 0; i < periods.count; ++i) {
        if (n >= dayNumber(periods.items[i].first) && n <= dayNumber(periods.items[i].last)) return true;
    }
    return false;
}

}  // namespace pktool::data
