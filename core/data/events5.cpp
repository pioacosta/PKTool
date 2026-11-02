#include "core/data/events5.h"

#include <cstring>

#include "core/data/card_pack.h"
#include "core/util/bytes.h"

namespace pktool::data {

int events5Count() { return generated::kEvents5Count; }

const Event5& event5(int index) { return generated::kEvents5[index]; }

int findEvent5(uint16_t species, int from) {
    for (int i = from < 0 ? 0 : from; i < generated::kEvents5Count; ++i) {
        if (generated::kEvents5[i].species == species) return i;
    }
    return -1;
}

bool canReceiveEvent5(const Event5& card, uint8_t gameVersion) {
    if (card.pgf[pgf::kOriginGame] != 0) return gameVersion == card.pgf[pgf::kOriginGame];
    if (gameVersion < 20 || gameVersion > 23) return false;
    if (card.restrictVersion == 0) return true;
    return (card.restrictVersion >> (gameVersion - 20)) & 1;
}

EventPeriods eventPeriods(const Event5& card) {
    return {generated::kEventPeriods5 + card.periodFirst, card.periodCount};
}

bool findWholeCard5(const Event5& card, const std::vector<std::string>& dirs, uint8_t out[kWholePgfSize]) {
    return findInCardPack(dirs, kCardPack5, "PKG5", kWholePgfSize, [&card](const uint8_t* whole) {
        return util::readU16(whole + 0xB0) == card.cardId && std::memcmp(whole, card.pgf, sizeof card.pgf) == 0;
    }, out);
}

}  // namespace pktool::data
