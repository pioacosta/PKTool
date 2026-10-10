#include "core/pokemon/gb_common.h"

#include "core/data/personal.h"

namespace pktool::pokemon::gb {
namespace {

// Smallest n with n * n >= value, at most 255 (the games' square root table).
unsigned statExpTerm(uint16_t statExp) {
    unsigned root = 0;
    while (root < 255 && root * root < statExp) ++root;
    return root >> 2;
}

}  // namespace

uint16_t packDvs(const uint8_t ivs[kStatCount]) {
    auto clamp = [](uint8_t v) { return static_cast<uint16_t>(v > kDvMax ? kDvMax : v); };
    return static_cast<uint16_t>((clamp(ivs[kAtk]) << 12) | (clamp(ivs[kDef]) << 8) | (clamp(ivs[kSpe]) << 4) |
                                 clamp(ivs[kSpA]));
}

void unpackDvs(uint16_t dv16, uint8_t ivs[kStatCount]) {
    ivs[kAtk] = (dv16 >> 12) & 0xF;
    ivs[kDef] = (dv16 >> 8) & 0xF;
    ivs[kSpe] = (dv16 >> 4) & 0xF;
    ivs[kSpA] = ivs[kSpD] = dv16 & 0xF;
    ivs[kHp] = hpDv(ivs);
}

uint8_t hpDv(const uint8_t ivs[kStatCount]) {
    return static_cast<uint8_t>(((ivs[kAtk] & 1) << 3) | ((ivs[kDef] & 1) << 2) | ((ivs[kSpe] & 1) << 1) |
                                (ivs[kSpA] & 1));
}

bool isShinyDvs(const uint8_t ivs[kStatCount]) {
    return ivs[kDef] == 10 && ivs[kSpe] == 10 && ivs[kSpA] == 10 && (ivs[kAtk] & 2) != 0;
}

Gender genderFromDvs(uint16_t species, const uint8_t ivs[kStatCount]) {
    const data::PersonalGb* info = data::personalGb2(species, true);
    if (info == nullptr) return Gender::Genderless;
    switch (info->genderRatio) {
        case data::kGenderRatioGenderless: return Gender::Genderless;
        case data::kGenderRatioFemaleOnly: return Gender::Female;
        case data::kGenderRatioMaleOnly: return Gender::Male;
        default: return ivs[kAtk] > (info->genderRatio >> 4) ? Gender::Male : Gender::Female;
    }
}

uint8_t unownForm(const uint8_t ivs[kStatCount]) {
    unsigned value = ((ivs[kAtk] & 6) << 5) | ((ivs[kDef] & 6) << 3) | ((ivs[kSpe] & 6) << 1) | ((ivs[kSpA] & 6) >> 1);
    return static_cast<uint8_t>(value / 10);
}

void calcStats(uint8_t format, uint16_t species, bool yellowOrCrystal, const uint8_t ivs[kStatCount],
               const uint16_t statExp[kStatCount], uint8_t level, uint16_t out[kStatCount]) {
    const data::PersonalGb* info = data::personalGb(format, species, yellowOrCrystal);
    for (int s = 0; s < kStatCount; ++s) out[s] = 0;
    if (info == nullptr) return;
    for (int s = 0; s < kStatCount; ++s) {
        const int stat = s == kSpD && format == 1 ? kSpA : s;  // one Special in Gen I
        const unsigned dv = s == kHp ? hpDv(ivs) : ivs[stat];
        const unsigned exp = statExp[stat == kSpD ? kSpA : stat];  // the Special Stat Exp covers both
        const unsigned value = ((info->baseStats[s] + dv) * 2 + statExpTerm(static_cast<uint16_t>(exp))) * level / 100;
        out[s] = static_cast<uint16_t>(s == kHp ? value + level + 10 : value + 5);
    }
}

uint8_t levelFor(uint8_t format, uint16_t species, uint32_t exp) {
    const data::PersonalGb* info = data::personalGb(format, species, true);
    if (info == nullptr) return 1;
    return data::levelForExp(static_cast<data::GrowthRate>(info->growthRate), exp);
}

uint32_t expFor(uint8_t format, uint16_t species, uint8_t level) {
    const data::PersonalGb* info = data::personalGb(format, species, true);
    if (info == nullptr) return 0;
    return data::expForLevel(static_cast<data::GrowthRate>(info->growthRate), level);
}

}  // namespace pktool::pokemon::gb
