#include <algorithm>
#include <cstdio>
#include <cstring>

#include "core/data/events4.h"
#include "core/data/events5.h"
#include "core/data/names.h"
#include "core/legality/legalize.h"
#include "core/legality/legality.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/gift4.h"
#include "core/pokemon/gift5.h"
#include "core/pokemon/pk4.h"
#include "core/pokemon/pk5.h"
#include "core/pokemon/edit4.h"
#include "core/showdown/import4.h"
#include "core/showdown/showdown.h"
#include "tests/test.h"

using namespace pktool;
namespace pk4 = pokemon::pk4;
namespace create4 = pokemon::create4;

namespace {

create4::Trainer testTrainer() {
    create4::Trainer t;
    t.name = "PKTOOL";
    t.tid = 12345;
    t.sid = 54321;
    t.version = legality::version::kPlatinum;
    t.today = {26, 9, 29};
    return t;
}

const char* kSets =
    "Chompy (Garchomp) (F) @ Choice Scarf\n"
    "Ability: Sand Veil\n"
    "Level: 50\n"
    "Shiny: Yes\n"
    "Tera Type: Dragon\n"
    "EVs: 4 HP / 252 Atk / 252 Spe\n"
    "Jolly Nature\n"
    "IVs: 0 SpA\n"
    "- Earthquake\n"
    "- Dragon Claw\n"
    "- Stone Edge\n"
    "- Fire Fang\n"
    "\n"
    "Pikachu @ Light Ball  \n"
    "- Volt Tackle\n"
    "- Hidden Power [Ice]\n"
    "- Not A Move\n";

}  // namespace

TEST(showdown_parse_sets) {
    std::vector<showdown::Set> sets = showdown::parse(kSets);
    CHECK_EQ(sets.size(), 2u);
    const showdown::Set& s = sets[0];
    CHECK_EQ(s.species, 445);
    CHECK(s.nickname == "Chompy");
    CHECK_EQ(s.gender, static_cast<int>(pokemon::Gender::Female));
    CHECK(std::strcmp(data::itemName(s.item), "Choice Scarf") == 0);
    CHECK(std::strcmp(data::abilityName(s.ability), "Sand Veil") == 0);
    CHECK_EQ(s.level, 50);
    CHECK(s.shiny);
    CHECK_EQ(s.nature, 13);
    CHECK_EQ(s.evs[pokemon::kAtk], 252);
    CHECK_EQ(s.evs[pokemon::kHp], 4);
    CHECK_EQ(s.ivs[pokemon::kSpA], 0);
    CHECK_EQ(s.ivs[pokemon::kSpe], 31);
    CHECK(std::strcmp(data::moveName(s.moves[3]), "Fire Fang") == 0);

    const showdown::Set& pika = sets[1];
    CHECK_EQ(pika.species, 25);
    CHECK_EQ(pika.moves[1], 237);  // Hidden Power
    CHECK_EQ(pika.moves[2], 0);
    CHECK(!pika.warnings.empty());
}

TEST(create_new_pokemon_is_legal_after_legalize) {
    const uint16_t species[] = {1, 25, 74, 150, 245, 393, 445, 448, 483};
    for (uint16_t sp : species) {
        pokemon::Pokemon p = create4::newPokemon(testTrainer(), sp, 70, sp * 7919u);
        CHECK(p.moves[0] != 0);
        legality::LegalizeResult r = legality::legalizeGen4(p, 4);
        if (!r.report.legal()) std::printf("  not legal: %s\n", data::speciesName(sp));
        CHECK(r.report.legal());

        uint8_t raw[pk4::kStoredSize];
        create4::encode(r.pokemon, raw);
        pokemon::Pokemon back = pk4::toPokemon(raw, pk4::kStoredSize);
        CHECK_EQ(back.species, sp);
        CHECK_EQ(back.pid, r.pokemon.pid);
        CHECK(back.nickname == r.pokemon.nickname);
        CHECK(back.trainer.name == "PKTOOL");
        CHECK_EQ(back.met.metLocation, r.pokemon.met.metLocation);
        CHECK(legality::checkGen4(back).legal());
    }
}

TEST(create_event_only_species_is_reported) {
    // Arceus has no in-game encounter in Gen IV: only its event is legal.
    pokemon::Pokemon p = create4::newPokemon(testTrainer(), 493, 80, 1);
    legality::LegalizeResult r = legality::legalizeGen4(p, 4);
    CHECK(!r.report.legal());
    CHECK(!r.unresolved.empty());
    CHECK(data::findEvent4(493) >= 0);
}

