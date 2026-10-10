#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/saves/gb_save.h"

namespace pktool::saves {

// Generation I save file (Red, Blue, Yellow; Japanese Red/Green/Blue/Pikachu
// too): a 32 KiB SRAM dump. International and Japanese games have different
// layouts (12 boxes of 20 / 8 boxes of 30). The trainer data and the party are
// in the first bank under one checksum (the complement of the sum of the bytes
// from the OT name up to the checksum byte); the box lists are in the banks at
// 0x4000 and 0x6000, with the current box mirrored in the main bank. Boxes
// other than the current one only hold valid data once the game has changed
// boxes at least once (bit 7 of the current-box byte).
class Gen1Save final : public GbSave {
public:
    static constexpr size_t kFileSize = 0x8000;

    LoadResult load(std::vector<uint8_t> data);
    // Game::RedBlue, Game::Yellow or Game::Unknown.
    static Game detect(const std::vector<uint8_t>& data);

    Game game() const override { return yellow_ ? Game::Yellow : Game::RedBlue; }

    std::string trainerName() const override;
    uint16_t trainerId() const override;
    uint8_t trainerGender() const override { return 0; }
    uint8_t gameVersion() const override;
    bool hasTrainerGender() const override { return false; }
    int maxPlayHours() const override { return 255; }
    bool hasBoxNames() const override { return false; }

    TrainerData trainerData() const override;
    bool setTrainerData(const TrainerData& trainer, std::string* error = nullptr) override;
    size_t trainerNameMaxLength() const override { return japanese_ ? 5 : 7; }

    std::string boxName(int box) const override;
    bool setBoxName(int, const std::string&) override { return false; }

    bool checksumsValid() const override;

protected:
    size_t boxListOffset(int box) const override;
    size_t currentBoxOffset() const override;
    size_t partyListOffset() const override;
    int currentBox() const override;
    bool boxStorageValid(int box) const override;
    bool readsCurrentBoxFromMirror() const override { return true; }
    bool initialiseBoxStorage() override;
    void fillPartyArea(uint8_t* blob) const override;
    void setMarker(Slot& slot) const override;
    void writeChecksums() override;

    data::BagLayout bagLayout() const override { return data::BagLayout::Gen1; }
    void readBag(size_t offset, uint8_t* out, size_t length) const override;
    size_t gbPocketOffset(int pocket) const override;
    void writeBag(size_t offset, const uint8_t* in, size_t length) override;

private:
    uint8_t computeChecksum() const;

    bool yellow_ = false;
};

}  // namespace pktool::saves
