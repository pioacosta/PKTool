#include "core/data/sorted_names.h"

#include <cstring>

#include "core/data/gen12.h"
#include "core/data/gen3.h"
#include "core/data/moves.h"
#include "core/data/names.h"

namespace pktool::data {
namespace {

constexpr uint16_t kMaxItemGen5 = 638;
constexpr uint16_t kMaxBallGen3 = 12;  // Premier Ball
constexpr uint16_t kMaxBallGen4 = 24;  // Sport Ball
constexpr uint16_t kMaxBallGen5 = 25;  // Dream Ball
constexpr uint16_t kNatureCount = 25;

// Views are cached per format: 0 Gen III, 1 Gen IV, 2 Gen V, 3 Gen I, 4 Gen II.
int formatSlot(uint8_t format) {
    return format >= 5 ? 2 : format == 3 ? 0 : format == 2 ? 4 : format == 1 ? 3 : 1;
}

uint16_t maxId(NameKind kind, uint8_t format) {
    switch (kind) {
        case NameKind::Species:
            return format >= 5   ? kMaxSpeciesGen5
                   : format == 3 ? kMaxSpeciesGen3
                   : format == 2 ? kMaxSpeciesGen2
                   : format == 1 ? kMaxSpeciesGen1
                                 : kMaxSpeciesGen4;
        case NameKind::Move:
            return maxMove(format);
        case NameKind::Item:
            return format >= 5 ? kMaxItemGen5 : kMaxItemGen4;
        case NameKind::Nature:
            return kNatureCount - 1;
        case NameKind::Ball:
            return format >= 5 ? kMaxBallGen5 : format == 3 ? kMaxBallGen3 : kMaxBallGen4;
    }
    return 0;
}

// Reads one UTF-8 character (BMP only; anything else is "?") and advances `s`.
uint32_t nextCp(const char*& s) {
    auto c = static_cast<unsigned char>(*s++);
    if (c < 0x80) return c;
    int extra = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 0;
    uint32_t cp = extra == 1 ? c & 0x1F : extra == 2 ? c & 0x0F : extra == 3 ? c & 0x07 : '?';
    for (int k = 0; k < extra && (static_cast<unsigned char>(*s) & 0xC0) == 0x80; ++k) {
        cp = (cp << 6) | (static_cast<unsigned char>(*s++) & 0x3F);
    }
    return cp;
}

void appendCp(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Katakana become hiragana; voiced, half-voiced and small kana become the
// plain kana, so the code point order is the gojuon order (a i u e o, ka ...).
uint32_t foldKana(uint32_t h) {
    if (h >= 0x30A1 && h <= 0x30F6) h -= 0x60;
    if (h == 0x3094) return 0x3046;  // vu
    if (h == 0x3095) return 0x304B;  // small ka
    if (h == 0x3096) return 0x3051;  // small ke
    if (h >= 0x3041 && h <= 0x3049 && (h & 1)) return h + 1;  // small a i u e o
    if (h == 0x3063) return 0x3064;                           // small tsu
    if (h == 0x3083 || h == 0x3085 || h == 0x3087) return h + 1;  // small ya yu yo
    if (h == 0x308E) return 0x308F;                               // small wa
    if (h >= 0x304C && h <= 0x3062 && !(h & 1)) return h - 1;     // ga ... di
    if (h == 0x3065 || h == 0x3067 || h == 0x3069) return h - 1;  // du de do
    if (h >= 0x3070 && h <= 0x307D) return 0x306F + ((h - 0x306F) / 3) * 3;  // ba pa ... bo po
    return h;
}

// Sort-key character of a code point: lowercase ASCII letters and digits,
// Latin accented letters without their accent, folded kana, Hangul syllables
// and ideographs as they are; space stays ' ' and anything else is 0.
uint32_t foldCp(uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z') return cp - 'A' + 'a';
    if ((cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9') || cp == ' ') return cp;
    if (cp >= 0xFF21 && cp <= 0xFF3A) return cp - 0xFF21 + 'a';  // full-width letters and digits
    if (cp >= 0xFF41 && cp <= 0xFF5A) return cp - 0xFF41 + 'a';
    if (cp >= 0xFF10 && cp <= 0xFF19) return cp - 0xFF10 + '0';
    if (cp == 0xDF) return 's';  // sharp s
    if (cp >= 0xC0 && cp <= 0xFF) {
        if (cp >= 0xE0) cp -= 0x20;  // lowercase block -> uppercase block
        if (cp <= 0xC5) return 'a';
        if (cp == 0xC7) return 'c';
        if (cp >= 0xC8 && cp <= 0xCB) return 'e';
        if (cp >= 0xCC && cp <= 0xCF) return 'i';
        if (cp == 0xD1) return 'n';
        if ((cp >= 0xD2 && cp <= 0xD6) || cp == 0xD8) return 'o';
        if (cp >= 0xD9 && cp <= 0xDC) return 'u';
        if (cp == 0xDD) return 'y';
        return 0;
    }
    if (cp == 0x152 || cp == 0x153) return 'o';
    if (cp == 0x15E || cp == 0x15F) return 's';
    if (cp >= 0x3041 && cp <= 0x3096) return foldKana(cp);
    if (cp >= 0x30A1 && cp <= 0x30F6) return foldKana(cp);
    if (cp >= 0xAC00 && cp <= 0xD7A3) return cp;
    if (cp >= 0x4E00 && cp <= 0x9FFF) return cp;
    return 0;
}

// Group of the first letter: the character itself for Latin, one id per kana
// row (a ka sa ta na ha ma ya ra wa) and per Hangul initial consonant.
uint8_t groupOf(uint32_t folded) {
    if (folded < 0x80) return static_cast<uint8_t>(folded);
    if (folded >= 0x3041 && folded <= 0x3093) {
        static const uint16_t kRows[] = {0x3042, 0x304B, 0x3055, 0x305F, 0x306A, 0x306F, 0x307E, 0x3084, 0x3089, 0x308F};
        int row = 0;
        for (int i = 0; i < 10; ++i) {
            if (folded >= kRows[i]) row = i;
        }
        return static_cast<uint8_t>(0x80 + row);
    }
    if (folded >= 0xAC00 && folded <= 0xD7A3) {
        static const uint8_t kInitial[19] = {0, 0, 1, 2, 2, 3, 4, 5, 5, 6, 6, 7, 8, 8, 9, 10, 11, 12, 13};
        return static_cast<uint8_t>(0xA0 + kInitial[(folded - 0xAC00) / 588]);
    }
    return 0xD0;  // ideographs
}

struct View {
    bool built = false;
    std::vector<uint16_t> ids;
};

View g_views[2][5][5];

Order g_order = Order::Alphabetical;

bool isValid(NameKind kind, uint8_t format, uint16_t id) {
    if (kind == NameKind::Nature) return true;
    if (id == 0) return allowsNone(kind);
    if (std::strcmp(nameOf(kind, format, id), "???") == 0) return false;
    if (kind == NameKind::Item && format == 3) return itemToGen3(id) != 0;
    if (kind == NameKind::Item && format == 2) {  // only what a Gen II Pokemon can hold
        const uint8_t item2 = itemToGen2(id);
        return item2 != 0 && isHeldItem2(item2);
    }
    if (kind == NameKind::Item && format == 1) return false;  // Gen I Pokemon hold nothing
    return true;
}

}  // namespace

void setOrder(Order order) {
    g_order = order;
}

Order currentOrder() {
    return g_order;
}

std::string foldName(const char* name) {
    std::string out;
    for (const char* s = name; *s;) {
        uint32_t c = foldCp(nextCp(s));
        if (c == 0) continue;  // punctuation is ignored
        if (c == ' ' && out.empty()) continue;
        appendCp(out, c);
    }
    return out;
}

uint8_t initialOf(const char* name) {
    for (const char* s = name; *s;) {
        uint32_t c = foldCp(nextCp(s));
        if (c != 0 && c != ' ') return groupOf(c);
    }
    return '~';
}

void resetSortedViews() {
    for (auto& order : g_views) {
        for (auto& kind : order) {
            for (View& view : kind) view = View();
        }
    }
}

bool allowsNone(NameKind kind) {
    return kind == NameKind::Move || kind == NameKind::Item;
}

const char* nameOf(NameKind kind, uint8_t format, uint16_t id) {
    (void)format;
    switch (kind) {
        case NameKind::Species: return speciesName(id);
        case NameKind::Move: return moveName(id);
        case NameKind::Item: return itemName(id);
        case NameKind::Nature: return natureName(static_cast<uint8_t>(id));
        case NameKind::Ball: return ballName(static_cast<uint8_t>(id));
    }
    return "???";
}

const std::vector<uint16_t>& sortedIds(NameKind kind, uint8_t format) {
    View& view = g_views[static_cast<int>(g_order)][static_cast<int>(kind)][formatSlot(format)];
    if (!view.built) {
        view.built = true;
        const uint16_t last = maxId(kind, format);
        // Natures are IDs 0-24, the only list whose first ID is a real value.
        for (uint16_t id = kind == NameKind::Nature ? 0 : 1; id <= last; ++id) {
            if (isValid(kind, format, id)) view.ids.push_back(id);
        }
        if (g_order == Order::Alphabetical) {
            sortIdsByName(view.ids, [&](uint16_t id) { return nameOf(kind, format, id); });
        }
        if (allowsNone(kind)) view.ids.insert(view.ids.begin(), 0);
        view.ids.shrink_to_fit();
    }
    return view.ids;
}

int sortedIndex(NameKind kind, uint8_t format, uint16_t id) {
    const std::vector<uint16_t>& ids = sortedIds(kind, format);
    for (size_t i = 0; i < ids.size(); ++i) {
        if (ids[i] == id) return static_cast<int>(i);
    }
    return -1;
}

uint16_t stepSorted(NameKind kind, uint8_t format, uint16_t id, int delta) {
    const std::vector<uint16_t>& ids = sortedIds(kind, format);
    const int count = static_cast<int>(ids.size());
    if (count == 0) return id;
    int at = sortedIndex(kind, format, id);
    if (at < 0) return ids[static_cast<size_t>(delta > 0 ? 0 : count - 1)];
    int next = (at + delta) % count;
    if (next < 0) next += count;
    return ids[static_cast<size_t>(next)];
}

uint16_t jumpSorted(NameKind kind, uint8_t format, uint16_t id, int direction) {
    const std::vector<uint16_t>& ids = sortedIds(kind, format);
    const int count = static_cast<int>(ids.size());
    if (count == 0) return id;
    const int at = sortedIndex(kind, format, id);
    if (g_order == Order::ById) {
        return stepSorted(kind, format, id, direction > 0 ? 10 : -10);
    }
    const int to = jumpGroup(count, at, direction, [&](int i) -> uint8_t {
        uint16_t entry = ids[static_cast<size_t>(i)];
        return entry == 0 && allowsNone(kind) ? 0 : initialOf(nameOf(kind, format, entry));
    });
    return ids[static_cast<size_t>(to)];
}

}  // namespace pktool::data