TEST(showdown_import_is_legal) {
    std::vector<showdown::Set> sets = showdown::parse(kSets);
    showdown::Imported chomp = showdown::buildGen4(sets[0], testTrainer(), 1, 4);
    for (const std::string& n : chomp.warnings) std::printf("  warning: %s\n", n.c_str());
    CHECK(chomp.report.legal());
    CHECK_EQ(chomp.pokemon.species, 445);
    CHECK_EQ(chomp.pokemon.nature, 13);
    CHECK(chomp.pokemon.shiny);
    CHECK(chomp.pokemon.gender == pokemon::Gender::Female);
    CHECK_EQ(chomp.pokemon.level, 50);
    CHECK_EQ(chomp.pokemon.evs[pokemon::kSpe], 252);
    CHECK(std::strcmp(data::moveName(chomp.pokemon.moves[0]), "Earthquake") == 0);
    CHECK(chomp.pokemon.nickname == "Chompy");
}

TEST(event_cards_receive_legal) {
    CHECK(data::events4Count() > 200);
    int illegal = 0;
    for (int i = 0; i < data::events4Count(); ++i) {
        const data::Event4& card = data::event4(i);
        uint8_t raw[pk4::kStoredSize];
        pokemon::Pokemon p = pokemon::gift4::receive(card, {26, 9, 29}, 1234u + i, raw);
        CHECK_EQ(p.species, card.species);
        CHECK(p.met.fatefulEncounter);
        CHECK_EQ(pk4::calcChecksum(raw), pk4::storedChecksum(raw));
        legality::Report r = legality::checkGen4(p);
        if (!r.legal()) {
            ++illegal;
            std::printf("  %s (card %d):", card.title, card.cardId);
            for (const auto& f : r.findings) {
                if (f.severity == legality::Severity::Invalid) std::printf(" [%s]", f.text.c_str());
            }
            std::printf("\n");
        }
        CHECK(pokemon::gift4::matchingEvent(p) != nullptr);
        data::EventPeriods periods = data::eventPeriods(card);
        uint8_t met[3] = {p.met.metDate.year, p.met.metDate.month, p.met.metDate.day};
        if (periods.count > 0) CHECK(data::inEventPeriods(periods, met));
        uint32_t templatePid = card.templatePk4[0] | card.templatePk4[1] << 8 | card.templatePk4[2] << 16 |
                               static_cast<uint32_t>(card.templatePk4[3]) << 24;
        if (templatePid <= 1) CHECK(!p.shiny);
    }
    CHECK_EQ(illegal, 0);
}

TEST(event5_cards_receive_legal) {
    CHECK(data::events5Count() > 600);
    int illegal = 0;
    for (int i = 0; i < data::events5Count(); ++i) {
        const data::Event5& card = data::event5(i);
        create4::Trainer t = testTrainer();
        t.version = pokemon::gift5::receivingGame(card, legality::version::kWhite2);
        uint8_t raw[pokemon::pk5::kStoredSize];
        pokemon::Pokemon p = pokemon::gift5::receive(card, t, 4321u + i, raw);
        CHECK_EQ(p.format, 5);
        CHECK_EQ(p.species, card.species);
        CHECK(p.met.fatefulEncounter);
        CHECK_EQ(pk4::calcChecksum(raw), pk4::storedChecksum(raw));
        CHECK(data::canReceiveEvent5(card, p.originGame));
        uint8_t pidType = card.pgf[data::pgf::kPidType];
        if (pidType == 0) CHECK(!p.shiny);
        if (pidType == 2 && !p.isEgg) CHECK(p.shiny);
        if (p.isEgg) CHECK(p.trainer.name == "PKTOOL");
        legality::Report r = legality::checkGen5(p);
        if (!r.legal()) {
            ++illegal;
            std::printf("  %s (card %d, #%d):", card.title, card.cardId, card.species);
            for (const auto& f : r.findings) {
                if (f.severity == legality::Severity::Invalid) std::printf(" [%s]", f.text.c_str());
            }
            std::printf("\n");
        }
        CHECK(pokemon::gift5::matchingEvent(p) != nullptr);
        const pokemon::Date& d = p.isEgg ? p.met.eggDate : p.met.metDate;
        uint8_t met[3] = {d.year, d.month, d.day};
        if (data::eventPeriods(card).count > 0) CHECK(data::inEventPeriods(data::eventPeriods(card), met));
    }
    CHECK_EQ(illegal, 0);
}

