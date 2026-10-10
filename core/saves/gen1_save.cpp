#include "core/saves/gen1_save.h"

#include <cstring>

#include "core/pokemon/pk1.h"
#include "core/saves/game.h"
#include "core/text/gen12_text.h"

namespace pktool::saves {
namespace {

// Offsets from PKHeX's SAV1Offsets.
struct Layout {
    size_t ot, money, badges, tid, pikaFriendship, boxIndex, playTime, party, currentBox, checksum, starter;
    int boxCount, boxCapacity;
};
constexpr Layout kInternational = {0x2598, 0x25F3, 0x2602, 0x2605, 0x271C, 0x284C, 0x2CED, 0x2F2C, 0x30C0, 0x3523, 0x29C3, 12, 20};
constexpr Layout kJapanese = {0x2598, 0x25EE, 0x25F8, 0x25FB, 0x2712, 0x2842, 0x2CA0, 0x2ED5, 0x302D, 0x3594, 0x29B9, 8, 30};

constexpr size_t kStringBytesInt = 11;
constexpr size_t kStringBytesJpn = 6;
constexpr uint8_t kInitialisedFlag = 0x80;

bool validList(const std::vector<uint8_t>& d, size_t offset, int maxCount) {
    const uint8_t count = d[offset];
    return count <= maxCount && d[offset + 1 + count] == 0xFF;
}

bool listsMatch(const std::vector<uint8_t>& d, const Layout& l) {
    return validList(d, l.party, l.boxCapacity) && validList(d, l.currentBox, l.boxCapacity);
}

uint8_t checksumOf(const std::vector<uint8_t>& d, const Layout& l) {
    uint8_t sum = 0;
    for (size_t i = l.ot; i < l.checksum; ++i) sum = static_cast<uint8_t>(sum + d[i]);
    return static_cast<uint8_t>(~sum);
}

bool isYellow(const std::vector<uint8_t>& d, const Layout& l) {
    if (d[l.starter] != 0) return d[l.starter] == 0x54;  // Pikachu
    return d[l.pikaFriendship] != 0;
}

// BCD of `digits` bytes, big endian.
uint32_t readBcd(const uint8_t* p, int bytes) {
    uint32_t value = 0;
    for (int i = 0; i < bytes; ++i) value = value * 100 + (p[i] >> 4) * 10 + (p[i] & 0xF);
    return value;
}

void writeBcd(uint8_t* p, int bytes, uint32_t value) {
    for (int i = bytes - 1; i >= 0; --i) {
        p[i] = static_cast<uint8_t>(((value / 10 % 10) << 4) | (value % 10));
        value /= 100;
    }
}

const Layout& layoutOf(bool japanese) {
    return japanese ? kJapanese : kInternational;
}

}  // namespace

Game Gen1Save::detect(const std::vector<uint8_t>& d) {
    if (!sizeOk(d.size()) || d.size() >= 0x10000) return Game::Unknown;
    const Layout* layouts[] = {&kJapanese, &kInternational};
    for (const Layout* l : layouts) {  // a matching checksum wins over bare list shapes
        if (listsMatch(d, *l) && d[l->checksum] == checksumOf(d, *l)) return isYellow(d, *l) ? Game::Yellow : Game::RedBlue;
    }
    for (const Layout* l : layouts) {
        if (listsMatch(d, *l)) return isYellow(d, *l) ? Game::Yellow : Game::RedBlue;
    }
    return Game::Unknown;
}

LoadResult Gen1Save::load(std::vector<uint8_t> d) {
    if (!sizeOk(d.size()) || d.size() >= 0x10000) return LoadResult::WrongSize;
    if (detect(d) == Game::Unknown) return LoadResult::UnknownFormat;
    const Layout* layout = nullptr;
    for (const Layout* l : {&kJapanese, &kInternational}) {
        if (listsMatch(d, *l) && d[l->checksum] == checksumOf(d, *l)) {
            layout = l;
            break;
        }
    }
    const bool checksumOk = layout != nullptr;
    if (layout == nullptr) layout = listsMatch(d, kJapanese) ? &kJapanese : &kInternational;
    japanese_ = layout == &kJapanese;
    yellow_ = isYellow(d, *layout);
    data_ = std::move(d);
    geometry_.format = 1;
    geometry_.boxCount = layout->boxCount;
    geometry_.boxCapacity = layout->boxCapacity;
    geometry_.bodyStored = pokemon::pk1::kStoredBody;
    geometry_.bodyParty = pokemon::pk1::kPartyBody;
    geometry_.stringBytes = japanese_ ? kStringBytesJpn : kStringBytesInt;
    geometry_.blobSize = pokemon::pk1::kEntitySize;
    unpackAll();
    return checksumOk ? LoadResult::Ok : LoadResult::BadChecksum;
}

size_t Gen1Save::boxListOffset(int box) const {
    const size_t length = listLength(false);
    const int half = boxCount_ / 2;
    if (box < half) return 0x4000 + static_cast<size_t>(box) * length;
    return 0x6000 + static_cast<size_t>(box - half) * length;
}

size_t Gen1Save::currentBoxOffset() const {
    return layoutOf(japanese_).currentBox;
}

size_t Gen1Save::partyListOffset() const {
    return layoutOf(japanese_).party;
}

int Gen1Save::currentBox() const {
    const int index = data_[layoutOf(japanese_).boxIndex] & 0x7F;
    return index < boxCount_ ? index : -1;
}

bool Gen1Save::boxStorageValid(int box) const {
    if (box == currentBox()) return true;  // the mirror and its own bank slot are kept in step
    return (data_[layoutOf(japanese_).boxIndex] & kInitialisedFlag) != 0;
}

bool Gen1Save::initialiseBoxStorage() {
    data_[layoutOf(japanese_).boxIndex] |= kInitialisedFlag;
    return true;
}

void Gen1Save::fillPartyArea(uint8_t* blob) const {
    pokemon::pk1::fillPartyArea(blob);
}

void Gen1Save::setMarker(Slot& slot) const {
    slot.marker = slot.blob[pokemon::pk1::offset::kSpecies] == 0 ? 0xFF : slot.blob[pokemon::pk1::offset::kSpecies];
}

uint8_t Gen1Save::computeChecksum() const {
    return checksumOf(data_, layoutOf(japanese_));
}

void Gen1Save::writeChecksums() {
    data_[layoutOf(japanese_).checksum] = computeChecksum();
}

bool Gen1Save::checksumsValid() const {
    return data_[layoutOf(japanese_).checksum] == computeChecksum();
}

std::string Gen1Save::trainerName() const {
    return decodeName(data_.data() + layoutOf(japanese_).ot, japanese_ ? kStringBytesJpn : kStringBytesInt);
}

uint16_t Gen1Save::trainerId() const {
    const uint8_t* p = data_.data() + layoutOf(japanese_).tid;
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

uint8_t Gen1Save::gameVersion() const {
    return yellow_ ? 38 : 35;  // Yellow, Red (PKHeX GameVersion numbering)
}

std::string Gen1Save::boxName(int) const {
    return std::string();
}

TrainerData Gen1Save::trainerData() const {
    const Layout& l = layoutOf(japanese_);
    TrainerData t;
    t.name = trainerName();
    t.tid = trainerId();
    t.money = readBcd(data_.data() + l.money, 3);
    const uint8_t* time = data_.data() + l.playTime;
    t.hours = time[1] != 0 ? 255 : time[0];
    t.minutes = time[2];
    t.seconds = time[3];
    t.badges = data_[l.badges];
    return t;
}

bool Gen1Save::setTrainerData(const TrainerData& t, std::string* error) {
    if (!validateTrainer(t, error)) return false;
    if (t.hours > 255) {
        if (error) *error = "Play time is limited to 255 hours";
        return false;
    }
    if (t.badges > 0xFF) {
        if (error) *error = "Badge bits are out of range";
        return false;
    }
    const Layout& l = layoutOf(japanese_);
    const size_t fieldBytes = japanese_ ? kStringBytesJpn : kStringBytesInt;
    uint8_t name[kStringBytesInt];
    if (!text::encodeGb(1, t.name, name, fieldBytes, trainerNameMaxLength(), language()) ||
        decodeName(name, fieldBytes) != t.name) {
        if (error) *error = "Name has characters the game cannot show";
        return false;
    }
    std::memcpy(data_.data() + l.ot, name, fieldBytes);
    uint8_t* id = data_.data() + l.tid;
    id[0] = static_cast<uint8_t>(t.tid >> 8);
    id[1] = static_cast<uint8_t>(t.tid);
    writeBcd(data_.data() + l.money, 3, t.money);
    uint8_t* time = data_.data() + l.playTime;
    time[0] = static_cast<uint8_t>(t.hours);
    time[1] = t.hours == 255 ? 1 : 0;
    time[2] = t.minutes;
    time[3] = t.seconds;
    if (t.hours == 255) time[4] = 0;
    data_[l.badges] = static_cast<uint8_t>(t.badges);
    return true;
}

// Bag: PKHeX SAV1Offsets Items / PCItems (count byte of each list).
size_t Gen1Save::gbPocketOffset(int pocket) const {
    if (japanese_) return pocket == 0 ? 0x25C4 : 0x27DC;
    return pocket == 0 ? 0x25C9 : 0x27E6;
}

void Gen1Save::readBag(size_t offset, uint8_t* out, size_t length) const {
    if (offset + length <= data_.size()) std::memcpy(out, data_.data() + offset, length);
}

void Gen1Save::writeBag(size_t offset, const uint8_t* in, size_t length) {
    if (offset + length <= data_.size()) std::memcpy(data_.data() + offset, in, length);
}

}  // namespace pktool::saves
