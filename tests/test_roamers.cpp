// Roaming legendaries (Gen III, IV and V): the checker verifies the roaming
// area and the PID/IV rule of PKHeX's IsRoaming encounters, and LEGALIZE builds
// roamers that pass it.

#include <cstdio>

#include "core/data/encounters3.h"
#include "core/legality/legality.h"
#include "core/legality/legalize.h"
#include "core/legality/pid_iv.h"
#include "core/legality/roamer.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/edit4.h"
#include "tests/test.h"

using namespace pktool;
namespace create4 = pokemon::create4;

namespace {

constexpr uint16_t kRaikou = 243;
constexpr uint16_t kEntei = 244;
constexpr uint16_t kLatias = 380;
constexpr uint16_t kLatios = 381;
constexpr uint16_t kMesprit = 481;
constexpr uint16_t kCresselia = 488;
constexpr uint16_t kMoltres = 146;
constexpr uint16_t kTornadus = 641;

create4::Trainer trainer(uint8_t version) {
    create4::Trainer t;
    t.name = "PKTOOL";
    t.tid = 12345;
    t.sid = 54321;
    t.version = version;
    t.today = {26, 9, 29};
    return t;
}

bool hasFinding(const legality::Report& r, legality::Severity s, const char* part) {
    for (const legality::Finding& f : r.findings) {
        if (f.severity == s && f.text.find(part) != std::string::npos) return true;
    }
    return false;
}

void dump(const char* what, const legality::LegalizeResult& r) {
    std::printf("  %s:\n", what);
    for (const std::string& s : r.changes) std::printf("    change: %s\n", s.c_str());
    for (const std::string& s : r.unresolved) std::printf("    unresolved: %s\n", s.c_str());
    for (const legality::Finding& f : r.report.findings) std::printf("    [%d] %s\n", static_cast<int>(f.severity), f.text.c_str());
}

// LEGALIZE a freshly created roamer; the result must be legal.
pokemon::Pokemon legalRoamer(uint8_t version, uint16_t species, uint8_t level, const char* what) {
    pokemon::Pokemon p = create4::newPokemon(trainer(version), species, level, species * 31u + version);
    legality::LegalizeResult r = legality::legalize(p, 4);
    if (!r.unresolved.empty() || !r.report.legal()) dump(what, r);
    CHECK(r.unresolved.empty());
    CHECK(r.report.legal());
    return r.pokemon;
}

bool roamingMatch(const pokemon::Pokemon& p) {
    const data::Encounter4* e = nullptr;
    legality::EncounterStatus s = p.format == 3   ? legality::findEncounter3Native(p, &e)
                                  : p.format == 4 ? legality::findEncounter4(p, &e)
                                                  : legality::findEncounter5(p, &e);
    return s == legality::EncounterStatus::Found && e->kind == data::kEncounterRoamer;
}

}  // namespace

TEST(roamers_met_areas_follow_pkhex) {
    namespace rm = legality::roamer;
    CHECK(rm::metOk3(legality::version::kRuby, 16));
    CHECK(rm::metOk3(legality::version::kEmerald, 49));
    CHECK(!rm::metOk3(legality::version::kEmerald, 50));
    CHECK(!rm::metOk3(legality::version::kFireRed, 16));
    CHECK(rm::metOk3(legality::version::kLeafGreen, 101));
    CHECK(rm::metOk3(legality::version::kLeafGreen, 125));
    CHECK(!rm::metOk3(legality::version::kLeafGreen, 126));
    // Gen IV: Sinnoh routes on grass, the Johto set for the beasts, Kanto for the HG/SS eon duo.
    CHECK(rm::metOk4(kMesprit, 16, rm::kGroundGrass));
    CHECK(!rm::metOk4(kMesprit, 16 + 34, rm::kGroundGrass));  // 50: not in the grass set
    CHECK(!rm::metOk4(kMesprit, 16, 0));
    CHECK(!rm::metOk4(kMesprit, 16, 4));
    CHECK(rm::metOk4(kRaikou, 177, rm::kGroundGrass));
    CHECK(!rm::metOk4(kRaikou, 177, rm::kGroundWater));  // 177 has no water
    CHECK(rm::metOk4(kRaikou, 178, rm::kGroundWater));
    CHECK(!rm::metOk4(kRaikou, 193, rm::kGroundWater));  // Route 45 surf tile
    CHECK(rm::metOk4(kLatias, 149, rm::kGroundGrass));
    CHECK(!rm::metOk4(kLatias, 177, rm::kGroundGrass));
    // Gen V: routes 15-29 and 31.
    CHECK(rm::metOk5(15));
    CHECK(rm::metOk5(29));
    CHECK(!rm::metOk5(30));
    CHECK(rm::metOk5(31));
    CHECK(!rm::metOk5(14));
    CHECK(!rm::metOk5(32));
}

