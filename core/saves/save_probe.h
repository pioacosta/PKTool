#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/saves/game.h"

// Finding Pokémon saves on the SD card and summarising them for the browser.
namespace pktool::saves {

struct SaveSummary {
    std::string path;
    Game game = Game::Unknown;
    uint8_t version = 0;       // game version ID (Diamond, Pearl, ..., Black 2)
    std::string trainer;
    uint16_t trainerId = 0;
    std::vector<uint16_t> party;  // species
    long fileSize = 0;            // file size and modification time when it was read:
    long modified = 0;            // findPokemonSaves() reuses the summary while both match
};

// Loads `path` if it is a supported Pokémon save (other files, including
// other games' saves, are rejected quickly by size or format).
bool probeSave(const std::string& path, SaveSummary& out);

// Same result as probeSave() but reading the whole file (the reference the partial
// reads of probeSave() must agree with; probeSave() falls back to it when in doubt).
bool probeSaveFull(const std::string& path, SaveSummary& out);

// Every supported Pokémon save up to `maxDepth` folders below `root`. Files whose size and
// modification time match an entry of `previous` (an earlier scan) are not read again.
std::vector<SaveSummary> findPokemonSaves(const std::string& root, int maxDepth,
                                          const std::vector<SaveSummary>* previous = nullptr);

// "Diamond", "Black 2", ... ("Pokemon" if unknown).
const char* versionName(uint8_t version);

// Species on the box art of a game version (Dialga for Diamond, ...).
uint16_t coverSpecies(uint8_t version);

}  // namespace pktool::saves
