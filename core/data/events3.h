#pragma once

#include <cstdint>

// Gen III events and Colosseum/XD encounters, generated from PKHeX
// (EncountersWC3, Encounters3Colo, Encounters3XD, MystryMew).
//
// Event3: gifts handed out by distributions (Wishmaker Jirachi, Channel
// Jirachi, MYSTRY Mew, the 10th Anniversary legendaries...). Every one is
// stamped with a fixed trainer, origin game, met level and PID method.
// Gc3: the Pokemon of Pokemon Colosseum and XD that can reach a GBA save
// (gifts, starters, trades, Shadow Pokemon, Poke Spot slots).
namespace pktool::data {

// How the PID and IVs of an event gift were generated (PKHeX PIDType):
// B-A-C-D is the "reversed" Method 1, with its PID halves swapped.
enum EventMethod3 : uint8_t {
    kEvBacdR,      // BACD, 16-bit origin seed
    kEvBacdRA,     // BACD, 16-bit origin seed, shiny PIDs pushed to non-shiny
    kEvBacdRbcd,   // BACD forced shiny, origin seed from a clock digit sum (0-213)
    kEvBacdTa,     // BACD whose origin seed was drawn by a table roll, anti-shiny
    kEvBacdUax,    // BACD with the PID forced non-shiny (xor), any origin seed
    kEvBacdM,      // MYSTRY Mew: one of 86 seeds (and 4 later frames of each)
    kEvChannel,    // Pokemon Channel Jirachi (XD RNG)
    kEvBacdU,      // BACD, any origin seed, no anti-shiny (Pokemon Box eggs)
    kEvBacdTs,     // BACD forced shiny whose 16-bit seed came from the PCJP 5th Anniversary table
    kEvMethod2,    // Method 2 (a VBlank between PID and IVs; Methods 1 and 4 also occur): Wish and PokePark eggs
};

enum EventShiny3 : uint8_t { kEvShinyRandom, kEvShinyNever, kEvShinyAlways };

// PKHeX's GiftGender3: the OT gender, fixed or drawn from the RNG.
enum EventOtGender3 : uint8_t {
    kEvOtRecipient,
    kEvOtOnly0,
    kEvOtOnly1,
    kEvOtRandAlgo,
    kEvOtRandD3,
    kEvOtRandS3,
    kEvOtRandS7,
    kEvOtRandSG15,
    kEvOtRandD3Male,
    kEvOtRandD3Female,
};

enum : uint8_t {
    kEvFateful = 1,
    kEvNationalRibbon = 2,
    kEvEgg = 4,      // an egg (unhatched: met location 255, met level 0 or 5; hatched: any hatch location)
    kEvTid3000 = 8,  // trainer ID is random in 1-2999 (Pokemon Center New York)
};

constexpr uint16_t kEventIdAny = 0xFFFF;

struct Event3 {
    uint16_t species;
    uint8_t level;
    uint8_t metLevel;   // 0 for the Channel Jirachi
    uint8_t versions;   // VersionBit3 mask: the games the gift is stamped with
    uint8_t language;   // 0 = any language but Japanese
    uint8_t method;     // EventMethod3
    uint8_t shiny;      // EventShiny3
    uint8_t otGender;   // EventOtGender3
    uint8_t flags;      // kEvFateful, kEvNationalRibbon
    uint16_t tid;       // kEventIdAny = random
    uint16_t sid;       // kEventIdAny = random
    uint16_t moves[4];
    const char* ot;     // UTF-8, empty = the recipient; "a|b" lists alternatives (PCNY, PCJP)
    const char* title;  // for the Add menu, e.g. "Jirachi [WISHMKR] (ENG)"
};

int events3Count();
const Event3& event3(int index);  // the table sorted by species, then the extra rows (eggs, PCNY, PCJP)

// First event of `species` at or after `from`, -1 if none.
int findEvent3(uint16_t species, int from = 0);

// Colosseum / XD encounter kinds (what decides the PID rules).
enum GcKind : uint8_t {
    kGcColoStarter,  // Umbreon / Espeon
    kGcColoGift,     // Duking's Plusle
    kGcColoShadow,
    kGcColoEReader,  // Japanese e-Reader shadows
    kGcXdGift,       // Eevee, Chikorita, Cyndaquil, Totodile
    kGcXdTrade,
    kGcXdShadow,
    kGcXdPokeSpot,
    kGcColoBonus,    // Colosseum bonus-disc gifts: Pikachu, Celebi (Japanese only) and the Mt. Battle Ho-Oh
};

enum : uint8_t { kGcFateful = 1, kGcNeverShiny = 2, kGcPokeBall = 4, kGcFemaleOt = 8, kGcJapanOnly = 16 };

struct Gc3 {
    uint16_t species;
    uint8_t level;
    uint8_t location;
    uint8_t kind;      // GcKind
    uint8_t flags;     // kGcFateful, kGcNeverShiny, kGcPokeBall
    uint16_t tid;      // fixed trainer ID (trades and gifts), 0 = the player's
    uint8_t trainer;   // 0 = the player, 1 = Duking, 2 = Hordel, 3-5 = bonus-disc Pikachu, Celebi, Mt. Battle Ho-Oh
    uint8_t nickname;  // 1 = Zaprong (Elekid), 0 = the species name
    uint16_t moves[4];
};

int gc3Count();
const Gc3& gc3(int index);  // sorted by species

// Trainer name of an NPC (1 = Duking, 2 = Hordel, 3-5 bonus-disc OTs) in a language, "" if none.
const char* gcTrainerName(uint8_t trainer, uint8_t language);

namespace generated {
extern const Event3 kEvents3[];
extern const int kEvents3Count;
// Event eggs, PCNY and PCJP gifts (tools/gen_data.py --only eggs3): events3Count() continues with them.
extern const Event3 kEvents3Extra[];
extern const int kEvents3ExtraCount;
extern const Gc3 kGc3[];
extern const int kGc3Count;
extern const char* const kGc3TrainerNames[6][8];
extern const uint16_t kMystryMewSeeds[];
extern const int kMystryMewSeedCount;
}  // namespace generated

}  // namespace pktool::data
