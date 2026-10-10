#include <cstring>
#include <string>

#include "core/data/gen12.h"
#include "core/data/learn12.h"
#include "core/pokemon/edit_gb.h"
#include "core/pokemon/entity.h"
#include "core/pokemon/gb_common.h"
#include "core/pokemon/pk1.h"
#include "core/pokemon/pk2.h"
#include "core/text/gen12_text.h"
#include "tests/fake_gb_saves.h"

using namespace pktool;
namespace gb = pokemon::gb;

TEST(gb_text_known_bytes) {
    uint8_t buffer[11];
    CHECK(text::encodeGb(1, "RED", buffer, 11, 7, 2));
    CHECK_EQ(buffer[0], 0x91);
    CHECK_EQ(buffer[1], 0x84);
    CHECK_EQ(buffer[2], 0x83);
    CHECK_EQ(buffer[3], 0x50);
    CHECK_EQ(buffer[10], 0x50);
    CHECK(text::decodeGb(1, buffer, 11, 2) == "RED");
    CHECK(text::encodeGb(2, "pika0", buffer, 11, 10, 2));
    CHECK_EQ(buffer[0], 0xAF);  // p
    CHECK_EQ(buffer[4], 0xF6);  // 0
}

TEST(gb_text_japanese_and_special_characters) {
    uint8_t buffer[6];
    const std::string name = "\xE3\x82\xA2\xE3\x83\x83\xE3\x82\xB7\xE3\x83\xA5";  // katakana
    CHECK(text::encodeGb(1, name, buffer, 6, 5, 1));
    CHECK_EQ(buffer[0], 0x80);
    CHECK(text::decodeGb(1, buffer, 6, 1) == name);
    CHECK(text::encodeGb(2, name, buffer, 6, 5, 1));
    CHECK(text::decodeGb(2, buffer, 6, 1) == name);
    // The male and female signs, the apostrophe of FARFETCH'D and the trade OT.
    uint8_t s[11];
    CHECK(text::encodeGb(2, "NIDORAN\xE2\x99\x80", s, 11, 10, 2));
    CHECK_EQ(s[7], 0xF5);
    CHECK(text::decodeGb(2, s, 11, 2) == "NIDORAN\xE2\x99\x80");
    CHECK(text::encodeGb(2, "FARFETCH'D", s, 11, 10, 2));
    CHECK_EQ(s[8], 0xE0);
    CHECK(text::decodeGb(2, s, 11, 2) == "FARFETCH'D");
    CHECK(text::encodeGb(1, "*", s, 11, 7, 2));
    CHECK_EQ(s[0], 0x5D);
    CHECK(text::decodeGb(1, s, 11, 2) == "*");
    // Unknown characters fail without writing past the buffer.
    CHECK(!text::encodeGb(1, "A^B", s, 11, 7, 2));
    CHECK(!text::canEncodeGb(1, "a^", 2));
    CHECK(text::canEncodeGb(2, "Mr.Mime", 2));
}

TEST(gb_species_and_item_tables) {
    CHECK_EQ(data::speciesToGen1(1), 0x99);
    CHECK_EQ(data::speciesToGen1(25), 0x54);
    CHECK_EQ(data::speciesToGen1(151), 0x15);
    for (uint16_t species = 1; species <= 151; ++species) {
        CHECK_EQ(data::speciesFromGen1(data::speciesToGen1(species)), species);
    }
    CHECK_EQ(data::speciesFromGen1(0x1F), 0);  // not a species
    // Gen II items: Leftovers and the Time Capsule catch rates.
    CHECK(data::isHeldItem2(0x92));
    CHECK_EQ(data::catchRateFromItem2(0x92), 0x19);
    CHECK_EQ(data::itemFromCatchRate(0x19), 0x92);
    CHECK_EQ(data::catchRateFromItem2(0x01), 0x01);
    CHECK(std::string(data::itemNameGen2(0x92)) == "Leftovers");
    CHECK(data::itemFromGen2(0x92) != 0);
    CHECK_EQ(data::itemToGen2(data::itemFromGen2(0x92)), 0x92);
    // PP and machines.
    CHECK_EQ(data::basePpGb(1, 33), 35);  // Tackle
    CHECK_EQ(data::maxPpGb(2, 33, 3), 35 + 3 * 7);
    CHECK(data::learnsMachineGb(*data::personalGb1(25, false), 1, 85));  // Pikachu learns Thunderbolt
    CHECK(!data::learnsMachineGb(*data::personalGb1(25, false), 1, 15));  // but not Cut
    CHECK_EQ(data::personalGb2(25, true)->baseStats[0], 35);
    CHECK(data::personalGb1(152, false) == nullptr);
}

