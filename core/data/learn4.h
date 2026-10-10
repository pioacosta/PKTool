#pragma once

#include <cstddef>
#include <cstdint>

// Gen IV move learnability (D/P, Pt, HG/SS combined), generated from PKHeX.
namespace pktool::data {

enum LearnSource : uint8_t {
    kLearnLevelUp = 1,
    kLearnMachine = 2,  // TM or HM
    kLearnTutor = 4,
    kLearnEgg = 8,
};

struct LearnEntry {
    uint16_t move;
    uint8_t sources;   // LearnSource flags
    uint8_t minLevel;  // lowest level-up level (only with kLearnLevelUp)
};

// Result of a learnset lookup: the entry, decoded from the packed tables, or nothing.
// Usable like the pointer it replaced (`if (hit)`, `hit->sources`, `hit != nullptr`).
class LearnHit {
public:
    LearnHit() = default;
    explicit LearnHit(const LearnEntry& entry) : entry_(entry), found_(true) {}
    explicit operator bool() const { return found_; }
    const LearnEntry* operator->() const { return &entry_; }
    const LearnEntry& operator*() const { return entry_; }
    friend bool operator==(const LearnHit& h, std::nullptr_t) { return !h.found_; }
    friend bool operator!=(const LearnHit& h, std::nullptr_t) { return h.found_; }

private:
    LearnEntry entry_ = {0, 0, 0};
    bool found_ = false;
};

// The learnset tables are byte streams, one run per species ([index[s], index[s + 1])),
// with the moves in ascending order. Each entry is:
//   - the move delta (against the previous move, 0 at the start of the species) from 1 to
//     15 and the LearnSource bits in one byte: delta << 4 | sources;
//   - for larger deltas: the byte `sources`, then delta - 16 (0-254), or 255 followed by
//     the delta as a little-endian u16;
//   - one byte with the lowest level when the sources include kLearnLevelUp.
// tools/gen_data.py (pack_learnsets) writes them; this finds `move` in a run.
LearnHit decodeLearn(const uint8_t* begin, const uint8_t* end, uint16_t move);

// How `species` itself can learn `move` (no pre-evolutions).
LearnHit findLearn4(uint16_t species, uint16_t move);

// Pre-evolution of `species` in Gen IV, or 0.
uint16_t preEvolution4(uint16_t species);

// Minimum level at which `species` can exist by evolving (0 = no level
// requirement: friendship, trade, stone, or not evolved).
uint8_t evolveLevel4(uint16_t species);

// First species of the evolution chain (the one that hatches from eggs).
uint16_t baseSpecies4(uint16_t species);

namespace generated {
extern const uint8_t kLearn4[];
extern const uint32_t kLearn4Index[];
extern const uint16_t kPreEvolution4[];
extern const uint8_t kEvolveLevel4[];
}  // namespace generated

}  // namespace pktool::data
