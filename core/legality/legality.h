#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/data/encounters4.h"
#include "core/pokemon/pokemon.h"

// Legality checks for Generation IV Pokémon.
//
// The checker is deliberately conservative: a finding is only Invalid when
// the data is impossible under every rule it knows. Cases it cannot fully
// verify yet (events, Gen III transfers, special RNG paths) become Warnings,
// so legitimate Pokémon are never reported as illegal.
namespace pktool::legality {

enum class Severity : uint8_t { Valid, Warning, Invalid };

struct Finding {
    Severity severity;
    std::string text;
};

struct Report {
    std::vector<Finding> findings;

    Severity overall() const;
    bool legal() const { return overall() != Severity::Invalid; }
};

Report checkGen4(const pokemon::Pokemon& p);

// Gen V entities (p.format == 5): encounter (B/W/B2/W2 tables, Poke
// Transfer, eggs, trades), learnsets (plus Gen IV ones for transferred
// Pokémon), gender/ability/nature vs PID, the wild PID bit-31 rule, hidden
// abilities, balls, EVs, PP. Dream World and N's Pokémon are checked (legality5_dw.h).
Report checkGen5(const pokemon::Pokemon& p);

// Gen III entities (p.format == 3): native R/S/E/FR/LG encounters, hatched
// eggs (met level 0), trades, gender/ability vs PID, Method 1/2/4 PID/IVs,
// Gen III items and balls. Events (core/legality/events3_check.h) and
// Colosseum/XD Pokemon are matched against PKHeX's tables, including their
// special PID/IV methods; the ones PKTool does not know are warnings.
Report checkGen3(const pokemon::Pokemon& p);

// Game Boy entities (p.format == 1 or 2, legality12.cpp): species, level and
// EXP, types and catch rate (Gen I), held item (Gen II), the encounter (wild,
// static, NPC trade, event gift, Day-Care egg, Crystal's caught data), moves
// and PP, names. Anything that cannot be verified is a warning.
Report checkGb(const pokemon::Pokemon& p);
Report checkGen1(const pokemon::Pokemon& p);
Report checkGen2(const pokemon::Pokemon& p);

// checkGen1 .. checkGen5 depending on p.format.
Report check(const pokemon::Pokemon& p);

// Gen III met locations with a special meaning.
namespace location3 {
constexpr uint16_t kRoute117 = 32;     // R/S/E Day-Care
constexpr uint16_t kFourIsland = 146;  // FR/LG Day-Care
constexpr uint16_t kGiftEgg = 253;
constexpr uint16_t kTrade = 254;
constexpr uint16_t kFateful = 255;
}  // namespace location3

enum class EncounterStatus : uint8_t {
    NotApplicable,  // eggs, hatched, events, trades, Pokewalker, other generations
    Found,
    NotFound,
};

// Finds the wild or static encounter matching the met location, met level
// and origin game, for the species or one of its pre-evolutions.
EncounterStatus findEncounter4(const pokemon::Pokemon& p, const data::Encounter4** match = nullptr,
                               uint16_t* matchedSpecies = nullptr);

// Gen III Pokémon in a Gen III save: the R/S/E/FR/LG encounter at the met
// location and level (findEncounter3 is for Pal Park origins).
EncounterStatus findEncounter3Native(const pokemon::Pokemon& p, const data::Encounter4** match = nullptr,
                                     uint16_t* matchedSpecies = nullptr);

// Gen V: a B/W/B2/W2 encounter of the species line at the met location and
// level (hidden-ability encounters preferred when the Pokémon has it).
EncounterStatus findEncounter5(const pokemon::Pokemon& p, const data::Encounter4** match = nullptr,
                               uint16_t* matchedSpecies = nullptr);

// Same for Pokémon moved from Gen III with Pal Park: a Gen III encounter of
// the origin game at or below the met level (the level at transfer).
EncounterStatus findEncounter3(const pokemon::Pokemon& p, const data::Encounter4** match = nullptr,
                               uint16_t* matchedSpecies = nullptr);

// Unhatched-egg rules shared by the Gen IV and Gen V checkers: level 1, met
// level 0, a Day-Care egg location, a species that hatches from eggs, the
// game's egg name and nickname flag, egg cycles within the species' count.
void checkEgg(const pokemon::Pokemon& p, Report& r);

// Game version IDs stored in Gen III/IV entities.
namespace version {
constexpr uint8_t kRed = 35;  // Game Boy games (PKHeX numbering); Red/Blue and Gold/Silver share a save
constexpr uint8_t kYellow = 38;
constexpr uint8_t kGold = 39;
constexpr uint8_t kCrystal = 41;
constexpr uint8_t kSapphire = 1;
constexpr uint8_t kRuby = 2;
constexpr uint8_t kEmerald = 3;
constexpr uint8_t kFireRed = 4;
constexpr uint8_t kLeafGreen = 5;
constexpr uint8_t kHeartGold = 7;
constexpr uint8_t kSoulSilver = 8;
constexpr uint8_t kDiamond = 10;
constexpr uint8_t kPearl = 11;
constexpr uint8_t kPlatinum = 12;
constexpr uint8_t kColosseumXD = 15;
constexpr uint8_t kWhite = 20;
constexpr uint8_t kBlack = 21;
constexpr uint8_t kWhite2 = 22;
constexpr uint8_t kBlack2 = 23;
}  // namespace version

bool isGen3Origin(uint8_t game);
bool isGen4Origin(uint8_t game);
bool isGen5Origin(uint8_t game);

}  // namespace pktool::legality
