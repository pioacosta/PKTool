#include "core/saves/gba_cart.h"

#include <cstring>

namespace pktool::saves::gba {
namespace {

constexpr uint32_t kTitle = 0xA0;
constexpr uint32_t kGameCode = 0xAC;
constexpr uint32_t kFixedValue = 0xB2;  // always 0x96
constexpr uint32_t kComplement = 0xBD;  // header checksum
constexpr uint32_t kBankSize = 0x10000;

struct PokemonGame {
    const char* code;  // first three letters of the game code (the fourth is the region)
    const char* name;
};

const PokemonGame kGames[] = {
    {"AXV", "Pok\xC3\xA9mon Ruby"},     {"AXP", "Pok\xC3\xA9mon Sapphire"}, {"BPE", "Pok\xC3\xA9mon Emerald"},
    {"BPR", "Pok\xC3\xA9mon FireRed"},  {"BPG", "Pok\xC3\xA9mon LeafGreen"},
};

// Flash command: AA to 5555, 55 to 2AAA, then the command to 5555.
void command(Bus& bus, uint8_t cmd) {
    bus.writeSave(0x5555, 0xAA);
    bus.writeSave(0x2AAA, 0x55);
    bus.writeSave(0x5555, cmd);
}

void selectBank(Bus& bus, uint8_t bank) {
    command(bus, 0xB0);  // bank switch (128 KiB Macronix/Sanyo chips, as the games use)
    bus.writeSave(0x0000, bank);
}

void readAll(Bus& bus, std::vector<uint8_t>& out) {
    out.resize(2 * kBankSize);
    for (uint8_t bank = 0; bank < 2; ++bank) {
        selectBank(bus, bank);
        for (uint32_t i = 0; i < kBankSize; ++i) {
            out[bank * kBankSize + i] = bus.readSave(static_cast<uint16_t>(i));
        }
    }
    selectBank(bus, 0);  // leave it as the game expects
}

}  // namespace

CartInfo identify(Bus& bus) {
    CartInfo info;
    uint8_t header[0xC0];
    for (uint32_t i = 0; i < sizeof header; ++i) header[i] = bus.readRom(i);
    uint8_t sum = 0;
    for (uint32_t i = kTitle; i < kComplement; ++i) sum = static_cast<uint8_t>(sum + header[i]);
    const uint8_t complement = static_cast<uint8_t>(-(sum + 0x19));
    if (header[kFixedValue] != 0x96 || header[kComplement] != complement) return info;  // empty slot
    info.present = true;
    for (uint32_t i = 0; i < 12 && header[kTitle + i] != 0; ++i) {
        char c = static_cast<char>(header[kTitle + i]);
        info.title += (c >= 0x20 && c < 0x7F) ? c : '?';
    }
    for (uint32_t i = 0; i < 4; ++i) {
        char c = static_cast<char>(header[kGameCode + i]);
        info.gameCode += (c >= 0x20 && c < 0x7F) ? c : '?';
    }
    for (const PokemonGame& game : kGames) {
        if (info.gameCode.compare(0, 3, game.code) == 0) info.gameName = game.name;
    }
    return info;
}

const char* describe(ReadResult result) {
    switch (result) {
        case ReadResult::Ok:
            return "OK";
        case ReadResult::NoCartridge:
            return "No GBA cartridge found in the\nGBA slot (DS and DS Lite only).";
        case ReadResult::NotPokemon:
            return "The cartridge is not Ruby,\nSapphire, Emerald, FireRed\nor LeafGreen.";
        case ReadResult::ReadMismatch:
            return "The save read differently twice.\nClean the cartridge contacts,\nreinsert it and try again.";
    }
    return "Unknown error.";
}

ReadResult readSave(Bus& bus, CartInfo& info, std::vector<uint8_t>& out) {
    info = identify(bus);
    if (!info.present) return ReadResult::NoCartridge;
    if (info.gameName == nullptr) return ReadResult::NotPokemon;
    std::vector<uint8_t> second;
    readAll(bus, out);
    readAll(bus, second);
    if (out != second) {
        out.clear();
        return ReadResult::ReadMismatch;
    }
    return ReadResult::Ok;
}

std::string backupFileName(const CartInfo& info, const std::string& timestamp) {
    std::string code;
    for (char c : info.gameCode) code += ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) ? c : '_';
    return (code.empty() ? std::string("GBA") : code) + "_" + timestamp + ".sav";
}

}  // namespace pktool::saves::gba
