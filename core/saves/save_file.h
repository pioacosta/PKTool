#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/data/items.h"
#include "core/pokemon/entity.h"
#include "core/saves/game.h"

namespace pktool::saves {

enum class LoadResult : uint8_t { Ok, WrongSize, UnknownFormat, BadChecksum };

// Fetches `length` bytes of a save file at `offset`; false on a read error. Used by
// readProbeImage() of each format to load only the parts of a save a summary needs.
using RangeReader = std::function<bool(size_t offset, size_t length, uint8_t* out)>;
enum class SlotState : uint8_t { Empty, Valid, BadChecksum };

// Editable trainer card (see SaveFile::setTrainerData). Changing the trainer
// ID or secret ID makes every Pokémon already in the save count as traded
// (their OT no longer matches), and legality checks will report them so.
struct TrainerData {
    std::string name;       // 1-7 characters (5 in Japanese Gen III), encodable in the game's charset
    uint8_t gender = 0;     // 0 male, 1 female
    uint16_t tid = 0;
    uint16_t sid = 0;
    uint32_t money = 0;     // 0-maxMoney() (999999, Gen V 9999999)
    uint16_t hours = 0;     // play time: 0-999 h
    uint8_t minutes = 0;    // 0-59
    uint8_t seconds = 0;    // 0-59
    // Badge n obtained = bit n. Gen III and V: 8 badges; Gen IV: 8 (Sinnoh or
    // Johto) and, in HeartGold/SoulSilver, bits 8-15 for the Kanto badges.
    uint16_t badges = 0;
};

constexpr uint32_t kMaxMoney = 999999;       // Gen III and IV
constexpr uint32_t kMaxMoneyGen5 = 9999999;  // Gen V
constexpr size_t kTrainerNameMax = 7;
constexpr int kMaxPlayHours = 999;

// A bag pocket: its slots hold one item each.
struct BagPocket {
    const char* name;
    data::PocketKind kind;
    int capacity;  // number of slots
};

// One occupied slot. `item` is an ID of the save's own generation (Gen III
// saves use Gen III item IDs).
struct BagSlot {
    uint16_t item = 0;
    uint16_t count = 0;
};

// A loaded save file of any supported generation. Slots are exchanged as
// decrypted entities in the save's format (PK1/PK2 blobs for Game Boy saves,
// PK3 for Gen III, PK4 for Gen IV, PK5 for Gen V): storedSize() bytes in boxes, partySize() in the party.
class SaveFile {
public:
    static constexpr int kSlotsPerBox = 30;
    static constexpr int kPartyMax = 6;

    virtual ~SaveFile() = default;

    virtual Game game() const = 0;
    // Entity format of the slots (1-5).
    uint8_t format() const { return gameGeneration(game()); }
    // Bytes of a slot: PK1/PK2 blobs (67/72, party and box alike), PK3 100/80,
    // PK4 236/136, PK5 220/136.
    size_t partySize() const { return pokemon::entity::partySize(format()); }
    size_t storedSize() const { return pokemon::entity::storedSize(format()); }
    virtual const std::vector<uint8_t>& data() const = 0;

    // Trainer card.
    virtual std::string trainerName() const = 0;
    virtual uint16_t trainerId() const = 0;
    virtual uint16_t secretId() const = 0;
    virtual uint8_t trainerGender() const = 0;  // 0 male, 1 female
    // Game Boy saves have no secret ID; only Crystal has a trainer gender.
    virtual bool hasSecretId() const { return true; }
    virtual bool hasTrainerGender() const { return true; }
    // Longest play time the game counts (hours): 999, 255 in Gen I.
    virtual int maxPlayHours() const { return kMaxPlayHours; }
    virtual uint8_t language() const = 0;
    // Game version ID for Pokémon created in this save.
    virtual uint8_t gameVersion() const = 0;

    // The whole editable trainer card. The setter validates everything first
    // and, if anything is invalid, returns false with a short reason in
    // `error` and changes nothing. Call refreshChecksums() before exporting.
    // Gen III badges are event flags (as in PKHeX), Gen IV/V badges a byte.
    virtual TrainerData trainerData() const = 0;
    virtual bool setTrainerData(const TrainerData& trainer, std::string* error = nullptr) = 0;
    // Longest trainer name this save accepts.
    virtual size_t trainerNameMaxLength() const { return kTrainerNameMax; }
    // Number of badge bits in TrainerData::badges (8, or 16 in HG/SS).
    virtual int badgeCount() const { return 8; }
    // Highest money the game allows.
    uint32_t maxMoney() const { return format() >= 5 ? kMaxMoneyGen5 : kMaxMoney; }

