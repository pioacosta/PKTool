#include "core/saves/game.h"

namespace pktool::saves {

const char* gameName(Game game) {
    switch (game) {
        case Game::RubySapphire:
            return "Ruby/Sapphire";
        case Game::Emerald:
            return "Emerald";
        case Game::FireRedLeafGreen:
            return "FireRed/LeafGreen";
        case Game::DiamondPearl:
            return "Diamond/Pearl";
        case Game::Platinum:
            return "Platinum";
        case Game::HeartGoldSoulSilver:
            return "HeartGold/SoulSilver";
        case Game::BlackWhite:
            return "Black/White";
        case Game::Black2White2:
            return "Black 2/White 2";
        case Game::RedBlue:
            return "Red/Blue";
        case Game::Yellow:
            return "Yellow";
        case Game::GoldSilver:
            return "Gold/Silver";
        case Game::Crystal:
            return "Crystal";
        case Game::Unknown:
            break;
    }
    return "Unknown";
}

uint8_t gameGeneration(Game game) {
    switch (game) {
        case Game::RedBlue:
        case Game::Yellow:
            return 1;
        case Game::GoldSilver:
        case Game::Crystal:
            return 2;
        case Game::RubySapphire:
        case Game::Emerald:
        case Game::FireRedLeafGreen:
            return 3;
        case Game::DiamondPearl:
        case Game::Platinum:
        case Game::HeartGoldSoulSilver:
            return 4;
        case Game::BlackWhite:
        case Game::Black2White2:
            return 5;
        case Game::Unknown:
            break;
    }
    return 0;
}

}  // namespace pktool::saves
