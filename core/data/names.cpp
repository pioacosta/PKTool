#include "core/data/names.h"

#include "core/data/language.h"

namespace pktool::data {
namespace {

const char* lookup(const char* const* table, int count, int index) {
    if (index < 0 || index >= count) {
        return "???";
    }
    return table[index];
}

}  // namespace

const char* speciesNameEn(uint16_t species) {
    return lookup(generated::kSpeciesNames, generated::kSpeciesNamesCount, species);
}

const char* moveNameEn(uint16_t move) {
    return lookup(generated::kMoveNames, generated::kMoveNamesCount, move);
}

const char* abilityNameEn(uint16_t ability) {
    return lookup(generated::kAbilityNames, generated::kAbilityNamesCount, ability);
}

const char* natureNameEn(uint8_t nature) {
    return lookup(generated::kNatureNames, generated::kNatureNamesCount, nature);
}

const char* itemNameEn(uint16_t item) {
    return lookup(generated::kItemNames, generated::kItemNamesCount, item);
}

const char* speciesName(uint16_t species) {
    const char* local = packName(NameTable::Species, species);
    return local ? local : speciesNameEn(species);
}

const char* moveName(uint16_t move) {
    const char* local = packName(NameTable::Move, move);
    return local ? local : moveNameEn(move);
}

const char* abilityName(uint16_t ability) {
    const char* local = packName(NameTable::Ability, ability);
    return local ? local : abilityNameEn(ability);
}

const char* natureName(uint8_t nature) {
    const char* local = packName(NameTable::Nature, nature);
    return local ? local : natureNameEn(nature);
}

const char* itemName(uint16_t item) {
    const char* local = packName(NameTable::Item, item);
    return local ? local : itemNameEn(item);
}

namespace {

// Gen IV/V item that is the Poke Ball with this ball ID (0 = none).
uint16_t ballItem(uint8_t ball) {
    if (ball >= 1 && ball <= 16) return ball;                  // Master Ball ... Cherish Ball
    if (ball >= 17 && ball <= 25) return static_cast<uint16_t>(475 + ball);  // Fast ... Park Ball
    return 0;
}

}  // namespace

const char* ballName(uint8_t ball) {
    if (const char* local = packName(NameTable::Item, ballItem(ball))) return local;
    static const char* const kBalls[] = {
        "(None)",     "Master Ball", "Ultra Ball", "Great Ball",  "Poke Ball",  "Safari Ball",
        "Net Ball",   "Dive Ball",   "Nest Ball",  "Repeat Ball", "Timer Ball", "Luxury Ball",
        "Premier Ball", "Dusk Ball", "Heal Ball",  "Quick Ball",  "Cherish Ball", "Fast Ball",
        "Level Ball", "Lure Ball",   "Heavy Ball", "Love Ball",   "Friend Ball", "Moon Ball",
        "Sport Ball", "Park Ball",
    };
    return lookup(kBalls, static_cast<int>(sizeof(kBalls) / sizeof(kBalls[0])), ball);
}

}  // namespace pktool::data
