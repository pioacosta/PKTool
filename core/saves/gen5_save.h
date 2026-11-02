#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/saves/save_file.h"

namespace pktool::saves {

// Generation V save file (Black, White, Black 2, White 2).
//
// The 512 KiB file starts with the main save: fixed-position blocks (box
// names, 24 boxes, items, party, trainer data...), each followed by a
// CRC16-CCITT that is also mirrored in a checksum block at the end of the
// main save. The checksum block has its own CRC, which is how the format is
// detected. A backup copy of the main save follows it; when it is present,
// it is rewritten with the main save so both copies stay identical, as after
// saving in the game.
class Gen5Save final : public SaveFile {
public:
    static constexpr size_t kFileSize = 0x80000;
    static constexpr int kBoxCount = 24;

    struct Block {
        uint32_t offset;
        uint32_t length;
        uint32_t checksumOffset;
        uint32_t checksumMirror;
    };

    // Takes ownership of the file contents.
    LoadResult load(std::vector<uint8_t> data);
    static Game detect(const std::vector<uint8_t>& data);

    // Fast path for summarising a save (see Gen4Save::readProbeImage): fills `image` (kFileSize
    // zero bytes) with the footer blocks, the party and the trainer card. False if the file is
    // not a Gen V save.
    static bool readProbeImage(std::vector<uint8_t>& image, const RangeReader& read);

    Game game() const override { return game_; }
    const std::vector<uint8_t>& data() const override { return data_; }

    std::string trainerName() const override;
    uint16_t trainerId() const override;
    uint16_t secretId() const override;
    uint8_t trainerGender() const override;
    uint8_t language() const override;
    uint8_t gameVersion() const override;

    // Trainer card and bag (PKHeX PlayerData5, Misc5, MyItem5): name, IDs,
    // gender and play time are in the trainer block, money and the badge
    // byte in the misc block (its offset differs between B/W and B2/W2), the
    // bag in the item block. refreshChecksums() covers all of them and
    // rewrites the backup copy.
    TrainerData trainerData() const override;
    bool setTrainerData(const TrainerData& trainer, std::string* error = nullptr) override;

    int partyCount() const override;
    SlotState readPartySlot(int slot, uint8_t* decrypted) const override;
    bool writePartySlot(int slot, const uint8_t* decrypted) override;

protected:
    void setPartyCount(int count) override;
    void clearPartySlot(int slot) override;
    data::BagLayout bagLayout() const override;
    void readBag(size_t offset, uint8_t* out, size_t length) const override;
    void writeBag(size_t offset, const uint8_t* in, size_t length) override;

public:

    int boxCount() const override { return kBoxCount; }
    std::string boxName(int box) const override;
    bool setBoxName(int box, const std::string& name) override;
    SlotState readBoxSlot(int box, int slot, uint8_t* decrypted) const override;
    bool writeBoxSlot(int box, int slot, const uint8_t* decrypted) override;
    bool clearBoxSlot(int box, int slot) override;

    // Mystery Gift album (PKHeX MysteryBlock5): 0x100 bytes of received flags
    // (one per card ID) and 12 PGF slots, encrypted with the LCRNG seeded by
    // the u32 that follows them. Only Pokémon, item and Entralink cards are
    // accepted (card type 1-3).
    static constexpr int kGiftSlots = 12;
    static constexpr size_t kPgfSize = 0xCC;
    int giftSlotCount() const override { return kGiftSlots; }
    size_t giftCardSize() const override { return kPgfSize; }
    bool readGift(int slot, uint8_t* card) const override;
    bool giftWaiting(int slot) const override;
    bool addGift(const uint8_t* card, std::string* error = nullptr) override;
    bool removeGift(int slot) override;
    // Whether the album's received flag of a card ID is set.
    bool giftReceived(uint16_t cardId) const;

    void refreshChecksums() override;
    bool checksumsValid() const override;
    bool hasBackup() const { return hasBackup_; }

private:
    size_t mainSize() const;
    size_t miscOffset() const;
    // The album decrypted (0xA90 bytes) and back.
    std::vector<uint8_t> readMystery() const;
    void writeMystery(std::vector<uint8_t> album);

    std::vector<uint8_t> data_;
    Game game_ = Game::Unknown;
    const Block* blocks_ = nullptr;
    int blockCount_ = 0;
    bool hasBackup_ = false;
};

}  // namespace pktool::saves
