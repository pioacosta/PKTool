#include "core/data/events3.h"

namespace pktool::data {

// The table of events, then the extra rows (event eggs, PCNY and PCJP gifts).
int events3Count() { return generated::kEvents3Count + generated::kEvents3ExtraCount; }

const Event3& event3(int index) {
    return index < generated::kEvents3Count ? generated::kEvents3[index] : generated::kEvents3Extra[index - generated::kEvents3Count];
}

int findEvent3(uint16_t species, int from) {
    for (int i = from < 0 ? 0 : from; i < events3Count(); ++i) {
        if (event3(i).species == species) return i;
    }
    return -1;
}

int gc3Count() { return generated::kGc3Count; }

const Gc3& gc3(int index) { return generated::kGc3[index]; }

const char* gcTrainerName(uint8_t trainer, uint8_t language) {
    if (trainer >= 6 || language >= 8) return "";
    return generated::kGc3TrainerNames[trainer][language];
}

}  // namespace pktool::data
