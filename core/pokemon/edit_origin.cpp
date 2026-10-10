#include "core/pokemon/edit_origin.h"

#include <algorithm>
#include <cstdio>

#include "core/data/encounters3.h"
#include "core/data/gen12.h"
#include "core/data/encounters4.h"
#include "core/data/encounters5.h"
#include "core/pokemon/edit4.h"
#include "core/pokemon/pk4.h"
#include "core/text/gen12_text.h"

namespace pktool::pokemon::editorigin {
namespace {

// Game version IDs whose Gen IV saves store extended locations.
constexpr uint8_t kVersionHeartGold = 7;
constexpr uint8_t kVersionSoulSilver = 8;
constexpr uint8_t kVersionPlatinum = 12;

constexpr int kLocationCount3 = 256;

const data::LocationName* table(uint8_t format, int* count) {
    if (format >= 5) {
        *count = data::generated::kLocations5Count;
        return data::generated::kLocations5;
    }
    *count = data::generated::kLocations4Count;
    return data::generated::kLocations4;
}

// First entry of a Gen IV/V table whose ID is >= `location`.
int lowerBound(const data::LocationName* first, int count, uint16_t location) {
    return static_cast<int>(
        std::lower_bound(first, first + count, location, [](const data::LocationName& n, uint16_t id) { return n.id < id; }) -
        first);
}

int daysInMonth(int year, int month) {
    static const uint8_t kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && (2000 + year) % 4 == 0) return 29;  // 2000-2099: every 4th year
    return kDays[month - 1];
}

Date stepOneDay(Date d, int direction) {
    if (direction > 0) {
        if (d.day < daysInMonth(d.year, d.month)) {
            ++d.day;
        } else if (d.month < 12) {
            d.month++;
            d.day = 1;
        } else if (d.year < 99) {
            d.year++;
            d.month = 1;
            d.day = 1;
        }
    } else {
        if (d.day > 1) {
            --d.day;
        } else if (d.month > 1) {
            d.month--;
            d.day = static_cast<uint8_t>(daysInMonth(d.year, d.month));
        } else if (d.year > 0) {
            d.year--;
            d.month = 12;
            d.day = 31;
        } else {
            return Date{};  // before 2000-01-01: unset
        }
    }
    return d;
}

}  // namespace

// ----- Original trainer ---------------------------------------------------------

size_t otNameMaxLength(uint8_t) {
    return pk4::kOtNameMaxLength;  // 7 characters in Gen I to V
}

size_t otNameMaxLengthFor(const Pokemon& p) {
    return p.format <= 2 && p.language == 1 ? 5 : otNameMaxLength(p.format);  // Japanese Game Boy games: 5
}

bool setOtName(Pokemon& p, const std::string& name) {
    if (name.empty()) return false;
    // Cut on UTF-8 character boundaries.
    size_t chars = 0;
    size_t end = 0;
    while (end < name.size() && chars < otNameMaxLengthFor(p)) {
        ++end;
        while (end < name.size() && (static_cast<uint8_t>(name[end]) & 0xC0) == 0x80) ++end;
        ++chars;
    }
    // The Game Boy character sets are small: refuse what they cannot show.
    if (p.format <= 2 && !text::canEncodeGb(p.format, name.substr(0, end), p.language)) return false;
    p.trainer.name = name.substr(0, end);
    return true;
}

void setTid(Pokemon& p, uint16_t tid) {
    p.trainer.tid = tid;
    if (p.format <= 2) return;  // no PID: nothing derives from the IDs
    edit4::refreshDerived(p);
}

void setSid(Pokemon& p, uint16_t sid) {
    p.trainer.sid = sid;
    if (p.format <= 2) return;
    edit4::refreshDerived(p);
}

// ----- Met and egg data ------------------------------------------------------------

bool hasMetDate(uint8_t format) {
    return format >= 4;
}

bool hasEggData(uint8_t format) {
    return format >= 4;
}

bool canStoreLocation(const Pokemon& p, uint16_t location) {
    if (p.format == 2) return location < 128;  // 7 bits
    if (p.format == 3) return location < kLocationCount3;
    if (p.format >= 5) return true;
    bool extended = p.originGame == kVersionPlatinum || p.originGame == kVersionHeartGold ||
                    p.originGame == kVersionSoulSilver;
    bool newer = (location > 111 && location < 2000) || (location > 2010 && location < 3000);
    return extended || !newer;
}

bool isNamedLocation(uint8_t format, uint16_t location) {
    if (format == 2) {
        return location < data::generated::kLocations2Count && data::generated::kLocations2[location][0] != '\0';
    }
    if (format == 3) {
        return location < data::generated::kLocations3Count && data::generated::kLocations3[location][0] != '\0';
    }
    int count = 0;
    const data::LocationName* first = table(format, &count);
    int at = lowerBound(first, count, location);
    return at < count && first[at].id == location;
}

std::string locationText(uint8_t format, uint16_t location) {
    if (isNamedLocation(format, location)) {
        return format == 2   ? data::locationName2(location)
               : format == 3 ? data::locationName3(location)
               : format >= 5 ? data::locationName5(location)
                             : data::locationName4(location);
    }
    char buffer[24];
    std::snprintf(buffer, sizeof buffer, "Location %u", static_cast<unsigned>(location));
    return buffer;
}

