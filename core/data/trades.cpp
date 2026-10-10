#include "core/data/trades.h"

namespace pktool::data {

int trades345Count() { return generated::kTrades345Count; }

const Trade345& trade345(int index) { return generated::kTrades345[index]; }

}  // namespace pktool::data
