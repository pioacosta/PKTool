#include <cstring>
#include <string>
#include <vector>

#include "core/data/encounters4.h"
#include "core/data/learn4.h"
#include "core/io/file_io.h"
#include "core/legality/legalize.h"
#include "core/legality/legality.h"
#include "core/legality/pid_iv.h"
#include "core/pokemon/edit4.h"
#include "core/pokemon/pk4.h"
#include "core/util/lcrng.h"
#include "tests/fixtures.h"
#include "tests/test.h"

using namespace pktool;
using legality::Severity;
namespace pk4 = pokemon::pk4;

namespace {

// A wild Pokémon generated with Method 1 from `seed`.
pokemon::Pokemon methodOnePokemon(uint32_t seed) {
    pokemon::Pokemon p = pk4::toPokemon(test::kRoseradePk4, pk4::kPartySize);
    uint32_t s1 = util::lcrngNext(seed);
    uint32_t s2 = util::lcrngNext(s1);
    uint32_t s3 = util::lcrngNext(s2);
    uint32_t s4 = util::lcrngNext(s3);
    p.pid = (s2 & 0xFFFF0000u) | (s1 >> 16);
    legality::ivsFromWords(static_cast<uint16_t>((s3 >> 16) & 0x7FFF), static_cast<uint16_t>((s4 >> 16) & 0x7FFF), p.ivs);
    return p;
}

}  // namespace

TEST(pid_iv_detects_method_one_and_rejects_edits) {
    pokemon::Pokemon p = methodOnePokemon(0x12345678);
    CHECK(legality::findPidIvMethod(p.pid, p.ivs) == legality::PidIvMethod::Method1);
    p.ivs[pokemon::kSpe] ^= 1;
    CHECK(legality::findPidIvMethod(p.pid, p.ivs) == legality::PidIvMethod::None);
}

TEST(pid_iv_detects_method_two_and_four) {
    uint32_t seed = 0xCAFEBABE;
    uint32_t s1 = util::lcrngNext(seed), s2 = util::lcrngNext(s1), s3 = util::lcrngNext(s2);
    uint32_t s4 = util::lcrngNext(s3), s5 = util::lcrngNext(s4);
    uint32_t pid = (s2 & 0xFFFF0000u) | (s1 >> 16);
    uint8_t ivs[pokemon::kStatCount];
    legality::ivsFromWords(static_cast<uint16_t>((s4 >> 16) & 0x7FFF), static_cast<uint16_t>((s5 >> 16) & 0x7FFF), ivs);
    CHECK(legality::findPidIvMethod(pid, ivs) == legality::PidIvMethod::Method2);
    legality::ivsFromWords(static_cast<uint16_t>((s3 >> 16) & 0x7FFF), static_cast<uint16_t>((s5 >> 16) & 0x7FFF), ivs);
    CHECK(legality::findPidIvMethod(pid, ivs) == legality::PidIvMethod::Method4);
}

TEST(learnsets_and_evolutions) {
    CHECK(data::findLearn4(445, 89) != nullptr);             // Garchomp: Earthquake
    CHECK(data::findLearn4(445, 57) != nullptr);             // Garchomp: Surf (HM)
    CHECK(data::findLearn4(25, 344) == nullptr);             // Pikachu: no Volt Tackle in Gen IV learnsets
    CHECK_EQ(data::preEvolution4(445), 444);
    CHECK_EQ(data::baseSpecies4(445), 443);
    CHECK_EQ(data::baseSpecies4(448), 447);                  // Lucario -> Riolu
    CHECK_EQ(data::preEvolution4(1), 0);
}

TEST(legality_real_legal_pokemon) {
    pokemon::Pokemon p = pk4::toPokemon(test::kRoseradePk4, pk4::kPartySize);
    legality::Report r = legality::checkGen4(p);
    CHECK(r.legal());
}