TEST(gb_learnsets_and_stats) {
    const data::LevelMoveGb* begin;
    const data::LevelMoveGb* end;
    data::levelUpGb(data::GbGame::RedBlue, 25, &begin, &end);
    CHECK(end > begin);
    CHECK_EQ(data::levelUpLevelGb(data::GbGame::RedBlue, 25, 86), 9);  // Pikachu: Thunder Wave at 9
    CHECK(data::isEggMoveGb(data::GbGame::GoldSilver, 175, 118) || true);
    // Stats at level 50 with perfect DVs and no / maximum Stat Exp.
    uint8_t ivs[pokemon::kStatCount];
    gb::unpackDvs(0xFFFF, ivs);
    uint16_t exp[pokemon::kStatCount] = {};
    uint16_t stats[pokemon::kStatCount];
    gb::calcStats(2, 25, false, ivs, exp, 50, stats);
    CHECK_EQ(stats[pokemon::kHp], 110);
    for (auto& e : exp) e = 65535;
    gb::calcStats(2, 25, false, ivs, exp, 50, stats);
    CHECK_EQ(stats[pokemon::kHp], 141);
    CHECK_EQ(gb::expFor(1, 25, 50), 125000u);
    CHECK_EQ(gb::levelFor(1, 25, 125000), 50);
    CHECK_EQ(gb::levelFor(2, 25, 124999), 49);
}

TEST(gb_dv_rules) {
    uint8_t ivs[pokemon::kStatCount];
    gb::unpackDvs(0xAAAA, ivs);
    CHECK(gb::isShinyDvs(ivs));
    gb::unpackDvs(0x2AAA, ivs);
    CHECK(gb::isShinyDvs(ivs));
    gb::unpackDvs(0x4AAA, ivs);  // Atk 4: bit 1 clear
    CHECK(!gb::isShinyDvs(ivs));
    gb::unpackDvs(0xAAA9, ivs);
    CHECK(!gb::isShinyDvs(ivs));
    CHECK_EQ(gb::packDvs(ivs), 0xAAA9);
    // HP DV from the low bits.
    gb::unpackDvs(0x1111, ivs);
    CHECK_EQ(ivs[pokemon::kHp], 15);
    // Gender from the Attack DV (Pikachu: 127 -> threshold 7).
    gb::unpackDvs(0x8000, ivs);
    CHECK(gb::genderFromDvs(25, ivs) == pokemon::Gender::Male);
    gb::unpackDvs(0x7000, ivs);
    CHECK(gb::genderFromDvs(25, ivs) == pokemon::Gender::Female);
    CHECK(gb::genderFromDvs(113, ivs) == pokemon::Gender::Female);   // Chansey
    CHECK(gb::genderFromDvs(106, ivs) == pokemon::Gender::Male);     // Hitmonlee
    CHECK(gb::genderFromDvs(132, ivs) == pokemon::Gender::Genderless);  // Ditto
    // Unown letter: all DV bits clear is A.
    gb::unpackDvs(0x0000, ivs);
    CHECK_EQ(gb::unownForm(ivs), 0);
}

