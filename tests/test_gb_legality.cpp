#include <cstdio>
#include <string>

#include "core/data/gen12.h"
#include "core/data/names.h"
#include "core/legality/encounters_gb.h"
#include "core/legality/legality.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/edit_gb.h"
#include "core/pokemon/entity.h"
#include "core/pokemon/gb_common.h"
#include "tests/fake_gb_saves.h"

using namespace pktool;
namespace gbn = pktool::legality::gb;

namespace {

pokemon::create4::Trainer trainerFor(uint8_t version) {
    pokemon::create4::Trainer t;
    t.name = "ASH";
    t.tid = 12345;
    t.version = version;
    t.language = 2;
    t.today = {26, 1, 1};
    return t;
}

std::string describe(const legality::Report& r) {
    std::string out;
    for (const auto& f : r.findings) {
        if (f.severity == legality::Severity::Invalid) out += f.text + "; ";
    }
    return out;
}

// Round trip through the entity bytes, like the box view does.
pokemon::Pokemon viaEntity(const pokemon::Pokemon& p) {
    uint8_t blob[pokemon::entity::kMaxPartySize] = {};
    pokemon::create4::encode(p, blob);
    return pokemon::entity::decode(p.format, blob, 0);
}

}  // namespace

TEST(gb_new_pokemon_are_legal_for_every_species) {
    int checked = 0;
    for (uint8_t version : {35, 38, 39, 41}) {
        const uint8_t format = version <= 38 ? 1 : 2;
        const uint8_t versions = gbn::versionMask(format, version, false);
        const uint16_t max = format == 1 ? 151 : 251;
        for (uint16_t species = 1; species <= max; ++species) {
            const uint8_t lowest = gbn::lowestEncounterLevel(format, species, versions);
            if (lowest == 0) continue;
            pokemon::Pokemon p = pokemon::create4::newPokemon(trainerFor(version), species, std::max<uint8_t>(lowest, 2), 7 * species);
            pokemon::Pokemon q = viaEntity(p);
            legality::Report r = legality::check(q);
            if (!r.legal()) {
                std::printf("  version %u species %u (%s) level %u: %s\n", version, species, data::speciesName(species),
                            q.level, describe(r).c_str());
            }
            CHECK(r.legal());
            ++checked;
        }
    }
    CHECK(checked > 500);
}

TEST(gb_legality_flags_impossible_data) {
    pokemon::create4::Trainer t = trainerFor(39);
    pokemon::Pokemon base = pokemon::create4::newPokemon(t, 25, 20, 3);  // Pikachu in Gold
    base.originGame = 39;
    CHECK(legality::check(viaEntity(base)).legal());

    pokemon::Pokemon p = base;
    p.moves[2] = 143;  // Sky Attack: Pikachu cannot learn it
    p.pp[2] = 10;
    CHECK(!legality::check(viaEntity(p)).legal());

    p = base;
    p.moves[0] = 0;
    p.moves[1] = 0;
    p.moves[2] = 0;
    p.moves[3] = 0;
    CHECK(!legality::check(viaEntity(p)).legal());  // no moves at all

    p = base;
    p.exp += 10000;  // level no longer matches EXP
    CHECK(!legality::check(viaEntity(p)).legal());

    p = base;
    p.heldItem = data::itemFromGen2(0x07);  // the Bicycle cannot be held
    CHECK(!legality::check(viaEntity(p)).legal());

    p = base;
    p.pp[0] = 99;
    CHECK(!legality::check(viaEntity(p)).legal());

    p = base;
    p.level = 2;
    p.exp = pokemon::gb::expFor(2, 25, 2);
    CHECK(!legality::check(viaEntity(p)).legal());  // Pikachu is not found below level 3 in Gold/Silver (Viridian Forest)

    p = base;
    p.trainer.name.clear();
    CHECK(!legality::check(viaEntity(p)).legal());
}

TEST(gb_legality_gen1_catch_rate_and_types) {
    pokemon::create4::Trainer t = trainerFor(35);
    pokemon::Pokemon base = pokemon::create4::newPokemon(t, 129, 10, 5);  // Magikarp
    CHECK(legality::check(viaEntity(base)).legal());
    pokemon::Pokemon p = base;
    p.catchRate = 6;
    CHECK(!legality::check(viaEntity(p)).legal());
    p.catchRate = 0xAD;  // a Berry: the Pokemon came through the Time Capsule
    legality::Report r = legality::check(viaEntity(p));
    CHECK(r.legal());
    p = base;
    p.types[1] = 3;
    CHECK(!legality::check(viaEntity(p)).legal());
    // Mew cannot be caught in the wild: only the event gifts, with their own trainer data (see
    // test_gb_legality_data.cpp), are legal.
    pokemon::Pokemon mew = pokemon::create4::newPokemon(t, 151, 5, 9);
    legality::Report mewReport = legality::check(viaEntity(mew));
    CHECK(mewReport.overall() == legality::Severity::Invalid);
}

TEST(gb_legality_gen2_eggs_and_shiny_statics) {
    pokemon::create4::Trainer t = trainerFor(41);
    pokemon::Pokemon egg = pokemon::create4::newEgg(t, 25, 11);  // Pichu egg
    CHECK(egg.isEgg);
    CHECK_EQ(egg.species, 172);
    CHECK_EQ(egg.level, 5);
    legality::Report r = legality::check(viaEntity(egg));
    if (!r.legal()) std::printf("  egg: %s\n", describe(r).c_str());
    CHECK(r.legal());
    pokemon::Pokemon bad = egg;
    bad.friendship = 200;  // more cycles than any species has
    CHECK(!legality::check(viaEntity(bad)).legal());

    // The red Gyarados of the Lake of Rage is always shiny with fixed DVs.
    pokemon::Pokemon gyarados = pokemon::create4::newPokemon(t, 130, 30, 3);
    pokemon::gb::unpackDvs(0xEAAA, gyarados.ivs);
    pokemon::editgb::refreshDerived(gyarados);
    gyarados.met.metLevel = 30;
    gyarados.met.metLocation = 38;
    gyarados.met.encounterType = 2;
    CHECK(gyarados.shiny);
    CHECK(legality::check(viaEntity(gyarados)).legal());
}