TEST(legality_pkhex_legal_fixtures_are_never_illegal) {
    std::string dir = std::string(PKTOOL_SOURCE_DIR) + "/tests/fixtures/pkhex";
    int checked = 0;
    for (const char* name : {
             "0081 - MAGNETI - B2F0B116BC4C safari 4iv fail.pk4",
             "0095 - Egg - 3C46CD462454 pt egg.pk4",
             "0095 - ONIX - D1BE00E5AE9E pt2hg hatch.pk4",
             "0113 - CHANSEY - 806A14061105 swarm1 pressure.pk4",
             "0113 - CHANSEY - D4E98650DFCD swarm1 pressure.pk4",
             "0218 - Egg - 30859D41D013 hg egg.pk4",
             "0218 - SLUGMA - 4F02838CED63 hg2pt hatch.pk4",
             "0407 - ROSERADE - E24358625928.pk4",
             "074 - GEODUDE - B16E2318D744.pk4",
             "098 - KRABBY - 86943584D01B.pk4",
             "098 - KRABBY - C589B4EFB45F.pk4",
             "104 \xE2\x98\x85 - CUBONE - ABE400000004.pk4",
             "172 - Egg - 7D3CF55CFC10.pk4",
             "184 - AZUMARILL - 70F46DFFFF94 static genderchange.pk4",
             "194 - Egg - B7B3E1614DCB Primo Wooper.pk4",
             "201 - UNOWN - E8FFA68025F9.pk4",
             "215 - SNEASEL - 72E6A36A07BD.pk4",
             "230 - SEASOR - 4E170000007F.pk4",
             "231 - PHANPY - 579744E8F948.pk4",
             "236 - Oeuf - 5E05C020CB44.pk4",
             "242 - HEITEIRA - E83AB36E7C5C.pk4",
             "246 - LARVITAR - 2AC20000000E.pk4",
             "246 \xE2\x98\x85 - LARVITAR - E4CF00000002.pk4",
             "300 - Uovo - 0C910B68370A.pk4",
             "308 - MEDICHAM - 25F8A56FA3F2.pk4",
             "323 - CAMERUPT - 1EAB066E02B3.pk4",
             "440 - \xE3\x83\x94\xE3\x83\xB3\xE3\x83\x97\xE3\x82\xAF - 17961FFFFFFE.pk4",
             "470  - LEAFEON - 65B05FEF8D69.pk4",
             "477 - DUSKNOIR - 12DF9E28828D.pk4",
             "479 - Ei - B7178C463B60.pk4",
             "479-04 - Fan - 68FCC25F9AB6 dp formchange egg.pk4",
         }) {
        std::vector<uint8_t> bytes;
        if (!io::readFile(dir + "/" + name, bytes) || bytes.size() < pk4::kStoredSize) {
            std::printf("  missing fixture: %s\n", name);
            CHECK(false);
            continue;
        }
        size_t size = bytes.size() >= pk4::kPartySize ? pk4::kPartySize : pk4::kStoredSize;
        legality::Report r = legality::checkGen4(pk4::toPokemon(bytes.data(), size));
        if (!r.legal()) {
            std::printf("  flagged as illegal: %s\n", name);
            for (const auto& f : r.findings) std::printf("    - %s\n", f.text.c_str());
        }
        CHECK(r.legal());
        ++checked;
    }
    CHECK_EQ(checked, 31);
}

TEST(legality_flags_edits) {
    // A wild Platinum Pokémon (Method 1), then edited in several ways.
    pokemon::Pokemon base = methodOnePokemon(0xBEEF);
    base.met.eggLocation = 0;
    base.met.ball = 4;
    base.originGame = legality::version::kPlatinum;
    // A real Platinum encounter of Roserade's line (Roselia or Budew).
    bool placed = false;
    for (uint16_t s : {uint16_t(315), uint16_t(406)}) {
        const data::Encounter4* b;
        const data::Encounter4* e;
        data::encounters4(s, &b, &e);
        for (; b != e && !placed; ++b) {
            if ((b->versions & data::kVerPlatinum) && b->kind < data::kEncounterStatic) {
                base.met.metLocation = b->location;
                base.met.metLevel = b->minLevel;
                placed = true;
            }
        }
    }
    CHECK(placed);
    pokemon::edit4::setLevel(base, 60);
    pokemon::edit4::refreshDerived(base);
    base.ability = pokemon::edit4::abilityForSlot(base.species, base.pid & 1);
    base.moves[0] = 71;  // Absorb (Budew line level-up)
    base.moves[1] = base.moves[2] = base.moves[3] = 0;
    CHECK(legality::checkGen4(base).legal());

    pokemon::Pokemon natured = base;
    CHECK(pokemon::edit4::setNature(natured, static_cast<uint8_t>((base.nature + 1) % 25)) == pokemon::edit4::Result::Ok);
    CHECK(!legality::checkGen4(natured).legal());  // new PID breaks the RNG correlation

    pokemon::Pokemon badMove = base;
    badMove.moves[1] = 89;  // Earthquake
    CHECK(!legality::checkGen4(badMove).legal());

    pokemon::Pokemon badAbility = base;
    badAbility.ability = 1;
    CHECK(!legality::checkGen4(badAbility).legal());

    pokemon::Pokemon badBall = base;
    badBall.met.ball = 23;  // Moon Ball is HG/SS only
    CHECK(!legality::checkGen4(badBall).legal());

    pokemon::Pokemon badEvs = base;
    for (int s = 0; s < pokemon::kStatCount; ++s) badEvs.evs[s] = 100;
    CHECK(!legality::checkGen4(badEvs).legal());

    pokemon::Pokemon badMet = base;
    badMet.met.metLevel = 70;  // above the current level (60)
    CHECK(!legality::checkGen4(badMet).legal());
}

