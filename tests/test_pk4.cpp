#include <cstring>

#include "core/pokemon/pk4.h"
#include "core/pokemon/stats.h"
#include "core/data/personal.h"
#include "core/util/bytes.h"
#include "tests/fixtures.h"
#include "tests/test.h"

using namespace pktool;
namespace pk4 = pokemon::pk4;

TEST(pk4_fixture_checksum_is_valid) {
    CHECK_EQ(pk4::calcChecksum(test::kRoseradePk4), pk4::storedChecksum(test::kRoseradePk4));
}

TEST(pk4_fixture_parses) {
    pokemon::Pokemon p = pk4::toPokemon(test::kRoseradePk4, pk4::kPartySize);
    CHECK_EQ(p.pid, 0x58625928u);
    CHECK_EQ(p.species, 407);
    CHECK(p.nickname == "ROSERADE");
    CHECK(!p.isNicknamed);
    CHECK(p.trainer.name == "TEST");
    CHECK_EQ(p.trainer.tid, 11111);
    CHECK_EQ(p.trainer.sid, 11111);
    CHECK(p.trainer.gender == pokemon::Gender::Female);
    CHECK_EQ(p.level, 1);
    CHECK_EQ(p.ability, 30);  // Natural Cure
    CHECK_EQ(p.moves[0], 241);
    CHECK_EQ(p.moves[3], 345);
    CHECK_EQ(p.ivs[pokemon::kHp], 5);
    CHECK_EQ(p.ivs[pokemon::kSpe], 27);
    CHECK_EQ(p.nature, 10);  // Timid
    CHECK_EQ(p.met.ball, 4);
    CHECK_EQ(p.originGame, 12);  // Platinum
    CHECK(!p.shiny);
}

TEST(pk4_stat_formula_matches_stored_party_stats) {
    pokemon::Pokemon p = pk4::toPokemon(test::kRoseradePk4, pk4::kPartySize);
    const data::PersonalInfo* info = data::personal4(p.species);
    CHECK(info != nullptr);
    uint16_t stats[pokemon::kStatCount];
    pokemon::calcStats(p.species, info->baseStats, p.ivs, p.evs, p.level, p.nature, stats);
    for (int s = 0; s < pokemon::kStatCount; ++s) {
        CHECK_EQ(stats[s], util::readU16(test::kRoseradePk4 + pk4::offset::kPartyStats + 2 + s * 2));
    }
}

TEST(pk4_stat_formula_garchomp_reference) {
    // Lv. 100 Jolly Garchomp, 31 IVs, 4 HP / 252 Atk / 252 Spe EVs.
    const uint8_t base[] = {108, 130, 95, 102, 80, 85};
    const uint8_t ivs[] = {31, 31, 31, 31, 31, 31};
    const uint8_t evs[] = {4, 252, 0, 252, 0, 0};
    uint16_t stats[pokemon::kStatCount];
    pokemon::calcStats(445, base, ivs, evs, 100, 13, stats);
    const uint16_t expected[] = {358, 359, 226, 333, 176, 206};
    for (int s = 0; s < pokemon::kStatCount; ++s) {
        CHECK_EQ(stats[s], expected[s]);
    }
}

TEST(pk4_encrypt_decrypt_round_trip_for_every_shuffle) {
    for (uint32_t sv = 0; sv < 32; ++sv) {
        uint8_t plain[pk4::kPartySize];
        std::memcpy(plain, test::kRoseradePk4, sizeof(plain));
        util::writeU32(plain, (sv << 13) | 0x1234);
        pk4::refreshChecksum(plain);

        uint8_t data[pk4::kPartySize];
        std::memcpy(data, plain, sizeof(data));
        pk4::encrypt(data, sizeof(data));
        CHECK(std::memcmp(data + 8, plain + 8, sizeof(data) - 8) != 0);
        CHECK(std::memcmp(data, plain, 8) == 0);  // header stays in the clear
        pk4::decrypt(data, sizeof(data));
        CHECK(std::memcmp(data, plain, sizeof(data)) == 0);
    }
}

