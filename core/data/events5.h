#pragma once

#include <cstdint>

#include "core/data/event_period.h"

// Gen V Pokemon Wonder Cards (PGF), generated from PKHeX's Mystery Gift
// database. Each card keeps the Pokemon part of the PGF (bytes 0x00-0x5F);
// the pgf:: offsets below describe it.
namespace pktool::data {

struct Event5 {
    uint16_t cardId;
    uint16_t species;
    uint8_t restrictVersion;   // bit 0 White, 1 Black, 2 White 2, 3 Black 2; 0 = any
    uint8_t restrictLanguage;  // language that could receive it; 0 = any
    uint8_t date[3];           // year - 2000, month, day; all 0 = received on the day
    const char* title;         // card title and region, e.g. "Get Pikachu! (ENG)"
    uint16_t periodFirst;      // distribution periods: kEventPeriods5[periodFirst, +periodCount)
    uint8_t periodCount;       // 0 = unknown
    uint8_t pgf[0x60];
};

namespace pgf {
constexpr int kTid = 0x00;
constexpr int kSid = 0x02;
constexpr int kOriginGame = 0x04;   // 0 = the receiving game
constexpr int kPid = 0x08;          // 0 = random
constexpr int kRibbons = 0x0C;      // two bytes, see gift5.cpp
constexpr int kBall = 0x0E;
constexpr int kHeldItem = 0x10;
constexpr int kMoves = 0x12;        // four u16; all 0 = level-up moves
constexpr int kSpecies = 0x1A;
constexpr int kForm = 0x1C;
constexpr int kLanguage = 0x1D;     // 0 = the receiving game's
constexpr int kNickname = 0x1E;     // 11 UTF-16 chars, empty = none
constexpr int kNature = 0x34;       // 0xFF = random
constexpr int kGender = 0x35;       // 2 = random
constexpr int kAbilityType = 0x36;  // 0/1/2 fixed slot, 3 = random 0/1, 4 = random 0/1/hidden
constexpr int kPidType = 0x37;      // 0 = never shiny, 1 = random, 2 = always shiny
constexpr int kEggLocation = 0x38;
constexpr int kMetLocation = 0x3A;
constexpr int kMetLevel = 0x3C;
constexpr int kContest = 0x3D;      // six contest stats
constexpr int kIvs = 0x43;          // HP, Atk, Def, Spe, SpA, SpD; > 31 = random
constexpr int kOtName = 0x4A;       // 8 UTF-16 chars
constexpr int kOtGender = 0x5A;     // 3 = the receiving trainer's
constexpr int kLevel = 0x5B;        // 0 = random
constexpr int kIsEgg = 0x5C;
}  // namespace pgf

int events5Count();
const Event5& event5(int index);  // sorted by species, then card ID

// Index of the first card for `species` at or after `from`, -1 if none.
int findEvent5(uint16_t species, int from = 0);

// Whether a game (version ID 20-23) can receive the card.
bool canReceiveEvent5(const Event5& card, uint8_t gameVersion);

// The card's distribution periods (none when unknown).
EventPeriods eventPeriods(const Event5& card);

namespace generated {
extern const Event5 kEvents5[];
extern const int kEvents5Count;
extern const EventPeriod kEventPeriods5[];
}  // namespace generated

}  // namespace pktool::data
