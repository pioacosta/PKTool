#include "core/saves/save_file.h"

#include <cstring>

#include "core/data/gen12.h"
#include "core/data/names.h"
#include "core/util/bytes.h"
#include "core/saves/gen1_save.h"
#include "core/saves/gen2_save.h"
#include "core/saves/gen3_save.h"
#include "core/saves/gen4_save.h"
#include "core/saves/gen5_save.h"

namespace pktool::saves {
namespace {

bool fail(std::string* error, const char* message) {
    if (error) *error = message;
    return false;
}

size_t utf8Length(const std::string& text) {
    size_t count = 0;
    for (unsigned char c : text) {
        if ((c & 0xC0) != 0x80) ++count;
    }
    return count;
}

}  // namespace

bool SaveFile::validateTrainer(const TrainerData& t, std::string* error) const {
    size_t length = utf8Length(t.name);
    if (length == 0) return fail(error, "Name cannot be empty");
    if (length > trainerNameMaxLength()) return fail(error, "Name is too long");
    if (t.gender > 1) return fail(error, "Gender must be 0 or 1");
    if (t.money > maxMoney()) return fail(error, format() >= 5 ? "Money is above 9999999" : "Money is above 999999");
    if (t.hours > kMaxPlayHours) return fail(error, "Play time hours are above 999");
    if (t.minutes > 59) return fail(error, "Play time minutes are above 59");
    if (t.seconds > 59) return fail(error, "Play time seconds are above 59");
    if (t.badges >> badgeCount()) return fail(error, "Badge bits are out of range");
    return true;
}

int SaveFile::pocketCount() const {
    return data::bagSpec(bagLayout()).pocketCount;
}

namespace {
bool isGbBag(data::BagLayout layout) {
    return layout == data::BagLayout::Gen1 || layout == data::BagLayout::Gen2GoldSilver ||
           layout == data::BagLayout::Gen2Crystal;
}

const data::PocketSpec* findPocket(data::BagLayout layout, int pocket) {
    data::BagSpec spec = data::bagSpec(layout);
    if (pocket < 0 || pocket >= spec.pocketCount) return nullptr;
    return &spec.pockets[pocket];
}
}  // namespace

BagPocket SaveFile::pocketInfo(int pocket) const {
    const data::PocketSpec* p = findPocket(bagLayout(), pocket);
    if (!p) return {"", data::PocketKind::Items, 0};
    return {p->name, p->kind, p->capacity};
}

std::vector<uint16_t> SaveFile::pocketItems(int pocket) const {
    const data::PocketSpec* p = findPocket(bagLayout(), pocket);
    if (!p) return {};
    return std::vector<uint16_t>(p->items, p->items + p->itemCount);
}

bool SaveFile::pocketAllows(int pocket, uint16_t item) const {
    const data::PocketSpec* p = findPocket(bagLayout(), pocket);
    return p && item != 0 && data::pocketAllows(*p, item);
}

bool SaveFile::itemExists(uint16_t item) const {
    if (item == 0) return false;
    // An item some pocket of this game lists exists, whatever its name table entry says.
    if (isGbBag(bagLayout())) {
        // Game Boy bags hold single-byte IDs; the pocket tables list every valid one.
        const data::BagSpec gb = data::bagSpec(bagLayout());
        for (int p = 0; p < gb.pocketCount; ++p) {
            if (data::pocketAllows(gb.pockets[p], item)) return true;
        }
        return false;
    }
    // Otherwise it must have a name and not be newer than the game (e.g. a Gen V item in a
    // Gen IV save): the highest item any pocket lists is the game's last item ID.
    const data::BagSpec spec = data::bagSpec(bagLayout());
    uint16_t last = 0;
    for (int p = 0; p < spec.pocketCount; ++p) {
        for (int i = 0; i < spec.pockets[p].itemCount; ++i) {
            if (spec.pockets[p].items[i] == item) return true;
            if (spec.pockets[p].items[i] > last) last = spec.pockets[p].items[i];
        }
    }
    const int count = format() == 3 ? data::generated::kItemNames3Count : data::generated::kItemNamesCount;
    return item <= last && item < count && std::strcmp(itemName(item), "???") != 0;
}

int SaveFile::itemMaxCount(int pocket, uint16_t item) const {
    const data::PocketSpec* p = findPocket(bagLayout(), pocket);
    return p ? data::pocketMaxCount(*p, item) : 0;
}

std::vector<BagSlot> SaveFile::readPocket(int pocket) const {
    std::vector<BagSlot> slots;
    const data::PocketSpec* p = findPocket(bagLayout(), pocket);
    if (!p) return slots;
    if (isGbBag(bagLayout())) return readGbPocket(*p, pocket);
    std::vector<uint8_t> raw(static_cast<size_t>(p->capacity) * 4);
    readBag(p->offset, raw.data(), raw.size());
    const uint16_t key = bagCountKey();
    for (int i = 0; i < p->capacity; ++i) {
        BagSlot slot;
        slot.item = util::readU16(&raw[i * 4]);
        slot.count = static_cast<uint16_t>(util::readU16(&raw[i * 4 + 2]) ^ key);
        if (slot.item != 0 && slot.count != 0) slots.push_back(slot);
    }
    return slots;
}

bool SaveFile::setPocket(int pocket, const std::vector<BagSlot>& slots, std::string* error) {
    const data::PocketSpec* p = findPocket(bagLayout(), pocket);
    if (!p) return fail(error, "No such pocket");
    if (slots.size() > static_cast<size_t>(p->capacity)) return fail(error, "Too many items for this pocket");
    for (const BagSlot& slot : slots) {
        if (!itemExists(slot.item)) return fail(error, "Not an item of this game");
        if (slot.count == 0) return fail(error, "Quantity must be at least 1");
        if (slot.count > data::pocketMaxCount(*p, slot.item)) return fail(error, "Quantity is above the limit");
    }
    if (isGbBag(bagLayout())) return writeGbPocket(*p, pocket, slots, error);
    std::vector<uint8_t> raw(static_cast<size_t>(p->capacity) * 4, 0);
    const uint16_t key = bagCountKey();
    for (int i = 0; i < p->capacity; ++i) {
        BagSlot slot = static_cast<size_t>(i) < slots.size() ? slots[i] : BagSlot{};
        util::writeU16(&raw[i * 4], slot.item);
        util::writeU16(&raw[i * 4 + 2], static_cast<uint16_t>(slot.count ^ key));
    }
    writeBag(p->offset, raw.data(), raw.size());
    return true;
}

std::vector<BagSlot> SaveFile::readGbPocket(const data::PocketSpec& p, int pocket) const {
    std::vector<BagSlot> slots;
    const size_t base = gbPocketOffset(pocket);
    if (p.kind == data::PocketKind::TMsHMs) {  // one count per item, in the pocket's item order
        std::vector<uint8_t> raw(p.itemCount);
        readBag(base, raw.data(), raw.size());
        for (int i = 0; i < p.itemCount; ++i) {
            if (raw[i] != 0) slots.push_back({p.items[i], raw[i]});
        }
        return slots;
    }
    const bool single = p.kind == data::PocketKind::KeyItems;  // key items have no quantity
    const size_t stride = single ? 1 : 2;
    std::vector<uint8_t> raw(1 + p.capacity * stride);
    readBag(base, raw.data(), raw.size());
    if (raw[0] > p.capacity) return slots;  // uninitialised (0xFF) or corrupt
    for (int i = 0; i < raw[0]; ++i) {
        BagSlot slot;
        slot.item = raw[1 + i * stride];
        slot.count = single ? 1 : raw[2 + i * stride];
        if (slot.item != 0 && slot.item != 0xFF && slot.count != 0) slots.push_back(slot);
    }
    return slots;
}

bool SaveFile::writeGbPocket(const data::PocketSpec& p, int pocket, const std::vector<BagSlot>& slots,
                             std::string* error) {
    const size_t base = gbPocketOffset(pocket);
    if (p.kind == data::PocketKind::TMsHMs) {
        std::vector<uint8_t> raw(p.itemCount, 0);
        for (const BagSlot& slot : slots) {
            int index = -1;
            for (int i = 0; i < p.itemCount; ++i) {
                if (p.items[i] == slot.item) index = i;
            }
            if (index < 0) return fail(error, "This item does not belong in this pocket");
            if (raw[index] != 0) return fail(error, "Each TM or HM can be listed once");
            raw[index] = static_cast<uint8_t>(slot.count);
        }
        writeBag(base, raw.data(), raw.size());
        return true;
    }
    const bool single = p.kind == data::PocketKind::KeyItems;
    const size_t stride = single ? 1 : 2;
    // count, entries, 0xFF; the rest of the pocket is cleared.
    std::vector<uint8_t> raw(1 + p.capacity * stride + 1, 0);
    raw[0] = static_cast<uint8_t>(slots.size());
    for (size_t i = 0; i < slots.size(); ++i) {
        raw[1 + i * stride] = static_cast<uint8_t>(slots[i].item);
        if (!single) raw[2 + i * stride] = static_cast<uint8_t>(slots[i].count);
    }
    raw[1 + slots.size() * stride] = 0xFF;
    writeBag(base, raw.data(), raw.size());
    return true;
}

const char* SaveFile::itemName(uint16_t item) const {
    if (format() == 1) return item < 256 ? data::itemNameGen1(static_cast<uint8_t>(item)) : "???";
    if (format() == 2) return item < 256 ? data::itemNameGen2(static_cast<uint8_t>(item)) : "???";
    return format() == 3 ? data::itemNameGen3(item) : data::itemName(item);
}

bool SaveFile::removePartySlot(int slot) {
    int count = partyCount();
    if (slot < 0 || slot >= count || count <= 1) return false;
    uint8_t buffer[236];
    for (int i = slot; i + 1 < count; ++i) {
        if (readPartySlot(i + 1, buffer) != SlotState::Valid) return false;
        writePartySlot(i, buffer);
    }
    clearPartySlot(count - 1);
    setPartyCount(count - 1);
    return true;
}

bool SaveFile::appendParty(const uint8_t* decryptedParty) {
    int count = partyCount();
    if (count >= kPartyMax) return false;
    writePartySlot(count, decryptedParty);
    setPartyCount(count + 1);
    return true;
}

std::unique_ptr<SaveFile> loadSaveFile(std::vector<uint8_t> data, LoadResult* result) {
    LoadResult status = LoadResult::UnknownFormat;
    std::unique_ptr<SaveFile> save;
    if (Gen3Save::detect(data) != Game::Unknown) {
        auto gen3 = std::make_unique<Gen3Save>();
        status = gen3->load(std::move(data));
        if (status == LoadResult::Ok) save = std::move(gen3);
    } else if (Gen1Save::detect(data) != Game::Unknown) {
        auto gen1 = std::make_unique<Gen1Save>();
        status = gen1->load(std::move(data));
        if (status == LoadResult::Ok) save = std::move(gen1);
    } else if (Gen2Save::detect(data) != Game::Unknown) {
        auto gen2 = std::make_unique<Gen2Save>();
        status = gen2->load(std::move(data));
        if (status == LoadResult::Ok) save = std::move(gen2);
    } else if (Gen5Save::detect(data) != Game::Unknown) {
        auto gen5 = std::make_unique<Gen5Save>();
        status = gen5->load(std::move(data));
        if (status == LoadResult::Ok) save = std::move(gen5);
    } else {
        auto gen4 = std::make_unique<Gen4Save>();
        status = gen4->load(std::move(data));
        if (status == LoadResult::Ok) save = std::move(gen4);
    }
    if (result) *result = status;
    return save;
}

}  // namespace pktool::saves