uint16_t stepLocation(const Pokemon& p, uint16_t current, int steps) {
    const int direction = steps < 0 ? -1 : 1;
    int remaining = steps < 0 ? -steps : steps;
    if (p.format == 3 || p.format == 2) {
        const int limit = p.format == 2 ? 128 : kLocationCount3;
        int location = current;
        while (remaining > 0) {
            int next = location + direction;
            while (next >= 0 && next < limit && !isNamedLocation(p.format, static_cast<uint16_t>(next))) next += direction;
            if (next < 0 || next >= limit) break;
            location = next;
            --remaining;
        }
        return static_cast<uint16_t>(location);
    }
    int count = 0;
    const data::LocationName* first = table(p.format, &count);
    // Position of `current` in the table; between entries, the first step
    // lands on the neighbour in the stepping direction.
    int at = lowerBound(first, count, current);
    int index = (at < count && first[at].id == current) ? at : (direction > 0 ? at - 1 : at);
    uint16_t location = current;
    while (remaining > 0) {
        int next = index + direction;
        while (next >= 0 && next < count && !canStoreLocation(p, first[next].id)) next += direction;
        if (next < 0 || next >= count) break;
        index = next;
        location = first[index].id;
        --remaining;
    }
    return location;
}

void setMetLevel(Pokemon& p, int level) {
    const int max = p.format == 2 ? 63 : 100;  // Crystal's caught data keeps 6 bits
    p.met.metLevel = static_cast<uint8_t>(std::min(std::max(level, 1), max));
}

bool isDateSet(const Date& d) {
    return d.year != 0 || d.month != 0 || d.day != 0;
}

bool isValidDate(const Date& d) {
    if (!isDateSet(d)) return true;
    return d.year <= 99 && d.month >= 1 && d.month <= 12 && d.day >= 1 && d.day <= daysInMonth(d.year, d.month);
}

std::string dateText(const Date& d) {
    if (!isDateSet(d)) return "None";
    char buffer[24];
    std::snprintf(buffer, sizeof buffer, "%04d-%02d-%02d", 2000 + d.year, d.month, d.day);
    return buffer;
}

Date stepDay(const Date& d, int days, const Date& fallback) {
    if (!isValidDate(d) || !isDateSet(d)) return isValidDate(fallback) && isDateSet(fallback) ? fallback : Date{0, 1, 1};
    Date out = d;
    const int direction = days < 0 ? -1 : 1;
    for (int n = days < 0 ? -days : days; n > 0; --n) out = stepOneDay(out, direction);
    return out;
}

Date stepMonth(const Date& d, int months, const Date& fallback) {
    if (!isValidDate(d) || !isDateSet(d)) return isValidDate(fallback) && isDateSet(fallback) ? fallback : Date{0, 1, 1};
    int index = d.year * 12 + (d.month - 1) + months;
    if (index < 0) return Date{};
    if (index > 99 * 12 + 11) index = 99 * 12 + 11;
    Date out;
    out.year = static_cast<uint8_t>(index / 12);
    out.month = static_cast<uint8_t>(index % 12 + 1);
    out.day = static_cast<uint8_t>(std::min<int>(d.day, daysInMonth(out.year, out.month)));
    return out;
}

// ----- Pokérus ----------------------------------------------------------------------

uint8_t pokerusStrain(uint8_t value) {
    return value >> 4;
}

uint8_t pokerusDays(uint8_t value) {
    return value & 0xF;
}

uint8_t pokerusMaxDays(uint8_t strain) {
    return strain == 0 ? 0 : static_cast<uint8_t>((strain & 3) + 1);
}

void setPokerusStrain(Pokemon& p, int strain) {
    strain = std::min(std::max(strain, 0), 15);
    const uint8_t oldStrain = pokerusStrain(p.pokerus);
    const uint8_t oldDays = pokerusDays(p.pokerus);
    uint8_t days = 0;
    if (strain != 0) {
        const uint8_t maxDays = pokerusMaxDays(static_cast<uint8_t>(strain));
        // New infection: full duration. Existing strain: keep infected / cured.
        days = oldStrain == 0 ? maxDays : std::min(oldDays, maxDays);
    }
    p.pokerus = static_cast<uint8_t>((strain << 4) | days);
}

bool setPokerusDays(Pokemon& p, int days) {
    const uint8_t strain = pokerusStrain(p.pokerus);
    if (strain == 0) return false;
    days = std::min(std::max(days, 0), static_cast<int>(pokerusMaxDays(strain)));
    p.pokerus = static_cast<uint8_t>((strain << 4) | days);
    return true;
}

std::string pokerusText(uint8_t value) {
    const uint8_t strain = pokerusStrain(value);
    const uint8_t days = pokerusDays(value);
    if (strain == 0) return "None";
    char buffer[48];
    if (days == 0) {
        std::snprintf(buffer, sizeof buffer, "Cured (strain %u)", static_cast<unsigned>(strain));
    } else {
        std::snprintf(buffer, sizeof buffer, "Infected (strain %u, %u day%s)", static_cast<unsigned>(strain),
                      static_cast<unsigned>(days), days == 1 ? "" : "s");
    }
    return buffer;
}

}  // namespace pktool::pokemon::editorigin