TEST(roamers_gen3_have_truncated_ivs_outside_emerald) {
    // Raikou roams FireRed/LeafGreen: Method 1 PID, only HP and Attack IVs.
    pokemon::Pokemon p = legalRoamer(legality::version::kLeafGreen, kRaikou, 50, "raikou lg");
    CHECK(roamingMatch(p));
    CHECK(legality::roamer::metOk3(p.originGame, p.met.metLocation));
    CHECK_EQ(p.met.metLevel, 50);
    CHECK(p.ivs[pokemon::kDef] == 0 && p.ivs[pokemon::kSpe] == 0 && p.ivs[pokemon::kSpA] == 0 && p.ivs[pokemon::kSpD] == 0);
    CHECK(p.ivs[pokemon::kAtk] <= 7);
    CHECK(legality::roamer::ivsMatch3(p.originGame, p.pid, p.ivs));
    legality::Report r = legality::check(p);
    CHECK(hasFinding(r, legality::Severity::Valid, "roamer"));

    // Full IVs cannot come from the roamer bug.
    pokemon::Pokemon full = p;
    for (int s = 0; s < pokemon::kStatCount; ++s) full.ivs[s] = 31;
    r = legality::check(full);
    CHECK(!r.legal());
    CHECK(hasFinding(r, legality::Severity::Invalid, "Roamer"));

    // The PID must be a Method 1 frame of the surviving IV byte.
    pokemon::Pokemon pid = p;
    pid.pid ^= 0x00010000;
    pokemon::edit4::refreshDerived(pid);
    CHECK(!legality::check(pid).legal());

    // Outside the roaming area (Kanto routes 1-25): Hoenn location id.
    pokemon::Pokemon far = p;
    far.met.metLocation = 20;
    r = legality::check(far);
    CHECK(!r.legal());
    CHECK(hasFinding(r, legality::Severity::Invalid, "cannot be found"));
}

TEST(roamers_gen3_hoenn_and_emerald) {
    // Ruby: Latios roams (Latias is static); Sapphire the other way round.
    pokemon::Pokemon ruby = legalRoamer(legality::version::kRuby, kLatios, 40, "latios ruby");
    CHECK(roamingMatch(ruby));
    CHECK(ruby.ivs[pokemon::kSpe] == 0 && ruby.ivs[pokemon::kSpA] == 0);
    pokemon::Pokemon sapphire = legalRoamer(legality::version::kSapphire, kLatias, 40, "latias sapphire");
    CHECK(roamingMatch(sapphire));

    // Emerald fixed the bug: both roam, with ordinary Method 1 IVs.
    pokemon::Pokemon emerald = legalRoamer(legality::version::kEmerald, kLatias, 40, "latias emerald");
    CHECK(roamingMatch(emerald));
    CHECK(legality::findPidIvMethod(emerald.pid, emerald.ivs) == legality::PidIvMethod::Method1);
    legality::Report r = legality::check(emerald);
    CHECK(hasFinding(r, legality::Severity::Valid, "Method 1 (roamer)"));
    // ... so a PID that is no Method 1 frame is wrong there.
    pokemon::Pokemon bad = emerald;
    bad.pid ^= 0x00010000;
    pokemon::edit4::refreshDerived(bad);
    CHECK(!legality::check(bad).legal());

    // A different level is not the roamer.
    pokemon::Pokemon level = ruby;
    level.met.metLevel = 41;
    level.level = 41;
    CHECK(!roamingMatch(level));
}

