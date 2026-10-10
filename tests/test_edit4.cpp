#include <cstring>

#include "core/data/moves.h"
#include "core/data/personal.h"
#include "core/pokemon/edit4.h"
#include "core/pokemon/pk4.h"
#include "core/util/bytes.h"
#include "tests/fixtures.h"
#include "tests/test.h"

using namespace pktool;
using namespace pktool::pokemon;
namespace pk4 = pokemon::pk4;

namespace {

constexpr uint16_t kGarchomp = 445;
constexpr uint16_t kTauros = 128;     // male only
constexpr uint16_t kMagnemite = 81;   // genderless
constexpr uint16_t kRoserade = 407;   // Natural Cure / Poison Point
constexpr uint16_t kLucario = 448;

Pokemon roserade() {
    return pk4::toPokemon(test::kRoseradePk4, pk4::kPartySize);
}

}  // namespace

TEST(edit4_find_pid_satisfies_every_constraint) {
    for (uint8_t nature = 0; nature < 25; nature += 6) {
        for (bool shiny : {false, true}) {
            for (Gender gender : {Gender::Male, Gender::Female}) {
                for (uint8_t slot = 0; slot < 2; ++slot) {
                    uint32_t pid = 0;
                    CHECK(edit4::findPid(kGarchomp, 12345, 54321, nature, shiny, gender, slot, nature * 7 + slot, pid));
                    CHECK_EQ(natureFromPid(pid), nature);
                    CHECK_EQ(isShiny(pid, 12345, 54321), shiny);
                    CHECK(edit4::genderFromPid(kGarchomp, pid) == gender);
                    CHECK_EQ(pid & 1, slot);
                }
            }
        }
    }
}

TEST(edit4_gender_rules) {
    CHECK(edit4::canHaveGender(kTauros, Gender::Male));
    CHECK(!edit4::canHaveGender(kTauros, Gender::Female));
    CHECK(edit4::canHaveGender(kMagnemite, Gender::Genderless));
    CHECK(!edit4::canHaveGender(kMagnemite, Gender::Male));
    uint32_t pid;
    CHECK(!edit4::findPid(kTauros, 0, 0, 0, false, Gender::Female, 0, 1, pid));
    // Garchomp's ratio is 127: low byte < 127 means female.
    CHECK(edit4::genderFromPid(kGarchomp, 0x00000010) == Gender::Female);
    CHECK(edit4::genderFromPid(kGarchomp, 0x000000F0) == Gender::Male);
}

TEST(edit4_trait_setters_keep_other_traits) {
    Pokemon p = roserade();
    uint8_t slot = p.pid & 1;
    Gender gender = p.gender;

    CHECK(edit4::setNature(p, 13) == edit4::Result::Ok);  // Jolly
    CHECK_EQ(p.nature, 13);
    CHECK(!p.shiny);
    CHECK(p.gender == gender);
    CHECK_EQ(p.pid & 1, slot);

    CHECK(edit4::setShiny(p, true) == edit4::Result::Ok);
    CHECK(p.shiny);
    CHECK_EQ(p.nature, 13);
    CHECK(p.gender == gender);

    Gender other = gender == Gender::Male ? Gender::Female : Gender::Male;
    CHECK(edit4::setGender(p, other) == edit4::Result::Ok);
    CHECK(p.gender == other);
    CHECK(p.shiny);
    CHECK_EQ(p.nature, 13);

    CHECK(edit4::setAbilitySlot(p, 1 - slot) == edit4::Result::Ok);
    CHECK_EQ(p.pid & 1, 1u - slot);
    CHECK_EQ(p.ability, edit4::abilityForSlot(kRoserade, 1 - slot));
    CHECK(p.shiny);
    CHECK(p.gender == other);
}

