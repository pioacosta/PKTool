#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/saves/gb_save.h"

namespace pktool::saves {

// Generation II save file (Gold, Silver, Crystal; the Japanese and Korean
// games too): a 32 KiB SRAM dump (64 KiB for the Japanese games). Gold and
// Silver cannot be told apart. The trainer data and the party sit in the main
// block, which has a 16-bit sum checksum (stored twice) and is copied to
// backup positions the game falls back on; the box lists are in the banks at
// 0x4000 and 0x6000 (those are the truth; the current box is mirrored in the
// main block). Crystal's main block is shorter, so its sum, copy and offsets
// differ from Gold/Silver's. Korean saves keep a second, differently computed
// checksum.
class Gen2Save final : public GbSave {
public:
    static constexpr size_t kFileSize = 0x8000;
    static constexpr size_t kJapaneseFileSize = 0x10000;

    LoadResult load(std::vector<uint8_t> data);
    // Game::GoldSilver, Game::Crystal or Game::Unknown.
    static Game detect(const std::vector<uint8_t>& data);

    Game game() const override { return crystal_ ? Game::Crystal : Game::GoldSilver; }

    std::string trainerName() const override;
    uint16_t trainerId() const override;
    uint8_t trainerGender() const override;
    uint8_t gameVersion() const override;
    bool hasTrainerGender() const override { return crystal_; }

    TrainerData trainerData() const override;
    bool setTrainerData(const TrainerData& trainer, std::string* error = nullptr) override;
    size_t trainerNameMaxLength() const override { return japanese_ || korean_ ? 5 : 7; }
    int badgeCount() const override { return 16; }  // Johto and Kanto

    std::string boxName(int box) const override;
    bool setBoxName(int box, const std::string& name) override;

    bool checksumsValid() const override;

protected:
    size_t boxListOffset(int box) const override;
    size_t currentBoxOffset() const override;
    size_t partyListOffset() const override;
    int currentBox() const override;
    void fillPartyArea(uint8_t* blob) const override;
    void setMarker(Slot& slot) const override;
    void writeChecksums() override;

    data::BagLayout bagLayout() const override {
        return crystal_ ? data::BagLayout::Gen2Crystal : data::BagLayout::Gen2GoldSilver;
    }
    void readBag(size_t offset, uint8_t* out, size_t length) const override;
    size_t gbPocketOffset(int pocket) const override;
    void writeBag(size_t offset, const uint8_t* in, size_t length) override;

private:
    uint16_t sum() const;
    uint16_t koreanSum() const;

    bool crystal_ = false;
    int variant_ = 0;  // index into the layout table
};

}  // namespace pktool::saves
