#pragma once

#include <cstdint>
#include <string>

#include "core/io/file_io.h"

// User preferences, stored as a small text file (default /PKTOOL/settings.ini):
//
//     # PKTool settings
//     sort_order=alphabetical      (or "id")
//     new_level=50
//     animations=on                (or "off")
//     language=ja                  (names: en ja fr it de es ko; only written when not en)
//
// Reading is forgiving: unknown keys, malformed lines and out-of-range values
// are ignored and the default stays. Writing goes through safeWriteFile.
// Backups before every write to a save are not a setting: they are always made.
namespace pktool::io {

struct Settings {
    // Order of the option lists (species, moves, items, natures, balls): by
    // name (false) or by ID (true).
    bool sortById = false;
    // Level of Pokemon made with Add > New Pokemon.
    uint8_t newLevel = 50;
    // Cosmetic UI animations (selection easing, sprite bob, screen fades).
    bool animations = true;
    // Language of the Pokemon names (game language ID, see core/data/language.h).
    uint8_t language = 2;

    bool operator==(const Settings& o) const {
        return sortById == o.sortById && newLevel == o.newLevel && animations == o.animations && language == o.language;
    }
    bool operator!=(const Settings& o) const { return !(*this == o); }
};

constexpr uint8_t kMinNewLevel = 1;
constexpr uint8_t kMaxNewLevel = 100;

std::string settingsToText(const Settings& settings);
// Applies the keys found in `text` on top of `settings`.
void parseSettings(const std::string& text, Settings& settings);

// Missing or unreadable file: returns false and leaves `settings` untouched.
bool loadSettings(const std::string& path, Settings& settings);
// Creates the parent directory if needed, then writes the file safely.
WriteResult saveSettings(const std::string& path, const Settings& settings);

}  // namespace pktool::io