TEST(pk4_shuffle_places_blocks_by_pid) {
    // sv = 1 stores the blocks as A, B, D, C.
    uint8_t plain[pk4::kStoredSize] = {};
    util::writeU32(plain, 1u << 13);
    for (int block = 0; block < 4; ++block) {
        plain[8 + block * 32] = static_cast<uint8_t>(0xA0 + block);
    }
    pk4::refreshChecksum(plain);
    uint8_t data[pk4::kStoredSize];
    std::memcpy(data, plain, sizeof(data));
    pk4::encrypt(data, sizeof(data));
    // Undo only the XOR layer to observe the stored block order.
    uint32_t seed = pk4::storedChecksum(plain);
    for (size_t i = 8; i < pk4::kStoredSize; i += 2) {
        seed = seed * 0x41C64E6Du + 0x6073u;
        util::writeU16(data + i, util::readU16(data + i) ^ static_cast<uint16_t>(seed >> 16));
    }
    CHECK_EQ(data[8 + 0 * 32], 0xA0);
    CHECK_EQ(data[8 + 1 * 32], 0xA1);
    CHECK_EQ(data[8 + 2 * 32], 0xA3);
    CHECK_EQ(data[8 + 3 * 32], 0xA2);
}

TEST(pk4_apply_round_trip_preserves_unknown_bytes) {
    uint8_t data[pk4::kPartySize];
    std::memcpy(data, test::kRoseradePk4, sizeof(data));
    pokemon::Pokemon p = pk4::toPokemon(data, sizeof(data));
    pk4::applyPokemon(p, data);
    CHECK(std::memcmp(data, test::kRoseradePk4, sizeof(data)) == 0);
}

TEST(pk4_apply_edits_fields) {
    uint8_t data[pk4::kPartySize];
    std::memcpy(data, test::kRoseradePk4, sizeof(data));
    pokemon::Pokemon p = pk4::toPokemon(data, sizeof(data));
    p.nickname = "Rosie";
    p.isNicknamed = true;
    p.heldItem = 234;  // Leftovers
    p.ivs[pokemon::kSpA] = 31;
    p.evs[pokemon::kSpe] = 252;
    p.moves[1] = 188;
    p.pokerus = 0x11;
    pk4::applyPokemon(p, data);

    CHECK_EQ(pk4::calcChecksum(data), pk4::storedChecksum(data));
    pokemon::Pokemon q = pk4::toPokemon(data, sizeof(data));
    CHECK(q.nickname == "Rosie");
    CHECK(q.isNicknamed);
    CHECK_EQ(q.heldItem, 234);
    CHECK_EQ(q.ivs[pokemon::kSpA], 31);
    CHECK_EQ(q.ivs[pokemon::kHp], 5);
    CHECK_EQ(q.evs[pokemon::kSpe], 252);
    CHECK_EQ(q.moves[1], 188);
    CHECK_EQ(q.pokerus, 0x11);
    CHECK(q.trainer.name == "TEST");
}

TEST(pk4_empty_slot_detection) {
    uint8_t zero[pk4::kStoredSize] = {};
    CHECK(pk4::isEmptySlot(zero, sizeof(zero)));
    zero[100] = 1;
    CHECK(!pk4::isEmptySlot(zero, sizeof(zero)));
}

TEST(pk4_shiny_and_nature_rules) {
    CHECK(pokemon::isShiny(0x00000000, 0, 0));
    CHECK(pokemon::isShiny(0x00070000, 0, 0));
    CHECK(!pokemon::isShiny(0x00080000, 0, 0));
    CHECK(pokemon::isShiny(0x12345678, 0x1234, 0x5678));
    CHECK_EQ(pokemon::natureFromPid(25 * 1000 + 13), 13);
}
