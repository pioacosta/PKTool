#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/io/file_io.h"

// PKTool Bank: Pokémon storage on the SD card, shared by all saves.
//
// File format (little endian), written with io::safeWriteFile:
//   "PKBK", u16 version (2 or 3), u16 box count, [version 2+: box count * 16
//   bytes of box names, UTF-8, NUL padded; all zero = default name "Box N"],
//   then box count * 30 slots of 140 bytes: u8 format (0 = empty, 1 = PK1,
//   2 = PK2, 3 = PK3, 4 = PK4, 5 = PK5), 3 reserved bytes and the decrypted
//   stored entity (136 bytes; the Game Boy blobs use 67 / 72 of them, PK3 the
//   first 80, the rest is zero); finally a CRC16-CCITT of everything before it.
// Version 1 files (no names) are still read; the next save writes version 2.
// A Bank that holds a Gen I or Gen II Pokemon is written as version 3, so an
// older PKTool refuses it instead of dropping those slots; without them the
// file stays version 2.
namespace pktool::bank {

class Bank {
public:
    static constexpr int kBoxCount = 20;
    static constexpr int kSlotsPerBox = 30;
    static constexpr size_t kEntitySize = 136;
    static constexpr size_t kBoxNameBytes = 16;     // storage per name (UTF-8)
    static constexpr size_t kBoxNameMaxChars = 12;  // what the on-screen keyboard accepts

    enum class LoadResult : uint8_t { Ok, Created, Corrupt };
    enum class SortKey : uint8_t { Dex, Name, Level };

    struct SearchFilter {
        std::string text;      // case-insensitive substring of the species name ("" = any)
        bool shinyOnly = false;
        uint8_t format = 0;    // 0 = any, otherwise 1-5
    };
    struct Match {
        int box;
        int slot;
    };

    // Loads `path`; a missing file gives an empty bank (Created). A corrupt
    // file is left untouched and the bank stays empty and read-only.
    LoadResult load(const std::string& path);
    // Same from a file already in memory (no path, so save() has nowhere to
    // write). Used by the fuzz tests.
    LoadResult loadBytes(const std::vector<uint8_t>& data);
    io::WriteResult save();
    bool readOnly() const { return readOnly_; }
    const std::string& path() const { return path_; }

    // Entity format of a slot (0 if empty, 1-5) and its decrypted data.
    uint8_t format(int box, int slot) const;
    const uint8_t* entity(int box, int slot) const;
    bool set(int box, int slot, uint8_t format, const uint8_t* decrypted);
    bool clear(int box, int slot);
    // Removes the Pokémon of a slot; false if it is empty or the Bank read-only.
    bool release(int box, int slot);
    int count() const;

    // Box names. boxName() gives "Box N" for a box without a custom name.
    // setBoxName() keeps at most kBoxNameMaxChars characters (and
    // kBoxNameBytes - 1 bytes); an empty or default name restores the default.
    std::string boxName(int box) const;
    bool hasCustomBoxName(int box) const;
    bool setBoxName(int box, const std::string& name);

    // Stable sort of one box (box >= 0) or of the whole Bank (box < 0, box by
    // box in order); empty slots end up at the end. Dex: National Dex number,
    // then form; Name: species name; Level: highest first. Done in place with
    // an index of the slots, never a copy of the Bank. False if read-only.
    bool sort(SortKey key, int box);

    // Slots matching the filter, in Bank order.
    std::vector<Match> search(const SearchFilter& filter) const;

    std::vector<uint8_t> serialize() const;

private:
    LoadResult parse(const std::vector<uint8_t>& data);
    struct Slot {
        uint8_t format = 0;
        uint8_t data[kEntitySize] = {};
    };
    Slot* at(int box, int slot);
    const Slot* at(int box, int slot) const;
    void ensureSlots();

    // The 600 slots (82 KB) are only allocated when the Bank is loaded or first written: an app
    // that never opens the Bank does not pay for it, and an unloaded Bank reads as empty.
    std::vector<Slot> slots_;
    char names_[kBoxCount][kBoxNameBytes] = {};
    std::string path_;
    bool readOnly_ = false;
};

}  // namespace pktool::bank
