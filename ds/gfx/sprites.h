#pragma once

#include <cstdint>
#include <cstdio>
#include <list>
#include <map>
#include <string>
#include <vector>

#include "ds/gfx/canvas.h"

// Optional sprite pack loaded from the SD card.
//
// The pack lives in assets/nitrofs/sprites (built by tools/fetch_sprites.py)
// and is packed into PKTool.nds with NitroFS; a copy in /PKTOOL/sprites on the
// SD card is used when the loader does not support NitroFS.
// Files are "<kind>/<species>.bin":
//   "PKS1" magic, u16 width, u16 height, then width*height pixels in the DS
//   bitmap format (bit 15 = opaque), all little endian.
// or one pack per kind, "<kind>.pak" (tools/pack_sprites.py), preferred:
//   "PKSP" magic, u16 count, u16 0, then u32 offsets[count + 2] from the start
//   of the file (species s is bytes offsets[s]..offsets[s + 1], empty if equal),
//   then the PKS1 blobs. The pack stays open, so a sprite costs one seek and
//   one read instead of a file lookup among ~650 files (slow on a flashcart).
// Kinds: "icons" (box icons), "front" and "front-shiny" (summary sprites).
namespace pktool::gfx {

class SpriteStore {
public:
    // Directories are searched in order; the first one with icons wins.
    explicit SpriteStore(std::vector<std::string> directories) : directories_(std::move(directories)) {}
    ~SpriteStore();
    SpriteStore(const SpriteStore&) = delete;
    SpriteStore& operator=(const SpriteStore&) = delete;

    // Returns an image with pixels == nullptr when the sprite is unavailable.
    // The pixels stay valid until the next call to get(): draw them first.
    Image get(const char* kind, uint16_t species);

    bool available();  // true if any directory contains the sprite pack

    // Frees every cached sprite (e.g. to make room before writing a save).
    void clear() {
        cache_.clear();
        recent_.clear();
        cachedBytes_ = 0;
    }

    // Parses a sprite file's contents (exposed for tests).
    static bool parse(const std::vector<uint8_t>& bytes, int& width, int& height, std::vector<uint16_t>& pixels);

private:
    struct Entry {
        int width = 0;
        int height = 0;
        std::vector<uint16_t> pixels;
        std::list<uint32_t>::iterator recent;  // position in recent_
    };

    const std::string* directory();  // the directory in use, or nullptr
    int kindIndex(const char* kind);  // small number for a kind name (registered on first use)
    // Reads species' blob from the kind's pack; false if there is no pack.
    bool readPacked(int kind, uint16_t species, std::vector<uint8_t>& bytes);
    struct Pack {
        std::FILE* file = nullptr;  // open for the store's lifetime
        std::vector<uint32_t> offsets;
        bool tried = false;
    };
    std::vector<Pack> packs_;  // by kind index

    std::vector<std::string> directories_;
    int chosen_ = -2;  // -2 not searched yet, -1 none found
    std::vector<std::string> kinds_;  // names of the kinds seen so far; the cache key uses the index
    std::map<uint32_t, Entry> cache_;  // kind index << 16 | species
    // Keys, most recently used first. When the cache is full the least recently used sprites go
    // one at a time (the box icons in use stay; stale front sprites leave), not all at once.
    std::list<uint32_t> recent_;
    size_t cachedBytes_ = 0;
};

}  // namespace pktool::gfx
