// Colosseum bonus-disc gifts of PKHeX's Encounters3RSE (Pikachu, Celebi, Mt. Battle Ho-Oh).

#include <cstdio>
#include <string>

#include "core/data/events3.h"
#include "core/legality/legality.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/cxd3.h"
#include "core/pokemon/gift3.h"
#include "core/pokemon/pk3.h"
#include "tests/test.h"

using namespace pktool;

namespace {

int bonusIndex(uint16_t species) {
    for (int i = 0; i < data::gc3Count(); ++i) {
        if (data::gc3(i).kind == data::kGcColoBonus && data::gc3(i).species == species) return i;
    }
    return -1;
}

pokemon::Pokemon make(int index, uint8_t language, uint32_t seed = 5) {
    pokemon::create4::Trainer t;
    t.name = "ASH";
    t.tid = 1234;
    t.sid = 4321;
    t.language = language;
    t.version = 3;
    uint8_t raw[pokemon::pk3::kStoredSize];
    pokemon::Pokemon p = pokemon::cxd3::create(index, t, seed, pokemon::gift3::Criteria(), raw);
    return pokemon::pk3::toPokemon(raw, pokemon::pk3::kStoredSize);
}

bool has(const legality::Report& r, legality::Severity s, const char* text) {
    for (const legality::Finding& f : r.findings) {
        if (f.severity == s && f.text.find(text) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

TEST(colo_bonus_gifts_are_in_the_table) {
    int pika = bonusIndex(25), celebi = bonusIndex(251), hooh = bonusIndex(250);
    CHECK(pika >= 0 && celebi >= 0 && hooh >= 0);
    CHECK_EQ(data::gc3(pika).level, 10);
    CHECK_EQ(data::gc3(pika).tid, 31121);
    CHECK_EQ(data::gc3(celebi).tid, 31121);
    CHECK((data::gc3(celebi).flags & data::kGcFemaleOt) != 0);
    CHECK((data::gc3(pika).flags & data::kGcFemaleOt) == 0);
    CHECK_EQ(data::gc3(hooh).level, 70);
    CHECK_EQ(data::gc3(hooh).tid, 10048);
    CHECK_EQ(data::gc3(hooh).location, 255);
    CHECK((data::gc3(hooh).flags & data::kGcJapanOnly) == 0);
    CHECK_EQ(data::gc3(hooh).moves[0], 105);
    CHECK_EQ(std::string(data::gcTrainerName(data::gc3(hooh).trainer, 2)), "MATTLE");
    CHECK_EQ(std::string(data::gcTrainerName(data::gc3(hooh).trainer, 3)), "MT BATA");  // cut to 7 letters
}

TEST(colo_bonus_gifts_check_valid_and_catch_edits) {
    for (uint16_t species : {25, 251, 250}) {
        int index = bonusIndex(species);
        const uint8_t language = (species == 250) ? 2 : 1;
        pokemon::Pokemon p = make(index, language);
        CHECK(p.species == species);
        CHECK_EQ(p.trainer.tid, data::gc3(index).tid);
        CHECK_EQ(p.trainer.sid, 0);
        legality::Report r = legality::checkGen3(p);
        if (!r.legal()) {
            for (const legality::Finding& f : r.findings) std::printf("  [%d] %s\n", static_cast<int>(f.severity), f.text.c_str());
        }
        CHECK(r.legal());
        CHECK(has(r, legality::Severity::Valid, "bonus-disc"));
        CHECK_EQ(p.trainer.gender == pokemon::Gender::Female, species == 251);

        pokemon::Pokemon bad = p;
        bad.trainer.gender = species == 251 ? pokemon::Gender::Male : pokemon::Gender::Female;
        CHECK(!legality::checkGen3(bad).legal());
        bad = p;
        bad.pid ^= 0x00010000u;
        CHECK(!legality::checkGen3(bad).legal());
        bad = p;
        bad.trainer.name = "OTHER";
        CHECK(!legality::checkGen3(bad).legal());
        if (species != 250) {
            bad = p;
            bad.language = 2;
            CHECK(!legality::checkGen3(bad).legal());
        }
    }
}

TEST(colo_bonus_ho_oh_follows_the_recipient_language) {
    int index = bonusIndex(250);
    pokemon::Pokemon p = make(index, 5);
    CHECK_EQ(p.trainer.name, std::string("DUELLBE"));
    CHECK(legality::checkGen3(p).legal());
}