TEST(edit4_set_species_keeps_level_and_renames) {
    Pokemon p = roserade();
    p.language = 7;  // Spanish
    edit4::setLevel(p, 40);
    CHECK(edit4::setSpecies(p, kLucario) == edit4::Result::Ok);
    CHECK_EQ(p.species, kLucario);
    CHECK_EQ(p.level, 40);
    const data::PersonalInfo* info = data::personal4(kLucario);
    CHECK_EQ(p.exp, data::expForLevel(static_cast<data::GrowthRate>(info->growthRate), 40));
    CHECK(p.nickname == "LUCARIO");
    CHECK_EQ(p.ability, edit4::abilityForSlot(kLucario, p.pid & 1));
    CHECK(edit4::setSpecies(p, 0) == edit4::Result::Impossible);
    CHECK(edit4::setSpecies(p, 494) == edit4::Result::Impossible);

    p.isNicknamed = true;
    p.nickname = "Rosie";
    CHECK(edit4::setSpecies(p, kGarchomp) == edit4::Result::Ok);
    CHECK(p.nickname == "Rosie");
}

TEST(edit4_moves_refill_pp_and_reject_duplicates) {
    Pokemon p = roserade();
    p.ppUps[0] = 3;
    CHECK(edit4::setMove(p, 0, 89) == edit4::Result::Ok);  // Earthquake, 10 PP
    CHECK_EQ(p.pp[0], 16);
    CHECK(edit4::setMove(p, 1, 89) == edit4::Result::Impossible);
    CHECK(edit4::setMove(p, 1, 0) == edit4::Result::Ok);
    CHECK_EQ(p.pp[1], 0);
    CHECK(edit4::setMove(p, 4, 1) == edit4::Result::Impossible);
    CHECK_EQ(data::basePp4(33), 35);  // Tackle
}

TEST(edit4_iv_and_ev_limits) {
    Pokemon p = roserade();
    edit4::setIv(p, kAtk, 40);
    CHECK_EQ(p.ivs[kAtk], 31);
    for (int s = 0; s < kStatCount; ++s) p.evs[s] = 0;
    edit4::setEv(p, kAtk, 255);
    edit4::setEv(p, kSpe, 255);
    CHECK_EQ(p.evs[kAtk], 255);
    CHECK_EQ(p.evs[kSpe], 255);
    edit4::setEv(p, kHp, 100);
    CHECK_EQ(p.evs[kHp], 0);
    edit4::setEv(p, kSpe, 252);
    edit4::setEv(p, kHp, 100);
    CHECK_EQ(p.evs[kHp], 3);
}

TEST(pk4_refresh_party_stats_after_level_change) {
    uint8_t data[pk4::kPartySize];
    std::memcpy(data, test::kRoseradePk4, sizeof(data));
    Pokemon p = pk4::toPokemon(data, sizeof(data));
    edit4::setLevel(p, 50);
    pk4::applyPokemon(p, data);
    pk4::refreshPartyStats(data);
    CHECK_EQ(pk4::calcChecksum(data), pk4::storedChecksum(data));
    Pokemon q = pk4::toPokemon(data, sizeof(data));
    CHECK_EQ(q.level, 50);
    uint16_t maxHp = util::readU16(data + pk4::offset::kPartyStats + 2);
    CHECK(maxHp > 100);
    CHECK_EQ(util::readU16(data + pk4::offset::kPartyStats), maxHp);  // was at full HP
}

TEST(pk4_ball_rules_follow_origin_game) {
    uint8_t data[pk4::kPartySize];
    std::memcpy(data, test::kRoseradePk4, sizeof(data));
    Pokemon p = pk4::toPokemon(data, sizeof(data));  // origin: Platinum

    p.met.ball = 11;  // Luxury Ball
    pk4::applyPokemon(p, data);
    CHECK_EQ(data[pk4::offset::kBallDpPt], 11);
    CHECK_EQ(data[pk4::offset::kBallHgss], 0);

    p.met.ball = 17;  // Fast Ball cannot exist outside HG/SS
    pk4::applyPokemon(p, data);
    CHECK_EQ(data[pk4::offset::kBallDpPt], 4);
    CHECK_EQ(data[pk4::offset::kBallHgss], 0);

    p.originGame = 7;  // HeartGold
    p.met.ball = 23;   // Moon Ball
    pk4::applyPokemon(p, data);
    CHECK_EQ(data[pk4::offset::kBallDpPt], 4);
    CHECK_EQ(data[pk4::offset::kBallHgss], 23);
    CHECK_EQ(pk4::toPokemon(data, sizeof(data)).met.ball, 23);
}
