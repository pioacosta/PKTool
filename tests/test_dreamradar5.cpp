// Gen V Dream Radar (Black 2 / White 2): PKHeX's EncounterStatic5Radar table,
// its checker and LEGALIZE (Therian Formes, hidden abilities).

#include <cstdio>

#include "core/data/dreamworld5.h"
#include "core/data/names.h"
#include "core/data/personal.h"
#include "core/legality/legality.h"
#include "core/legality/legality5_dw.h"
#include "core/legality/legalize.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/edit4.h"
#include "tests/test.h"

using namespace pktool;
namespace create4 = pokemon::create4;
namespace edit4 = pokemon::edit4;

namespace {

constexpr uint8_t kDreamBall = 25;
constexpr uint16_t kRalts = 280;
constexpr uint16_t kKirlia = 281;
constexpr uint16_t kRotom = 479;
constexpr uint16_t kTornadus = 641;
constexpr uint16_t kThundurus = 642;
constexpr uint16_t kLandorus = 645;
constexpr uint16_t kRegenerator = 144;

create4::Trainer trainer5(uint8_t version = legality::version::kBlack2) {
    create4::Trainer t;
    t.name = "PKTOOL";
    t.tid = 12345;
    t.sid = 54321;
    t.version = version;
    t.today = {26, 9, 29};
    return t;
}

// A genuine Dream Radar Pokémon: met at "Dream Radar" at `metLevel`, Dream
// Ball, hidden ability, not shiny.
pokemon::Pokemon radar(uint16_t species, uint8_t level, uint8_t metLevel = 5) {
    pokemon::Pokemon p = create4::newPokemon(trainer5(), species, level, species * 77u + level);
    p.met.metLocation = legality::kLocationDreamRadar5;
    p.met.metLevel = metLevel;
    p.met.eggLocation = 0;
    p.met.ball = kDreamBall;
    p.met.fatefulEncounter = false;
    if (species != kRotom) CHECK(edit4::setAbilitySlot(p, 2) == edit4::Result::Ok);
    CHECK(edit4::setShiny(p, false) == edit4::Result::Ok);
    return p;
}

pokemon::Pokemon therian(uint16_t species, uint8_t level) {
    pokemon::Pokemon p = radar(species, level, 40);
    p.form = 1;
    p.hiddenAbility = true;
    p.ability = species == kTornadus ? 144 : species == kThundurus ? 10 : 22;
    return p;
}

bool hasFinding(const legality::Report& r, legality::Severity s, const char* part) {
    for (const legality::Finding& f : r.findings) {
        if (f.severity == s && f.text.find(part) != std::string::npos) return true;
    }
    return false;
}

void dump(const char* what, const legality::Report& r) {
    std::printf("  %s:\n", what);
    for (const legality::Finding& f : r.findings) {
        std::printf("    [%d] %s\n", static_cast<int>(f.severity), f.text.c_str());
    }
}

}  // namespace

TEST(radar5_table_matches_pkhex) {
    int count = 0;
    const data::DreamRadar5* table = data::dreamRadarTable5(&count);
    CHECK_EQ(count, 26);
    CHECK(table != nullptr);
    CHECK(data::dreamRadar5(79) != nullptr);   // Slowpoke
    CHECK(data::dreamRadar5(249) != nullptr);  // Lugia
    CHECK(data::dreamRadar5(25) == nullptr);   // Pikachu
    const data::DreamRadar5* rotom = data::dreamRadar5(kRotom);
    CHECK(rotom != nullptr && rotom->anyAbility == 1);
    for (uint16_t species : {kTornadus, kThundurus, kLandorus}) {
        const data::DreamRadar5* e = data::dreamRadar5(species);
        CHECK(e != nullptr && e->form == 1 && e->anyAbility == 0);
    }
}

TEST(radar5_genuine_pokemon_pass) {
    pokemon::Pokemon ralts = radar(kRalts, 5);
    legality::Report r = legality::checkGen5(ralts);
    if (!r.legal()) dump("ralts", r);
    CHECK(r.legal());
    CHECK(hasFinding(r, legality::Severity::Valid, "Dream Radar"));
    CHECK(!hasFinding(r, legality::Severity::Warning, "Dream Radar"));

    // Raised and evolved afterwards, still met at a multiple of 5.
    pokemon::Pokemon kirlia = radar(kKirlia, 30, 25);
    r = legality::checkGen5(kirlia);
    if (!r.legal()) dump("kirlia", r);
    CHECK(r.legal());

    pokemon::Pokemon tornadus = therian(kTornadus, 40);
    r = legality::checkGen5(tornadus);
    if (!r.legal()) dump("tornadus", r);
    CHECK(r.legal());
    CHECK(legality::checkGen5(therian(kThundurus, 40)).legal());
    CHECK(legality::checkGen5(therian(kLandorus, 45)).legal());

    // Rotom has no hidden ability to receive.
    pokemon::Pokemon rotom = radar(kRotom, 5);
    rotom.hiddenAbility = false;
    rotom.ability = edit4::abilityForSlot(kRotom, 0);
    r = legality::checkGen5(rotom);
    if (!r.legal()) dump("rotom", r);
    CHECK(r.legal());
}

