// Gen V Dream World (Entree Forest) and N's Pokémon legality, LEGALIZE through
// the Dream World, and the N's / Pokéstar fields of the PK5 codec.

#include <cstdio>

#include "core/data/dreamworld5.h"
#include "core/data/encounters5.h"
#include "core/data/learn5.h"
#include "core/data/names.h"
#include "core/data/personal.h"
#include "core/legality/legality.h"
#include "core/legality/legalize.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/edit4.h"
#include "core/pokemon/pk5.h"
#include "tests/test.h"

using namespace pktool;
namespace create4 = pokemon::create4;
namespace edit4 = pokemon::edit4;

namespace {

constexpr uint8_t kEntreeForest = 75;
constexpr uint8_t kDreamBall = 25;
constexpr uint16_t kRattata = 19;
constexpr uint16_t kPidgeotto = 17;
constexpr uint16_t kShellos = 422;
constexpr uint16_t kSandile = 551;
constexpr uint16_t kZorua = 570;
constexpr uint16_t kMeFirst = 382;

create4::Trainer trainer5(uint8_t version = legality::version::kWhite2) {
    create4::Trainer t;
    t.name = "PKTOOL";
    t.tid = 12345;
    t.sid = 54321;
    t.version = version;
    t.today = {26, 9, 29};
    return t;
}

// A Dream World capture of `species`: met at the Entree Forest at `metLevel`,
// Dream Ball, hidden ability, not shiny.
pokemon::Pokemon dreamWorld(uint16_t species, uint8_t level, uint8_t metLevel = 10, uint8_t version = legality::version::kWhite2) {
    pokemon::Pokemon p = create4::newPokemon(trainer5(version), species, level, species * 77u + level);
    p.met.metLocation = kEntreeForest;
    p.met.metLevel = metLevel;
    p.met.eggLocation = 0;
    p.met.ball = kDreamBall;
    p.met.fatefulEncounter = false;
    CHECK(edit4::setAbilitySlot(p, 2) == edit4::Result::Ok);
    CHECK(edit4::setShiny(p, false) == edit4::Result::Ok);
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

// A Pokémon caught from N in Black 2 / White 2 with the given fixed PID.
pokemon::Pokemon nPokemon(uint16_t species, uint8_t level, uint8_t location, uint32_t pid, uint8_t nature, uint8_t slot) {
    create4::Trainer t = trainer5();
    t.name = "N";
    t.tid = 2;
    t.sid = 0;
    pokemon::Pokemon p = create4::newPokemon(t, species, level, 99);
    p.pid = pid;
    p.shiny = pokemon::isShiny(p.pid, 2, 0);
    p.gender = edit4::genderFromPid(species, p.pid);
    p.nature = nature;
    p.hiddenAbility = false;
    p.ability = edit4::abilityForSlot(species, slot);
    for (int s = 0; s < pokemon::kStatCount; ++s) p.ivs[s] = 30;
    p.met.metLocation = location;
    p.met.metLevel = level;
    p.met.eggLocation = 0;
    p.met.ball = data::kBallPoke;
    p.met.fatefulEncounter = false;
    p.nSparkle = true;
    return p;
}

// Sandile caught from N in Black 2 / White 2 (PID 0xFF01007F, second ability).
pokemon::Pokemon nSandile() {
    return nPokemon(kSandile, 22, 34, 0xFF01007F, 6, 1);  // Desert Resort, Docile
}

}  // namespace

TEST(dw5_table_is_sorted_and_complete) {
    int previous = 0;
    int count = 0;
    for (int i = 0; i < data::generated::kDreamWorld5Count; ++i) {
        const data::DreamWorld5& e = data::generated::kDreamWorld5[i];
        CHECK(e.species >= previous);
        CHECK(e.species >= 1 && e.species <= data::kMaxSpeciesGen5);
        previous = e.species;
        ++count;
    }
    CHECK(count > 250);
    const data::DreamWorld5* begin;
    const data::DreamWorld5* end;
    data::dreamWorld5(kRattata, &begin, &end);
    CHECK_EQ(end - begin, 1);
    CHECK_EQ(begin->level, 10);
    data::dreamWorld5(kShellos, &begin, &end);
    CHECK_EQ(end - begin, 2);  // west and east sea
    int n = 0;
    CHECK(data::nPokemon5(&n) != nullptr);
    CHECK_EQ(n, 15);
}

TEST(dw5_genuine_dream_world_pokemon_pass) {
    pokemon::Pokemon rattata = dreamWorld(kRattata, 10);
    legality::Report r = legality::checkGen5(rattata);
    if (!r.legal()) dump("rattata", r);
    CHECK(r.legal());
    CHECK(hasFinding(r, legality::Severity::Valid, "Entree Forest"));

    // One of the special moves (Me First) is allowed.
    rattata.moves[3] = kMeFirst;
    rattata.pp[3] = 5;
    CHECK(legality::checkGen5(rattata).legal());

    // Evolved afterwards: Pidgeotto at level 25 from a level 10 Pidgey, also in B/W.
    pokemon::Pokemon pidgeotto = dreamWorld(kPidgeotto, 25, 10, legality::version::kBlack);
    r = legality::checkGen5(pidgeotto);
    if (!r.legal()) dump("pidgeotto", r);
    CHECK(r.legal());

    // The wild balls are fine too.
    pidgeotto.met.ball = data::kBallPoke;
    CHECK(legality::checkGen5(pidgeotto).legal());
}

TEST(dw5_invalid_dream_world_pokemon_fail) {
    pokemon::Pokemon good = dreamWorld(kRattata, 30);
    CHECK(legality::checkGen5(good).legal());

    pokemon::Pokemon p = good;  // wrong level
    p.met.metLevel = 11;
    CHECK(!legality::checkGen5(p).legal());

    p = good;  // ball that cannot be used
    p.met.ball = data::kBallCherish;
    CHECK(!legality::checkGen5(p).legal());
    p.met.ball = 5;  // Safari Ball
    CHECK(!legality::checkGen5(p).legal());

    p = good;  // no hidden ability although the species has one
    CHECK(edit4::setAbilitySlot(p, 0) == edit4::Result::Ok);
    legality::Report r = legality::checkGen5(p);
    CHECK(!r.legal());
    CHECK(hasFinding(r, legality::Severity::Invalid, "hidden ability"));

    p = good;  // shiny
    CHECK(edit4::setShiny(p, true) == edit4::Result::Ok);
    CHECK(!legality::checkGen5(p).legal());

    p = good;  // a move that is neither learnable nor a special move
    p.moves[3] = 448;  // Spacial Rend
    p.pp[3] = 5;
    CHECK(!legality::checkGen5(p).legal());

    p = good;  // a species the Dream World never gave
    p = dreamWorld(495, 30);
    CHECK(!legality::checkGen5(p).legal());

    p = good;  // Dream Ball outside the Entree Forest
    p.met.metLocation = 34;
    CHECK(!legality::checkGen5(p).legal());

    p = good;  // fateful encounter flag
    p.met.fatefulEncounter = true;
    CHECK(!legality::checkGen5(p).legal());

    // Shellos is only in Black / White.
    CHECK(legality::checkGen5(dreamWorld(kShellos, 10, 10, legality::version::kBlack)).legal());
    CHECK(!legality::checkGen5(dreamWorld(kShellos, 10, 10, legality::version::kWhite2)).legal());
}

namespace {

bool learnableByLine(uint16_t species, uint16_t move) {
    for (uint16_t s = species; s != 0; s = data::preEvolution5(s)) {
        if (data::findLearn5(s, move) != nullptr) return true;
    }
    return false;
}

}  // namespace

TEST(dw5_only_one_special_move) {
    int tested = 0;
    for (int i = 0; i < data::generated::kDreamWorld5Count && tested < 8; ++i) {
        const data::DreamWorld5& e = data::generated::kDreamWorld5[i];
        const data::PersonalInfo* info = data::personal5(e.species);
        if (!(e.versions & data::kVerWhite2) || e.gender != data::kDwAny || e.form != data::kDwAny || e.level != 10 ||
            info == nullptr || info->hiddenAbility == 0 || info->hiddenAbility == info->abilities[0]) {
            continue;
        }
        uint16_t odd[3];
        int n = 0;
        for (uint16_t m : e.moves) {
            if (m != 0 && !learnableByLine(e.species, m)) odd[n++] = m;
        }
        if (n < 2) continue;
        pokemon::Pokemon p = dreamWorld(e.species, 10);
        CHECK(legality::checkGen5(p).legal());
        p.moves[2] = odd[0];
        p.pp[2] = 1;
        legality::Report one = legality::checkGen5(p);
        if (!one.legal()) dump(data::speciesName(e.species), one);
        CHECK(one.legal());
        p.moves[3] = odd[1];
        p.pp[3] = 1;
        CHECK(!legality::checkGen5(p).legal());
        ++tested;
    }
    CHECK(tested > 0);
}

TEST(dw5_forced_gender_is_checked) {
    // The Global Link Garchomp (level 48) is always male.
    pokemon::Pokemon p = dreamWorld(445, 48, 48);
    p.gender = pokemon::Gender::Male;
    p.pid = (p.pid & ~0xFFu) | 0x80;  // male PID for Garchomp (50%)
    if (pokemon::isShiny(p.pid, p.trainer.tid, p.trainer.sid)) p.pid ^= 0x100;
    p.shiny = false;
    legality::Report r = legality::checkGen5(p);
    if (!r.legal()) dump("garchomp", r);
    CHECK(r.legal());
    p.pid = (p.pid & ~0xFFu) | 0xFF;  // extreme gender value cannot come from the forced gender
    CHECK(!legality::checkGen5(p).legal());
    p.pid = (p.pid & ~0xFFu) | 0x10;  // female PID
    p.gender = pokemon::Gender::Female;
    CHECK(!legality::checkGen5(p).legal());
}

TEST(dw5_legalize_uses_dream_world_for_hidden_ability) {
    pokemon::Pokemon p = create4::newPokemon(trainer5(), kRattata, 30, 5);
    CHECK(edit4::setAbilitySlot(p, 2) == edit4::Result::Ok);
    legality::LegalizeResult r = legality::legalize(p, 4);
    for (const std::string& u : r.unresolved) std::printf("  unresolved: %s\n", u.c_str());
    if (!r.report.legal()) dump("legalized rattata", r.report);
    CHECK(r.report.legal());
    CHECK(r.pokemon.hiddenAbility);
    CHECK_EQ(r.pokemon.met.metLocation, kEntreeForest);
    CHECK_EQ(r.pokemon.met.metLevel, 10);
    CHECK_EQ(r.pokemon.met.eggLocation, 0);
    CHECK(!r.pokemon.shiny);

    // With one of the special moves.
    pokemon::Pokemon q = create4::newPokemon(trainer5(), kRattata, 30, 5);
    CHECK(edit4::setAbilitySlot(q, 2) == edit4::Result::Ok);
    q.moves[3] = kMeFirst;
    q.pp[3] = 5;
    r = legality::legalize(q, 4);
    CHECK(r.report.legal());
    CHECK_EQ(r.pokemon.met.metLocation, kEntreeForest);
    CHECK_EQ(r.pokemon.moves[3], kMeFirst);

    // An evolved Pokémon is caught as its pre-evolution.
    pokemon::Pokemon e = create4::newPokemon(trainer5(), kPidgeotto, 30, 8);
    CHECK(edit4::setAbilitySlot(e, 2) == edit4::Result::Ok);
    r = legality::legalize(e, 4);
    if (!r.report.legal()) dump("legalized pidgeotto", r.report);
    CHECK(r.report.legal());
    CHECK(r.pokemon.hiddenAbility);

    // A shiny cannot come from the Dream World: it falls back to another route.
    pokemon::Pokemon s = create4::newPokemon(trainer5(), kRattata, 30, 5);
    CHECK(edit4::setAbilitySlot(s, 2) == edit4::Result::Ok);
    CHECK(edit4::setShiny(s, true) == edit4::Result::Ok);
    r = legality::legalize(s, 4);
    CHECK(r.report.legal());
    CHECK(r.pokemon.shiny);
    CHECK(r.pokemon.met.metLocation != kEntreeForest);
}

TEST(dw5_hidden_ability_needs_a_species_with_one) {
    // Pupitar has no hidden ability: the table repeats its first ability.
    pokemon::Pokemon p = create4::newPokemon(trainer5(), 247, 40, 3);
    CHECK(edit4::setAbilitySlot(p, 2) == edit4::Result::Impossible);
    p.hiddenAbility = true;
    CHECK(!legality::checkGen5(p).legal());
}

TEST(dw5_n_pokemon_recognised) {
    pokemon::Pokemon p = nSandile();
    legality::Report r = legality::checkGen5(p);
    if (!r.legal()) dump("n's sandile", r);
    CHECK(r.legal());
    CHECK(hasFinding(r, legality::Severity::Valid, "N's Pokemon"));

    // Evolved N's Sandile (Krokorok keeps the PID).
    pokemon::Pokemon k = p;
    k.species = 552;
    k.level = 30;
    k.exp = data::expForLevel(static_cast<data::GrowthRate>(data::personal5(552)->growthRate), 30);
    k.ability = edit4::abilityForSlot(552, 1);
    for (int i = 0; i < 4; ++i) {
        k.moves[i] = p.moves[i];
        k.pp[i] = p.pp[i];
    }
    CHECK(legality::checkGen5(k).legal());
}

TEST(dw5_n_pokemon_rules) {
    pokemon::Pokemon good = nSandile();
    CHECK(legality::checkGen5(good).legal());

    pokemon::Pokemon p = good;  // flag missing
    p.nSparkle = false;
    legality::Report r = legality::checkGen5(p);
    CHECK(!r.legal());
    CHECK(hasFinding(r, legality::Severity::Invalid, "sparkle"));

    p = good;
    p.trainer.name = "Bob";
    CHECK(!legality::checkGen5(p).legal());
    p = good;
    p.trainer.tid = 3;
    CHECK(!legality::checkGen5(p).legal());
    p = good;
    p.ivs[2] = 29;
    CHECK(!legality::checkGen5(p).legal());
    p = good;
    p.nature = 7;
    CHECK(!legality::checkGen5(p).legal());
    p = good;
    p.met.metLevel = 21;
    CHECK(!legality::checkGen5(p).legal());
    p = good;
    p.met.metLocation = 33;
    CHECK(!legality::checkGen5(p).legal());
    p = good;
    p.originGame = legality::version::kBlack;  // N's Pokémon are B2/W2 only
    CHECK(!legality::checkGen5(p).legal());
    p = good;
    p.met.ball = 3;  // Great Ball is fine for Sandile
    CHECK(legality::checkGen5(p).legal());
    p.met.ball = kDreamBall;
    CHECK(!legality::checkGen5(p).legal());

    // N's Zorua cannot have been captured: Poké Ball only.
    pokemon::Pokemon z = nPokemon(kZorua, 25, 10, 0xFF00001F, 11, 0);  // Driftveil City, Hasty
    legality::Report zr = legality::checkGen5(z);
    if (!zr.legal()) dump("n's zorua", zr);
    CHECK(zr.legal());
    z.met.ball = 3;
    CHECK(!legality::checkGen5(z).legal());
}

TEST(dw5_sparkle_flag_on_other_pokemon_is_invalid) {
    pokemon::Pokemon p = create4::newPokemon(trainer5(), kRattata, 12, 1);
    p.met.metLocation = 0;
    pokemon::Pokemon legal = legality::legalize(p, 4).pokemon;
    CHECK(legality::checkGen5(legal).legal());
    CHECK(!legal.nSparkle);

    pokemon::Pokemon flagged = legal;
    flagged.nSparkle = true;
    legality::Report r = legality::checkGen5(flagged);
    CHECK(!r.legal());
    CHECK(hasFinding(r, legality::Severity::Invalid, "N's Pokemon flag"));

    // LEGALIZE removes the flag.
    legality::LegalizeResult fixed = legality::legalize(flagged, 4);
    CHECK(!fixed.pokemon.nSparkle);
    CHECK(fixed.report.legal());
}

TEST(dw5_pokestar_fame_rules) {
    pokemon::Pokemon p = create4::newPokemon(trainer5(), kRattata, 12, 1);
    pokemon::Pokemon legal = legality::legalize(p, 4).pokemon;
    CHECK(legality::checkGen5(legal).legal());
    for (uint8_t fame : {uint8_t(25), uint8_t(30), uint8_t(255)}) {
        legal.pokeStarFame = fame;
        CHECK(legality::checkGen5(legal).legal());
    }
    legal.pokeStarFame = 7;
    CHECK(!legality::checkGen5(legal).legal());

    pokemon::Pokemon ditto = create4::newPokemon(trainer5(), 132, 30, 2);
    ditto = legality::legalize(ditto, 4).pokemon;
    CHECK(legality::checkGen5(ditto).legal());
    ditto.pokeStarFame = 25;
    CHECK(!legality::checkGen5(ditto).legal());
}

TEST(dw5_pk5_codec_keeps_sparkle_and_fame) {
    pokemon::Pokemon p = nSandile();
    p.pokeStarFame = 50;
    uint8_t raw[pokemon::pk4::kStoredSize];
    create4::encode(p, raw);
    pokemon::Pokemon back = pokemon::pk5::toPokemon(raw, sizeof raw);
    CHECK(back.nSparkle);
    CHECK_EQ(back.pokeStarFame, 50);
    CHECK_EQ(back.pid, 0xFF01007Fu);
    CHECK(!back.hiddenAbility);

    // Clearing the flag on an existing entity leaves the hidden-ability bit alone.
    back.hiddenAbility = true;
    back.nSparkle = false;
    pokemon::pk5::applyPokemon(back, raw);
    pokemon::Pokemon again = pokemon::pk5::toPokemon(raw, sizeof raw);
    CHECK(!again.nSparkle);
    CHECK(again.hiddenAbility);
}