TEST(roamers_gen3_shiny_and_nature_requests) {
    pokemon::Pokemon p = create4::newPokemon(trainer(legality::version::kFireRed), kEntei, 50, 777);
    p.shiny = true;
    p.nature = 3;
    legality::LegalizeResult r = legality::legalize(p, 4);
    if (!r.unresolved.empty()) dump("entei shiny", r);
    CHECK(r.unresolved.empty());
    CHECK(r.pokemon.shiny);
    CHECK_EQ(r.pokemon.nature, 3);
    CHECK(roamingMatch(r.pokemon));
    CHECK(r.report.legal());
}

TEST(roamers_gen4_dppt_and_hgss) {
    namespace rm = legality::roamer;
    pokemon::Pokemon mesprit = legalRoamer(legality::version::kPlatinum, kMesprit, 50, "mesprit pt");
    CHECK(roamingMatch(mesprit));
    CHECK(rm::metOk4(kMesprit, mesprit.met.metLocation, mesprit.met.encounterType));
    CHECK(legality::findPidIvMethod(mesprit.pid, mesprit.ivs) == legality::PidIvMethod::Method1);
    CHECK(legality::check(mesprit).legal());

    pokemon::Pokemon cresselia = legalRoamer(legality::version::kDiamond, kCresselia, 50, "cresselia d");
    CHECK(roamingMatch(cresselia));
    pokemon::Pokemon moltres = legalRoamer(legality::version::kPlatinum, kMoltres, 60, "moltres pt");
    CHECK(roamingMatch(moltres));

    pokemon::Pokemon raikou = legalRoamer(legality::version::kHeartGold, kRaikou, 40, "raikou hg");
    CHECK(roamingMatch(raikou));
    CHECK(raikou.met.metLocation >= 177);
    pokemon::Pokemon latias = legalRoamer(legality::version::kHeartGold, kLatias, 35, "latias hg");
    CHECK(roamingMatch(latias));
    CHECK(rm::metOk4(kLatias, latias.met.metLocation, latias.met.encounterType));

    // Invalid variants: tile, area, PID, level.
    pokemon::Pokemon tile = mesprit;
    tile.met.encounterType = 0;
    CHECK(!legality::check(tile).legal());
    pokemon::Pokemon area = mesprit;
    area.met.metLocation = 70;
    legality::Report r = legality::check(area);
    CHECK(!r.legal());
    CHECK(hasFinding(r, legality::Severity::Invalid, "cannot be found"));
    pokemon::Pokemon kanto = latias;
    kanto.met.metLocation = 177;  // Johto area, not the eon duo's Kanto
    CHECK(!legality::check(kanto).legal());
    pokemon::Pokemon pid = mesprit;
    pid.pid = 0x12345678;
    pokemon::edit4::refreshDerived(pid);
    r = legality::check(pid);
    CHECK(!r.legal());
    CHECK(!hasFinding(r, legality::Severity::Warning, "roamer"));
    pokemon::Pokemon level = mesprit;
    level.met.metLevel = 49;
    CHECK(!roamingMatch(level));
}

TEST(roamers_gen4_surf_on_route_45_is_invalid) {
    pokemon::Pokemon raikou = legalRoamer(legality::version::kSoulSilver, kRaikou, 40, "raikou ss");
    raikou.met.metLocation = 193;  // Route 45
    raikou.met.encounterType = legality::roamer::kGroundGrass;
    CHECK(roamingMatch(raikou));
    raikou.met.encounterType = legality::roamer::kGroundWater;
    CHECK(!roamingMatch(raikou));
}

TEST(roamers_gen5_tornadus_and_thundurus) {
    pokemon::Pokemon tornadus = legalRoamer(legality::version::kBlack, kTornadus, 40, "tornadus b");
    CHECK(roamingMatch(tornadus));
    CHECK(legality::roamer::metOk5(tornadus.met.metLocation));
    CHECK(legality::check(tornadus).legal());
    pokemon::Pokemon thundurus = legalRoamer(legality::version::kWhite, 642, 40, "thundurus w");
    CHECK(roamingMatch(thundurus));

    pokemon::Pokemon area = tornadus;
    area.met.metLocation = 30;
    legality::Report r = legality::check(area);
    CHECK(!r.legal());
    CHECK(hasFinding(r, legality::Severity::Invalid, "cannot be found"));
    pokemon::Pokemon level = tornadus;
    level.met.metLevel = 41;
    level.level = 41;
    CHECK(!roamingMatch(level));
}