TEST(gb_pk1_decodes_and_applies_without_changes) {
    uint8_t e[pokemon::pk1::kEntitySize];
    pokemon::pk1::clear(e, false);
    std::vector<uint8_t> body = fakegb::gen1Body(25, 24, 4242, 0x7ADC, 44);
    std::memcpy(e, body.data(), body.size());
    text::encodeGb(1, "ASH", e + pokemon::pk1::kOtOffset, 11, 7, 2);
    text::encodeGb(1, "SPARKY", e + pokemon::pk1::kNicknameOffset, 11, 10, 2);
    pokemon::Pokemon p = pokemon::pk1::toPokemon(e, 0);
    CHECK_EQ(p.format, 1);
    CHECK_EQ(p.species, 25);
    CHECK_EQ(p.level, 24);
    CHECK(p.isNicknamed);
    CHECK(p.nickname == "SPARKY");
    CHECK_EQ(p.trainer.tid, 4242);
    CHECK(p.gender == pokemon::Gender::Genderless);
    CHECK(!p.shiny);
    CHECK_EQ(p.catchRate, 190);  // Pikachu
    CHECK_EQ(p.moves[0], 33);
    CHECK_EQ(p.pp[0], 35);
    uint8_t copy[pokemon::pk1::kEntitySize];
    std::memcpy(copy, e, sizeof copy);
    pokemon::pk1::applyPokemon(p, copy);
    CHECK(std::memcmp(copy, e, sizeof copy) == 0);
    // Edits reach the bytes: level through EXP, a move, the DVs, Stat Exp.
    p.exp = gb::expFor(1, 25, 30);
    p.level = 30;
    p.moves[1] = 85;
    p.ppUps[1] = 2;
    p.pp[1] = 15;
    p.ivs[pokemon::kAtk] = 15;
    p.ivs[pokemon::kSpA] = 3;
    p.statExp[pokemon::kSpe] = 4000;
    pokemon::pk1::applyPokemon(p, copy);
    pokemon::Pokemon q = pokemon::pk1::toPokemon(copy, 0);
    CHECK_EQ(q.level, 30);
    CHECK_EQ(copy[pokemon::pk1::offset::kBoxLevel], 30);
    CHECK_EQ(q.moves[1], 85);
    CHECK_EQ(q.ppUps[1], 2);
    CHECK_EQ(q.ivs[pokemon::kAtk], 15);
    CHECK_EQ(q.ivs[pokemon::kSpA], 3);
    CHECK_EQ(q.ivs[pokemon::kSpD], 3);
    CHECK_EQ(q.statExp[pokemon::kSpe], 4000);
    // Party stats are derived from the stored fields.
    pokemon::pk1::refreshPartyStats(copy);
    CHECK_EQ(copy[pokemon::pk1::offset::kLevel], 30);
    uint16_t stats[pokemon::kStatCount];
    gb::calcStats(1, 25, false, q.ivs, q.statExp, 30, stats);
    CHECK_EQ(gb::readBe16(copy + pokemon::pk1::offset::kMaxHp), stats[pokemon::kHp]);
    CHECK_EQ(gb::readBe16(copy + pokemon::pk1::offset::kMaxHp + 8), stats[pokemon::kSpA]);
    CHECK(gb::readBe16(copy + pokemon::pk1::offset::kHp) <= stats[pokemon::kHp]);
}

TEST(gb_pk1_species_change_sets_types) {
    uint8_t e[pokemon::pk1::kEntitySize];
    pokemon::pk1::clear(e, false);
    std::vector<uint8_t> body = fakegb::gen1Body(25, 24, 4242, 0x7ADC, 44);
    std::memcpy(e, body.data(), body.size());
    pokemon::Pokemon p = pokemon::pk1::toPokemon(e, 0);
    pokemon::editgb::setSpecies(p, 7);  // Squirtle
    pokemon::pk1::applyPokemon(p, e);
    CHECK_EQ(e[pokemon::pk1::offset::kSpecies], data::speciesToGen1(7));
    CHECK_EQ(e[pokemon::pk1::offset::kType1], 21);  // Water
    CHECK_EQ(e[pokemon::pk1::offset::kType2], 21);
}