    // Bag. Pockets are listed in the game's order; Gen III bag PC storage is
    // not supported. Item IDs belong to the save's generation.
    int pocketCount() const;
    BagPocket pocketInfo(int pocket) const;
    // Every item the pocket may hold, in the game's order.
    std::vector<uint16_t> pocketItems(int pocket) const;
    bool pocketAllows(int pocket, uint16_t item) const;
    // Whether `item` is a real item of this save's game (non-zero ID with a
    // name in its item table), wherever it normally lives.
    bool itemExists(uint16_t item) const;
    // Quantity limit of an item in a pocket: the game's per-slot limit.
    int itemMaxCount(int pocket, uint16_t item) const;
    // Occupied slots in order (empty slots are skipped).
    std::vector<BagSlot> readPocket(int pocket) const;
    // Replaces the content of a pocket. Only the game's limits are enforced:
    // at most capacity slots, each item a real item of the game (it need not
    // belong in this pocket and may repeat), quantity in 1..itemMaxCount.
    // Slots are written compacted; the rest of the pocket is cleared. On
    // failure returns false with a reason in `error` and changes nothing.
    bool setPocket(int pocket, const std::vector<BagSlot>& slots, std::string* error = nullptr);
    // English item name for an ID of this save's generation.
    const char* itemName(uint16_t item) const;

    virtual int partyCount() const = 0;
    virtual SlotState readPartySlot(int slot, uint8_t* decrypted) const = 0;
    virtual bool writePartySlot(int slot, const uint8_t* decrypted) = 0;

    // Removes a party member, moving the following ones up (never the last
    // one: a party needs a Pokémon), or appends one at the end.
    bool removePartySlot(int slot);
    bool appendParty(const uint8_t* decryptedParty);

    virtual int boxCount() const = 0;
    // Slots of a box actually in use (30; the Game Boy games have 20 or 30).
    virtual int boxSlotCount() const { return kSlotsPerBox; }
    // Gen I boxes have fixed names.
    virtual bool hasBoxNames() const { return true; }
    virtual std::string boxName(int box) const = 0;
    // Renames a box (the games allow up to 8 characters).
    virtual bool setBoxName(int box, const std::string& name) = 0;
    static constexpr size_t kBoxNameMaxLength = 8;
    virtual SlotState readBoxSlot(int box, int slot, uint8_t* decrypted) const = 0;
    virtual bool writeBoxSlot(int box, int slot, const uint8_t* decrypted) = 0;
    virtual bool clearBoxSlot(int box, int slot) = 0;

protected:
    virtual void setPartyCount(int count) = 0;
    virtual void clearPartySlot(int slot) = 0;

    // Bag storage, implemented by each format. Offsets are relative to the
    // start of the bag; Gen III stores quantities XOR the low 16 bits of the
    // save's security key (bagCountKey, 0 when not encrypted).
    virtual data::BagLayout bagLayout() const = 0;
    virtual void readBag(size_t offset, uint8_t* out, size_t length) const = 0;
    virtual void writeBag(size_t offset, const uint8_t* in, size_t length) = 0;
    virtual uint16_t bagCountKey() const { return 0; }
    // Game Boy bags (format 1/2) are lists (count, entries, 0xFF) or, for the Gen II TM/HM
    // pocket, one count byte per item; readBag/writeBag then take absolute save offsets and
    // this gives the offset of the pocket's first byte.
    virtual size_t gbPocketOffset(int) const { return 0; }
    std::vector<BagSlot> readGbPocket(const data::PocketSpec& pocket, int index) const;
    bool writeGbPocket(const data::PocketSpec& pocket, int index, const std::vector<BagSlot>& slots,
                       std::string* error);

    // Shared validation of the fields every generation has. `error` may be null.
    bool validateTrainer(const TrainerData& trainer, std::string* error) const;

public:
    // Recomputes every checksum. Call before exporting data().
    virtual void refreshChecksums() = 0;
    virtual bool checksumsValid() const = 0;
};

// Detects the format and loads the save (Gen III, IV or V). Returns nullptr
// on failure, with the reason in `result`.
std::unique_ptr<SaveFile> loadSaveFile(std::vector<uint8_t> data, LoadResult* result = nullptr);

}  // namespace pktool::saves
