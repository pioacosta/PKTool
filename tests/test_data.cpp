#include <cstring>

#include "core/data/names.h"
#include "core/data/personal.h"
#include "tests/test.h"

using namespace pktool;

TEST(names_lookup) {
    CHECK(std::strcmp(data::speciesName(445), "Garchomp") == 0);
    CHECK(std::strcmp(data::speciesName(649), "Genesect") == 0);
    CHECK(std::strcmp(data::moveName(89), "Earthquake") == 0);
    CHECK(std::strcmp(data::abilityName(24), "Rough Skin") == 0);
    CHECK(std::strcmp(data::natureName(13), "Jolly") == 0);
    CHECK(std::strcmp(data::itemName(1), "Master Ball") == 0);
}

TEST(names_out_of_range_are_safe) {
    CHECK(std::strcmp(data::speciesName(650), "???") == 0);
    CHECK(std::strcmp(data::moveName(0xFFFF), "???") == 0);
    CHECK(std::strcmp(data::natureName(25), "???") == 0);
}

TEST(personal4_garchomp) {
    const data::PersonalInfo* info = data::personal4(445);
    CHECK(info != nullptr);
    CHECK_EQ(info->baseStats[0], 108);
    CHECK_EQ(info->baseStats[1], 130);
    CHECK_EQ(info->growthRate, static_cast<uint8_t>(data::GrowthRate::Slow));
    CHECK_EQ(info->abilities[0], 8);  // Sand Veil
    CHECK(data::personal4(0) == nullptr);
    CHECK(data::personal4(494) == nullptr);
}

TEST(experience_tables) {
    using data::GrowthRate;
    CHECK_EQ(data::expForLevel(GrowthRate::MediumFast, 100), 1000000u);
    CHECK_EQ(data::expForLevel(GrowthRate::MediumSlow, 100), 1059860u);
    CHECK_EQ(data::expForLevel(GrowthRate::Erratic, 100), 600000u);
    CHECK_EQ(data::expForLevel(GrowthRate::Fluctuating, 100), 1640000u);
    CHECK_EQ(data::levelForExp(GrowthRate::MediumFast, 0), 1);
    CHECK_EQ(data::levelForExp(GrowthRate::MediumFast, 7), 1);
    CHECK_EQ(data::levelForExp(GrowthRate::MediumFast, 8), 2);
    CHECK_EQ(data::levelForExp(GrowthRate::MediumFast, 999999), 99);
    CHECK_EQ(data::levelForExp(GrowthRate::MediumFast, 5000000), 100);
}
