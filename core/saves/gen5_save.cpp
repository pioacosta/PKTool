#include "core/saves/gen5_save.h"

#include <algorithm>
#include <cstring>

#include "core/pokemon/pk4.h"
#include "core/pokemon/pk5.h"
#include "core/text/gen4_text.h"
#include "core/util/bytes.h"
#include "core/util/crc16.h"

namespace pktool::saves {
namespace {

namespace pk4 = pokemon::pk4;
namespace pk5 = pokemon::pk5;
using Block = Gen5Save::Block;
using util::readU16;
using util::writeU16;

// BW save blocks: offset, length, checksum offset, checksum mirror (from PKHeX).
const Block kBlocksBW[] = {
    {0x00000, 0x03E0, 0x003E2, 0x23F00}, {0x00400, 0x0FF0, 0x013F2, 0x23F02}, {0x01400, 0x0FF0, 0x023F2, 0x23F04},
    {0x02400, 0x0FF0, 0x033F2, 0x23F06}, {0x03400, 0x0FF0, 0x043F2, 0x23F08}, {0x04400, 0x0FF0, 0x053F2, 0x23F0A},
    {0x05400, 0x0FF0, 0x063F2, 0x23F0C}, {0x06400, 0x0FF0, 0x073F2, 0x23F0E}, {0x07400, 0x0FF0, 0x083F2, 0x23F10},
    {0x08400, 0x0FF0, 0x093F2, 0x23F12}, {0x09400, 0x0FF0, 0x0A3F2, 0x23F14}, {0x0A400, 0x0FF0, 0x0B3F2, 0x23F16},
    {0x0B400, 0x0FF0, 0x0C3F2, 0x23F18}, {0x0C400, 0x0FF0, 0x0D3F2, 0x23F1A}, {0x0D400, 0x0FF0, 0x0E3F2, 0x23F1C},
    {0x0E400, 0x0FF0, 0x0F3F2, 0x23F1E}, {0x0F400, 0x0FF0, 0x103F2, 0x23F20}, {0x10400, 0x0FF0, 0x113F2, 0x23F22},
    {0x11400, 0x0FF0, 0x123F2, 0x23F24}, {0x12400, 0x0FF0, 0x133F2, 0x23F26}, {0x13400, 0x0FF0, 0x143F2, 0x23F28},
    {0x14400, 0x0FF0, 0x153F2, 0x23F2A}, {0x15400, 0x0FF0, 0x163F2, 0x23F2C}, {0x16400, 0x0FF0, 0x173F2, 0x23F2E},
    {0x17400, 0x0FF0, 0x183F2, 0x23F30}, {0x18400, 0x09C0, 0x18DC2, 0x23F32}, {0x18E00, 0x0534, 0x19336, 0x23F34},
    {0x19400, 0x0068, 0x1946A, 0x23F36}, {0x19500, 0x009C, 0x1959E, 0x23F38}, {0x19600, 0x1338, 0x1A93A, 0x23F3A},
    {0x1AA00, 0x07C4, 0x1B1C6, 0x23F3C}, {0x1B200, 0x0D54, 0x1BF56, 0x23F3E}, {0x1C000, 0x002C, 0x1C02E, 0x23F40},
    {0x1C100, 0x0658, 0x1C75A, 0x23F42}, {0x1C800, 0x0A94, 0x1D296, 0x23F44}, {0x1D300, 0x01AC, 0x1D4AE, 0x23F46},
    {0x1D500, 0x03EC, 0x1D8EE, 0x23F48}, {0x1D900, 0x005C, 0x1D95E, 0x23F4A}, {0x1DA00, 0x01E0, 0x1DBE2, 0x23F4C},
    {0x1DC00, 0x00A8, 0x1DCAA, 0x23F4E}, {0x1DD00, 0x0460, 0x1E162, 0x23F50}, {0x1E200, 0x1400, 0x1F602, 0x23F52},
    {0x1F700, 0x02A4, 0x1F9A6, 0x23F54}, {0x1FA00, 0x02DC, 0x1FCDE, 0x23F56}, {0x1FD00, 0x034C, 0x2004E, 0x23F58},
    {0x20100, 0x03EC, 0x204EE, 0x23F5A}, {0x20500, 0x00F8, 0x205FA, 0x23F5C}, {0x20600, 0x02FC, 0x208FE, 0x23F5E},
    {0x20900, 0x0094, 0x20996, 0x23F60}, {0x20A00, 0x035C, 0x20D5E, 0x23F62}, {0x20E00, 0x01CC, 0x20FCE, 0x23F64},
    {0x21000, 0x0168, 0x2116A, 0x23F66}, {0x21200, 0x00EC, 0x212EE, 0x23F68}, {0x21300, 0x01B0, 0x214B2, 0x23F6A},
    {0x21500, 0x001C, 0x2151E, 0x23F6C}, {0x21600, 0x04D4, 0x21AD6, 0x23F6E}, {0x21B00, 0x0034, 0x21B36, 0x23F70},
    {0x21C00, 0x003C, 0x21C3E, 0x23F72}, {0x21D00, 0x01AC, 0x21EAE, 0x23F74}, {0x21F00, 0x0B90, 0x22A92, 0x23F76},
    {0x22B00, 0x009C, 0x22B9E, 0x23F78}, {0x22C00, 0x0850, 0x23452, 0x23F7A}, {0x23500, 0x0028, 0x2352A, 0x23F7C},
    {0x23600, 0x0284, 0x23886, 0x23F7E}, {0x23900, 0x0010, 0x23912, 0x23F80}, {0x23A00, 0x005C, 0x23A5E, 0x23F82},
    {0x23B00, 0x016C, 0x23C6E, 0x23F84}, {0x23D00, 0x0040, 0x23D42, 0x23F86}, {0x23E00, 0x00FC, 0x23EFE, 0x23F88},
    {0x23F00, 0x008C, 0x23F9A, 0x23F9A},
};

// B2W2 save blocks: offset, length, checksum offset, checksum mirror (from PKHeX).
const Block kBlocksB2W2[] = {
    {0x00000, 0x03E0, 0x003E2, 0x25F00}, {0x00400, 0x0FF0, 0x013F2, 0x25F02}, {0x01400, 0x0FF0, 0x023F2, 0x25F04},
    {0x02400, 0x0FF0, 0x033F2, 0x25F06}, {0x03400, 0x0FF0, 0x043F2, 0x25F08}, {0x04400, 0x0FF0, 0x053F2, 0x25F0A},
    {0x05400, 0x0FF0, 0x063F2, 0x25F0C}, {0x06400, 0x0FF0, 0x073F2, 0x25F0E}, {0x07400, 0x0FF0, 0x083F2, 0x25F10},
    {0x08400, 0x0FF0, 0x093F2, 0x25F12}, {0x09400, 0x0FF0, 0x0A3F2, 0x25F14}, {0x0A400, 0x0FF0, 0x0B3F2, 0x25F16},
    {0x0B400, 0x0FF0, 0x0C3F2, 0x25F18}, {0x0C400, 0x0FF0, 0x0D3F2, 0x25F1A}, {0x0D400, 0x0FF0, 0x0E3F2, 0x25F1C},
    {0x0E400, 0x0FF0, 0x0F3F2, 0x25F1E}, {0x0F400, 0x0FF0, 0x103F2, 0x25F20}, {0x10400, 0x0FF0, 0x113F2, 0x25F22},
    {0x11400, 0x0FF0, 0x123F2, 0x25F24}, {0x12400, 0x0FF0, 0x133F2, 0x25F26}, {0x13400, 0x0FF0, 0x143F2, 0x25F28},
    {0x14400, 0x0FF0, 0x153F2, 0x25F2A}, {0x15400, 0x0FF0, 0x163F2, 0x25F2C}, {0x16400, 0x0FF0, 0x173F2, 0x25F2E},
    {0x17400, 0x0FF0, 0x183F2, 0x25F30}, {0x18400, 0x09EC, 0x18DEE, 0x25F32}, {0x18E00, 0x0534, 0x19336, 0x25F34},
    {0x19400, 0x00B0, 0x194B2, 0x25F36}, {0x19500, 0x00A8, 0x195AA, 0x25F38}, {0x19600, 0x1338, 0x1A93A, 0x25F3A},
    {0x1AA00, 0x07C4, 0x1B1C6, 0x25F3C}, {0x1B200, 0x0D54, 0x1BF56, 0x25F3E}, {0x1C000, 0x0094, 0x1C096, 0x25F40},
    {0x1C100, 0x0658, 0x1C75A, 0x25F42}, {0x1C800, 0x0A94, 0x1D296, 0x25F44}, {0x1D300, 0x01AC, 0x1D4AE, 0x25F46},
    {0x1D500, 0x03EC, 0x1D8EE, 0x25F48}, {0x1D900, 0x005C, 0x1D95E, 0x25F4A}, {0x1DA00, 0x01E0, 0x1DBE2, 0x25F4C},
    {0x1DC00, 0x00A8, 0x1DCAA, 0x25F4E}, {0x1DD00, 0x0460, 0x1E162, 0x25F50}, {0x1E200, 0x1400, 0x1F602, 0x25F52},
    {0x1F700, 0x02A4, 0x1F9A6, 0x25F54}, {0x1FA00, 0x00E0, 0x1FAE2, 0x25F56}, {0x1FB00, 0x034C, 0x1FE4E, 0x25F58},
    {0x1FF00, 0x04E0, 0x203E2, 0x25F5A}, {0x20400, 0x00F8, 0x204FA, 0x25F5C}, {0x20500, 0x02FC, 0x207FE, 0x25F5E},
    {0x20800, 0x0094, 0x20896, 0x25F60}, {0x20900, 0x035C, 0x20C5E, 0x25F62}, {0x20D00, 0x01D4, 0x20ED6, 0x25F64},
    {0x20F00, 0x01E0, 0x210E2, 0x25F66}, {0x21100, 0x00F0, 0x211F2, 0x25F68}, {0x21200, 0x01B4, 0x213B6, 0x25F6A},
    {0x21400, 0x04DC, 0x218DE, 0x25F6C}, {0x21900, 0x0034, 0x21936, 0x25F6E}, {0x21A00, 0x003C, 0x21A3E, 0x25F70},
    {0x21B00, 0x01AC, 0x21CAE, 0x25F72}, {0x21D00, 0x0B90, 0x22892, 0x25F74}, {0x22900, 0x00AC, 0x229AE, 0x25F76},
    {0x22A00, 0x0850, 0x23252, 0x25F78}, {0x23300, 0x0284, 0x23586, 0x25F7A}, {0x23600, 0x0010, 0x23612, 0x25F7C},
    {0x23700, 0x00A8, 0x237AA, 0x25F7E}, {0x23800, 0x016C, 0x2396E, 0x25F80}, {0x23A00, 0x0080, 0x23A82, 0x25F82},
    {0x23B00, 0x00FC, 0x23BFE, 0x25F84}, {0x23C00, 0x16A8, 0x252AA, 0x25F86}, {0x25300, 0x0498, 0x2579A, 0x25F88},
    {0x25800, 0x0060, 0x25862, 0x25F8A}, {0x25900, 0x00FC, 0x259FE, 0x25F8C}, {0x25A00, 0x03E4, 0x25DE6, 0x25F8E},
    {0x25E00, 0x00F0, 0x25EF2, 0x25F90}, {0x25F00, 0x0094, 0x25FA2, 0x25FA2},
};
constexpr uint32_t kBoxNamesOffset = 0x00000;
constexpr uint32_t kBoxOffset = 0x00400;
constexpr uint32_t kBoxStride = 0x1000;  // 30 slots (0xFF0) + checksum area
constexpr uint32_t kPartyOffset = 0x18E00;
constexpr uint32_t kTrainerOffset = 0x19400;
constexpr size_t kBoxNameChars = 10;
constexpr uint32_t kItemsOffset = 0x18400;
constexpr uint32_t kMiscOffsetBW = 0x21200;
constexpr uint32_t kMiscOffsetB2W2 = 0x21100;

// The checksum block (last block) doubles as the file footer.
bool footerValid(const std::vector<uint8_t>& data, const Block* blocks, int count) {
    const Block& info = blocks[count - 1];
    return readU16(data.data() + info.checksumOffset) == util::crc16Ccitt(data.data() + info.offset, info.length);
}

}  // namespace

bool Gen5Save::readProbeImage(std::vector<uint8_t>& image, const RangeReader& read) {
    if (image.size() != kFileSize) return false;
    for (const Block& footer : {kBlocksBW[sizeof(kBlocksBW) / sizeof(Block) - 1],
                                kBlocksB2W2[sizeof(kBlocksB2W2) / sizeof(Block) - 1]}) {
        if (!read(footer.offset, footer.checksumOffset + 2 - footer.offset, &image[footer.offset])) return false;
    }
    if (detect(image) == Game::Unknown) return false;
    // Party (0x18E00) and trainer card (0x19400) lie in consecutive blocks.
    return read(kPartyOffset, kTrainerOffset + 0x100 - kPartyOffset, &image[kPartyOffset]);
}

Game Gen5Save::detect(const std::vector<uint8_t>& data) {
    if (data.size() != kFileSize) return Game::Unknown;
    if (footerValid(data, kBlocksBW, sizeof(kBlocksBW) / sizeof(Block))) return Game::BlackWhite;
    if (footerValid(data, kBlocksB2W2, sizeof(kBlocksB2W2) / sizeof(Block))) return Game::Black2White2;
    return Game::Unknown;
}

LoadResult Gen5Save::load(std::vector<uint8_t> data) {
    if (data.size() != kFileSize) return LoadResult::WrongSize;
    Game game = detect(data);
    if (game == Game::Unknown) return LoadResult::UnknownFormat;
    data_ = std::move(data);
    game_ = game;
    if (game == Game::BlackWhite) {
        blocks_ = kBlocksBW;
        blockCount_ = sizeof(kBlocksBW) / sizeof(Block);
    } else {
        blocks_ = kBlocksB2W2;
        blockCount_ = sizeof(kBlocksB2W2) / sizeof(Block);
    }
    // Backup copy: a valid footer at the same place in the second copy.
    const Block& info = blocks_[blockCount_ - 1];
    size_t backup = mainSize();
    hasBackup_ = readU16(data_.data() + backup + info.checksumOffset) ==
                 util::crc16Ccitt(data_.data() + backup + info.offset, info.length);
    return LoadResult::Ok;
}

std::string Gen5Save::trainerName() const {
    return text::decodeGen5(data_.data() + kTrainerOffset + 4, 8);
}

uint16_t Gen5Save::trainerId() const { return readU16(data_.data() + kTrainerOffset + 0x14); }
uint16_t Gen5Save::secretId() const { return readU16(data_.data() + kTrainerOffset + 0x16); }
uint8_t Gen5Save::language() const { return data_[kTrainerOffset + 0x1E]; }
uint8_t Gen5Save::trainerGender() const { return data_[kTrainerOffset + 0x21] ? 1 : 0; }

uint8_t Gen5Save::gameVersion() const {
    uint8_t version = data_[kTrainerOffset + 0x1F];
    if (version >= 20 && version <= 23) return version;  // White, Black, White 2, Black 2
    return game_ == Game::BlackWhite ? 20 : 22;
}

size_t Gen5Save::miscOffset() const {
    return game_ == Game::BlackWhite ? kMiscOffsetBW : kMiscOffsetB2W2;
}

TrainerData Gen5Save::trainerData() const {
    const uint8_t* t = data_.data() + kTrainerOffset;
    const uint8_t* misc = data_.data() + miscOffset();
    TrainerData data;
    data.name = trainerName();
    data.gender = trainerGender();
    data.tid = trainerId();
    data.sid = secretId();
    data.money = util::readU32(misc);
    data.badges = misc[4];
    data.hours = readU16(t + 0x24);
    data.minutes = t[0x26];
    data.seconds = t[0x27];
    return data;
}

bool Gen5Save::setTrainerData(const TrainerData& data, std::string* error) {
    if (!validateTrainer(data, error)) return false;
    uint8_t name[16] = {};
    text::encodeGen5(data.name, name, 8, kTrainerNameMax);
    if (text::decodeGen5(name, 8) != data.name) {
        if (error) *error = "Name has characters the game cannot show";
        return false;
    }
    uint8_t* t = data_.data() + kTrainerOffset;
    uint8_t* misc = data_.data() + miscOffset();
    std::memcpy(t + 4, name, sizeof name);
    writeU16(t + 0x14, data.tid);
    writeU16(t + 0x16, data.sid);
    t[0x21] = data.gender;
    writeU16(t + 0x24, data.hours);
    t[0x26] = data.minutes;
    t[0x27] = data.seconds;
    util::writeU32(misc, data.money);
    misc[4] = static_cast<uint8_t>(data.badges);
    return true;
}

data::BagLayout Gen5Save::bagLayout() const {
    return game_ == Game::BlackWhite ? data::BagLayout::BlackWhite : data::BagLayout::Black2White2;
}

void Gen5Save::readBag(size_t offset, uint8_t* out, size_t length) const {
    std::memcpy(out, data_.data() + kItemsOffset + offset, length);
}

void Gen5Save::writeBag(size_t offset, const uint8_t* in, size_t length) {
    std::memcpy(data_.data() + kItemsOffset + offset, in, length);
}

int Gen5Save::partyCount() const {
    return std::min<int>(data_[kPartyOffset + 4], kPartyMax);
}

SlotState Gen5Save::readPartySlot(int slot, uint8_t* decrypted) const {
    if (slot < 0 || slot >= kPartyMax) return SlotState::Empty;
    const uint8_t* src = data_.data() + kPartyOffset + 8 + slot * pk5::kPartySize;
    if (pk4::isEmptySlot(src, pk5::kPartySize)) return SlotState::Empty;
    std::memcpy(decrypted, src, pk5::kPartySize);
    pk4::decrypt(decrypted, pk5::kPartySize);
    if (pk4::calcChecksum(decrypted) != pk4::storedChecksum(decrypted)) return SlotState::BadChecksum;
    return readU16(decrypted + pk4::offset::kSpecies) == 0 ? SlotState::Empty : SlotState::Valid;
}

bool Gen5Save::writePartySlot(int slot, const uint8_t* decrypted) {
    if (slot < 0 || slot >= kPartyMax) return false;
    uint8_t* dst = data_.data() + kPartyOffset + 8 + slot * pk5::kPartySize;
    std::memcpy(dst, decrypted, pk5::kPartySize);
    pk4::refreshChecksum(dst);
    pk4::encrypt(dst, pk5::kPartySize);
    return true;
}

void Gen5Save::setPartyCount(int count) {
    data_[kPartyOffset + 4] = static_cast<uint8_t>(count);
}

void Gen5Save::clearPartySlot(int slot) {
    if (slot < 0 || slot >= kPartyMax) return;
    std::memset(data_.data() + kPartyOffset + 8 + slot * pk5::kPartySize, 0, pk5::kPartySize);
}

std::string Gen5Save::boxName(int box) const {
    if (box < 0 || box >= kBoxCount) return std::string();
    return text::decodeGen5(data_.data() + kBoxNamesOffset + 4 + box * 0x28, kBoxNameChars);
}

bool Gen5Save::setBoxName(int box, const std::string& name) {
    if (box < 0 || box >= kBoxCount) return false;
    text::encodeGen5(name, data_.data() + kBoxNamesOffset + 4 + box * 0x28, kBoxNameChars, kBoxNameMaxLength);
    return true;
}

SlotState Gen5Save::readBoxSlot(int box, int slot, uint8_t* decrypted) const {
    if (box < 0 || box >= kBoxCount || slot < 0 || slot >= kSlotsPerBox) return SlotState::Empty;
    const uint8_t* src = data_.data() + kBoxOffset + box * kBoxStride + slot * pk5::kStoredSize;
    if (pk4::isEmptySlot(src, pk5::kStoredSize)) return SlotState::Empty;
    std::memcpy(decrypted, src, pk5::kStoredSize);
    pk4::decrypt(decrypted, pk5::kStoredSize);
    if (pk4::calcChecksum(decrypted) != pk4::storedChecksum(decrypted)) return SlotState::BadChecksum;
    return readU16(decrypted + pk4::offset::kSpecies) == 0 ? SlotState::Empty : SlotState::Valid;
}

bool Gen5Save::writeBoxSlot(int box, int slot, const uint8_t* decrypted) {
    if (box < 0 || box >= kBoxCount || slot < 0 || slot >= kSlotsPerBox) return false;
    uint8_t* dst = data_.data() + kBoxOffset + box * kBoxStride + slot * pk5::kStoredSize;
    std::memcpy(dst, decrypted, pk5::kStoredSize);
    pk4::refreshChecksum(dst);
    pk4::encrypt(dst, pk5::kStoredSize);
    return true;
}

bool Gen5Save::clearBoxSlot(int box, int slot) {
    if (box < 0 || box >= kBoxCount || slot < 0 || slot >= kSlotsPerBox) return false;
    std::memset(data_.data() + kBoxOffset + box * kBoxStride + slot * pk5::kStoredSize, 0, pk5::kStoredSize);
    return true;
}

void Gen5Save::refreshChecksums() {
    // The checksum block comes last, so its own CRC covers the updated mirrors.
    for (int i = 0; i < blockCount_; ++i) {
        const Block& b = blocks_[i];
        uint16_t crc = util::crc16Ccitt(data_.data() + b.offset, b.length);
        writeU16(data_.data() + b.checksumOffset, crc);
        writeU16(data_.data() + b.checksumMirror, crc);
    }
    // The games keep a backup copy right after the main save and write both
    // when saving; keep it identical so the two never disagree.
    if (hasBackup_) {
        std::copy(data_.begin(), data_.begin() + mainSize(), data_.begin() + mainSize());
    }
}

size_t Gen5Save::mainSize() const {
    const Block& info = blocks_[blockCount_ - 1];
    return (info.offset + 0x100 + 0xFF) & ~static_cast<size_t>(0xFF);  // 0x24000 (B/W) or 0x26000 (B2/W2)
}

bool Gen5Save::checksumsValid() const {
    for (int i = 0; i < blockCount_; ++i) {
        const Block& b = blocks_[i];
        uint16_t crc = util::crc16Ccitt(data_.data() + b.offset, b.length);
        if (readU16(data_.data() + b.checksumOffset) != crc || readU16(data_.data() + b.checksumMirror) != crc) {
            return false;
        }
    }
    return true;
}

}  // namespace pktool::saves
