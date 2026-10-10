#include "core/saves/gen2_save.h"

#include <cstring>

#include "core/pokemon/pk2.h"
#include "core/saves/game.h"
#include "core/text/gen12_text.h"

namespace pktool::saves {

namespace {

constexpr size_t kTrainer = 0x2009;  // start of the checksummed block, trainer ID
constexpr size_t kNoOffset = static_cast<size_t>(-1);

// Offsets from PKHeX's SAV2Offsets.
struct Variant {
    bool crystal, japanese, korean;
    size_t timePlayed, palette, money, johtoBadges, boxIndex, boxNames, party, currentBox, gender;
    size_t accumulatedEnd, checksum, checksum2;
    int boxCount, boxCapacity, splitAt;
    size_t stringBytes, boxNameBytes;
};

// Detection order of PKHeX's SaveUtil: international Gold/Silver, international
// Crystal, Japanese Gold/Silver, Japanese Crystal, Korean Gold/Silver.
constexpr Variant kVariants[] = {
    {false, false, false, 0x2053, 0x206B, 0x23DB, 0x23E4, 0x2724, 0x2727, 0x288A, 0x2D6C, kNoOffset, 0x2D68, 0x2D69, 0x7E6D, 14, 20, 7, 11, 9},
    {true, false, false, 0x2052, 0x206A, 0x23DC, 0x23E5, 0x2700, 0x2703, 0x2865, 0x2D10, 0x3E3D, 0x2B82, 0x2D0D, 0x1F0D, 14, 20, 7, 11, 9},
    {false, true, false, 0x2034, 0x204C, 0x23BC, 0x23C5, 0x2705, 0x2708, 0x283E, 0x2D10, kNoOffset, 0x2C8B, 0x2D0D, 0x7F0D, 9, 30, 6, 6, 9},
    {true, true, false, 0x2034, 0x204C, 0x23BE, 0x23C7, 0x26E2, 0x26E5, 0x281A, 0x2D10, 0x8000, 0x2AE2, 0x2D0D, 0x7F0D, 9, 30, 6, 6, 9},
    {false, false, true, 0x204D, 0x2065, 0x23D3, 0x23DC, 0x26FC, 0x26FF, 0x28CC, 0x2DAE, kNoOffset, 0x2DAA, 0x2DAB, 0x7E6B, 14, 20, 7, 11, 17},
};
constexpr int kVariantCount = sizeof(kVariants) / sizeof(kVariants[0]);

bool validList(const std::vector<uint8_t>& d, size_t offset, int maxCount) {
    if (offset >= d.size()) return false;
    const uint8_t count = d[offset];
    return count <= maxCount && offset + 1 + count < d.size() && d[offset + 1 + count] == 0xFF;
}

bool variantFits(const std::vector<uint8_t>& d, const Variant& v) {
    if (v.japanese && d.size() < Gen2Save::kJapaneseFileSize) return false;
    if (v.gender != kNoOffset && v.gender >= d.size()) return false;
    return validList(d, v.party, v.boxCapacity) && validList(d, v.currentBox, v.boxCapacity);
}

uint16_t sumOf(const std::vector<uint8_t>& d, const Variant& v) {
    uint16_t sum = 0;
    for (size_t i = kTrainer; i <= v.accumulatedEnd; ++i) sum = static_cast<uint16_t>(sum + d[i]);
    return sum;
}

uint16_t readLe16(const std::vector<uint8_t>& d, size_t offset) {
    return static_cast<uint16_t>(d[offset] | (d[offset + 1] << 8));
}

void writeLe16(std::vector<uint8_t>& d, size_t offset, uint16_t value) {
    d[offset] = static_cast<uint8_t>(value);
    d[offset + 1] = static_cast<uint8_t>(value >> 8);
}

int findVariant(const std::vector<uint8_t>& d, bool requireChecksum) {
    for (int i = 0; i < kVariantCount; ++i) {
        if (!variantFits(d, kVariants[i])) continue;
        if (requireChecksum && readLe16(d, kVariants[i].checksum) != sumOf(d, kVariants[i])) continue;
        return i;
    }
    return -1;
}

void copyRange(std::vector<uint8_t>& d, size_t from, size_t length, size_t to) {
    std::memmove(d.data() + to, d.data() + from, length);
}

}  // namespace

Game Gen2Save::detect(const std::vector<uint8_t>& d) {
    if (!sizeOk(d.size())) return Game::Unknown;
    int variant = findVariant(d, true);
    if (variant < 0) variant = findVariant(d, false);
    if (variant < 0) return Game::Unknown;
    return kVariants[variant].crystal ? Game::Crystal : Game::GoldSilver;
}

LoadResult Gen2Save::load(std::vector<uint8_t> d) {
    if (!sizeOk(d.size())) return LoadResult::WrongSize;
    int variant = findVariant(d, true);
    const bool checksumOk = variant >= 0;
    if (variant < 0) variant = findVariant(d, false);
    if (variant < 0) return LoadResult::UnknownFormat;
    const Variant& v = kVariants[variant];
    variant_ = variant;
    crystal_ = v.crystal;
    japanese_ = v.japanese;
    korean_ = v.korean;
    data_ = std::move(d);
    geometry_.format = 2;
    geometry_.boxCount = v.boxCount;
    geometry_.boxCapacity = v.boxCapacity;
    geometry_.bodyStored = pokemon::pk2::kStoredBody;
    geometry_.bodyParty = pokemon::pk2::kPartyBody;
    geometry_.stringBytes = v.stringBytes;
    geometry_.blobSize = pokemon::pk2::kEntitySize;
    unpackAll();
    return checksumOk ? LoadResult::Ok : LoadResult::BadChecksum;
}

size_t Gen2Save::boxListOffset(int box) const {
    const Variant& v = kVariants[variant_];
    const size_t stride = listLength(false) + 2;  // every box list is followed by two bytes
    if (box < v.splitAt) return 0x4000 + static_cast<size_t>(box) * stride;
    return 0x6000 + static_cast<size_t>(box - v.splitAt) * stride;
}

size_t Gen2Save::currentBoxOffset() const {
    return kVariants[variant_].currentBox;
}

size_t Gen2Save::partyListOffset() const {
    return kVariants[variant_].party;
}

int Gen2Save::currentBox() const {
    const int index = data_[kVariants[variant_].boxIndex];
    return index < boxCount_ ? index : -1;
}

void Gen2Save::fillPartyArea(uint8_t* blob) const {
    pokemon::pk2::fillPartyArea(blob);
}

void Gen2Save::setMarker(Slot& slot) const {
    const uint8_t species = slot.blob[pokemon::pk2::offset::kSpecies];
    slot.marker = species == 0 ? 0xFF : slot.blob[pokemon::pk2::kEggOffset] ? 0xFD : species;
}

uint16_t Gen2Save::sum() const {
    return sumOf(data_, kVariants[variant_]);
}

// Korean saves keep a second checksum over five byte ranges.
uint16_t Gen2Save::koreanSum() const {
    static const size_t kRanges[5][2] = {{0x106B, 0x1533}, {0x1534, 0x1A12}, {0x1A13, 0x1C38}, {0x3DD8, 0x3F79}, {0x7E39, 0x7E6A}};
    uint16_t total = 0;
    for (const auto& range : kRanges) {
        for (size_t i = range[0]; i < range[1]; ++i) total = static_cast<uint16_t>(total + data_[i]);
    }
    return total;
}

void Gen2Save::writeChecksums() {
    const Variant& v = kVariants[variant_];
    const uint16_t checksum = sum();
    writeLe16(data_, v.checksum, checksum);
    writeLe16(data_, v.checksum2, checksum);
    if (v.japanese) {
        copyRange(data_, kTrainer, v.crystal ? 0xADA : 0xC83, 0x7209);
    } else if (v.korean) {
        writeLe16(data_, v.checksum2, koreanSum());
    } else if (v.crystal) {
        copyRange(data_, 0x2009, 0xB7A, 0x1209);
    } else {
        copyRange(data_, 0x2009, 0x222F - 0x2009, 0x15C7);
        copyRange(data_, 0x222F, 0x23D9 - 0x222F, 0x3D69);
        copyRange(data_, 0x23D9, 0x2856 - 0x23D9, 0x0C6B);
        copyRange(data_, 0x2856, 0x288A - 0x2856, 0x7E39);
        copyRange(data_, 0x288A, 0x2D69 - 0x288A, 0x10E8);
    }
}

bool Gen2Save::checksumsValid() const {
    const Variant& v = kVariants[variant_];
    return readLe16(data_, v.checksum) == sum();
}

std::string Gen2Save::trainerName() const {
    const Variant& v = kVariants[variant_];
    return decodeName(data_.data() + kTrainer + 2, v.korean ? 10 : v.stringBytes);
}

uint16_t Gen2Save::trainerId() const {
    return static_cast<uint16_t>((data_[kTrainer] << 8) | data_[kTrainer + 1]);
}

uint8_t Gen2Save::trainerGender() const {
    const Variant& v = kVariants[variant_];
    return v.gender == kNoOffset ? 0 : (data_[v.gender] & 1);
}

uint8_t Gen2Save::gameVersion() const {
    return crystal_ ? 41 : 39;  // Crystal, Gold (PKHeX GameVersion numbering)
}

TrainerData Gen2Save::trainerData() const {
    const Variant& v = kVariants[variant_];
    TrainerData t;
    t.name = trainerName();
    t.gender = trainerGender();
    t.tid = trainerId();
    t.money = (static_cast<uint32_t>(data_[v.money]) << 16) | (static_cast<uint32_t>(data_[v.money + 1]) << 8) | data_[v.money + 2];
    t.hours = static_cast<uint16_t>((data_[v.timePlayed] << 8) | data_[v.timePlayed + 1]);
    t.minutes = data_[v.timePlayed + 2];
    t.seconds = data_[v.timePlayed + 3];
    t.badges = readLe16(data_, v.johtoBadges);
    return t;
}

bool Gen2Save::setTrainerData(const TrainerData& t, std::string* error) {
    if (!validateTrainer(t, error)) return false;
    const Variant& v = kVariants[variant_];
    const size_t fieldBytes = v.korean ? 10 : v.stringBytes;
    uint8_t name[11];
    if (!encodeName(t.name, name, fieldBytes, trainerNameMaxLength()) || decodeName(name, fieldBytes) != t.name) {
        if (error) *error = "Name has characters the game cannot show";
        return false;
    }
    std::memcpy(data_.data() + kTrainer + 2, name, fieldBytes);
    data_[kTrainer] = static_cast<uint8_t>(t.tid >> 8);
    data_[kTrainer + 1] = static_cast<uint8_t>(t.tid);
    data_[v.money] = static_cast<uint8_t>(t.money >> 16);
    data_[v.money + 1] = static_cast<uint8_t>(t.money >> 8);
    data_[v.money + 2] = static_cast<uint8_t>(t.money);
    data_[v.timePlayed] = static_cast<uint8_t>(t.hours >> 8);
    data_[v.timePlayed + 1] = static_cast<uint8_t>(t.hours);
    data_[v.timePlayed + 2] = t.minutes;
    data_[v.timePlayed + 3] = t.seconds;
    writeLe16(data_, v.johtoBadges, t.badges);
    if (v.gender != kNoOffset) {
        data_[v.gender] = t.gender;
        data_[v.palette] = t.gender;
    }
    return true;
}

std::string Gen2Save::boxName(int box) const {
    if (box < 0 || box >= boxCount_) return std::string();
    const Variant& v = kVariants[variant_];
    return decodeName(data_.data() + v.boxNames + static_cast<size_t>(box) * v.boxNameBytes, v.korean ? 16 : 8);
}

bool Gen2Save::setBoxName(int box, const std::string& name) {
    if (box < 0 || box >= boxCount_) return false;
    const Variant& v = kVariants[variant_];
    uint8_t buffer[17];
    if (!encodeName(name, buffer, v.boxNameBytes, kBoxNameMaxLength)) return false;
    std::memcpy(data_.data() + v.boxNames + static_cast<size_t>(box) * v.boxNameBytes, buffer, v.boxNameBytes);
    return true;
}

// Bag: PKHeX SAV2Offsets Pouch* per variant. Pocket order: Items, Key Items, Balls, TMs & HMs, PC.
size_t Gen2Save::gbPocketOffset(int pocket) const {
    static const size_t kPouches[kVariantCount][5] = {
        {0x241F, 0x2449, 0x2464, 0x23E6, 0x247E},  // international Gold/Silver
        {0x2420, 0x244A, 0x2465, 0x23E7, 0x247F},  // international Crystal
        {0x2400, 0x242A, 0x2445, 0x23C7, 0x245F},  // Japanese Gold/Silver
        {0x2402, 0x242C, 0x2447, 0x23C9, 0x2461},  // Japanese Crystal
        {0x2417, 0x2441, 0x245C, 0x23DE, 0x2476},  // Korean Gold/Silver
    };
    return pocket >= 0 && pocket < 5 ? kPouches[variant_][pocket] : 0;
}

void Gen2Save::readBag(size_t offset, uint8_t* out, size_t length) const {
    if (offset + length <= data_.size()) std::memcpy(out, data_.data() + offset, length);
}

void Gen2Save::writeBag(size_t offset, const uint8_t* in, size_t length) {
    if (offset + length <= data_.size()) std::memcpy(data_.data() + offset, in, length);
}

}  // namespace pktool::saves
