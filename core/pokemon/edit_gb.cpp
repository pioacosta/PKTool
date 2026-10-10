#include "core/pokemon/edit_gb.h"

#include <cstdlib>

#include "core/data/gen12.h"
#include "core/data/language.h"
#include "core/data/moves.h"
#include "core/data/personal.h"
#include "core/pokemon/gb_common.h"

namespace pktool::pokemon::editgb {
namespace {

constexpr uint16_t kUnown = 201;

// Attack DVs that make a Gen II Pokemon shiny when the other DVs are 10.
bool shinyAtk(int atk) {
    return (atk & 2) != 0;
}

// Whether `atk` gives `gender` for the species' ratio.
bool atkMatches(uint16_t species, int atk, Gender gender) {
    const data::PersonalGb* info = data::personalGb2(species, true);
    if (info == nullptr) return false;
    switch (info->genderRatio) {
        case data::kGenderRatioGenderless: return gender == Gender::Genderless;
        case data::kGenderRatioFemaleOnly: return gender == Gender::Female;
        case data::kGenderRatioMaleOnly: return gender == Gender::Male;
        default: return (atk > (info->genderRatio >> 4)) == (gender == Gender::Male);
    }
}

// The Attack DV closest to the current one that gives `gender` (and keeps the
// shiny bit if requested); -1 if there is none.
int pickAtk(uint16_t species, int current, Gender gender, bool needShinyBit) {
    int best = -1;
    for (int atk = 0; atk <= gb::kDvMax; ++atk) {
        if (!atkMatches(species, atk, gender)) continue;
        if (needShinyBit && !shinyAtk(atk)) continue;
        if (best < 0 || std::abs(atk - current) < std::abs(best - current)) best = atk;
    }
    return best;
}

}  // namespace

bool validSpecies(const Pokemon& p, uint16_t species) {
    return species >= 1 && species <= (p.format == 1 ? data::kMaxSpeciesGen1 : data::kMaxSpeciesGen2);
}

bool canHaveGender(uint16_t species, Gender gender) {
    const data::PersonalGb* info = data::personalGb2(species, true);
    if (info == nullptr) return false;
    switch (info->genderRatio) {
        case data::kGenderRatioGenderless: return gender == Gender::Genderless;
        case data::kGenderRatioFemaleOnly: return gender == Gender::Female;
        case data::kGenderRatioMaleOnly: return gender == Gender::Male;
        default: return gender != Gender::Genderless;
    }
}

void refreshDerived(Pokemon& p) {
    p.ivs[kSpD] = p.ivs[kSpA];
    p.ivs[kHp] = gb::hpDv(p.ivs);
    p.statExp[kSpD] = p.statExp[kSpA];
    p.level = gb::levelFor(p.format, p.species, p.exp);
    if (p.format == 2) {
        p.shiny = gb::isShinyDvs(p.ivs);
        p.gender = gb::genderFromDvs(p.species, p.ivs);
        p.form = p.species == kUnown ? gb::unownForm(p.ivs) : 0;
    } else {
        p.shiny = false;
        p.gender = Gender::Genderless;
    }
}

Result setSpecies(Pokemon& p, uint16_t species) {
    if (!validSpecies(p, species)) return Result::Impossible;
    const uint8_t level = p.level;
    p.species = species;
    setLevel(p, level);
    if (p.format == 1) {
        if (const data::PersonalGb* info = data::personalGb1(species, false)) {
            p.catchRate = info->catchRate;
            p.types[0] = info->types[0];
            p.types[1] = info->types[1];
        }
    }
    if (!p.isNicknamed) p.nickname = data::defaultNickname(species, p.language, p.format);
    refreshDerived(p);
    return Result::Ok;
}

void setLevel(Pokemon& p, uint8_t level) {
    if (level < 1) level = 1;
    if (level > 100) level = 100;
    p.exp = gb::expFor(p.format, p.species, level);
    p.level = level;
}

Result setMove(Pokemon& p, int index, uint16_t move) {
    if (index < 0 || index >= 4 || move > data::maxMove(p.format)) return Result::Impossible;
    for (int i = 0; i < 4; ++i) {
        if (i != index && move != 0 && p.moves[i] == move) return Result::Impossible;
    }
    p.moves[index] = move;
    if (move == 0) p.ppUps[index] = 0;
    p.pp[index] = data::maxPpGb(p.format, move, p.ppUps[index]);
    return Result::Ok;
}

void setDv(Pokemon& p, Stat stat, uint8_t value) {
    if (stat == kHp) return;  // derived from the other DVs
    if (value > gb::kDvMax) value = gb::kDvMax;
    p.ivs[stat] = value;
    if (stat == kSpA || stat == kSpD) p.ivs[kSpA] = p.ivs[kSpD] = value;
    refreshDerived(p);
}

void setStatExp(Pokemon& p, Stat stat, uint16_t value) {
    p.statExp[stat] = value;
    if (stat == kSpA || stat == kSpD) p.statExp[kSpA] = p.statExp[kSpD] = value;
}

Result setShiny(Pokemon& p, bool shiny) {
    if (p.format != 2) return Result::Impossible;
    const Gender gender = p.gender;
    if (shiny) {
        const int atk = pickAtk(p.species, p.ivs[kAtk], gender, true);
        if (atk < 0) return Result::Impossible;  // no shiny Attack DV gives this gender
        p.ivs[kAtk] = static_cast<uint8_t>(atk);
        p.ivs[kDef] = p.ivs[kSpe] = p.ivs[kSpA] = p.ivs[kSpD] = 10;
    } else if (gb::isShinyDvs(p.ivs)) {
        p.ivs[kSpA] = p.ivs[kSpD] = 9;  // any other value breaks the pattern and keeps the Attack DV
    }
    refreshDerived(p);
    return Result::Ok;
}

Result setGender(Pokemon& p, Gender gender) {
    if (p.format != 2 || !canHaveGender(p.species, gender)) return Result::Impossible;
    int atk = pickAtk(p.species, p.ivs[kAtk], gender, p.shiny);
    if (atk < 0) {  // no shiny Attack DV has this gender: give up the shine
        atk = pickAtk(p.species, p.ivs[kAtk], gender, false);
        if (atk < 0) return Result::Impossible;
        p.ivs[kSpA] = p.ivs[kSpD] = 9;
    }
    p.ivs[kAtk] = static_cast<uint8_t>(atk);
    refreshDerived(p);
    return Result::Ok;
}

}  // namespace pktool::pokemon::editgb
