#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Languages of the Pokemon names (species, moves, abilities, natures, items).
//
// English is compiled in (names.cpp). The other languages PKHeX provides live
// in language packs, assets/nitrofs/lang/<code>.bin (tools/gen_data.py --only
// langs), loaded from the first asset directory that has them. Exactly one
// pack is active at a time (the user's choice); a second one is loaded on
// demand to give a Pokemon its own language's default nickname.
// Language IDs are the games' (Pokemon::language).
namespace pktool::data {

constexpr uint8_t kLangJapanese = 1;
constexpr uint8_t kLangEnglish = 2;
constexpr uint8_t kLangFrench = 3;
constexpr uint8_t kLangItalian = 4;
constexpr uint8_t kLangGerman = 5;
constexpr uint8_t kLangSpanish = 7;
constexpr uint8_t kLangKorean = 8;

// Selectable languages, in the order the Settings screen lists them.
constexpr uint8_t kNameLanguages[] = {kLangEnglish, kLangJapanese, kLangFrench, kLangItalian,
                                      kLangGerman,  kLangSpanish,  kLangKorean};
constexpr int kNameLanguageCount = static_cast<int>(sizeof(kNameLanguages));

bool isNameLanguage(uint8_t language);
// "en", "ja", ... ("" for an unsupported ID) and back (0 if unknown).
const char* languageCode(uint8_t language);
uint8_t languageFromCode(const std::string& code);
// English name of the language ("Japanese").
const char* languageName(uint8_t language);
// The language written in itself, UTF-8; Latin names for the Latin ones.
const char* languageNativeName(uint8_t language);

// Directories searched (in order) for "lang/<code>.bin". Drops the packs
// loaded on demand (the active one stays until the language is set again).
void setLanguageDirs(const std::vector<std::string>& directories);

// Makes `language` the one of every name accessor. False (and nothing
// changes) when its pack cannot be loaded; English never fails.
bool setNameLanguage(uint8_t language);
uint8_t nameLanguage();
// True when the pack of `language` loads (English: always).
bool languageAvailable(uint8_t language);

// Species name in any language, regardless of the active one (English when
// the language is unknown or its pack is missing).
const char* speciesNameIn(uint8_t language, uint16_t species);

// Name the game stores in the nickname field of a Pokemon that has none:
// the species name in its language, in capitals for Gen III and IV (not for
// Japanese and Korean), French without the accents the old games lack.
// format = 1-5 (Gen I/II names are in capitals too, MR.MIME has no space).
std::string defaultNickname(uint16_t species, uint8_t language, uint8_t format);

// --- used by names.cpp -----------------------------------------------------
enum class NameTable : uint8_t { Species, Move, Ability, Nature, Item, ItemGen3 };
// Active-language name, or nullptr when English is active or the pack has no
// (non-empty) name for `id`.
const char* packName(NameTable table, uint16_t id);

}  // namespace pktool::data
