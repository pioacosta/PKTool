#include "ds/gfx/sprites.h"

#include "core/io/file_io.h"
#include "core/util/bytes.h"

namespace pktool::gfx {
namespace {

// Bounds memory use (the DS heap is ~2.5 MB and saving needs room for a
// 512 KB save): box icons are ~2.4 KB, front sprites ~12.8 KB.
constexpr size_t kMaxCachedSprites = 96;
constexpr size_t kMaxCachedBytes = 128 * 1024;
constexpr int kMaxDimension = 128;

}  // namespace

bool SpriteStore::parse(const std::vector<uint8_t>& bytes, int& width, int& height, std::vector<uint16_t>& pixels) {
    if (bytes.size() < 8 || bytes[0] != 'P' || bytes[1] != 'K' || bytes[2] != 'S' || bytes[3] != '1') {
        return false;
    }
    width = util::readU16(&bytes[4]);
    height = util::readU16(&bytes[6]);
    if (width <= 0 || height <= 0 || width > kMaxDimension || height > kMaxDimension ||
        bytes.size() < 8 + static_cast<size_t>(width * height * 2)) {
        return false;
    }
    pixels.resize(static_cast<size_t>(width * height));
    for (size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = util::readU16(&bytes[8 + i * 2]);
    }
    return true;
}

int SpriteStore::kindIndex(const char* kind) {
    for (size_t i = 0; i < kinds_.size(); ++i) {
        if (kinds_[i] == kind) return static_cast<int>(i);
    }
    kinds_.push_back(kind);
    return static_cast<int>(kinds_.size() - 1);
}

Image SpriteStore::get(const char* kind, uint16_t species) {
    // Called for every icon of every frame: the lookup must not build strings or allocate.
    const uint32_t id = (static_cast<uint32_t>(kindIndex(kind)) << 16) | species;
    auto it = cache_.find(id);
    if (it != cache_.end()) {
        recent_.splice(recent_.begin(), recent_, it->second.recent);
    } else {
        const std::string key = std::string(kind) + "/" + std::to_string(species);
        while (!recent_.empty() && (cache_.size() >= kMaxCachedSprites || cachedBytes_ >= kMaxCachedBytes)) {
            auto oldest = cache_.find(recent_.back());
            cachedBytes_ -= oldest->second.pixels.size() * sizeof(uint16_t);
            cache_.erase(oldest);
            recent_.pop_back();
        }
        Entry entry;
        std::vector<uint8_t> bytes;
        const std::string* dir = directory();
        if (dir != nullptr && (readPacked(kindIndex(kind), species, bytes) ||
                               io::readFile(io::joinPath(*dir, key + ".bin"), bytes))) {
            if (!parse(bytes, entry.width, entry.height, entry.pixels)) {
                entry.pixels.clear();
            }
        }
        cachedBytes_ += entry.pixels.size() * sizeof(uint16_t);
        recent_.push_front(id);
        entry.recent = recent_.begin();
        it = cache_.emplace(id, std::move(entry)).first;
    }
    Image image;
    if (!it->second.pixels.empty()) {
        image.width = it->second.width;
        image.height = it->second.height;
        image.pixels = it->second.pixels.data();
    }
    return image;
}

const std::string* SpriteStore::directory() {
    if (chosen_ == -2) {
        chosen_ = -1;
        for (size_t i = 0; i < directories_.size(); ++i) {
            if (io::fileExists(io::joinPath(directories_[i], "icons.pak")) ||
                io::fileExists(io::joinPath(directories_[i], "icons/25.bin"))) {
                chosen_ = static_cast<int>(i);
                break;
            }
        }
    }
    return chosen_ >= 0 ? &directories_[chosen_] : nullptr;
}

SpriteStore::~SpriteStore() {
    for (Pack& pack : packs_) {
        if (pack.file != nullptr) std::fclose(pack.file);
    }
}

bool SpriteStore::readPacked(int kind, uint16_t species, std::vector<uint8_t>& bytes) {
    if (static_cast<size_t>(kind) >= packs_.size()) packs_.resize(static_cast<size_t>(kind) + 1);
    Pack& pack = packs_[static_cast<size_t>(kind)];
    if (!pack.tried) {
        pack.tried = true;
        const std::string* dir = directory();
        if (dir == nullptr) return false;
        std::FILE* f = std::fopen(io::joinPath(*dir, kinds_[static_cast<size_t>(kind)] + ".pak").c_str(), "rb");
        uint8_t header[8];
        if (f != nullptr && std::fread(header, 1, sizeof header, f) == sizeof header && header[0] == 'P' &&
            header[1] == 'K' && header[2] == 'S' && header[3] == 'P') {
            const size_t count = util::readU16(header + 4);
            std::vector<uint8_t> table((count + 2) * 4);
            if (std::fread(table.data(), 1, table.size(), f) == table.size()) {
                pack.offsets.resize(count + 2);
                for (size_t i = 0; i < pack.offsets.size(); ++i) pack.offsets[i] = util::readU32(&table[i * 4]);
                pack.file = f;
                f = nullptr;
            }
        }
        if (f != nullptr) std::fclose(f);
    }
    if (pack.file == nullptr) return false;
    bytes.clear();
    if (species + 1u >= pack.offsets.size()) return true;  // packed kind without this species
    const uint32_t begin = pack.offsets[species];
    const uint32_t end = pack.offsets[species + 1u];
    if (end <= begin || end - begin > 8 + 2u * kMaxDimension * kMaxDimension) return true;
    bytes.resize(end - begin);
    if (std::fseek(pack.file, static_cast<long>(begin), SEEK_SET) != 0 ||
        std::fread(bytes.data(), 1, bytes.size(), pack.file) != bytes.size()) {
        bytes.clear();
    }
    return true;
}

bool SpriteStore::available() {
    return directory() != nullptr;
}

}  // namespace pktool::gfx
