#include "core/bank/bank.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "core/data/names.h"
#include "core/pokemon/entity.h"
#include "core/util/bytes.h"
#include "core/util/crc16.h"

namespace pktool::bank {
namespace {

constexpr char kMagic[4] = {'P', 'K', 'B', 'K'};
constexpr uint16_t kVersion = 2;
constexpr uint16_t kVersionGameBoy = 3;  // written when a Gen I/II Pokemon is inside
constexpr size_t kHeaderSize = 8;
constexpr size_t kSlotSize = 4 + Bank::kEntitySize;

struct SortInfo {
    uint16_t species = 0;
    uint8_t form = 0;
    uint8_t level = 0;
    bool empty = true;
};

int compareNames(uint16_t a, uint16_t b) {
    const char* x = data::speciesName(a);
    const char* y = data::speciesName(b);
    for (;; ++x, ++y) {
        int cx = std::tolower(static_cast<unsigned char>(*x));
        int cy = std::tolower(static_cast<unsigned char>(*y));
        if (cx != cy) return cx < cy ? -1 : 1;
        if (cx == 0) return 0;
    }
}

}  // namespace

void Bank::ensureSlots() {
    if (slots_.empty()) slots_.assign(kBoxCount * kSlotsPerBox, Slot());
}

Bank::Slot* Bank::at(int box, int slot) {
    if (box < 0 || box >= kBoxCount || slot < 0 || slot >= kSlotsPerBox) return nullptr;
    ensureSlots();
    return &slots_[box * kSlotsPerBox + slot];
}

const Bank::Slot* Bank::at(int box, int slot) const {
    if (box < 0 || box >= kBoxCount || slot < 0 || slot >= kSlotsPerBox || slots_.empty()) return nullptr;
    return &slots_[box * kSlotsPerBox + slot];
}

uint8_t Bank::format(int box, int slot) const {
    const Slot* s = at(box, slot);
    return s ? s->format : 0;
}

const uint8_t* Bank::entity(int box, int slot) const {
    const Slot* s = at(box, slot);
    return s && s->format ? s->data : nullptr;
}

bool Bank::set(int box, int slot, uint8_t format, const uint8_t* decrypted) {
    Slot* s = at(box, slot);
    if (s == nullptr || readOnly_ || format < 1 || format > 5) return false;
    s->format = format;
    // PK1/PK2 blobs and PK3 are shorter than a stored PK4/PK5: the rest of the slot stays zero.
    size_t size = pokemon::entity::storedSize(format);
    std::memset(s->data, 0, kEntitySize);
    std::memcpy(s->data, decrypted, size);
    return true;
}

bool Bank::clear(int box, int slot) {
    Slot* s = at(box, slot);
    if (s == nullptr || readOnly_) return false;
    *s = Slot();
    return true;
}

bool Bank::release(int box, int slot) {
    const Slot* s = at(box, slot);
    if (s == nullptr || s->format == 0) return false;
    return clear(box, slot);
}

int Bank::count() const {
    int n = 0;
    for (const Slot& s : slots_) n += s.format ? 1 : 0;
    return n;
}

// ----- Box names ----------------------------------------------------------------

std::string Bank::boxName(int box) const {
    if (box < 0 || box >= kBoxCount) return "";
    if (names_[box][0] == 0) return "Box " + std::to_string(box + 1);
    return names_[box];
}

bool Bank::hasCustomBoxName(int box) const {
    return box >= 0 && box < kBoxCount && names_[box][0] != 0;
}

bool Bank::setBoxName(int box, const std::string& name) {
    if (box < 0 || box >= kBoxCount || readOnly_) return false;
    // Keep whole UTF-8 characters, at most kBoxNameMaxChars of them.
    size_t bytes = 0;
    size_t chars = 0;
    while (bytes < name.size() && chars < kBoxNameMaxChars) {
        size_t len = 1;
        unsigned char c = static_cast<unsigned char>(name[bytes]);
        if (c >= 0xF0) {
            len = 4;
        } else if (c >= 0xE0) {
            len = 3;
        } else if (c >= 0xC0) {
            len = 2;
        }
        if (bytes + len > name.size() || bytes + len > kBoxNameBytes - 1) break;
        bytes += len;
        ++chars;
    }
    std::string kept = name.substr(0, bytes);
    while (!kept.empty() && kept.back() == ' ') kept.pop_back();
    if (kept == "Box " + std::to_string(box + 1)) kept.clear();
    std::memset(names_[box], 0, kBoxNameBytes);
    std::memcpy(names_[box], kept.data(), kept.size());
    return true;
}

// ----- Sorting and searching ------------------------------------------------------

bool Bank::sort(SortKey key, int box) {
    if (readOnly_ || box >= kBoxCount) return false;
    ensureSlots();
    const int lo = box < 0 ? 0 : box * kSlotsPerBox;
    const int hi = box < 0 ? kBoxCount * kSlotsPerBox : lo + kSlotsPerBox;
    // Sort keys and an index; the 140-byte slots themselves are moved once, in place.
    std::vector<SortInfo> info(hi - lo);
    for (int i = lo; i < hi; ++i) {
        const Slot& s = slots_[i];
        if (s.format == 0) continue;
        pokemon::Pokemon p = pokemon::entity::decode(s.format, s.data, pokemon::entity::kStoredSize);
        info[i - lo] = {p.species, p.form, p.level, false};
    }
    std::vector<uint16_t> order(hi - lo);
    for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<uint16_t>(i);
    std::stable_sort(order.begin(), order.end(), [&](uint16_t x, uint16_t y) {
        const SortInfo& a = info[x];
        const SortInfo& b = info[y];
        if (a.empty != b.empty) return b.empty;
        if (a.empty) return false;
        switch (key) {
            case SortKey::Dex:
                return a.species != b.species ? a.species < b.species : a.form < b.form;
            case SortKey::Name: {
                int c = compareNames(a.species, b.species);
                return c != 0 ? c < 0 : a.form < b.form;
            }
            case SortKey::Level:
                return a.level > b.level;
        }
        return false;
    });
    // order[target] = source: follow the cycles with one spare slot.
    std::vector<bool> done(order.size(), false);
    for (size_t start = 0; start < order.size(); ++start) {
        if (done[start] || order[start] == start) continue;
        Slot spare = slots_[lo + start];
        size_t j = start;
        while (order[j] != start) {
            slots_[lo + j] = slots_[lo + order[j]];
            done[j] = true;
            j = order[j];
        }
        slots_[lo + j] = spare;
        done[j] = true;
    }
    return true;
}

std::vector<Bank::Match> Bank::search(const SearchFilter& filter) const {
    std::string needle = filter.text;
    for (char& c : needle) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::vector<Match> out;
    for (size_t i = 0; i < slots_.size(); ++i) {
        const Slot& s = slots_[i];
        if (s.format == 0) continue;
        if (filter.format != 0 && s.format != filter.format) continue;
        pokemon::Pokemon p = pokemon::entity::decode(s.format, s.data, pokemon::entity::kStoredSize);
        if (filter.shinyOnly && !p.shiny) continue;
        if (!needle.empty()) {
            std::string name = data::speciesName(p.species);
            for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (name.find(needle) == std::string::npos) continue;
        }
        out.push_back({static_cast<int>(i) / kSlotsPerBox, static_cast<int>(i) % kSlotsPerBox});
    }
    return out;
}

// ----- File ---------------------------------------------------------------------

std::vector<uint8_t> Bank::serialize() const {
    const size_t slotsAt = kHeaderSize + kBoxCount * kBoxNameBytes;
    std::vector<uint8_t> out(slotsAt + kBoxCount * kSlotsPerBox * kSlotSize + 2, 0);  // (an unloaded Bank is empty)
    std::memcpy(out.data(), kMagic, 4);
    bool gameBoy = false;
    for (const Slot& s : slots_) gameBoy = gameBoy || (s.format != 0 && s.format <= 2);
    util::writeU16(out.data() + 4, gameBoy ? kVersionGameBoy : kVersion);
    util::writeU16(out.data() + 6, kBoxCount);
    for (int b = 0; b < kBoxCount; ++b) {
        std::memcpy(out.data() + kHeaderSize + b * kBoxNameBytes, names_[b], kBoxNameBytes);
    }
    for (size_t i = 0; i < slots_.size(); ++i) {
        uint8_t* dst = out.data() + slotsAt + i * kSlotSize;
        dst[0] = slots_[i].format;
        if (slots_[i].format) std::memcpy(dst + 4, slots_[i].data, kEntitySize);
    }
    util::writeU16(out.data() + out.size() - 2, util::crc16Ccitt(out.data(), out.size() - 2));
    return out;
}

Bank::LoadResult Bank::load(const std::string& path) {
    path_ = path;
    readOnly_ = false;
    slots_.assign(kBoxCount * kSlotsPerBox, Slot());
    std::memset(names_, 0, sizeof names_);
    if (!io::fileExists(path)) return LoadResult::Created;
    std::vector<uint8_t> data;
    if (!io::readFile(path, data)) {
        readOnly_ = true;  // never overwrite a file we cannot read
        return LoadResult::Corrupt;
    }
    return parse(data);
}

Bank::LoadResult Bank::loadBytes(const std::vector<uint8_t>& data) {
    path_.clear();
    readOnly_ = false;
    slots_.assign(kBoxCount * kSlotsPerBox, Slot());
    std::memset(names_, 0, sizeof names_);
    return parse(data);
}

Bank::LoadResult Bank::parse(const std::vector<uint8_t>& data) {
    bool ok = data.size() >= kHeaderSize + 2 && std::memcmp(data.data(), kMagic, 4) == 0;
    uint16_t version = ok ? util::readU16(data.data() + 4) : 0;
    ok = ok && version >= 1 && version <= kVersionGameBoy;
    size_t boxes = ok ? util::readU16(data.data() + 6) : 0;
    size_t namesSize = version >= 2 ? boxes * kBoxNameBytes : 0;
    ok = ok && data.size() == kHeaderSize + namesSize + boxes * kSlotsPerBox * kSlotSize + 2 &&
         util::readU16(data.data() + data.size() - 2) == util::crc16Ccitt(data.data(), data.size() - 2);
    if (!ok) {
        readOnly_ = true;  // never overwrite a file we cannot read
        return LoadResult::Corrupt;
    }
    for (size_t b = 0; version >= 2 && b < boxes && b < static_cast<size_t>(kBoxCount); ++b) {
        std::memcpy(names_[b], data.data() + kHeaderSize + b * kBoxNameBytes, kBoxNameBytes);
        names_[b][kBoxNameBytes - 1] = 0;
    }
    const size_t slotsAt = kHeaderSize + namesSize;
    for (size_t i = 0; i < boxes * kSlotsPerBox && i < slots_.size(); ++i) {
        const uint8_t* src = data.data() + slotsAt + i * kSlotSize;
        if (src[0] >= 1 && src[0] <= 5) {
            slots_[i].format = src[0];
            std::memcpy(slots_[i].data, src + 4, kEntitySize);
        }
    }
    return LoadResult::Ok;
}

io::WriteResult Bank::save() {
    if (readOnly_) return io::WriteResult::TempWriteFailed;
    size_t slash = path_.find_last_of('/');
    if (slash != std::string::npos) io::makeDirectories(path_.substr(0, slash));
    return io::safeWriteFile(path_, serialize());
}

}  // namespace pktool::bank