TEST(radar5_invalid_variants_are_caught) {
    const pokemon::Pokemon good = radar(kRalts, 20, 15);
    CHECK(legality::checkGen5(good).legal());

    pokemon::Pokemon p = good;
    p.met.metLevel = 12;
    CHECK(hasFinding(legality::checkGen5(p), legality::Severity::Invalid, "multiple of 5"));
    p = good;
    p.met.metLevel = 45;
    p.level = 50;
    CHECK(hasFinding(legality::checkGen5(p), legality::Severity::Invalid, "multiple of 5"));
    p = good;
    p.met.ball = data::kBallPoke;
    CHECK(hasFinding(legality::checkGen5(p), legality::Severity::Invalid, "Dream Ball"));
    p = good;
    p.originGame = legality::version::kBlack;
    CHECK(hasFinding(legality::checkGen5(p), legality::Severity::Invalid, "Black 2 / White 2"));
    p = good;
    p.hiddenAbility = false;
    p.ability = edit4::abilityForSlot(kRalts, 0);
    CHECK(hasFinding(legality::checkGen5(p), legality::Severity::Invalid, "hidden ability"));
    p = good;
    p.met.fatefulEncounter = true;
    CHECK(!legality::checkGen5(p).legal());
    p = good;
    p.met.eggLocation = 60002;
    CHECK(!legality::checkGen5(p).legal());
    p = good;
    CHECK(edit4::setShiny(p, true) == edit4::Result::Ok);
    CHECK(hasFinding(legality::checkGen5(p), legality::Severity::Invalid, "never shiny"));

    // A species the radar does not give, or an evolved one below its level.
    p = radar(25, 10);  // Pikachu
    CHECK(hasFinding(legality::checkGen5(p), legality::Severity::Invalid, "cannot be found at the Dream Radar"));
    p = radar(kKirlia, 15, 15);
    CHECK(!legality::checkGen5(p).legal());  // Kirlia needs level 20

    // Wrong form: the radar's genies are Therian.
    p = therian(kTornadus, 40);
    p.form = 0;
    CHECK(hasFinding(legality::checkGen5(p), legality::Severity::Invalid, "Form"));
}

TEST(radar5_therian_formes_exist_only_there) {
    pokemon::Pokemon p = therian(kLandorus, 45);
    p.met.metLocation = 45;  // somewhere else
    CHECK(hasFinding(legality::checkGen5(p), legality::Severity::Invalid, "Therian"));
}

TEST(radar5_legalize_builds_therian_formes) {
    for (uint16_t species : {kTornadus, kThundurus, kLandorus}) {
        pokemon::Pokemon p = create4::newPokemon(trainer5(legality::version::kWhite2), species, 50, species);
        p.form = 1;
        legality::LegalizeResult r = legality::legalize(p, 4);
        if (!r.unresolved.empty() || !r.report.legal()) dump("therian", r.report);
        CHECK(r.unresolved.empty());
        CHECK(r.report.legal());
        CHECK_EQ(r.pokemon.form, 1);
        CHECK_EQ(r.pokemon.met.metLocation, legality::kLocationDreamRadar5);
        CHECK_EQ(r.pokemon.met.ball, kDreamBall);
        CHECK(r.pokemon.hiddenAbility);
        CHECK(r.pokemon.met.metLevel % 5 == 0 && r.pokemon.met.metLevel <= 40 && r.pokemon.met.metLevel >= 5);
        CHECK(!r.pokemon.shiny);
        if (species == kTornadus) CHECK_EQ(r.pokemon.ability, kRegenerator);
    }
    // From a Black save the origin moves to Black 2.
    pokemon::Pokemon p = create4::newPokemon(trainer5(legality::version::kBlack), kTornadus, 50, 5);
    p.form = 1;
    legality::LegalizeResult r = legality::legalize(p, 4);
    CHECK(r.unresolved.empty());
    CHECK(r.pokemon.originGame == legality::version::kBlack2 || r.pokemon.originGame == legality::version::kWhite2);

    // A shiny Therian cannot exist.
    p = create4::newPokemon(trainer5(legality::version::kBlack2), kTornadus, 50, 7);
    p.form = 1;
    p.shiny = true;
    r = legality::legalize(p, 4);
    CHECK(!r.unresolved.empty());
}

TEST(radar5_legalize_fixes_a_broken_radar_pokemon) {
    pokemon::Pokemon p = radar(kKirlia, 30, 25);
    p.met.metLevel = 12;
    p.met.ball = data::kBallPoke;
    legality::LegalizeResult r = legality::legalize(p, 4);
    if (!r.unresolved.empty() || !r.report.legal()) dump("kirlia", r.report);
    CHECK(r.unresolved.empty());
    CHECK(r.report.legal());
    CHECK_EQ(r.pokemon.met.metLocation, legality::kLocationDreamRadar5);
    CHECK_EQ(r.pokemon.met.metLevel, 30);
    CHECK_EQ(r.pokemon.met.ball, kDreamBall);
}

TEST(radar5_legalize_hidden_ability_requests_stay_legal) {
    // Whatever route gives a hidden ability (Hidden Grotto, Dream World, Dream
    // Radar, Day-Care), the result must pass the checker.
    for (uint16_t species : {kRalts, uint16_t{374}, uint16_t{442}, uint16_t{447}, uint16_t{561}}) {
        pokemon::Pokemon p = create4::newPokemon(trainer5(legality::version::kBlack2), species, 40, species);
        CHECK(edit4::setAbilitySlot(p, 2) == edit4::Result::Ok);
        legality::LegalizeResult r = legality::legalize(p, 4);
        if (!r.unresolved.empty() || !r.report.legal()) {
            std::printf("species %u\n", species);
            dump("hidden", r.report);
            for (const std::string& u : r.unresolved) std::printf("    unresolved: %s\n", u.c_str());
        }
        CHECK(r.report.legal());
        CHECK(r.pokemon.hiddenAbility);
    }
}