TEST(event_dates_fall_in_distribution_periods) {
    // 2008-03-03 to 2008-04-03 and 2009-12-30 to 2010-01-02 (across leap day and new year).
    const data::EventPeriod items[] = {{{8, 3, 3}, {8, 4, 3}}, {{9, 12, 30}, {10, 1, 2}}};
    data::EventPeriods periods{items, 2};
    bool seenFirst = false, seenSecond = false, seenLast = false;
    for (uint32_t seed = 0; seed < 2000; ++seed) {
        pokemon::Date d = pokemon::gift4::eventDate(periods, seed, {26, 1, 1});
        uint8_t day[3] = {d.year, d.month, d.day};
        CHECK(data::inEventPeriods(periods, day));
        seenFirst = seenFirst || d.year == 8;
        seenSecond = seenSecond || d.year == 9;
        seenLast = seenLast || (d.year == 10 && d.month == 1 && d.day == 2);
    }
    CHECK(seenFirst && seenSecond && seenLast);
    const uint8_t outside[3] = {8, 4, 4};
    CHECK(!data::inEventPeriods(periods, outside));
    pokemon::Date fallback = pokemon::gift4::eventDate({}, 7, {26, 1, 1});
    CHECK_EQ(fallback.year, 26);

    // GameStop's shiny Suicune (English) was handed out from January 31 to February 6, 2011.
    int dated4 = 0, suicune = 0;
    for (int i = 0; i < data::events4Count(); ++i) {
        const data::Event4& card = data::event4(i);
        data::EventPeriods p = data::eventPeriods(card);
        dated4 += p.count > 0;
        uint16_t tid = static_cast<uint16_t>(card.templatePk4[pk4::offset::kTid] |
                                             card.templatePk4[pk4::offset::kTid + 1] << 8);
        if (card.species == 245 && card.language == 2 && tid == 1311) {
            ++suicune;
            CHECK_EQ(p.count, 1);
            CHECK(p.count == 1 && p.items[0].first[0] == 11 && p.items[0].first[1] == 1 && p.items[0].first[2] == 31);
            CHECK(p.count == 1 && p.items[0].last[1] == 2 && p.items[0].last[2] == 6);
        }
    }
    CHECK_EQ(suicune, 1);
    CHECK(dated4 > 150);
}

TEST(event5_gift_is_checked_against_its_card) {
    // A Wonder Card Pokémon with a changed ball no longer matches its card.
    int index = -1;
    for (int i = 0; i < data::events5Count() && index < 0; ++i) {
        const data::Event5& card = data::event5(i);
        if (card.pgf[data::pgf::kIsEgg] == 0 && card.pgf[data::pgf::kBall] == data::kBallCherish) index = i;
    }
    CHECK(index >= 0);
    const data::Event5& card = data::event5(index);
    create4::Trainer t = testTrainer();
    t.version = pokemon::gift5::receivingGame(card, legality::version::kBlack2);
    uint8_t raw[pokemon::pk5::kStoredSize];
    pokemon::Pokemon p = pokemon::gift5::receive(card, t, 99, raw);
    CHECK(legality::checkGen5(p).legal());
    p.met.ball = data::kBallPoke;
    CHECK(!legality::checkGen5(p).legal());
}

TEST(create_uses_lowest_legal_level) {
    CHECK_EQ(legality::minCreateLevel4(150), 70);  // Mewtwo: Cerulean Cave (HG/SS)
    CHECK_EQ(legality::minCreateLevel4(25), 1);    // Pikachu hatches from eggs
    CHECK_EQ(legality::minCreateLevel4(493), 0);   // Arceus: events only

    // Too low: LEGALIZE says which level is needed.
    pokemon::Pokemon low = create4::newPokemon(testTrainer(), 150, 50, 3);
    legality::LegalizeResult r = legality::legalizeGen4(low, 4);
    CHECK(!r.report.legal());
    bool explained = false;
    for (const std::string& u : r.unresolved) explained = explained || u.find("Lv. 70") != std::string::npos;
    CHECK(explained);

    // At that level it is legal, even from a Platinum save (met in HG/SS).
    pokemon::Pokemon ok = create4::newPokemon(testTrainer(), 150, legality::minCreateLevel4(150), 3);
    legality::LegalizeResult fixed = legality::legalizeGen4(ok, 4);
    CHECK(fixed.report.legal());
    CHECK(fixed.pokemon.originGame == legality::version::kHeartGold ||
          fixed.pokemon.originGame == legality::version::kSoulSilver);
}

