#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/saves/save_file.h"

namespace pktool::saves {

// Generation III save file (Ruby, Sapphire, Emerald, FireRed, LeafGreen).
//
// The 128 KiB flash holds two copies ("slots") of the main save, each made of
// 14 sectors of 4 KiB. Every sector carries 0xF80 bytes of data and a footer:
// sector ID (0xFF4), checksum (0xFF6), signature 0x08012025 (0xFF8) and save
// counter (0xFFC). The sectors of a slot are rotated on every save, so the
// data is found by sector ID: 0 = trainer info, 1-4 = party and game state,
// 5-13 = PC boxes. The slot with the higher counter is the current one; it is
// edited in place, like PKHeX does. Emulators may append a small trailer
// (e.g. an RTC block), which is kept as is.
class Gen3Save final : public SaveFile {
public:
    static constexpr size_t kFileSize = 0x20000;
    static constexpr size_t kHalfFileSize = 0x10000;  // emulators configured for 64 KiB flash
    static constexpr int kBoxCount = 14;

    LoadResult load(std::vector<uint8_t> data);
    static Game detect(const std::vector<uint8_t>& data);

    // Fast path for summarising a save (see Gen4Save::readProbeImage): fills `image` (as many
    // zero bytes as the file has) with the sector footers of both slots and all of the current
    // slot. False if the file is not a Gen III save.
    static bool readProbeImage(std::vector<uint8_t>& image, const RangeReader& read);

    Game game() const override { return game_; }
    const std::vector<uint8_t>& data() const override { return data_; }

    std::string trainerName() const override;
    uint16_t trainerId() const override;
    uint16_t secretId() const override;
    uint8_t trainerGender() const override;
    uint8_t language() const override;
    uint8_t gameVersion() const override;

    // Trainer card and bag, mirroring PKHeX: the name, IDs, gender and play
    // time live in the small block; money (and, in Emerald and FireRed/
    // LeafGreen, bag quantities) are XOR-encrypted with the security key;
    // badges are 8 event flags. Japanese saves allow 5 name characters.
    TrainerData trainerData() const override;
    bool setTrainerData(const TrainerData& trainer, std::string* error = nullptr) override;
    size_t trainerNameMaxLength() const override { return japanese_ ? 5 : kTrainerNameMax; }

    int partyCount() const override;
    SlotState readPartySlot(int slot, uint8_t* decrypted) const override;
    bool writePartySlot(int slot, const uint8_t* decrypted) override;

protected:
    void setPartyCount(int count) override;
    void clearPartySlot(int slot) override;
    data::BagLayout bagLayout() const override;
    void readBag(size_t offset, uint8_t* out, size_t length) const override;
    void writeBag(size_t offset, const uint8_t* in, size_t length) override;
    uint16_t bagCountKey() const override;

public:
    int boxCount() const override { return kBoxCount; }
    std::string boxName(int box) const override;
    bool setBoxName(int box, const std::string& name) override;
    SlotState readBoxSlot(int box, int slot, uint8_t* decrypted) const override;
    bool writeBoxSlot(int box, int slot, const uint8_t* decrypted) override;
    bool clearBoxSlot(int box, int slot) override;

    void refreshChecksums() override;
    bool checksumsValid() const override;

    bool japanese() const { return japanese_; }
    int activeSlot() const { return activeSlot_; }

private:
    // Logical areas, named by their first sector ID.
    enum Area : int { kSmall = 0, kLarge = 1, kStorage = 5 };

    size_t fileOffset(Area area, size_t offset) const;
    void readArea(Area area, size_t offset, uint8_t* out, size_t length) const;
    void writeArea(Area area, size_t offset, const uint8_t* in, size_t length);
    size_t partyOffset() const;
    uint32_t securityKey() const;  // 0 in Ruby/Sapphire
    size_t moneyOffset() const;
    size_t bagOffset() const;
    bool badgeFlag(int badge) const;
    void setBadgeFlag(int badge, bool value);
    SlotState decodeSlot(uint8_t* data, size_t size) const;
    uint8_t mostCommonOrigin(uint8_t first, uint8_t second, uint8_t fallback) const;

    std::vector<uint8_t> data_;
    Game game_ = Game::Unknown;
    int activeSlot_ = 0;
    size_t sectorOffset_[14] = {};  // file offset of each sector ID in the active slot
    bool japanese_ = false;
};

}  // namespace pktool::saves