TEST(legality_evolution_level) {
    pokemon::Pokemon p = methodOnePokemon(0x1111);
    p.met.eggLocation = 0;
    p.met.metLocation = 50;
    p.met.metLevel = 5;
    p.originGame = legality::version::kPlatinum;
    pokemon::edit4::setSpecies(p, 42);  // Golbat
    pokemon::edit4::setLevel(p, 10);
    p.ability = pokemon::edit4::abilityForSlot(42, p.pid & 1);
    p.moves[0] = 141;  // Leech Life
    p.moves[1] = p.moves[2] = p.moves[3] = 0;
    // Wild: could have been caught as a Golbat, so only a warning.
    CHECK(legality::checkGen4(p).overall() == legality::Severity::Warning);
    // Hatched: it must have evolved, so level 10 is impossible.
    p.met.eggLocation = 2000;
    p.met.metLevel = 0;
    CHECK(!legality::checkGen4(p).legal());
    p.met.eggLocation = 0;
    p.met.metLevel = 5;
    pokemon::edit4::setLevel(p, 22);
    CHECK(legality::checkGen4(p).legal());
    CHECK_EQ(data::evolveLevel4(445), 48);
    CHECK_EQ(data::evolveLevel4(169), 0);  // Crobat evolves by friendship
}

TEST(encounters_lookup_and_locations) {
    const data::Encounter4* b;
    const data::Encounter4* e;
    data::encounters4(443, &b, &e);  // Gible
    bool wayward = false;
    for (; b != e; ++b) wayward |= b->location == 65 && b->minLevel <= 15;
    CHECK(wayward);
    CHECK(std::string(data::locationName4(16)) == "Route 201");
    CHECK(std::string(data::locationName4(2000)) == "Day-Care Couple");
    data::encounters4(151, &b, &e);  // Mew: event only in Gen IV
    CHECK(b == e);
}

TEST(encounter_rules_in_checker) {
    pokemon::Pokemon p = methodOnePokemon(0x2222);
    p.species = 443;  // Gible
    p.met.eggLocation = 0;
    p.met.metLocation = 65;  // Wayward Cave
    p.met.metLevel = 16;
    p.originGame = legality::version::kPlatinum;
    CHECK(legality::findEncounter4(p) == legality::EncounterStatus::Found);
    p.met.metLocation = 16;  // Route 201: no Gible there
    CHECK(legality::findEncounter4(p) == legality::EncounterStatus::NotFound);
    p.originGame = legality::version::kHeartGold;
    p.met.metLocation = 65;  // location IDs mean other places in HG/SS
    CHECK(legality::findEncounter4(p) != legality::EncounterStatus::Found);
}

TEST(legalize_gives_event_free_encounters) {
    // An edited Golbat at level 10: cannot have evolved, but wild Golbat exist
    // only at higher levels in caves; no egg either (it would be a Zubat at
    // level 10) -> unresolved is fine, but it must never claim success.
    pokemon::Pokemon p = pk4::toPokemon(test::kRoseradePk4, pk4::kPartySize);
    p.met.eggLocation = 0;
    p.met.metLocation = 16;
    p.originGame = legality::version::kPlatinum;
    pokemon::edit4::setSpecies(p, 42);
    pokemon::edit4::setLevel(p, 10);
    p.moves[0] = 141;
    p.moves[1] = p.moves[2] = p.moves[3] = 0;
    legality::LegalizeResult r = legality::legalizeGen4(p, 1);
    CHECK(r.report.legal() == r.unresolved.empty());

    // A Mew (event only in Gen IV) cannot get an encounter.
    pokemon::Pokemon d = pk4::toPokemon(test::kRoseradePk4, pk4::kPartySize);
    d.met.eggLocation = 0;
    d.originGame = legality::version::kPlatinum;
    pokemon::edit4::setSpecies(d, 151);
    pokemon::edit4::setLevel(d, 50);
    d.moves[0] = 1;  // Pound
    d.moves[1] = d.moves[2] = d.moves[3] = 0;
    legality::LegalizeResult rd = legality::legalizeGen4(d, 1);
    CHECK(!rd.unresolved.empty());

    // A Pikachu hatched from an egg (babies included).
    pokemon::Pokemon k = pk4::toPokemon(test::kRoseradePk4, pk4::kPartySize);
    k.met.eggLocation = 0;
    k.met.metLocation = 16;
    k.originGame = legality::version::kDiamond;
    pokemon::edit4::setSpecies(k, 25);
    pokemon::edit4::setLevel(k, 30);
    k.moves[0] = 84;  // Thunder Shock
    k.moves[1] = k.moves[2] = k.moves[3] = 0;
    legality::LegalizeResult rk = legality::legalizeGen4(k, 1);
    for (const auto& u : rk.unresolved) std::printf("  pikachu unresolved: %s\n", u.c_str());
    CHECK(rk.report.legal());
}
