#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/saves/game.h"
#include "core/saves/save_file.h"

namespace pktool::saves {

// Generation IV save file (Diamond, Pearl, Platinum, HeartGold, SoulSilver).
//
// The 512 KiB file holds two 256 KiB partitions. Each partition contains a
// "general" block (trainer, party, items, ...) and a "storage" block (PC
// boxes). The game alternates partitions when saving, and the newest copy of
// each block is chosen independently from the counters in the block footers.
// Each block ends with a footer whose last two bytes are a CRC16-CCITT of the
// block data preceding the footer.
class Gen4Save final : public SaveFile {
public:
    static constexpr size_t kFileSize = 0x80000;
    static constexpr size_t kPartitionSize = 0x40000;
    static constexpr int kBoxCount = 18;

    using LoadResult = saves::LoadResult;
    using SlotState = saves::SlotState;

    struct Layout {
        Game game;
        uint32_t generalSize;
        uint32_t storageOffset;  // relative to the partition start
        uint32_t storageSize;
        uint32_t footerSize;
        uint32_t trainerOffset;  // inside the general block
        uint32_t partyOffset;    // inside the general block
    };

    // Takes ownership of the file contents.
    LoadResult load(std::vector<uint8_t> data);

    // Fast path for summarising a save without reading all 512 KiB: fills `image` (kFileSize
    // zero bytes) with the footers and then the newest copy of the general and storage blocks,
    // which is all load() and the trainer/party getters look at. The partitions chosen are
    // returned in `general` and `storage`; load() on the image must pick the same ones (it
    // would otherwise be looking at zeros). False if the file is not a Gen IV save.
    static bool readProbeImage(std::vector<uint8_t>& image, const RangeReader& read, int* general, int* storage);

    bool isLoaded() const { return layout_ != nullptr; }
    Game game() const override;
    const std::vector<uint8_t>& data() const override { return data_; }

    // Partition (0 or 1) holding the active copy of each block.
    int activeGeneralPartition() const { return generalPartition_; }
    int activeStoragePartition() const { return storagePartition_; }

    // Trainer card.
    std::string trainerName() const override;
    uint16_t trainerId() const override;
    uint16_t secretId() const override;
    uint8_t trainerGender() const override;  // 0 male, 1 female
    uint8_t language() const override;       // game language ID
    // Game version ID of this save. D/P and HG/SS saves do not store it: it
    // is taken from the trainer's own Pokémon (Diamond / HeartGold if none).
    uint8_t gameVersion() const override;

    // Trainer card and bag (PKHeX SAV4): everything lives in the general
    // block, which refreshChecksums() covers. HG/SS badges are 16 bits:
    // Johto in bits 0-7 and Kanto in bits 8-15.
    TrainerData trainerData() const override;
    bool setTrainerData(const TrainerData& trainer, std::string* error = nullptr) override;
    int badgeCount() const override { return isHgss() ? 16 : 8; }

    // Party (decrypted slots are 236 bytes).
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

    // PC boxes (decrypted slots are 136 bytes).
    int boxCount() const override { return kBoxCount; }
    std::string boxName(int box) const override;
    bool setBoxName(int box, const std::string& name) override;
    int currentBox() const;
    SlotState readBoxSlot(int box, int slot, uint8_t* decrypted) const override;
    bool writeBoxSlot(int box, int slot, const uint8_t* decrypted) override;
    bool clearBoxSlot(int box, int slot) override;

    // Recomputes the CRC of both active blocks. Call before exporting data().
    void refreshChecksums() override;
    bool checksumsValid() const override;

    // Identifies the format without loading it. Returns nullptr if unknown.
    static const Layout* detect(const std::vector<uint8_t>& data);

private:
    uint8_t* general();
    const uint8_t* general() const;
    uint8_t* storage();
    const uint8_t* storage() const;
    size_t boxSlotOffset(int box, int slot) const;
    bool isHgss() const;
    uint8_t guessVersion() const;

    std::vector<uint8_t> data_;
    const Layout* layout_ = nullptr;
    int generalPartition_ = 0;
    int storagePartition_ = 0;
    uint8_t version_ = 0;
};

}  // namespace pktool::saves
