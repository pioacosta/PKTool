#include "core/legality/pcjp_table.h"

#include "core/util/lcrng.h"

namespace pktool::legality::rng3 {
namespace {

constexpr uint32_t kTableWeight = 1000;
constexpr uint32_t kEntryWeight = 125;

}  // namespace

PcjpResult pcjpResult(uint16_t seed16) {
    uint32_t seed = seed16;
    seed = util::lcrngNext(seed);
    const uint32_t rand1 = seed >> 16;
    seed = util::lcrngNext(seed);
    const uint32_t rand2 = seed >> 16;
    const uint32_t rand = (rand1 << 16) | rand2;

    // WeightedTable3.GetPeriodicWeight
    const uint32_t high = rand >> 16;
    const uint32_t first = ((high << 2) & 0xFFFF) + high;
    uint32_t second = ((rand & 0xFFFF) << 1) + (first >> 16);
    second += high + (second >> 16);
    const uint32_t result = (kTableWeight * (second & 0xFFFF)) >> 16;

    const uint32_t eighth = result / kEntryWeight;
    PcjpResult r;
    r.wish = (eighth & 1) == 1;
    r.index = eighth >> 1;
    r.shiny = r.index == 0 && (result % kEntryWeight) >= 100;
    return r;
}

uint32_t pcjpIndex(uint16_t species) { return ((static_cast<uint32_t>(species) >> 2) + 1) & 3u; }

bool pcjpTableMatch(uint16_t species, bool wish, bool shiny, uint16_t seed16) {
    const PcjpResult r = pcjpResult(seed16);
    return r.index == pcjpIndex(species) && r.wish == wish && r.shiny == shiny;
}

}  // namespace pktool::legality::rng3
