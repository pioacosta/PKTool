#include "core/data/card_pack.h"

#include <cstring>

#include "core/io/file_io.h"
#include "core/util/bytes.h"

namespace pktool::data {

bool findInCardPack(const std::vector<std::string>& dirs, const char* pack, const char* magic, size_t size,
                    const std::function<bool(const uint8_t*)>& match, uint8_t* out) {
    for (const std::string& dir : dirs) {
        io::FileReader file;
        if (!file.open(io::joinPath(dir, pack))) continue;
        uint8_t head[8];
        if (!file.read(0, sizeof head, head) || std::memcmp(head, magic, 4) != 0 || util::readU16(head + 6) != size) {
            continue;
        }
        const size_t count = util::readU16(head + 4);
        // A few cards at a time: the packs are 140-210 KB, the DS heap small.
        constexpr size_t kChunkBytes = 0x1000;
        const size_t perChunk = kChunkBytes / size;
        uint8_t chunk[kChunkBytes];
        for (size_t first = 0; first < count; first += perChunk) {
            const size_t n = count - first < perChunk ? count - first : perChunk;
            if (!file.read(sizeof head + first * size, n * size, chunk)) break;
            for (size_t i = 0; i < n; ++i) {
                if (match(chunk + i * size)) {
                    std::memcpy(out, chunk + i * size, size);
                    return true;
                }
            }
        }
    }
    return false;
}

}  // namespace pktool::data
