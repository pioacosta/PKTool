// Regression tests for problems found by the fuzzers and the mass test.

#include <cstring>

#include "core/bank/bank.h"
#include "core/data/events3.h"
#include "core/legality/legalize.h"
#include "core/legality/legality.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/gift3.h"
#include "core/pokemon/entity.h"
#include "core/pokemon/pk3.h"
#include "core/pokemon/pk4.h"
#include "core/pokemon/pk5.h"
#include "core/util/bytes.h"
#include "core/util/crc16.h"
#include "tests/fuzz/fuzz_targets.h"
#include "tests/test.h"

using namespace pktool;

// The party level byte is only a fallback for species without data; a random
// byte there made the model level 200 (found by the entity fuzzer).
TEST(regress_party_level_byte_is_clamped) {
    for (uint8_t format = 3; format <= 5; ++format) {
        uint8_t buffer[pokemon::entity::kMaxPartySize] = {};
        size_t size = pokemon::entity::partySize(format);
        size_t levelOffset = format == 3 ? pokemon::pk3::offset::kPartyLevel : pokemon::pk4::offset::kPartyLevel;
        // Species 0xFFFF has no personal data in any generation.
        util::writeU16(buffer + (format == 3 ? pokemon::pk3::offset::kSpecies : pokemon::pk4::offset::kSpecies), 0xFFFF);
        buffer[levelOffset] = 200;
        pokemon::Pokemon p = pokemon::entity::decode(format, buffer, size);
        CHECK(p.level <= 100);
        buffer[levelOffset] = 42;
        CHECK_EQ(pokemon::entity::decode(format, buffer, size).level, 42);
    }
}

// A Gen III egg of a species whose line has a Gen IV baby (Chimecho ->
// Chingling, Roselia -> Budew) must hatch the Gen III species: the baby does
// not exist in a Gen III save, so its entity could not be stored (found by the
// mass test).
TEST(regress_gen3_egg_species_exists_in_gen3) {
    pokemon::create4::Trainer t;
    t.name = "TEST";
    t.tid = 1;
    t.sid = 2;
    t.version = 3;  // Emerald
    t.today = {26, 9, 29};
    const uint16_t kChimecho = 358, kRoselia = 315, kSnorlax = 143;
    for (uint16_t species : {kChimecho, kRoselia, kSnorlax}) {
        pokemon::Pokemon egg = pokemon::create4::newEgg(t, species, 7);
        CHECK_EQ(egg.format, 3);
        CHECK_EQ(egg.species, species);
        uint8_t raw[136];
        pokemon::create4::encode(egg, raw);
        CHECK_EQ(pokemon::entity::decode(3, raw, 80).species, species);
    }
}

// Bank files can be parsed from memory (what the fuzzer uses) and a damaged
// one is read-only.
TEST(regress_bank_load_bytes) {
    bank::Bank b;
    CHECK(b.load("/nonexistent/pktool-regress.pkb") == bank::Bank::LoadResult::Created);
    std::vector<uint8_t> file = b.serialize();
    bank::Bank c;
    CHECK(c.loadBytes(file) == bank::Bank::LoadResult::Ok);
    CHECK(c.serialize() == file);
    file[10] ^= 1;
    CHECK(c.loadBytes(file) == bank::Bank::LoadResult::Corrupt);
    CHECK(c.readOnly());
    CHECK(c.loadBytes({}) == bank::Bank::LoadResult::Corrupt);
}

// Legalizing a legal Colosseum/XD Shadow Pokemon again re-rolled its PID as a
// Method 1 frame (its ability bit is stored apart from the PID, which the
// "PID-derived traits" step took for a request) and made it illegal. Found by
// the mass test with these generator seeds (tests/test_mass.cpp).
TEST(regress_legalize_keeps_a_legal_colosseum_xd_pokemon) {
    for (uint64_t seed : {24544ull, 24868ull, 44956ull}) {
        fuzz::Rng rng(seed);
        int generation = 3 + static_cast<int>(rng.below(3));
        bool clean = rng.chance(30);
        pokemon::Pokemon p = fuzz::randomPokemon(rng, generation, clean);
        legality::LegalizeResult first = legality::legalize(p, 4);
        CHECK(first.report.legal());
        legality::LegalizeResult again = legality::legalize(first.pokemon, 4);
        CHECK(again.report.legal());
        CHECK_EQ(again.pokemon.pid, first.pokemon.pid);
    }
}

// Asking for a male Pokemon Channel Jirachi (it is genderless, no such frame) used to run
// 400000 searches of 65536 RNG origins each: minutes of work, found as a hang
// by the legality fuzzer. It must now end at once and say it is not satisfied.
TEST(regress_channel_jirachi_impossible_request_ends) {
    pokemon::create4::Trainer t;
    t.name = "TEST";
    t.tid = 1;
    t.sid = 2;
    t.version = 4;
    t.today = {26, 9, 29};
    int found = 0;
    for (int i = 0; i < data::events3Count(); ++i) {
        const data::Event3& e = data::event3(i);
        if (e.method != data::kEvChannel) continue;
        ++found;
        pokemon::gift3::Criteria want;
        want.gender = 0;  // Jirachi is genderless: no frame can be male
        uint8_t raw[pokemon::pk3::kStoredSize];
        bool satisfied = true;
        pokemon::gift3::receive(e, t, 5, want, raw, &satisfied);
        CHECK(!satisfied);
        // An ordinary request still works.
        pokemon::gift3::Criteria any;
        any.nature = 3;
        satisfied = false;
        pokemon::gift3::receive(e, t, 5, any, raw, &satisfied);
        CHECK(satisfied);
    }
    CHECK(found > 0);
}
