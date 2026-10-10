#include <cstring>

#include "core/bank/bank.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/entity.h"
#include "core/pokemon/time_capsule.h"
#include "tests/fake_gb_saves.h"

using namespace pktool;
namespace tc = pokemon::timecapsule;

namespace {
pokemon::create4::Trainer trainer(uint8_t version) {
    pokemon::create4::Trainer t;
    t.name = "ASH";
    t.tid = 1;
    t.version = version;
    return t;
}
}  // namespace

TEST(time_capsule_gen1_to_gen2_and_back) {
    pokemon::Pokemon p = pokemon::create4::newPokemon(trainer(35), 25, 20, 4);
    uint8_t one[pokemon::entity::kStoredSize] = {};
    pokemon::create4::encode(p, one);
    uint8_t two[pokemon::entity::kStoredSize] = {};
    std::vector<std::string> notes;
    CHECK(tc::toGen2(one, two, &notes).ok);
    pokemon::Pokemon q = pokemon::entity::decode(2, two, 0);
    CHECK_EQ(q.species, 25);
    CHECK_EQ(q.level, 20);
    CHECK_EQ(q.friendship, 70);
    CHECK_EQ(q.moves[0], p.moves[0]);
    CHECK_EQ(two[pokemon::pk2::offset::kHeldItem], data::itemFromCatchRate(p.catchRate));
    CHECK(q.trainer.name == "ASH");
    CHECK(!notes.empty());
    uint8_t back[pokemon::entity::kStoredSize] = {};
    CHECK(tc::toGen1(two, back, nullptr).ok);
    pokemon::Pokemon r = pokemon::entity::decode(1, back, 0);
    CHECK_EQ(r.catchRate, data::catchRateFromItem2(data::itemFromCatchRate(p.catchRate)));  // lossy, like the games
    CHECK_EQ(r.exp, p.exp);
    CHECK_EQ(pokemon::gb::packDvs(r.ivs), pokemon::gb::packDvs(p.ivs));
}

TEST(time_capsule_refuses_gen2_only_pokemon) {
    uint8_t two[pokemon::entity::kStoredSize] = {};
    uint8_t one[pokemon::entity::kStoredSize] = {};
    pokemon::create4::encode(pokemon::create4::newPokemon(trainer(41), 152, 10, 1), two);  // Chikorita
    CHECK(!tc::toGen1(two, one, nullptr).ok);
    pokemon::Pokemon pikachu = pokemon::create4::newPokemon(trainer(41), 25, 20, 2);
    pokemon::create4::encode(pikachu, two);
    two[pokemon::pk2::offset::kMoves + 3] = 200;  // Outrage: a Gen II move
    CHECK(!tc::toGen1(two, one, nullptr).ok);
    pokemon::create4::encode(pikachu, two);
    two[pokemon::pk2::kEggOffset] = 1;
    CHECK(!tc::toGen1(two, one, nullptr).ok);
    pokemon::create4::encode(pikachu, two);
    CHECK(tc::toGen1(two, one, nullptr).ok);
}

TEST(time_capsule_bank_holds_game_boy_pokemon) {
    bank::Bank b;
    uint8_t one[pokemon::entity::kStoredSize] = {};
    pokemon::create4::encode(pokemon::create4::newPokemon(trainer(35), 129, 10, 3), one);
    CHECK(b.set(0, 0, 1, one));
    std::vector<uint8_t> bytes = b.serialize();
    CHECK_EQ(bytes[4], 3);  // version 3: older PKTool refuses it
    CHECK_EQ(b.format(0, 0), 1);
    bank::Bank empty;
    CHECK_EQ(empty.serialize()[4], 2);
}