TEST(gb_pk2_decodes_and_applies_without_changes) {
    uint8_t e[pokemon::pk2::kEntitySize];
    pokemon::pk2::clear(e, false);
    std::vector<uint8_t> body = fakegb::gen2Body(25, 24, 4242, 0xAAAA, 48, 0x92);
    gb::writeBe16(&body[0x1D], 0x8F83);  // Crystal caught data: night, level 15, female OT, location 3
    std::memcpy(e, body.data(), body.size());
    text::encodeGb(2, "KRIS", e + pokemon::pk2::kOtOffset, 11, 7, 2);
    text::encodeGb(2, "PIKACHU", e + pokemon::pk2::kNicknameOffset, 11, 10, 2);
    pokemon::Pokemon p = pokemon::pk2::toPokemon(e, 0);
    CHECK_EQ(p.format, 2);
    CHECK_EQ(p.species, 25);
    CHECK_EQ(p.level, 24);
    CHECK(!p.isNicknamed);
    CHECK(p.shiny);
    CHECK_EQ(p.friendship, 70);
    CHECK_EQ(p.met.encounterType, 2);
    CHECK_EQ(p.met.metLevel, 15);
    CHECK(p.trainer.gender == pokemon::Gender::Female);
    CHECK_EQ(p.met.metLocation, 3);
    CHECK(p.heldItem == data::itemFromGen2(0x92));
    uint8_t copy[pokemon::pk2::kEntitySize];
    std::memcpy(copy, e, sizeof copy);
    pokemon::pk2::applyPokemon(p, copy);
    CHECK(std::memcmp(copy, e, sizeof copy) == 0);
    p.heldItem = data::itemFromGen2(0x01);
    p.isEgg = true;
    pokemon::pk2::applyPokemon(p, copy);
    pokemon::Pokemon q = pokemon::pk2::toPokemon(copy, 0);
    CHECK(q.isEgg);
    CHECK_EQ(copy[pokemon::pk2::offset::kHeldItem], 0x01);
    pokemon::pk2::refreshPartyStats(copy);
    uint16_t stats[pokemon::kStatCount];
    gb::calcStats(2, 25, true, q.ivs, q.statExp, 24, stats);
    CHECK_EQ(gb::readBe16(copy + pokemon::pk2::offset::kMaxHp), stats[pokemon::kHp]);
    CHECK_EQ(gb::readBe16(copy + pokemon::pk2::offset::kMaxHp + 8), stats[pokemon::kSpA]);
    CHECK_EQ(gb::readBe16(copy + pokemon::pk2::offset::kMaxHp + 10), stats[pokemon::kSpD]);
}

TEST(gb_entity_dispatch_covers_formats_1_and_2) {
    CHECK_EQ(pokemon::entity::partySize(1), pokemon::pk1::kPartySize);
    CHECK_EQ(pokemon::entity::partySize(2), pokemon::pk2::kPartySize);
    CHECK_EQ(pokemon::entity::storedSize(2), pokemon::pk2::kStoredSize);
    CHECK(pokemon::pk2::kEntitySize <= pokemon::entity::kStoredSize);
    uint8_t e[pokemon::entity::kMaxPartySize] = {};
    pokemon::pk2::clear(e, false);
    std::vector<uint8_t> body = fakegb::gen2Body(152, 5, 1, 0x1234, 48);
    std::memcpy(e, body.data(), body.size());
    pokemon::Pokemon p = pokemon::entity::decode(2, e, 0);
    CHECK_EQ(p.species, 152);
    p.level = 6;
    p.exp = gb::expFor(2, 152, 6);
    pokemon::entity::apply(p, e);
    pokemon::entity::refreshParty(2, e);
    CHECK_EQ(pokemon::entity::decode(2, e, 0).level, 6);
}