TEST(legalize_uses_pal_park_for_gen3_only_encounters) {
    // Kyogre: Lv. 50 in HG/SS, but Lv. 45 in Sapphire. At Lv. 47 only a
    // Pal Park transfer from Sapphire works.
    pokemon::Pokemon p = create4::newPokemon(testTrainer(), 382, 47, 11);
    legality::LegalizeResult r = legality::legalizeGen4(p, 4);
    for (const std::string& u : r.unresolved) std::printf("  unresolved: %s\n", u.c_str());
    CHECK(r.report.legal());
    CHECK(r.unresolved.empty());
    CHECK_EQ(r.pokemon.originGame, legality::version::kSapphire);
    CHECK_EQ(r.pokemon.met.metLocation, 55);
    CHECK_EQ(r.pokemon.met.metLevel, 47);
    CHECK(legality::findEncounter3(r.pokemon) == legality::EncounterStatus::Found);
    CHECK_EQ(legality::minCreateLevel4(382), 45);

    // A Gen III origin is kept when it is valid (FireRed Mewtwo).
    pokemon::Pokemon mewtwo = create4::newPokemon(testTrainer(), 150, 72, 5);
    mewtwo.originGame = legality::version::kFireRed;
    mewtwo.met.metLocation = 55;
    mewtwo.met.metLevel = 71;
    legality::LegalizeResult kept = legality::legalizeGen4(mewtwo, 4);
    CHECK(kept.report.legal());
    CHECK_EQ(kept.pokemon.originGame, legality::version::kFireRed);
    CHECK_EQ(kept.pokemon.met.metLevel, 71);

    // Below every encounter level it is reported.
    pokemon::Pokemon low = create4::newPokemon(testTrainer(), 382, 30, 11);
    CHECK(!legality::legalizeGen4(low, 4).unresolved.empty());
}

namespace {

create4::Trainer gen5Trainer() {
    create4::Trainer t = testTrainer();
    t.version = legality::version::kWhite2;
    return t;
}

}  // namespace

TEST(create_gen5_pokemon_is_legal_after_legalize) {
    const uint16_t species[] = {495, 509, 25, 147, 570, 612, 637, 643, 646, 382};
    for (uint16_t sp : species) {
        uint8_t level = std::max<uint8_t>(50, legality::minCreateLevel5(sp));
        pokemon::Pokemon p = create4::newPokemon(gen5Trainer(), sp, level, sp * 31u);
        CHECK_EQ(p.format, 5);
        CHECK(p.moves[0] != 0);
        legality::LegalizeResult r = legality::legalize(p, 4);
        if (!r.report.legal()) {
            std::printf("  not legal: %s\n", data::speciesName(sp));
            for (const std::string& u : r.unresolved) std::printf("    %s\n", u.c_str());
        }
        CHECK(r.report.legal());
        uint8_t raw[pk4::kStoredSize];
        create4::encode(r.pokemon, raw);
        pokemon::Pokemon back = pokemon::pk5::toPokemon(raw, sizeof raw);
        CHECK_EQ(back.species, sp);
        CHECK_EQ(back.nature, r.pokemon.nature);
        CHECK(legality::check(back).legal());
    }
}

TEST(gen5_legalize_keeps_requests_and_hidden_ability) {
    // Excadrill with its hidden ability (Mold Breaker) needs a Hidden Grotto
    // or an egg; a shiny Jolly one must stay shiny and Jolly.
    pokemon::Pokemon p = create4::newPokemon(gen5Trainer(), 530, 50, 7);
    CHECK(pokemon::edit4::setAbilitySlot(p, 2) == pokemon::edit4::Result::Ok);
    CHECK(pokemon::edit4::setNature(p, 13) == pokemon::edit4::Result::Ok);
    CHECK(pokemon::edit4::setShiny(p, true) == pokemon::edit4::Result::Ok);
    legality::LegalizeResult r = legality::legalize(p, 4);
    for (const std::string& u : r.unresolved) std::printf("  unresolved: %s\n", u.c_str());
    CHECK(r.report.legal());
    CHECK(r.pokemon.hiddenAbility);
    CHECK_EQ(r.pokemon.nature, 13);
    CHECK(r.pokemon.shiny);

    // A Gen V showdown set.
    std::vector<showdown::Set> sets = showdown::parse(
        "Zoroark @ Life Orb\nAbility: Illusion\nLevel: 60\nEVs: 252 SpA / 252 Spe\nTimid Nature\n"
        "- Night Daze\n- Flamethrower\n- Focus Blast\n- Nasty Plot\n");
    CHECK_EQ(sets.size(), 1u);
    showdown::Imported zoroark = showdown::buildGen4(sets[0], gen5Trainer(), 3, 4);
    for (const std::string& w : zoroark.warnings) std::printf("  warning: %s\n", w.c_str());
    CHECK_EQ(zoroark.pokemon.format, 5);
    CHECK_EQ(zoroark.pokemon.nature, 10);
    CHECK(zoroark.report.legal());
}

