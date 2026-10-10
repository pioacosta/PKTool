#include "core/data/events4.h"

namespace pktool::data {

int events4Count() { return generated::kEvents4Count; }

const Event4& event4(int index) { return generated::kEvents4[index]; }

int findEvent4(uint16_t species, int from) {
    for (int i = from < 0 ? 0 : from; i < generated::kEvents4Count; ++i) {
        if (generated::kEvents4[i].species == species) return i;
    }
    return -1;
}

EventPeriods eventPeriods(const Event4& card) {
    return {generated::kEventPeriods4 + card.periodFirst, card.periodCount};
}

}  // namespace pktool::data
