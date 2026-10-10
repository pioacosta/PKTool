#pragma once

#include <cstdint>

// Gen V Dream World (Entree Forest) encounters and B2/W2 N's Pokemon,
// generated from PKHeX (tools/gen_data.py --only dreamworld5).
namespace pktool::data {

constexpr uint8_t kDwAny = 0xFF;  // DreamWorld5::form / gender: no restriction

// One Pokemon Global Link capture. The Dream World gives its species at
// `level` with at most one of the special `moves` (0 = unused slot) on top of
// what it can learn; the hidden ability is implied for species that have one.
struct DreamWorld5 {
    uint16_t species;
    uint16_t moves[3];
    uint8_t level;
    uint8_t form;       // kDwAny unless the species has several Dream World forms
    uint8_t gender;     // 0 male, 1 female, 2 genderless, kDwAny random
    uint8_t versions;   // VersionBit5 mask of the games that can receive it
    uint8_t languages;  // bit 0 Japanese, 1 Korean, 2 international (7 = all)
};

// N's Pokemon: fixed PID, OT "N", TID 2, SID 0, every IV 30, Poke Ball.
struct NPokemon5 {
    uint32_t pid;
    uint16_t species;
    uint8_t level;
    uint8_t location;
    uint8_t nature;
    uint8_t ability;  // 0 first, 1 second, 2 hidden
};

// Dream Radar (Black 2 / White 2): species and form received at level 5-40
// (a multiple of 5), always in a Dream Ball and never shiny. `anyAbility` is set
// for Rotom, which has no hidden ability to receive; the others have it.
struct DreamRadar5 {
    uint16_t species;
    uint8_t form;
    uint8_t anyAbility;
};

// The Dream Radar entry of this species, or null.
const DreamRadar5* dreamRadar5(uint16_t species);
// All entries.
const DreamRadar5* dreamRadarTable5(int* count);

// Entries of one species: [begin, end).
void dreamWorld5(uint16_t species, const DreamWorld5** begin, const DreamWorld5** end);

// The B2/W2 N's Pokemon table.
const NPokemon5* nPokemon5(int* count);

namespace generated {
extern const DreamWorld5 kDreamWorld5[];
extern const int kDreamWorld5Count;
extern const NPokemon5 kNPokemon5[];
extern const int kNPokemon5Count;
extern const DreamRadar5 kDreamRadar5[];
extern const int kDreamRadar5Count;
}  // namespace generated

}  // namespace pktool::data
