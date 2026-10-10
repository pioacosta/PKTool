#pragma once

#include <cstdint>

namespace pktool::saves {

// Save-file families. Paired versions (e.g. Diamond/Pearl) share a format and
// cannot be told apart from the save alone.
enum class Game : uint8_t {
    Unknown,
    RubySapphire,
    Emerald,
    FireRedLeafGreen,
    DiamondPearl,
    Platinum,
    HeartGoldSoulSilver,
    BlackWhite,
    Black2White2,
    RedBlue,  // Red, Blue (and the Japanese Green) cannot be told apart
    Yellow,
    GoldSilver,  // Gold and Silver cannot be told apart
    Crystal,
};

const char* gameName(Game game);
uint8_t gameGeneration(Game game);

}  // namespace pktool::saves
