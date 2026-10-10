#include <algorithm>
#include <cstdio>
#include <string>

#include "core/data/encounters12.h"
#include "core/legality/encounters_gb.h"
#include "core/legality/legality.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/entity.h"
#include "core/pokemon/gb_common.h"
#include "tests/test.h"

using namespace pktool;
namespace gbn = pktool::legality::gb;

namespace {

pokemon::create4::Trainer trainerFor(uint8_t version, uint16_t tid = 12345) {
    pokemon::create4::Trainer t;
    t.name = "ASH";
    t.tid = tid;
    t.version = version;
    t.language = 2;
    t.today = {26, 1, 1};
    return t;
}

pokemon::Pokemon viaEntity(const pokemon::Pokemon& p) {
    uint8_t blob[pokemon::entity::kMaxPartySize] = {};
    pokemon::create4::encode(p, blob);
    return pokemon::entity::decode(p.format, blob, 0);
}

bool legal(const pokemon::Pokemon& p) {
    return legality::check(viaEntity(p)).overall() != legality::Severity::Invalid;
}

// Whether the NPC trade is among the encounters the Pokemon can still be (wild ones may exist as well).
bool tradeFits(const pokemon::Pokemon& p) {
    gbn::Analysis a;
    gbn::analyse(viaEntity(p), a);
    for (const gbn::Candidate& c : a.usable) {
        if (c.source == gbn::Source::Trade) return true;
    }
    return false;
}

}  // namespace

TEST(gb_legality_gen1_gift_trainer_data) {
    pokemon::Pokemon mew = pokemon::create4::newPokemon(trainerFor(35), 151, 5, 9);
    mew.trainer.name = "YOSHIRA";  // Europe tour Mew
    pokemon::gb::unpackDvs(0xA1C5, mew.ivs);
    CHECK(legal(mew));
    pokemon::Pokemon p = mew;
    p.trainer.name = "ASH";
    CHECK(!legal(p));
    p = mew;
    pokemon::gb::unpackDvs(0xA1C4, p.ivs);
    CHECK(!legal(p));
}

TEST(gb_legality_light_ball_pikachu) {
    // Yellow's starter Pikachu and the Raichu it evolves into keep catch rate 163.
    pokemon::Pokemon yellow = pokemon::create4::newPokemon(trainerFor(38), 25, 5, 3);
    yellow.catchRate = 0xA3;
    CHECK(legal(yellow));
    pokemon::Pokemon raichu = pokemon::create4::newPokemon(trainerFor(38), 26, 30, 3);
    raichu.catchRate = 0xA3;
    CHECK(legal(raichu));
}

TEST(gb_legality_headbutt_trees_depend_on_trainer_id) {
    // Route 34 (location 15) only has normal-kind trees for trainer IDs ending in 1-9 (mask 0x001_3FE).
    const data::WildGb* slot = nullptr;
    for (int i = 0; i < data::generated::kWild2Count && !slot; ++i) {
        const data::WildGb& w = data::generated::kWild2[i];
        if (w.kind == data::kSlotHeadbutt && w.location == 15 && (w.versions & data::kGbGold)) slot = &w;
    }
    CHECK(slot != nullptr);
    if (!slot) return;
    for (uint16_t tid = 20000; tid < 20010; ++tid) {
        pokemon::Pokemon p = pokemon::create4::newPokemon(trainerFor(39, tid), slot->species, slot->levelMin, 5);
        gbn::Analysis a;
        gbn::analyse(viaEntity(p), a);
        bool found = false;
        for (const gbn::Candidate& c : a.pool) {
            if (c.source == gbn::Source::Wild && c.slotKind == data::kSlotHeadbutt && c.location == 15) {
                found = true;
                CHECK_EQ(c.treeBad, tid % 10 == 0);
            }
        }
        CHECK(found);
    }
    // A species that is only found at such a tree is invalid for an ID without it.
    CHECK(legality::check(viaEntity(pokemon::create4::newPokemon(trainerFor(39, 20001), slot->species, slot->levelMin, 5))).legal());
}

TEST(gb_legality_gen2_npc_trade_trainer_data) {
    pokemon::Pokemon onix = pokemon::create4::newPokemon(trainerFor(39), 95, 10, 4);  // Onix from Kyle
    onix.nickname = "ROCKY";
    onix.isNicknamed = true;
    onix.trainer.name = "KYLE";
    onix.trainer.tid = 48926;
    pokemon::gb::unpackDvs(0x9666, onix.ivs);
    CHECK(tradeFits(onix));
    pokemon::Pokemon p = onix;
    p.trainer.tid = 48927;
    CHECK(!tradeFits(p));
    p = onix;
    p.trainer.name = "ASH";
    CHECK(!tradeFits(p));
    p = onix;
    p.nickname = "ONIX";
    CHECK(!tradeFits(p));
    p = onix;
    pokemon::gb::unpackDvs(0x9667, p.ivs);
    CHECK(!tradeFits(p));
}
