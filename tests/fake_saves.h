#pragma once
// In-memory blank saves of every supported game, shared by the tests that
// need a loadable save without real files.

#include <cstdint>
#include <cstring>
#include <vector>

#include "core/saves/game.h"
#include "core/saves/gen3_save.h"
#include "core/saves/gen4_save.h"
#include "core/saves/gen5_save.h"
#include "core/text/gen4_text.h"
#include "core/util/bytes.h"
#include "core/util/crc16.h"
#include "tests/test.h"

namespace fake {

using namespace pktool;
using saves::Game;

// ---------------------------------------------------------------- builders

constexpr size_t kSector = 0x1000;
constexpr uint32_t kEmeraldKey = 0xA1B2C3D4;
constexpr uint32_t kFrlgKey = 0x1234ABCD;

inline uint16_t sectorChecksum(const uint8_t* sector) {
    uint32_t sum = 0;
    for (size_t i = 0; i < 0xF80; i += 4) sum += util::readU32(sector + i);
    return static_cast<uint16_t>(sum + (sum >> 16));
}

// A blank Gen III save (slot 1 newer, sectors in order) with trainer "ASH".
inline std::vector<uint8_t> blankGen3(Game game) {
    std::vector<uint8_t> data(saves::Gen3Save::kFileSize, 0);
    for (int slot = 0; slot < 2; ++slot) {
        for (int id = 0; id < 14; ++id) {
            uint8_t* sector = data.data() + (slot * 14 + id) * kSector;
            if (id == 0) {
                std::memset(sector, 0xFF, 8);
                text::encodeGen3("ASH", sector, 8, 7, 2);
                util::writeU16(sector + 0x0A, 12345);
                util::writeU16(sector + 0x0C, 54321);
                if (game == Game::FireRedLeafGreen) {
                    util::writeU32(sector + 0xAC, 1);
                    util::writeU32(sector + 0xF20, kFrlgKey);
                } else if (game == Game::Emerald) {
                    util::writeU32(sector + 0xAC, kEmeraldKey);
                    sector[0x900] = 1;  // Emerald uses more of the sector than Ruby/Sapphire
                }
            }
            util::writeU16(sector + 0xFF4, static_cast<uint16_t>(id));
            util::writeU32(sector + 0xFF8, 0x08012025);
            util::writeU32(sector + 0xFFC, slot == 1 ? 7 : 6);
            util::writeU16(sector + 0xFF6, sectorChecksum(sector));
        }
    }
    return data;
}

struct Gen4Layout {
    Game game;
    uint32_t generalSize, storageOffset, storageSize, footerSize, trainer;
};
inline const Gen4Layout kDp{Game::DiamondPearl, 0xC100, 0xC100, 0x121E0, 0x14, 0x64};
inline const Gen4Layout kPt{Game::Platinum, 0xCF2C, 0xCF2C, 0x121E4, 0x14, 0x68};
inline const Gen4Layout kHgss{Game::HeartGoldSoulSilver, 0xF628, 0xF700, 0x12310, 0x10, 0x64};

inline void writeFooter(uint8_t* block, uint32_t size, uint32_t footerSize) {
    uint8_t* end = block + size;
    util::writeU32(end - 0x14, 10);
    util::writeU32(end - 0x10, 10);
    util::writeU32(end - 0xC, size);
    util::writeU32(end - 0x8, 0x20060623);
    util::writeU16(end - 2, util::crc16Ccitt(block, size - footerSize));
}

inline std::vector<uint8_t> blankGen4(const Gen4Layout& l) {
    std::vector<uint8_t> data(saves::Gen4Save::kFileSize, 0);
    for (int partition = 0; partition < 2; ++partition) {
        uint8_t* general = &data[partition * saves::Gen4Save::kPartitionSize];
        text::encodeGen4("ASH", general + l.trainer, 8, 7);
        util::writeU16(general + l.trainer + 0x10, 12345);
        util::writeU16(general + l.trainer + 0x12, 54321);
        writeFooter(general, l.generalSize, l.footerSize);
        writeFooter(general + l.storageOffset, l.storageSize, l.footerSize);
    }
    return data;
}

// A blank Gen V save: `footerBlock` is the checksum table block (offset,
// length, checksum offset).
inline std::vector<uint8_t> blankGen5(bool b2w2) {
    std::vector<uint8_t> data(saves::Gen5Save::kFileSize, 0);
    text::encodeGen5("ASH", &data[0x19400 + 4], 8, 7);
    util::writeU16(&data[0x19400 + 0x14], 12345);
    util::writeU16(&data[0x19400 + 0x16], 54321);
    data[0x19400 + 0x1E] = 2;
    data[0x19400 + 0x1F] = b2w2 ? 23 : 21;
    uint32_t table = b2w2 ? 0x25F00 : 0x23F00;
    uint32_t length = b2w2 ? 0x94 : 0x8C;
    util::writeU16(&data[table + length + 0xE], util::crc16Ccitt(&data[table], length));
    saves::Gen5Save save;
    CHECK(save.load(data) == saves::LoadResult::Ok);
    save.refreshChecksums();
    return save.data();
}

}  // namespace fake
