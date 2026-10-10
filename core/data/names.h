#pragma once

#include <cstdint>

// Display names for Gen I-V identifiers, in the language chosen with
// data::setNameLanguage (language.h; English by default). Strings are UTF-8.
// Out-of-range identifiers return "???" instead of failing, so corrupted or
// unsupported data never crashes the UI.
namespace pktool::data {

const char* speciesName(uint16_t species);
const char* moveName(uint16_t move);
const char* abilityName(uint16_t ability);
const char* natureName(uint8_t nature);
const char* itemName(uint16_t item);
const char* ballName(uint8_t ball);

// The compiled-in English names, whatever the language: Showdown text and
// anything else that must not change with the user's choice.
const char* speciesNameEn(uint16_t species);
const char* moveNameEn(uint16_t move);
const char* abilityNameEn(uint16_t ability);
const char* natureNameEn(uint8_t nature);
const char* itemNameEn(uint16_t item);

constexpr uint16_t kMaxSpeciesGen4 = 493;
constexpr uint16_t kMaxSpeciesGen5 = 649;
constexpr uint16_t kMaxItemGen4 = 536;
constexpr uint16_t kMaxAbilityGen4 = 123;

// Ball IDs as stored in Gen III-V entities.
constexpr uint8_t kBallPoke = 4;
constexpr uint8_t kBallCherish = 16;  // last ball known to D/P/Pt
constexpr uint8_t kBallSport = 24;    // last ball added by HG/SS

namespace generated {
extern const char* const kSpeciesNames[];
extern const int kSpeciesNamesCount;
extern const char* const kMoveNames[];
extern const int kMoveNamesCount;
extern const char* const kAbilityNames[];
extern const int kAbilityNamesCount;
extern const char* const kNatureNames[];
extern const int kNatureNamesCount;
extern const char* const kItemNames[];
extern const int kItemNamesCount;
}  // namespace generated

}  // namespace pktool::data
