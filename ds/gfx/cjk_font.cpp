#include "ds/gfx/cjk_font.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>

#include "core/io/file_io.h"

namespace pktool::gfx {
namespace {

constexpr int kCell = 12;
constexpr size_t kBitmapBytes = kCell * kCell / 8;  // 12 bits per row
constexpr size_t kMaxCached = 256;

struct Entry {
    uint8_t advance = 0;
    uint8_t rows[kCell * 2] = {};  // 2 bytes per row, bit 0 = leftmost
};

std::FILE* g_file = nullptr;
std::vector<uint16_t> g_codes;
std::vector<uint8_t> g_advance;
long g_bitmapBase = 0;
uint8_t g_ascent = 10;
std::map<uint16_t, Entry> g_cache;
Glyph g_scratch;

int indexOf(uint32_t cp) {
    if (cp > 0xFFFF) return -1;
    auto it = std::lower_bound(g_codes.begin(), g_codes.end(), static_cast<uint16_t>(cp));
    if (it == g_codes.end() || *it != cp) return -1;
    return static_cast<int>(it - g_codes.begin());
}

}  // namespace

void closeCjkFont() {
    if (g_file != nullptr) std::fclose(g_file);
    g_file = nullptr;
    g_codes.clear();
    g_advance.clear();
    g_cache.clear();
}

bool openCjkFont(const std::vector<std::string>& directories) {
    closeCjkFont();
    for (const std::string& dir : directories) {
        std::FILE* f = std::fopen(io::joinPath(dir, "fonts/cjk.bin").c_str(), "rb");
        if (f == nullptr) continue;
        uint8_t head[10];
        if (std::fread(head, 1, sizeof head, f) != sizeof head || std::memcmp(head, "PKC1", 4) != 0 ||
            head[6] != kCell || head[7] != kCell) {
            std::fclose(f);
            continue;
        }
        const size_t count = static_cast<size_t>(head[4] | (head[5] << 8));
        std::vector<uint8_t> raw(count * 3);
        if (std::fread(raw.data(), 1, raw.size(), f) != raw.size()) {
            std::fclose(f);
            continue;
        }
        g_codes.resize(count);
        for (size_t i = 0; i < count; ++i) g_codes[i] = static_cast<uint16_t>(raw[i * 2] | (raw[i * 2 + 1] << 8));
        g_advance.assign(raw.begin() + static_cast<long>(count * 2), raw.end());
        g_bitmapBase = static_cast<long>(sizeof head + raw.size());
        g_ascent = head[8];
        g_file = f;
        return true;
    }
    return false;
}

bool cjkFontAvailable() {
    return g_file != nullptr;
}

bool cjkHas(uint32_t cp) {
    return g_file != nullptr && indexOf(cp) >= 0;
}

const Glyph* cjkGlyph(const Font& font, uint32_t cp, const uint8_t** bits) {
    int index = g_file != nullptr ? indexOf(cp) : -1;
    if (index < 0) return nullptr;
    const uint16_t key = static_cast<uint16_t>(cp);
    auto it = g_cache.find(key);
    if (it == g_cache.end()) {
        uint8_t packed[kBitmapBytes];
        if (std::fseek(g_file, g_bitmapBase + static_cast<long>(index) * static_cast<long>(kBitmapBytes), SEEK_SET) != 0 ||
            std::fread(packed, 1, sizeof packed, g_file) != sizeof packed) {
            return nullptr;
        }
        if (g_cache.size() >= kMaxCached) g_cache.clear();
        Entry entry;
        entry.advance = g_advance[static_cast<size_t>(index)];
        for (int row = 0; row < kCell; ++row) {
            uint32_t value = 0;
            for (int col = 0; col < kCell; ++col) {
                const int bit = row * kCell + col;
                if (packed[bit >> 3] & (1u << (bit & 7))) value |= 1u << col;
            }
            entry.rows[row * 2] = static_cast<uint8_t>(value & 0xFF);
            entry.rows[row * 2 + 1] = static_cast<uint8_t>(value >> 8);
        }
        it = g_cache.emplace(key, entry).first;
    }
    g_scratch.offset = 0;
    g_scratch.advance = it->second.advance;
    g_scratch.xOffset = 0;
    g_scratch.yOffset = static_cast<int8_t>(static_cast<int>(font.baseline) - g_ascent);
    g_scratch.width = kCell;
    g_scratch.height = kCell;
    *bits = it->second.rows;
    return &g_scratch;
}

}  // namespace pktool::gfx
