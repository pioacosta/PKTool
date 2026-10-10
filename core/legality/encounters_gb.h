#pragma once

#include <cstdint>
#include <vector>

#include "core/data/encounters12.h"
#include "core/pokemon/pokemon.h"

// Gen I/II encounter search (PKHeX EncounterGenerator1/2, simplified): which
// wild slots, static encounters, NPC trades, event gifts and Day-Care eggs a
// species line can come from in a game.
namespace pktool::legality::gb {

// Evolution line of a species within a format's Pokemon (151 / 251), base
// species first; the last entry is `species`.
struct Line {
    uint16_t species[3] = {0, 0, 0};
    int count = 0;
};
Line lineOf(uint8_t format, uint16_t species);
// Lowest level at which the last species of the line can exist when it was met
// as `line.species[from]` (0 = no level requirement: stones, trades, friendship).
uint8_t evolutionLevel(const Line& line, int from);

// Caught-data location of Pokemon hatched in Crystal (PKHeX Locations.HatchLocationC) and of NPC trades.
constexpr uint8_t kHatchLocationCrystal = 127;
constexpr uint8_t kTradeLocationCrystal = 126;

enum class Source : uint8_t { Wild, Static, Trade, Gift, Egg };

struct Candidate {
    Source source = Source::Wild;
    uint16_t species = 0;       // the species met (a member of the line)
    uint8_t levelMin = 0;
    uint8_t levelMax = 0;
    uint8_t versions = 0;       // data::GbVersion bits of the generation
    uint8_t location = 0;       // Gen II location (0 = none / unknown)
    uint8_t time = 0;           // Gen II time of day mask (0 = any)
    uint8_t slotKind = 0;       // data::GbSlotKind for wild slots
    uint8_t flags = 0;          // data::StaticFlag
    uint8_t gender = 255;       // fixed gender (static / trade), 255 = random
    uint16_t dvs = 0xFFFF;      // fixed DVs, 0xFFFF = random
    uint8_t moves[4] = {0, 0, 0, 0};  // fixed moves (all zero: level-up moves)
    int row = -1;               // index in the generated table (statics, trades, gifts)
    bool fromGift = false;      // row indexes the gift table
    bool treeBad = false;       // headbutt slot whose tree the trainer ID cannot reach
};

// Bit mask of the games a Pokemon may come from: `hint` is the save's game
// version (35 Red/Blue, 38 Yellow, 39 Gold/Silver, 41 Crystal), 0 = any.
uint8_t versionMask(uint8_t format, uint8_t hint, bool japanese);

// Every encounter of the line in the games of `versions` (a Day-Care egg is a
// candidate of the base species in Gen II).
void findCandidates(uint8_t format, const Line& line, uint8_t versions, std::vector<Candidate>& out);

// Whether the species can hatch from a Gen II egg.
bool canHatchGb(uint16_t species);

// Lowest level an ordinary new Pokemon of the species can have in the games of
// `versions`: wild, static and egg encounters (not NPC trades or event gifts);
// 0 if there is none.
uint8_t lowestEncounterLevel(uint8_t format, uint16_t species, uint8_t versions);

// Level-up moves of `species` at `level` in the game: Gen I wild Pokemon start
// with the species' initial moves, then the last four learned moves are kept.
void defaultMoves(uint8_t format, uint16_t species, uint8_t level, bool yellowOrCrystal, uint16_t moves[4]);

// The ordinary encounter (wild, static or egg, not a trade or gift) a new
// Pokemon of the line at `level` is created from: the most evolved stage that
// can be met at or below `level`. Returns false if there is none.
struct Start {
    int index = 0;            // position in the line of the species met
    uint8_t level = 0;        // level at the encounter
    Candidate candidate;
};
bool startOf(uint8_t format, const Line& line, uint8_t versions, uint8_t level, Start* out);

// Moves a Pokemon of `line` has at `level` after starting at `start`: the
// start's wild moves, then the level-up moves of every stage up to `level`.
void movesFrom(uint8_t format, const Line& line, const Start& start, uint8_t level, bool yellowOrCrystal,
               uint16_t moves[4]);

// Gen II Pokemon with Crystal's caught data (met level, location or time of day).
bool hasCaughtData(const pokemon::Pokemon& p);
// Gen I catch rate that fits the species line (either version's rate of any stage).
bool catchRateFits(const pokemon::Pokemon& p, const Line& line);

// Everything the checker and LEGALIZE need to know about where a Pokemon can
// come from. `found` holds every encounter of the line in its games, `usable`
// those it can still be (level, egg state, trade OT, fixed DVs / shininess /
// gender), `pool` those that also fit Crystal's caught data (or `usable` if
// none does: dataMismatch), `ordered` the pool by preference (wild, static,
// trade, egg, event gift). Do not copy: `ordered` points into `pool`.
struct Analysis {
    Line line;
    bool crystalData = false;
    bool tradeback = false;  // a Gen I Pokemon that has been through Gen II
    bool tradeOt = false;
    bool dataMismatch = false;
    bool levelTooLow = false;
    uint8_t lowest = 0;  // lowest level any encounter needs
    uint8_t versions = 0;
    std::vector<Candidate> found;
    std::vector<Candidate> usable;
    std::vector<Candidate> pool;
    std::vector<const Candidate*> ordered;
};
void analyse(const pokemon::Pokemon& p, Analysis& a);

// Whether the move can be known by the Pokemon: learned by a member of the
// line (level-up, TM/HM, tutor), an egg move when it comes from an egg, or one
// of the encounter's own moves (`c` may be null).
bool moveLearnable(const pokemon::Pokemon& p, const Analysis& a, uint16_t move, const Candidate* c,
                   bool* eggMoveOnly = nullptr);

// The Pokemon's DVs as Atk/Def/Spe/Spc word.
uint16_t dvWord(const pokemon::Pokemon& p);

}  // namespace pktool::legality::gb
