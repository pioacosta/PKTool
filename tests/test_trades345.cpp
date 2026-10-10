// In-game (NPC) trades of Gen III-V: the generated table and the checker.

#include <string>

#include "core/data/trades.h"
#include "core/legality/legality.h"
#include "core/legality/trade_check.h"
#include "core/pokemon/pokemon.h"
#include "tests/test.h"

using namespace pktool;
using legality::Severity;

namespace {

int findTrade(uint8_t gen, uint16_t species, uint8_t version) {
    for (int i = 0; i < data::trades345Count(); ++i) {
        const data::Trade345& t = data::trade345(i);
        if (t.gen == gen && t.species == species && ((t.versions >> version) & 1u)) return i;
    }
    return -1;
}

// The Pokemon a trade hands out, in the English game.
pokemon::Pokemon receive(const data::Trade345& t, uint8_t version, uint16_t location) {
    pokemon::Pokemon p;
    p.format = t.gen;
    p.species = t.species;
    p.level = t.level;
    p.form = t.form;
    p.pid = t.pid;
    p.nature = t.nature != 255 ? t.nature : static_cast<uint8_t>(t.pid % 25);
    p.gender = static_cast<pokemon::Gender>(t.gender < 3 ? t.gender : 0);
    for (int i = 0; i < 6; ++i) p.ivs[i] = t.ivs[i] != 255 ? t.ivs[i] : 10;
    p.language = 2;
    p.originGame = version;
    p.nickname = t.nickname[2];
    p.isNicknamed = true;
    p.trainer.name = t.ot[2];
    p.trainer.tid = t.tid;
    p.trainer.sid = t.sid;
    p.trainer.gender = t.otGender == 0 ? pokemon::Gender::Male : pokemon::Gender::Female;
    p.met.ball = 4;
    p.met.metLevel = t.level;
    p.met.metLocation = t.metLocation != 0 ? t.metLocation : location;
    return p;
}

legality::Report run(const pokemon::Pokemon& p) {
    legality::Report r;
    legality::checkNpcTrade(p, r);
    return r;
}

bool says(const legality::Report& r, Severity s, const char* text) {
    for (const legality::Finding& f : r.findings) {
        if (f.severity == s && f.text.find(text) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

TEST(trades345_table_matches_pkhex) {
    CHECK_EQ(data::trades345Count(), 73);
    int skitty = findTrade(3, 300, legality::version::kRuby);
    CHECK(skitty >= 0);
    const data::Trade345& t = data::trade345(skitty);
    CHECK_EQ(t.level, 3);
    int chatot = findTrade(4, 441, legality::version::kPlatinum);
    CHECK(chatot >= 0);
    CHECK_EQ(data::trade345(chatot).pid, 0x867u);
    CHECK_EQ(data::trade345(chatot).tid, 44142);
    CHECK(data::trade345(chatot).ot[2][0] != 0 && data::trade345(chatot).nickname[2][0] != 0);
    int emolga = findTrade(5, 587, legality::version::kBlack);
    CHECK(emolga >= 0);
    CHECK_EQ(data::trade345(emolga).pid, 0xD400007Fu);
    CHECK_EQ(data::trade345(emolga).nature, 9);  // Lax
}

TEST(trades345_exact_trades_are_valid_and_edits_are_caught) {
    struct Case {
        uint8_t gen;
        uint16_t species;
        uint8_t version;
        uint16_t location;
    };
    const Case cases[] = {{3, 300, legality::version::kRuby, 254}, {3, 273, legality::version::kEmerald, 254},
                          {3, 122, legality::version::kFireRed, 254},
                          {4, 441, legality::version::kPlatinum, 2001},
                          {4, 213, legality::version::kHeartGold, 130},
                          {5, 587, legality::version::kBlack, 30002},
                          {5, 548, legality::version::kBlack2, 30002}};
    for (const Case& c : cases) {
        int index = findTrade(c.gen, c.species, c.version);
        CHECK(index >= 0);
        const data::Trade345& t = data::trade345(index);
        pokemon::Pokemon p = receive(t, c.version, c.location);
        CHECK(legality::isNpcTrade(p));
        legality::Report r = run(p);
        CHECK(says(r, Severity::Valid, "In-game trade"));
        CHECK(r.legal());
        CHECK(r.overall() == Severity::Valid);

        pokemon::Pokemon bad = p;
        bad.trainer.tid ^= 1;
        CHECK(!run(bad).legal());
        bad = p;
        bad.trainer.name = "SOMEONE";
        CHECK(!run(bad).legal());
        bad = p;
        bad.ivs[0] = static_cast<uint8_t>((bad.ivs[0] + 1) & 31);
        CHECK(!run(bad).legal());
        bad = p;
        bad.met.ball = 1;
        CHECK(!run(bad).legal());
        if (t.flags & data::kTradeHasPid) {
            bad = p;
            bad.pid ^= 0x100;
            CHECK(!run(bad).legal());
        }
        // A changed nickname or the OT of another language is only a warning.
        bad = p;
        bad.nickname = "RENAMED";
        CHECK(run(bad).legal() && run(bad).overall() == Severity::Warning);
    }
}

TEST(trades345_unknown_species_warns) {
    int index = findTrade(3, 300, legality::version::kRuby);
    pokemon::Pokemon p = receive(data::trade345(index), legality::version::kRuby, 254);
    p.species = 25;
    legality::Report r = run(p);
    CHECK(r.legal());
    CHECK(says(r, Severity::Warning, "not in the PKTool trade table"));
}

TEST(trades345_evolved_trades_keep_the_trade_identity) {
    int index = findTrade(4, 441, legality::version::kPlatinum);
    pokemon::Pokemon p = receive(data::trade345(index), legality::version::kPlatinum, 2001);
    p.level = 60;
    CHECK(run(p).overall() == Severity::Valid);
}
