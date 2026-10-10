#include "core/pokemon/ribbons.h"

#include "core/pokemon/pk3.h"
#include "core/pokemon/pk4.h"
#include "core/util/bytes.h"

namespace pktool::pokemon::ribbons {

namespace {

// ----- Gen III ----------------------------------------------------------------

constexpr int kPk3Ranks = 5;
constexpr int kPk3Count = 17;

const char* const kPk3Names[kPk3Count] = {
    "Cool contest",    "Beauty contest",     "Cute contest",      "Smart contest", "Tough contest",
    "Champion",        "Winning",            "Victory",           "Artist",        "Effort",
    "Battle Champion", "Regional Champion",  "National Champion", "Country",       "National",
    "Earth",           "World",
};

// Bit of each flag in the 32-bit word at pk3::offset::kRibbons.
constexpr uint8_t kPk3FlagBits[kPk3Count - kPk3Ranks] = {15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26};

// ----- Gen IV and Gen V (same layout) -------------------------------------------

constexpr int kSetA = 28;  // Sinnoh set at 0x24
constexpr int kSetB = 32;  // Hoenn set at 0x3C
constexpr int kSetC = 20;  // Sinnoh contest set at 0x60
constexpr int kGen45Count = kSetA + kSetB + kSetC;

constexpr size_t kBaseA = 0x24;
constexpr size_t kBaseB = 0x3C;
constexpr size_t kBaseC = 0x60;

const char* const kGen45Names[kGen45Count] = {
    // 0x24
    "Sinnoh Champion", "Ability", "Great Ability", "Double Ability", "Multi Ability", "Pair Ability",
    "World Ability", "Alert", "Shock", "Downcast", "Careless", "Relax", "Snooze", "Smile", "Gorgeous", "Royal",
    "Gorgeous Royal", "Footprint", "Record", "Event", "Legend", "World Champion", "Birthday", "Special",
    "Souvenir", "Wishing", "Classic", "Premier",
    // 0x3C
    "Hoenn Cool", "Hoenn Cool Super", "Hoenn Cool Hyper", "Hoenn Cool Master", "Hoenn Beauty",
    "Hoenn Beauty Super", "Hoenn Beauty Hyper", "Hoenn Beauty Master", "Hoenn Cute", "Hoenn Cute Super",
    "Hoenn Cute Hyper", "Hoenn Cute Master", "Hoenn Smart", "Hoenn Smart Super", "Hoenn Smart Hyper",
    "Hoenn Smart Master", "Hoenn Tough", "Hoenn Tough Super", "Hoenn Tough Hyper", "Hoenn Tough Master",
    "Hoenn Champion", "Winning", "Victory", "Artist", "Effort", "Battle Champion", "Regional Champion",
    "National Champion", "Country", "National", "Earth", "World",
    // 0x60
    "Sinnoh Cool", "Sinnoh Cool Great", "Sinnoh Cool Ultra", "Sinnoh Cool Master", "Sinnoh Beauty",
    "Sinnoh Beauty Great", "Sinnoh Beauty Ultra", "Sinnoh Beauty Master", "Sinnoh Cute", "Sinnoh Cute Great",
    "Sinnoh Cute Ultra", "Sinnoh Cute Master", "Sinnoh Smart", "Sinnoh Smart Great", "Sinnoh Smart Ultra",
    "Sinnoh Smart Master", "Sinnoh Tough", "Sinnoh Tough Great", "Sinnoh Tough Ultra", "Sinnoh Tough Master",
};

// Byte offset and bit of a Gen IV / Gen V flag.
void locate45(int index, size_t& offset, int& bit) {
    size_t base = kBaseA;
    int local = index;
    if (index >= kSetA + kSetB) {
        base = kBaseC;
        local = index - kSetA - kSetB;
    } else if (index >= kSetA) {
        base = kBaseB;
        local = index - kSetA;
    }
    offset = base + static_cast<size_t>(local / 8);
    bit = local % 8;
}

bool validFormat(uint8_t format) { return format >= 3 && format <= 5; }

// Word (0..2) and bit of a Gen IV / Gen V flag.
void locateWord45(int index, int& word, int& bit) {
    if (index >= kSetA + kSetB) {
        word = 2;
        bit = index - kSetA - kSetB;
    } else if (index >= kSetA) {
        word = 1;
        bit = index - kSetA;
    } else {
        word = 0;
        bit = index;
    }
}

constexpr uint32_t kMaskPk3 = 0x07FFFFFFu;  // bit 31 is the fateful flag
constexpr uint32_t kMask45[3] = {0x0FFFFFFFu, 0xFFFFFFFFu, 0x000FFFFFu};
constexpr size_t kOffsets45[3] = {kBaseA, kBaseB, kBaseC};

}  // namespace

int count(uint8_t format) {
    if (format == 3) return kPk3Count;
    return validFormat(format) ? kGen45Count : 0;
}

const char* name(uint8_t format, int index) {
    if (index < 0 || index >= count(format)) return "";
    return format == 3 ? kPk3Names[index] : kGen45Names[index];
}

bool isRank(uint8_t format, int index) { return format == 3 && index >= 0 && index < kPk3Ranks; }

int maxValue(uint8_t format, int index) { return isRank(format, index) ? 4 : 1; }

const char* rankName(int value) {
    static const char* const kRanks[] = {"None", "Normal", "Super", "Hyper", "Master"};
    return value >= 0 && value <= 4 ? kRanks[value] : "?";
}

int get(uint8_t format, const uint8_t* d, int index) {
    if (index < 0 || index >= count(format)) return 0;
    if (format == 3) {
        uint32_t word = util::readU32(d + pk3::offset::kRibbons);
        if (index < kPk3Ranks) return static_cast<int>((word >> (3 * index)) & 7u);
        return static_cast<int>((word >> kPk3FlagBits[index - kPk3Ranks]) & 1u);
    }
    size_t offset;
    int bit;
    locate45(index, offset, bit);
    return (d[offset] >> bit) & 1;
}

bool set(uint8_t format, uint8_t* d, int index, int value) {
    if (index < 0 || index >= count(format)) return false;
    const int high = maxValue(format, index);
    value = value < 0 ? 0 : (value > high ? high : value);
    if (format == 3) {
        uint32_t word = util::readU32(d + pk3::offset::kRibbons);
        if (index < kPk3Ranks) {
            const int shift = 3 * index;
            word = (word & ~(7u << shift)) | (static_cast<uint32_t>(value) << shift);
        } else {
            const uint32_t mask = 1u << kPk3FlagBits[index - kPk3Ranks];
            word = value ? (word | mask) : (word & ~mask);
        }
        util::writeU32(d + pk3::offset::kRibbons, word);
        pk3::refreshChecksum(d);
        return true;
    }
    size_t offset;
    int bit;
    locate45(index, offset, bit);
    d[offset] = static_cast<uint8_t>(value ? (d[offset] | (1u << bit)) : (d[offset] & ~(1u << bit)));
    pk4::refreshChecksum(d);  // PK5 shares the checksum scheme
    return true;
}

int total(uint8_t format, const uint8_t* d) {
    int n = 0;
    const int c = count(format);
    for (int i = 0; i < c; ++i) {
        int v = get(format, d, i);
        n += isRank(format, i) ? (v > 4 ? 4 : v) : v;
    }
    return n;
}

int getWords(uint8_t format, const uint32_t* w, int index) {
    if (index < 0 || index >= count(format)) return 0;
    if (format == 3) {
        if (index < kPk3Ranks) return static_cast<int>((w[0] >> (3 * index)) & 7u);
        return static_cast<int>((w[0] >> kPk3FlagBits[index - kPk3Ranks]) & 1u);
    }
    int word, bit;
    locateWord45(index, word, bit);
    return static_cast<int>((w[word] >> bit) & 1u);
}

void setWords(uint8_t format, uint32_t* w, int index, int value) {
    if (index < 0 || index >= count(format)) return;
    const int high = maxValue(format, index);
    value = value < 0 ? 0 : (value > high ? high : value);
    if (format == 3) {
        if (index < kPk3Ranks) {
            const int shift = 3 * index;
            w[0] = (w[0] & ~(7u << shift)) | (static_cast<uint32_t>(value) << shift);
        } else {
            const uint32_t mask = 1u << kPk3FlagBits[index - kPk3Ranks];
            w[0] = value ? (w[0] | mask) : (w[0] & ~mask);
        }
        return;
    }
    int word, bit;
    locateWord45(index, word, bit);
    w[word] = value ? (w[word] | (1u << bit)) : (w[word] & ~(1u << bit));
}

int totalWords(uint8_t format, const uint32_t* w) {
    int n = 0;
    const int c = count(format);
    for (int i = 0; i < c; ++i) {
        int v = getWords(format, w, i);
        n += isRank(format, i) ? (v > 4 ? 4 : v) : v;
    }
    return n;
}

void snapshot(const uint8_t* d, Pokemon& p) {
    if (p.format == 3) {
        p.ribbonFormat = 3;
        p.ribbonWords[0] = util::readU32(d + pk3::offset::kRibbons) & kMaskPk3;
        p.ribbonWords[1] = p.ribbonWords[2] = 0;
    } else if (validFormat(p.format)) {
        p.ribbonFormat = 4;
        for (int i = 0; i < 3; ++i) p.ribbonWords[i] = util::readU32(d + kOffsets45[i]) & kMask45[i];
    }
}

void restore(const Pokemon& p, uint8_t* d) {
    if (p.ribbonFormat == 0) return;
    if (p.format == 3 && p.ribbonFormat == 3) {
        uint32_t old = util::readU32(d + pk3::offset::kRibbons);
        util::writeU32(d + pk3::offset::kRibbons, (old & ~kMaskPk3) | (p.ribbonWords[0] & kMaskPk3));
    } else if (p.format != 3 && p.ribbonFormat == 4) {
        for (int i = 0; i < 3; ++i) {
            uint32_t old = util::readU32(d + kOffsets45[i]);
            util::writeU32(d + kOffsets45[i], (old & ~kMask45[i]) | (p.ribbonWords[i] & kMask45[i]));
        }
    }
}

}  // namespace pktool::pokemon::ribbons
