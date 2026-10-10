#include "core/data/learn12.h"

#include "core/data/gen12.h"

namespace pktool::data {
namespace {

const LevelMoveGb* levelTable(GbGame game) {
    switch (game) {
        case GbGame::RedBlue: return generated::kLevelUpRB;
        case GbGame::Yellow: return generated::kLevelUpY;
        case GbGame::GoldSilver: return generated::kLevelUpGS;
        case GbGame::Crystal: return generated::kLevelUpC;
    }
    return generated::kLevelUpRB;
}

const uint16_t* levelIndex(GbGame game) {
    switch (game) {
        case GbGame::RedBlue: return generated::kLevelUpRBIndex;
        case GbGame::Yellow: return generated::kLevelUpYIndex;
        case GbGame::GoldSilver: return generated::kLevelUpGSIndex;
        case GbGame::Crystal: return generated::kLevelUpCIndex;
    }
    return generated::kLevelUpRBIndex;
}

uint16_t maxSpecies(GbGame game) {
    return game == GbGame::RedBlue || game == GbGame::Yellow ? kMaxSpeciesGen1 : kMaxSpeciesGen2;
}

}  // namespace

void levelUpGb(GbGame game, uint16_t species, const LevelMoveGb** begin, const LevelMoveGb** end) {
    const LevelMoveGb* table = levelTable(game);
    if (species == 0 || species > maxSpecies(game)) {
        *begin = *end = table;
        return;
    }
    const uint16_t* index = levelIndex(game);
    *begin = table + index[species];
    *end = table + index[species + 1];
}

uint8_t levelUpLevelGb(GbGame game, uint16_t species, uint16_t move) {
    const LevelMoveGb* begin;
    const LevelMoveGb* end;
    levelUpGb(game, species, &begin, &end);
    uint8_t best = 0;
    for (const LevelMoveGb* m = begin; m != end; ++m) {
        if (m->move == move && (best == 0 || m->level < best)) best = m->level == 0 ? 1 : m->level;
    }
    return best;
}

void eggMovesGb(GbGame game, uint16_t species, const uint8_t** begin, const uint8_t** end) {
    const bool crystal = game == GbGame::Crystal;
    const uint8_t* table = crystal ? generated::kEggMovesC : generated::kEggMovesGS;
    if (species == 0 || species > kMaxSpeciesGen2 || (game != GbGame::Crystal && game != GbGame::GoldSilver)) {
        *begin = *end = table;
        return;
    }
    const uint16_t* index = crystal ? generated::kEggMovesCIndex : generated::kEggMovesGSIndex;
    *begin = table + index[species];
    *end = table + index[species + 1];
}

bool isEggMoveGb(GbGame game, uint16_t species, uint16_t move) {
    const uint8_t* begin;
    const uint8_t* end;
    eggMovesGb(game, species, &begin, &end);
    for (const uint8_t* m = begin; m != end; ++m) {
        if (*m == move) return true;
    }
    return false;
}

}  // namespace pktool::data
