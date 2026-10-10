#include "core/saves/gen4_save.h"

#include <algorithm>
#include <cstring>

#include "core/pokemon/pk4.h"
#include "core/text/gen4_text.h"
#include "core/util/bytes.h"
#include "core/util/crc16.h"

// Offsets and block rules cross-checked against PKHeX.Core (SAV4*.cs, GPLv3).
namespace pktool::saves {
namespace {

using util::readU16;
using util::readU32;
using util::writeU16;
using util::writeU32;
namespace pk4 = pokemon::pk4;

const Gen4Save::Layout kLayouts[] = {
    {Game::DiamondPearl, 0xC100, 0xC100, 0x121E0, 0x14, 0x64, 0x98},
    {Game::Platinum, 0xCF2C, 0xCF2C, 0x121E4, 0x14, 0x68, 0xA0},
    {Game::HeartGoldSoulSilver, 0xF628, 0xF700, 0x12310, 0x10, 0x64, 0x98},
};

// Footer "SDK" magic values (international/Japanese and Korean releases).
constexpr uint32_t kMagicInternational = 0x20060623;
constexpr uint32_t kMagicKorean = 0x20070903;

constexpr int kBoxNameChars = 20;
constexpr size_t kSinnohBoxSize = Gen4Save::kSlotsPerBox * pk4::kStoredSize;  // 0xFF0
constexpr size_t kHgssBoxSize = 0x1000;  // 0xFF0 padded to 0x1000
constexpr size_t kHgssBoxEnd = Gen4Save::kBoxCount * kHgssBoxSize;
constexpr size_t kHgssBoxNames = 0x12008;
constexpr size_t kHgssBoxChangedFlags = kHgssBoxEnd + 4;
constexpr size_t kHgssWalkingMood = 0x87;

bool footerLooksValid(const uint8_t* block, uint32_t size) {
    const uint8_t* end = block + size;
    if (readU32(end - 0xC) != size) {
        return false;
    }
    uint32_t magic = readU32(end - 0x8);
    return magic == kMagicInternational || magic == kMagicKorean;
}

bool blockChecksumValid(const uint8_t* block, uint32_t size, uint32_t footerSize) {
    return util::crc16Ccitt(block, size - footerSize) == readU16(block + size - 2);
}

void refreshBlockChecksum(uint8_t* block, uint32_t size, uint32_t footerSize) {
    writeU16(block + size - 2, util::crc16Ccitt(block, size - footerSize));
}

// Save counter comparison, as the games do it. 0 = first, 1 = second, 2 = same.
int compareCounters(uint32_t a, uint32_t b) {
    // 0xFFFFFFFF means "never written", unless it is a (humanly impossible) rollover.
    if (a == 0xFFFFFFFF && b != 0xFFFFFFFE) {
        return 1;
    }
    if (b == 0xFFFFFFFF && a != 0xFFFFFFFE) {
        return 0;
    }
    if (a > b) {
        return 0;
    }
    if (a < b) {
        return 1;
    }
    return 2;
}

// Returns the partition (0/1) with the newest copy of a block. Mirrors PKHeX,
// which reads the counters 0x14 bytes before the end of the block for every
// Gen IV game (HG/SS footers are 0x10 bytes long, the offset is still valid).
int newestPartition(const std::vector<uint8_t>& data, uint32_t blockOffset, uint32_t size) {
    size_t first = blockOffset + size - 0x14;
    size_t second = first + Gen4Save::kPartitionSize;
    int major = compareCounters(readU32(&data[first]), readU32(&data[second]));
    if (major != 2) {
        return major;
    }
    int minor = compareCounters(readU32(&data[first + 4]), readU32(&data[second + 4]));
    return minor == 1 ? 1 : 0;
}

// Picks the newest copy whose checksum is valid. Returns -1 if none is valid.
int chooseBlock(const std::vector<uint8_t>& data, uint32_t blockOffset, uint32_t size, uint32_t footerSize) {
    int newest = newestPartition(data, blockOffset, size);
    for (int candidate : {newest, 1 - newest}) {
        const uint8_t* block = &data[candidate * Gen4Save::kPartitionSize + blockOffset];
        if (blockChecksumValid(block, size, footerSize)) {
            return candidate;
        }
    }
    return -1;
}

}  // namespace

bool Gen4Save::readProbeImage(std::vector<uint8_t>& image, const RangeReader& read, int* general, int* storage) {
    if (image.size() != kFileSize) return false;
    constexpr size_t kCounters = 0x14;  // the footer part detect() and newestPartition() look at
    for (const Layout& layout : kLayouts) {
        for (size_t partition = 0; partition < 2; ++partition) {
            size_t at = partition * kPartitionSize + layout.generalSize - kCounters;
            if (!read(at, kCounters, &image[at])) return false;
        }
    }
    const Layout* layout = detect(image);
    if (layout == nullptr) return false;
    for (size_t partition = 0; partition < 2; ++partition) {
        size_t at = partition * kPartitionSize + layout->storageOffset + layout->storageSize - kCounters;
        if (!read(at, kCounters, &image[at])) return false;
    }
    *general = newestPartition(image, 0, layout->generalSize);
    *storage = newestPartition(image, layout->storageOffset, layout->storageSize);
    size_t generalAt = *general * kPartitionSize;
    size_t storageAt = *storage * kPartitionSize + layout->storageOffset;
    return read(generalAt, layout->generalSize, &image[generalAt]) &&
           read(storageAt, layout->storageSize, &image[storageAt]);
}

const Gen4Save::Layout* Gen4Save::detect(const std::vector<uint8_t>& data) {
    if (data.size() != kFileSize) {
        return nullptr;
    }
    for (const Layout& layout : kLayouts) {
        for (size_t partition = 0; partition < 2; ++partition) {
            if (footerLooksValid(&data[partition * kPartitionSize], layout.generalSize)) {
                return &layout;
            }
        }
    }
    return nullptr;
}

Gen4Save::LoadResult Gen4Save::load(std::vector<uint8_t> data) {
    layout_ = nullptr;
    if (data.size() != kFileSize) {
        return LoadResult::WrongSize;
    }
    const Layout* layout = detect(data);
    if (layout == nullptr) {
        return LoadResult::UnknownFormat;
    }
    int general = chooseBlock(data, 0, layout->generalSize, layout->footerSize);
    int storage = chooseBlock(data, layout->storageOffset, layout->storageSize, layout->footerSize);
    if (general < 0 || storage < 0) {
        return LoadResult::BadChecksum;
    }
    data_ = std::move(data);
    layout_ = layout;
    generalPartition_ = general;
    storagePartition_ = storage;
    version_ = guessVersion();
    return LoadResult::Ok;
}

// D/P and HG/SS saves do not record which of the two games they belong to.
// The trainer's own Pokémon do (their origin game): take the most common one.
uint8_t Gen4Save::guessVersion() const {
    uint8_t first = 0;
    uint8_t second = 0;
    switch (game()) {
        case Game::DiamondPearl: first = 10; second = 11; break;
        case Game::HeartGoldSoulSilver: first = 7; second = 8; break;
        case Game::Platinum: return 12;
        default: return 0;
    }
    int votes[2] = {0, 0};
    const uint16_t tid = trainerId();
    const uint16_t sid = secretId();
    auto vote = [&](const uint8_t* d) {
        if (readU16(d + pk4::offset::kTid) != tid || readU16(d + pk4::offset::kSid) != sid) return;
        uint8_t origin = d[pk4::offset::kOriginGame];
        if (origin == first) ++votes[0];
        if (origin == second) ++votes[1];
    };
    uint8_t buffer[pk4::kPartySize];
    for (int slot = 0; slot < partyCount(); ++slot) {
        if (readPartySlot(slot, buffer) == SlotState::Valid) vote(buffer);
    }
    for (int box = 0; box < kBoxCount; ++box) {
        for (int slot = 0; slot < kSlotsPerBox; ++slot) {
            if (readBoxSlot(box, slot, buffer) == SlotState::Valid) vote(buffer);
        }
    }
    return votes[1] > votes[0] ? second : first;
}

Game Gen4Save::game() const {
    return layout_ ? layout_->game : Game::Unknown;
}

uint8_t* Gen4Save::general() {
    return &data_[generalPartition_ * kPartitionSize];
}

const uint8_t* Gen4Save::general() const {
    return &data_[generalPartition_ * kPartitionSize];
}

uint8_t* Gen4Save::storage() {
    return &data_[storagePartition_ * kPartitionSize + layout_->storageOffset];
}

const uint8_t* Gen4Save::storage() const {
    return &data_[storagePartition_ * kPartitionSize + layout_->storageOffset];
}

bool Gen4Save::isHgss() const {
    return layout_ && layout_->game == Game::HeartGoldSoulSilver;
}

std::string Gen4Save::trainerName() const {
    return text::decodeGen4(general() + layout_->trainerOffset, 8);
}

uint16_t Gen4Save::trainerId() const {
    return readU16(general() + layout_->trainerOffset + 0x10);
}

uint16_t Gen4Save::secretId() const {
    return readU16(general() + layout_->trainerOffset + 0x12);
}

uint8_t Gen4Save::trainerGender() const {
    return general()[layout_->trainerOffset + 0x18] ? 1 : 0;
}

uint8_t Gen4Save::language() const {
    return general()[layout_->trainerOffset + 0x19];
}

uint8_t Gen4Save::gameVersion() const {
    return version_;
}

TrainerData Gen4Save::trainerData() const {
    const uint8_t* t = general() + layout_->trainerOffset;
    TrainerData data;
    data.name = trainerName();
    data.gender = trainerGender();
    data.tid = trainerId();
    data.sid = secretId();
    data.money = readU32(t + 0x14);
    data.hours = readU16(t + 0x22);
    data.minutes = t[0x24];
    data.seconds = t[0x25];
    data.badges = t[0x1A];
    if (isHgss()) data.badges |= static_cast<uint16_t>(t[0x1F] << 8);
    return data;
}

bool Gen4Save::setTrainerData(const TrainerData& data, std::string* error) {
    if (!validateTrainer(data, error)) return false;
    uint8_t name[16] = {};
    text::encodeGen4(data.name, name, 8, kTrainerNameMax, language());
    if (text::decodeGen4(name, 8) != data.name) {
        if (error) *error = "Name has characters the game cannot show";
        return false;
    }
    uint8_t* t = general() + layout_->trainerOffset;
    std::memcpy(t, name, sizeof name);
    writeU16(t + 0x10, data.tid);
    writeU16(t + 0x12, data.sid);
    writeU32(t + 0x14, data.money);
    t[0x18] = data.gender;
    t[0x1A] = static_cast<uint8_t>(data.badges & 0xFF);
    if (isHgss()) t[0x1F] = static_cast<uint8_t>(data.badges >> 8);
    writeU16(t + 0x22, data.hours);
    t[0x24] = data.minutes;
    t[0x25] = data.seconds;
    return true;
}

// Start of the bag in the general block (PKHeX PlayerBag4*).
data::BagLayout Gen4Save::bagLayout() const {
    switch (layout_->game) {
        case Game::Platinum: return data::BagLayout::Platinum;
        case Game::HeartGoldSoulSilver: return data::BagLayout::HeartGoldSoulSilver;
        default: return data::BagLayout::DiamondPearl;
    }
}

namespace {
size_t bagStart(Game game) {
    return game == Game::Platinum ? 0x630 : game == Game::HeartGoldSoulSilver ? 0x644 : 0x624;
}
}  // namespace

void Gen4Save::readBag(size_t offset, uint8_t* out, size_t length) const {
    std::memcpy(out, general() + bagStart(layout_->game) + offset, length);
}

void Gen4Save::writeBag(size_t offset, const uint8_t* in, size_t length) {
    std::memcpy(general() + bagStart(layout_->game) + offset, in, length);
}

int Gen4Save::partyCount() const {
    return std::min<int>(general()[layout_->partyOffset - 4], kPartyMax);
}

Gen4Save::SlotState Gen4Save::readPartySlot(int slot, uint8_t* decrypted) const {
    if (slot < 0 || slot >= kPartyMax) {
        return SlotState::Empty;
    }
    const uint8_t* src = general() + layout_->partyOffset + slot * pk4::kPartySize;
    if (pk4::isEmptySlot(src, pk4::kPartySize)) {
        return SlotState::Empty;
    }
    std::memcpy(decrypted, src, pk4::kPartySize);
    pk4::decrypt(decrypted, pk4::kPartySize);
    if (pk4::calcChecksum(decrypted) != pk4::storedChecksum(decrypted)) {
        return SlotState::BadChecksum;
    }
    return readU16(decrypted + pk4::offset::kSpecies) == 0 ? SlotState::Empty : SlotState::Valid;
}

bool Gen4Save::writePartySlot(int slot, const uint8_t* decrypted) {
    if (slot < 0 || slot >= kPartyMax) {
        return false;
    }
    uint8_t* dst = general() + layout_->partyOffset + slot * pk4::kPartySize;
    std::memcpy(dst, decrypted, pk4::kPartySize);
    pk4::refreshChecksum(dst);
    pk4::encrypt(dst, pk4::kPartySize);
    return true;
}

void Gen4Save::setPartyCount(int count) {
    general()[layout_->partyOffset - 4] = static_cast<uint8_t>(count);
}

void Gen4Save::clearPartySlot(int slot) {
    if (slot < 0 || slot >= kPartyMax) return;
    std::memset(general() + layout_->partyOffset + slot * pk4::kPartySize, 0, pk4::kPartySize);
}

size_t Gen4Save::boxSlotOffset(int box, int slot) const {
    size_t boxStart = isHgss() ? box * kHgssBoxSize : 4 + box * kSinnohBoxSize;
    return boxStart + slot * pk4::kStoredSize;
}

std::string Gen4Save::boxName(int box) const {
    if (box < 0 || box >= kBoxCount) {
        return std::string();
    }
    size_t names = isHgss() ? kHgssBoxNames : 4 + kBoxCount * kSinnohBoxSize;
    return text::decodeGen4(storage() + names + box * kBoxNameChars * 2, kBoxNameChars);
}

bool Gen4Save::setBoxName(int box, const std::string& name) {
    if (box < 0 || box >= kBoxCount) {
        return false;
    }
    size_t names = isHgss() ? kHgssBoxNames : 4 + kBoxCount * kSinnohBoxSize;
    text::encodeGen4(name, storage() + names + box * kBoxNameChars * 2, kBoxNameChars, kBoxNameMaxLength, language());
    return true;
}

int Gen4Save::currentBox() const {
    int box = storage()[isHgss() ? kHgssBoxEnd : 0];
    return box < kBoxCount ? box : 0;
}

Gen4Save::SlotState Gen4Save::readBoxSlot(int box, int slot, uint8_t* decrypted) const {
    if (box < 0 || box >= kBoxCount || slot < 0 || slot >= kSlotsPerBox) {
        return SlotState::Empty;
    }
    const uint8_t* src = storage() + boxSlotOffset(box, slot);
    if (pk4::isEmptySlot(src, pk4::kStoredSize)) {
        return SlotState::Empty;
    }
    std::memcpy(decrypted, src, pk4::kStoredSize);
    pk4::decrypt(decrypted, pk4::kStoredSize);
    if (pk4::calcChecksum(decrypted) != pk4::storedChecksum(decrypted)) {
        return SlotState::BadChecksum;
    }
    return readU16(decrypted + pk4::offset::kSpecies) == 0 ? SlotState::Empty : SlotState::Valid;
}

bool Gen4Save::writeBoxSlot(int box, int slot, const uint8_t* decrypted) {
    if (box < 0 || box >= kBoxCount || slot < 0 || slot >= kSlotsPerBox) {
        return false;
    }
    uint8_t* dst = storage() + boxSlotOffset(box, slot);
    std::memcpy(dst, decrypted, pk4::kStoredSize);
    if (isHgss()) {
        // HG/SS keep the Pokéwalker mood only for party members, and only
        // re-save boxes flagged as changed.
        dst[kHgssWalkingMood] = 0;
        uint8_t* flags = storage() + kHgssBoxChangedFlags;
        writeU32(flags, readU32(flags) | (1u << box));
    }
    pk4::refreshChecksum(dst);
    pk4::encrypt(dst, pk4::kStoredSize);
    return true;
}

bool Gen4Save::clearBoxSlot(int box, int slot) {
    if (box < 0 || box >= kBoxCount || slot < 0 || slot >= kSlotsPerBox) {
        return false;
    }
    std::memset(storage() + boxSlotOffset(box, slot), 0, pk4::kStoredSize);
    if (isHgss()) {
        uint8_t* flags = storage() + kHgssBoxChangedFlags;
        writeU32(flags, readU32(flags) | (1u << box));
    }
    return true;
}

void Gen4Save::refreshChecksums() {
    refreshBlockChecksum(general(), layout_->generalSize, layout_->footerSize);
    refreshBlockChecksum(storage(), layout_->storageSize, layout_->footerSize);
}

bool Gen4Save::checksumsValid() const {
    return blockChecksumValid(general(), layout_->generalSize, layout_->footerSize) &&
           blockChecksumValid(storage(), layout_->storageSize, layout_->footerSize);
}

}  // namespace pktool::saves
