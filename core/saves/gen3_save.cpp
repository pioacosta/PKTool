#include "core/saves/gen3_save.h"

#include <cstring>

#include "core/pokemon/pk3.h"
#include "core/text/gen4_text.h"
#include "core/util/bytes.h"

namespace pktool::saves {
namespace {

using util::readU16;
using util::readU32;

constexpr size_t kSectorSize = 0x1000;
constexpr size_t kSectorData = 0xF80;
constexpr int kSectorsPerSlot = 14;
constexpr size_t kSlotSize = kSectorSize * kSectorsPerSlot;
constexpr size_t kFooterId = 0xFF4;
constexpr size_t kFooterChecksum = 0xFF6;
constexpr size_t kFooterSignature = 0xFF8;
constexpr size_t kFooterCounter = 0xFFC;
constexpr uint32_t kSignature = 0x08012025;
constexpr size_t kMaxTrailer = 0x100;  // emulator RTC blocks and similar

constexpr size_t kBoxNameChars = 9;  // 8 characters + terminator
constexpr size_t kBoxNamesOffset = 4 + Gen3Save::kBoxCount * SaveFile::kSlotsPerBox * pokemon::pk3::kStoredSize;

// Sum of the 32-bit words of a sector's data, folded to 16 bits.
uint16_t sectorChecksum(const uint8_t* sector) {
    uint32_t sum = 0;
    for (size_t i = 0; i < kSectorData; i += 4) sum += readU32(sector + i);
    return static_cast<uint16_t>(sum + (sum >> 16));
}

// Whether all 14 sectors of `slot` are present; fills their file offsets.
bool slotSectors(const std::vector<uint8_t>& data, int slot, size_t offsets[kSectorsPerSlot]) {
    size_t start = slot * kSlotSize;
    if (start + kSlotSize > data.size()) return false;
    uint32_t seen = 0;
    for (int i = 0; i < kSectorsPerSlot; ++i) {
        size_t ofs = start + i * kSectorSize;
        uint16_t id = readU16(data.data() + ofs + kFooterId);
        if (id >= kSectorsPerSlot || readU32(data.data() + ofs + kFooterSignature) != kSignature) return false;
        seen |= 1u << id;
        offsets[id] = ofs;
    }
    return seen == (1u << kSectorsPerSlot) - 1;
}

bool sizeOk(size_t size) {
    return size == Gen3Save::kHalfFileSize || (size >= Gen3Save::kFileSize && size <= Gen3Save::kFileSize + kMaxTrailer);
}

// The newer of two slots by save counter (PKHeX's SAV3BlockDetection).
int newerSlot(uint32_t counter0, uint32_t counter1) {
    if (counter0 == 0xFFFFFFFFu && counter1 != 0xFFFFFFFEu) return 1;
    if (counter1 == 0xFFFFFFFFu && counter0 != 0xFFFFFFFEu) return 0;
    return counter1 > counter0 ? 1 : 0;
}

Game gameFromSmall(const uint8_t* small) {
    // 0xAC: FR/LG keep a fixed 1, R/S battle tower data (0 when unused),
    // Emerald a random security key. Emerald also uses more of the sector.
    uint32_t value = readU32(small + 0xAC);
    if (value == 1) return Game::FireRedLeafGreen;
    if (value == 0) return Game::RubySapphire;
    for (size_t i = 0x890; i < 0xF2C; ++i) {
        if (small[i] != 0) return Game::Emerald;
    }
    return Game::RubySapphire;
}

// Offsets inside the small block (sector 0) and the large block (sectors
// 1-4), from PKHeX's SaveBlock3Small*/SaveBlock3Large*.
constexpr size_t kSmallGender = 0x08;
constexpr size_t kSmallTid = 0x0A;
constexpr size_t kSmallHours = 0x0E;
constexpr size_t kSmallKeyEmerald = 0xAC;
constexpr size_t kSmallKeyFrlg = 0xF20;

struct EventFlags {
    size_t base;     // event flag array in the large block
    int badgeFirst;  // flag number of the first badge
};
EventFlags eventFlags(Game game) {
    switch (game) {
        case Game::Emerald: return {0x1270, 0x867};
        case Game::FireRedLeafGreen: return {0xEE0, 0x820};
        default: return {0x1220, 0x807};
    }
}

}  // namespace

namespace {
// The slot a loader should use (the newer of the complete ones), or -1.
int chooseSlot(const std::vector<uint8_t>& data, size_t offsets[2][kSectorsPerSlot]) {
    bool valid[2] = {slotSectors(data, 0, offsets[0]), slotSectors(data, 1, offsets[1])};
    if (!valid[0] && !valid[1]) return -1;
    return !valid[0] ? 1 : !valid[1] ? 0
                                     : newerSlot(readU32(data.data() + offsets[0][0] + kFooterCounter),
                                                 readU32(data.data() + offsets[1][0] + kFooterCounter));
}
}  // namespace

Game Gen3Save::detect(const std::vector<uint8_t>& data) {
    if (!sizeOk(data.size())) return Game::Unknown;
    size_t offsets[2][kSectorsPerSlot];
    int slot = chooseSlot(data, offsets);
    if (slot < 0) return Game::Unknown;
    return gameFromSmall(data.data() + offsets[slot][0]);
}

bool Gen3Save::readProbeImage(std::vector<uint8_t>& image, const RangeReader& read) {
    if (!sizeOk(image.size())) return false;
    // Sector footers (ID, signature, counter) of both slots say which slot is current...
    for (int slot = 0; slot < 2; ++slot) {
        size_t start = slot * kSlotSize;
        if (start + kSlotSize > image.size()) continue;
        for (int i = 0; i < kSectorsPerSlot; ++i) {
            size_t at = start + i * kSectorSize + kFooterId;
            if (!read(at, kSectorSize - kFooterId, &image[at])) return false;
        }
    }
    size_t offsets[2][kSectorsPerSlot];
    int slot = chooseSlot(image, offsets);
    if (slot < 0) return false;
    // ...and load() checksums, and the party and boxes, need all of that slot.
    return read(slot * kSlotSize, kSlotSize, &image[slot * kSlotSize]);
}

LoadResult Gen3Save::load(std::vector<uint8_t> data) {
    if (!sizeOk(data.size())) return LoadResult::WrongSize;
    size_t offsets[2][kSectorsPerSlot];
    activeSlot_ = chooseSlot(data, offsets);
    if (activeSlot_ < 0) {
        activeSlot_ = 0;
        return LoadResult::UnknownFormat;
    }
    std::memcpy(sectorOffset_, offsets[activeSlot_], sizeof sectorOffset_);
    data_ = std::move(data);
    game_ = gameFromSmall(data_.data() + sectorOffset_[0]);
    // Japanese games only use 5 name characters and leave the last bytes zero.
    japanese_ = readU16(data_.data() + sectorOffset_[0] + 6) == 0;
    if (!checksumsValid()) {
        game_ = Game::Unknown;
        return LoadResult::BadChecksum;
    }
    return LoadResult::Ok;
}

size_t Gen3Save::fileOffset(Area area, size_t offset) const {
    return sectorOffset_[area + offset / kSectorData] + offset % kSectorData;
}

void Gen3Save::readArea(Area area, size_t offset, uint8_t* out, size_t length) const {
    for (size_t i = 0; i < length; ++i) out[i] = data_[fileOffset(area, offset + i)];
}

void Gen3Save::writeArea(Area area, size_t offset, const uint8_t* in, size_t length) {
    for (size_t i = 0; i < length; ++i) data_[fileOffset(area, offset + i)] = in[i];
}

std::string Gen3Save::trainerName() const {
    uint8_t name[8];
    readArea(kSmall, 0, name, sizeof name);
    return text::decodeGen3(name, japanese_ ? 5 : 7, japanese_ ? 1 : 2);
}

uint16_t Gen3Save::trainerId() const {
    uint8_t id[2];
    readArea(kSmall, 0x0A, id, 2);
    return readU16(id);
}

uint16_t Gen3Save::secretId() const {
    uint8_t id[2];
    readArea(kSmall, 0x0C, id, 2);
    return readU16(id);
}

uint8_t Gen3Save::trainerGender() const {
    uint8_t gender;
    readArea(kSmall, 0x08, &gender, 1);
    return gender & 1;
}

uint8_t Gen3Save::mostCommonOrigin(uint8_t first, uint8_t second, uint8_t fallback) const {
    int count[2] = {0, 0};
    uint8_t buffer[pokemon::pk3::kPartySize];
    auto tally = [&](const uint8_t* d) {
        pokemon::Pokemon p = pokemon::pk3::toPokemon(d, pokemon::pk3::kStoredSize);
        if (p.trainer.tid != trainerId() || p.trainer.sid != secretId()) return;
        if (p.originGame == first) ++count[0];
        if (p.originGame == second) ++count[1];
    };
    for (int i = 0; i < partyCount(); ++i) {
        if (readPartySlot(i, buffer) == SlotState::Valid) tally(buffer);
    }
    for (int box = 0; box < kBoxCount; ++box) {
        for (int slot = 0; slot < kSlotsPerBox; ++slot) {
            if (readBoxSlot(box, slot, buffer) == SlotState::Valid) tally(buffer);
        }
    }
    if (count[0] == 0 && count[1] == 0) return fallback;
    return count[1] > count[0] ? second : first;
}

uint8_t Gen3Save::language() const {
    // The save does not store it: use the trainer's own Pokémon.
    uint8_t buffer[pokemon::pk3::kPartySize];
    for (int i = 0; i < partyCount(); ++i) {
        if (readPartySlot(i, buffer) != SlotState::Valid) continue;
        pokemon::Pokemon p = pokemon::pk3::toPokemon(buffer, pokemon::pk3::kStoredSize);
        if (p.trainer.tid == trainerId() && p.language != 0 && !p.isEgg) return p.language;
    }
    return japanese_ ? 1 : 2;
}

uint8_t Gen3Save::gameVersion() const {
    switch (game_) {
        case Game::Emerald:
            return 3;
        case Game::RubySapphire:
            return mostCommonOrigin(2, 1, 2);  // Ruby / Sapphire
        case Game::FireRedLeafGreen:
            return mostCommonOrigin(4, 5, 4);  // FireRed / LeafGreen
        default:
            return 0;
    }
}

uint32_t Gen3Save::securityKey() const {
    size_t offset;
    switch (game_) {
        case Game::Emerald: offset = kSmallKeyEmerald; break;
        case Game::FireRedLeafGreen: offset = kSmallKeyFrlg; break;
        default: return 0;
    }
    uint8_t key[4];
    readArea(kSmall, offset, key, 4);
    return readU32(key);
}

size_t Gen3Save::moneyOffset() const {
    return game_ == Game::FireRedLeafGreen ? 0x290 : 0x490;
}

size_t Gen3Save::bagOffset() const {
    return game_ == Game::FireRedLeafGreen ? 0x298 : 0x498;
}

bool Gen3Save::badgeFlag(int badge) const {
    EventFlags flags = eventFlags(game_);
    int flag = flags.badgeFirst + badge;
    uint8_t byte;
    readArea(kLarge, flags.base + flag / 8, &byte, 1);
    return (byte >> (flag % 8)) & 1;
}

void Gen3Save::setBadgeFlag(int badge, bool value) {
    EventFlags flags = eventFlags(game_);
    int flag = flags.badgeFirst + badge;
    uint8_t byte;
    readArea(kLarge, flags.base + flag / 8, &byte, 1);
    byte = static_cast<uint8_t>(value ? byte | (1 << (flag % 8)) : byte & ~(1 << (flag % 8)));
    writeArea(kLarge, flags.base + flag / 8, &byte, 1);
}

TrainerData Gen3Save::trainerData() const {
    TrainerData t;
    t.name = trainerName();
    t.gender = trainerGender();
    t.tid = trainerId();
    t.sid = secretId();
    uint8_t buffer[4];
    readArea(kLarge, moneyOffset(), buffer, 4);
    t.money = readU32(buffer) ^ securityKey();
    readArea(kSmall, kSmallHours, buffer, 4);
    t.hours = readU16(buffer);
    t.minutes = buffer[2];
    t.seconds = buffer[3];
    for (int i = 0; i < 8; ++i) {
        if (badgeFlag(i)) t.badges |= static_cast<uint16_t>(1u << i);
    }
    return t;
}

bool Gen3Save::setTrainerData(const TrainerData& t, std::string* error) {
    if (!validateTrainer(t, error)) return false;
    const uint8_t language = japanese_ ? 1 : 2;
    const size_t nameChars = japanese_ ? 5 : 8;  // Japanese saves only own 5 slots
    uint8_t name[8];
    std::memset(name, text::kGen3Terminator, sizeof name);
    text::encodeGen3(t.name, name, nameChars, trainerNameMaxLength(), language);
    if (text::decodeGen3(name, trainerNameMaxLength(), language) != t.name) {
        if (error) *error = "Name has characters the game cannot show";
        return false;
    }
    writeArea(kSmall, 0, name, nameChars);
    uint8_t gender = t.gender;
    writeArea(kSmall, kSmallGender, &gender, 1);
    uint8_t ids[4];
    util::writeU16(ids, t.tid);
    util::writeU16(ids + 2, t.sid);
    writeArea(kSmall, kSmallTid, ids, 4);
    uint8_t clock[4];
    util::writeU16(clock, t.hours);
    clock[2] = t.minutes;
    clock[3] = t.seconds;
    writeArea(kSmall, kSmallHours, clock, 4);  // the frame counter after them is kept
    uint8_t money[4];
    util::writeU32(money, t.money ^ securityKey());
    writeArea(kLarge, moneyOffset(), money, 4);
    for (int i = 0; i < 8; ++i) setBadgeFlag(i, (t.badges >> i) & 1);
    return true;
}

data::BagLayout Gen3Save::bagLayout() const {
    switch (game_) {
        case Game::Emerald: return data::BagLayout::Emerald;
        case Game::FireRedLeafGreen: return data::BagLayout::FireRedLeafGreen;
        default: return data::BagLayout::RubySapphire;
    }
}

void Gen3Save::readBag(size_t offset, uint8_t* out, size_t length) const {
    readArea(kLarge, bagOffset() + offset, out, length);
}

void Gen3Save::writeBag(size_t offset, const uint8_t* in, size_t length) {
    writeArea(kLarge, bagOffset() + offset, in, length);
}

uint16_t Gen3Save::bagCountKey() const {
    return static_cast<uint16_t>(securityKey());
}

size_t Gen3Save::partyOffset() const {
    return game_ == Game::FireRedLeafGreen ? 0x34 : 0x234;
}

int Gen3Save::partyCount() const {
    uint8_t count;
    readArea(kLarge, partyOffset(), &count, 1);
    return count > kPartyMax ? kPartyMax : count;
}

void Gen3Save::setPartyCount(int count) {
    uint8_t value = static_cast<uint8_t>(count);
    writeArea(kLarge, partyOffset(), &value, 1);
}

SlotState Gen3Save::decodeSlot(uint8_t* d, size_t size) const {
    if (pokemon::pk3::isEmptySlot(d)) {
        std::memset(d, 0, size);
        return SlotState::Empty;
    }
    pokemon::pk3::decrypt(d);
    return pokemon::pk3::calcChecksum(d) == pokemon::pk3::storedChecksum(d) ? SlotState::Valid : SlotState::BadChecksum;
}

SlotState Gen3Save::readPartySlot(int slot, uint8_t* decrypted) const {
    if (slot < 0 || slot >= partyCount()) return SlotState::Empty;
    readArea(kLarge, partyOffset() + 4 + slot * pokemon::pk3::kPartySize, decrypted, pokemon::pk3::kPartySize);
    return decodeSlot(decrypted, pokemon::pk3::kPartySize);
}

bool Gen3Save::writePartySlot(int slot, const uint8_t* decrypted) {
    if (slot < 0 || slot >= kPartyMax) return false;
    uint8_t buffer[pokemon::pk3::kPartySize];
    std::memcpy(buffer, decrypted, sizeof buffer);
    pokemon::pk3::encrypt(buffer);
    writeArea(kLarge, partyOffset() + 4 + slot * pokemon::pk3::kPartySize, buffer, sizeof buffer);
    return true;
}

void Gen3Save::clearPartySlot(int slot) {
    uint8_t zero[pokemon::pk3::kPartySize] = {};
    writeArea(kLarge, partyOffset() + 4 + slot * pokemon::pk3::kPartySize, zero, sizeof zero);
}

std::string Gen3Save::boxName(int box) const {
    if (box < 0 || box >= kBoxCount) return "";
    uint8_t name[kBoxNameChars];
    readArea(kStorage, kBoxNamesOffset + box * kBoxNameChars, name, sizeof name);
    return text::decodeGen3(name, kBoxNameChars - 1, japanese_ ? 1 : 2);
}

bool Gen3Save::setBoxName(int box, const std::string& name) {
    if (box < 0 || box >= kBoxCount) return false;
    uint8_t buffer[kBoxNameChars];
    std::memset(buffer, text::kGen3Terminator, sizeof buffer);
    text::encodeGen3(name, buffer, kBoxNameChars, kBoxNameMaxLength, japanese_ ? 1 : 2);
    writeArea(kStorage, kBoxNamesOffset + box * kBoxNameChars, buffer, sizeof buffer);
    return true;
}

SlotState Gen3Save::readBoxSlot(int box, int slot, uint8_t* decrypted) const {
    if (box < 0 || box >= kBoxCount || slot < 0 || slot >= kSlotsPerBox) return SlotState::Empty;
    readArea(kStorage, 4 + (box * kSlotsPerBox + slot) * pokemon::pk3::kStoredSize, decrypted, pokemon::pk3::kStoredSize);
    return decodeSlot(decrypted, pokemon::pk3::kStoredSize);
}

bool Gen3Save::writeBoxSlot(int box, int slot, const uint8_t* decrypted) {
    if (box < 0 || box >= kBoxCount || slot < 0 || slot >= kSlotsPerBox) return false;
    uint8_t buffer[pokemon::pk3::kStoredSize];
    std::memcpy(buffer, decrypted, sizeof buffer);
    pokemon::pk3::encrypt(buffer);
    writeArea(kStorage, 4 + (box * kSlotsPerBox + slot) * pokemon::pk3::kStoredSize, buffer, sizeof buffer);
    return true;
}

bool Gen3Save::clearBoxSlot(int box, int slot) {
    if (box < 0 || box >= kBoxCount || slot < 0 || slot >= kSlotsPerBox) return false;
    uint8_t zero[pokemon::pk3::kStoredSize] = {};
    writeArea(kStorage, 4 + (box * kSlotsPerBox + slot) * pokemon::pk3::kStoredSize, zero, sizeof zero);
    return true;
}

void Gen3Save::refreshChecksums() {
    for (size_t ofs : sectorOffset_) util::writeU16(data_.data() + ofs + kFooterChecksum, sectorChecksum(data_.data() + ofs));
}

bool Gen3Save::checksumsValid() const {
    for (size_t ofs : sectorOffset_) {
        if (readU16(data_.data() + ofs + kFooterChecksum) != sectorChecksum(data_.data() + ofs)) return false;
    }
    return true;
}

}  // namespace pktool::saves
