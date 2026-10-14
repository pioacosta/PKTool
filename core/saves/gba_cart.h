#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Backing up the save of a Gen III Pokémon cartridge in the GBA slot (Slot-2)
// of a DS or DS Lite. Read-only: the only writes go to the flash chip's
// command registers to switch between its two 64 KiB banks (the sequence the
// games themselves use); no data is ever erased or programmed.
//
// The platform supplies the bus (ds/main.cpp on the DS; tests use an
// in-memory cartridge), so this file stays portable.
namespace pktool::saves::gba {

class Bus {
public:
    virtual ~Bus() = default;
    virtual uint8_t readRom(uint32_t offset) = 0;              // cartridge ROM, from 0x08000000
    virtual uint8_t readSave(uint16_t offset) = 0;             // save chip, from 0x0A000000 (8-bit bus)
    virtual void writeSave(uint16_t offset, uint8_t value) = 0;  // flash commands only
};

struct CartInfo {
    bool present = false;          // a cartridge with a valid header
    std::string gameCode;          // e.g. "BPEE" (header 0xAC)
    std::string title;             // header 0xA0, e.g. "POKEMON EMER"
    const char* gameName = nullptr;  // "Pokémon Emerald" for the Gen III games, else nullptr
};

CartInfo identify(Bus& bus);

enum class ReadResult : uint8_t {
    Ok,
    NoCartridge,   // nothing in the slot, or an unreadable header
    NotPokemon,    // a cartridge, but not Ruby/Sapphire/Emerald/FireRed/LeafGreen
    ReadMismatch,  // two reads of the save differ: the cartridge is not making contact
};

const char* describe(ReadResult result);

// Reads the 128 KiB flash save (both banks) twice and only succeeds when both
// reads are identical. `info` receives the cartridge identification.
ReadResult readSave(Bus& bus, CartInfo& info, std::vector<uint8_t>& out);

// "<game code>_<YYYYMMDD-HHMMSS>.sav", e.g. "BPEE_20261014-153000.sav".
std::string backupFileName(const CartInfo& info, const std::string& timestamp);

}  // namespace pktool::saves::gba