TEST(showdown_export_round_trip) {
    std::vector<showdown::Set> sets = showdown::parse(kSets);
    showdown::Imported chomp = showdown::buildGen4(sets[0], testTrainer(), 1, 4);
    std::string text = showdown::format(chomp.pokemon);
    std::vector<showdown::Set> back = showdown::parse(text);
    CHECK_EQ(back.size(), 1u);
    const showdown::Set& s = back[0];
    const pokemon::Pokemon& p = chomp.pokemon;
    CHECK_EQ(s.species, p.species);
    CHECK(s.nickname == p.nickname);
    CHECK_EQ(s.item, p.heldItem);
    CHECK_EQ(s.ability, p.ability);
    CHECK_EQ(s.level, p.level);
    CHECK_EQ(s.shiny, p.shiny);
    CHECK_EQ(s.nature, p.nature);
    CHECK_EQ(s.gender, static_cast<int>(p.gender));
    for (int i = 0; i < pokemon::kStatCount; ++i) {
        CHECK_EQ(s.evs[i], p.evs[i]);
        CHECK_EQ(s.ivs[i], p.ivs[i]);
    }
    for (int i = 0; i < 4; ++i) CHECK_EQ(s.moves[i], p.moves[i]);
    CHECK(s.warnings.empty());
}

TEST(eggs_are_created_legal) {
    const uint16_t species[] = {1, 25, 133, 147, 443, 495, 570, 610};
    for (uint16_t sp : species) {
        for (bool gen5 : {false, true}) {
            if (!gen5 && sp > 493) continue;
            create4::Trainer t = gen5 ? gen5Trainer() : testTrainer();
            pokemon::Pokemon egg = create4::newEgg(t, sp, sp * 13u);
            CHECK(egg.isEgg);
            legality::LegalizeResult r = legality::legalize(egg, 4);
            if (!r.report.legal()) {
                std::printf("  illegal egg: %s (gen %d)\n", data::speciesName(sp), gen5 ? 5 : 4);
                for (const std::string& u : r.unresolved) std::printf("    %s\n", u.c_str());
            }
            CHECK(r.report.legal());
            CHECK_EQ(r.pokemon.level, 1);
            // Pikachu eggs hatch into Pichu.
            if (sp == 25) CHECK_EQ(r.pokemon.species, 172);
            uint8_t raw[pk4::kStoredSize];
            create4::encode(r.pokemon, raw);
            pokemon::Pokemon back = gen5 ? pokemon::pk5::toPokemon(raw, sizeof raw) : pk4::toPokemon(raw, sizeof raw);
            CHECK(back.isEgg);
            CHECK(back.nickname == "Egg");
            CHECK(legality::check(back).legal());
        }
    }
    // A broken egg is caught and LEGALIZE repairs it.
    pokemon::Pokemon bad = create4::newEgg(testTrainer(), 4, 5);
    pokemon::edit4::setSpecies(bad, 6);  // Charizard egg
    pokemon::edit4::setLevel(bad, 30);
    bad.nickname = "CHARIZARD";
    CHECK(!legality::check(bad).legal());
    legality::LegalizeResult fixed = legality::legalize(bad, 4);
    CHECK(fixed.report.legal());
    CHECK_EQ(fixed.pokemon.species, 4);
    CHECK(fixed.pokemon.nickname == "Egg");
    // Legendaries do not come from eggs.
    pokemon::Pokemon mew = create4::newEgg(testTrainer(), 151, 9);
    CHECK(!legality::check(mew).legal());
}
