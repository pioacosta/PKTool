#include "core/data/events4.h"

#include <cstring>

#include "core/data/card_pack.h"
#include "core/util/bytes.h"

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

bool findWholeCard4(const Event4& card, const std::vector<std::string>& dirs, uint8_t out[kWholePcdSize]) {
    return findInCardPack(dirs, kCardPack4, "PKG4", kWholePcdSize, [&card](const uint8_t* whole) {
        return util::readU16(whole + 0x150) == card.cardId &&
               std::memcmp(whole + 8, card.templatePk4, sizeof card.templatePk4) == 0;
    }, out);
}

}  // namespace pktool::data
