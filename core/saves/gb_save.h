#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/saves/save_file.h"

namespace pktool::saves {

// Shared part of the Game Boy saves (Gen I and Gen II): 32 KiB SRAM dumps (64
// KiB for the Japanese Gold/Silver/Crystal), maybe followed by the emulator's
// RTC block, which is kept as it is.
//
// Pokemon live in "lists": a count byte, one species byte per slot (0xFF ends
// the list, 0xFD marks a Gen II egg), the bodies of all slots, then all OT
// names, then all nicknames. PKTool unpacks every list into self-contained
// entity blobs (pk1.h / pk2.h) and packs a list back only when it was
// modified, so an untouched list round-trips byte for byte. Boxes are
// compacted (no holes) when packed, like the games and PKHeX do. Party stats
// are never stored for a box: they are synthesized when a box slot is read.
class GbSave : public SaveFile {
public:
    static constexpr int kMaxBoxSlots = 30;
    static constexpr size_t kMaxBlob = 72;
    static constexpr size_t kMaxTrailer = 0x30;

    // 0x8000 or 0x10000 bytes plus an emulator RTC trailer (PKHeX: 0x0C-0x30
    // bytes, even, or 7).
    static bool sizeOk(size_t size);

    const std::vector<uint8_t>& data() const override { return data_; }

    bool japanese() const { return japanese_; }
    bool korean() const { return korean_; }

    uint8_t language() const override { return japanese_ ? 1 : korean_ ? 8 : 2; }
    uint16_t secretId() const override { return 0; }
    bool hasSecretId() const override { return false; }

    int partyCount() const override { return partyCount_; }
    SlotState readPartySlot(int slot, uint8_t* decrypted) const override;
    bool writePartySlot(int slot, const uint8_t* decrypted) override;

    int boxCount() const override { return boxCount_; }
    int boxSlotCount() const override { return boxCapacity_; }
    SlotState readBoxSlot(int box, int slot, uint8_t* decrypted) const override;
    bool writeBoxSlot(int box, int slot, const uint8_t* decrypted) override;
    bool clearBoxSlot(int box, int slot) override;

    void refreshChecksums() override;

protected:
    struct Geometry {
        uint8_t format = 1;
        int boxCount = 0;
        int boxCapacity = 20;
        size_t bodyStored = 0;   // bytes of a box entity
        size_t bodyParty = 0;    // bytes of a party entity
        size_t stringBytes = 11; // OT and nickname field size in the lists
        size_t blobSize = 0;
    };

    struct Slot {
        uint8_t marker = 0xFF;  // list header byte
        uint8_t blob[kMaxBlob] = {};
        bool present() const { return marker != 0 && marker != 0xFF; }
    };

    // ---- implemented by Gen1Save / Gen2Save
    // Raw offsets of the boxes' lists in the file.
    virtual size_t boxListOffset(int box) const = 0;   // the box's storage
    virtual size_t currentBoxOffset() const = 0;       // mirror of the current box
    virtual size_t partyListOffset() const = 0;
    virtual int currentBox() const = 0;
    // Gen I: other boxes are only valid once the "initialised" flag is set.
    virtual bool boxStorageValid(int box) const { (void)box; return true; }
    // Where a box is read from (the current box of Gen I lives in the mirror).
    virtual bool readsCurrentBoxFromMirror() const { return false; }
    // Called when a non-current box gets content in a save whose box storage is
    // not initialised yet; returns true if all boxes must be written.
    virtual bool initialiseBoxStorage() { return false; }
    virtual void fillPartyArea(uint8_t* blob) const = 0;
    virtual void setMarker(Slot& slot) const = 0;
    // Recomputes the game's checksum(s) over the (already packed) data.
    virtual void writeChecksums() = 0;

    void setPartyCount(int count) override;
    void clearPartySlot(int slot) override;

    // Reads all lists from data_ after the derived class set geometry_.
    void unpackAll();
    // Writes the modified lists back into data_.
    void packDirty();

    size_t listLength(bool party) const;
    void unpackList(const uint8_t* list, bool party, Slot* out, int count) const;
    // Returns true if the list was written (an empty list over an empty or
    // uninitialised destination is not).
    bool packList(uint8_t* list, bool party, const Slot* slots, bool destInitialised);
    static bool allZero(const uint8_t* bytes, size_t length);

    std::string decodeName(const uint8_t* field, size_t length) const;
    bool encodeName(const std::string& name, uint8_t* field, size_t length, size_t maxChars) const;

    std::vector<uint8_t> data_;
    Geometry geometry_;
    bool japanese_ = false;
    bool korean_ = false;
    int boxCount_ = 0;
    int boxCapacity_ = 20;
    int partyCount_ = 0;
    std::vector<Slot> boxes_;   // boxCount_ * boxCapacity_
    Slot party_[kPartyMax];
    std::vector<bool> boxDirty_;
    bool partyDirty_ = false;
    bool allBoxesDirty_ = false;
};

}  // namespace pktool::saves
