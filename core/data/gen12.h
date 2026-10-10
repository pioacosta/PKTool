#pragma once

#include <cstdint>

// Generation I and II (Red/Blue/Yellow, Gold/Silver/Crystal) data, generated
// from PKHeX. Species and moves use the National Dex / move numbers (Gen I
// stores its own internal species order, converted here); the held items of
// the model are Gen IV item IDs, like for Gen III (see itemFromGen2).
namespace pktool::data {

constexpr uint16_t kMaxSpeciesGen1 = 151;
constexpr uint16_t kMaxSpeciesGen2 = 251;
constexpr uint16_t kMaxMoveGen1 = 165;
constexpr uint16_t kMaxMoveGen2 = 251;

// Per-species data of Gen I (Red/Blue, Yellow) and Gen II (Gold/Silver,
// Crystal). Gen I has one Special stat: baseStats[SpA] == baseStats[SpD].
struct PersonalGb {
    uint8_t baseStats[6];  // HP, Atk, Def, Spe, SpA, SpD
    uint8_t types[2];      // the games' own type IDs (Normal 0 ... Bug 7, Ghost 8, Steel 9, Fire 20 ...)
    uint8_t catchRate;
    uint8_t baseExp;
    uint8_t growthRate;    // same numbering as data::GrowthRate
    uint8_t genderRatio;   // Gen II only (0 male only, 254 female only, 255 genderless, else threshold)
    uint8_t hatchCycles;   // Gen II only
    uint8_t eggGroups[2];  // Gen II only
    uint8_t initMoves[4];  // Gen I wild Pokemon start with these
    uint8_t machines[8];   // TM01-50, then the HMs (Gen I: 5, Gen II: 7)
};

const PersonalGb* personalGb1(uint16_t species, bool yellow);
const PersonalGb* personalGb2(uint16_t species, bool crystal);
// Format 1 or 2; `yellowOrCrystal` picks the Yellow / Crystal table.
const PersonalGb* personalGb(uint8_t format, uint16_t species, bool yellowOrCrystal);

// Gen I internal species ID <-> National Dex number (0 if unused / above 151).
uint16_t speciesFromGen1(uint8_t internal);
uint8_t speciesToGen1(uint16_t national);

// Gen II item ID <-> Gen IV item ID (0 when the item has no counterpart).
uint16_t itemFromGen2(uint8_t item2);
uint8_t itemToGen2(uint16_t item4);
// Whether a Gen II item ID can be held by a Pokemon.
bool isHeldItem2(uint8_t item2);
// Time Capsule: Gen II held item <-> Gen I catch rate byte.
uint8_t catchRateFromItem2(uint8_t item2);
uint8_t itemFromCatchRate(uint8_t rate);
// Whether a catch rate byte is also a Gen II held item ID.
bool catchRateIsItem(uint8_t rate);

// Base PP of a Gen I (move <= 165) or Gen II (move <= 251) move; 0 if unknown.
uint8_t basePpGb(uint8_t format, uint16_t move);
// PP with PP Ups: each Up adds a fifth of the base PP, at most 7 (Gen I/II).
uint8_t maxPpGb(uint8_t format, uint16_t move, uint8_t ppUps);

// Machine move IDs: TM01-TM50 then the HMs.
uint16_t machineMoveGb(uint8_t format, int index);
int machineCountGb(uint8_t format);
// Whether the species can learn the move with a TM or HM.
bool learnsMachineGb(const PersonalGb& personal, uint8_t format, uint16_t move);
// Evolution stage of a Gen I species (0 = first stage).
uint8_t evolutionStage1(uint16_t species);

// Type name of the games' own type IDs ("Normal", "Fire" ...; "???" if unknown).
const char* typeNameGb(uint8_t type);

const char* itemNameGen2(uint8_t item2);
const char* itemNameGen1(uint8_t item1);
const char* locationName2(uint16_t location);

// Unicode of a byte in the Gen I/II character sets (0xFFFF = terminator /
// unused). `language` is a game language ID (1 Japanese, 2 English, 3 French,
// 4 Italian, 5 German, 7 Spanish).
uint16_t charGb(uint8_t format, uint8_t language, uint8_t byte);

namespace generated {
extern const uint16_t kGen1CharsEn[256];
extern const uint16_t kGen1CharsJp[256];
extern const uint16_t kGen2KorSingle[256];
extern const uint16_t kGen2KorHangul[11 * 256];
extern const uint16_t kGen2CharsEn[256];
extern const uint16_t kGen2CharsFre[256];
extern const uint16_t kGen2CharsIta[256];
extern const uint16_t kGen2CharsJp[256];
extern const uint8_t kSpecies1ToNational[256];
extern const uint8_t kSpeciesToInternal1[152];
extern const uint16_t kItem2To4[256];
extern const uint8_t kHeldItems2[];
extern const int kHeldItems2Count;
extern const uint8_t kCatchRateIsItem[32];
extern const char* const kItemNames1[];
extern const int kItemNames1Count;
extern const char* const kItemNames2[];
extern const int kItemNames2Count;
extern const char* const kLocations2[];
extern const int kLocations2Count;
extern const uint8_t kPp1[];
extern const int kPp1Count;
extern const uint8_t kPp2[];
extern const int kPp2Count;
extern const uint8_t kMachines1[];
extern const int kMachines1Count;
extern const uint8_t kMachines2[];
extern const int kMachines2Count;
extern const uint8_t kEvoStage1[];
extern const PersonalGb kPersonalRB[];
extern const int kPersonalRBCount;
extern const PersonalGb kPersonalY[];
extern const int kPersonalYCount;
extern const PersonalGb kPersonalGS[];
extern const int kPersonalGSCount;
extern const PersonalGb kPersonalC[];
extern const int kPersonalCCount;
}  // namespace generated

}  // namespace pktool::data
