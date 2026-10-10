// Gen III move sources: egg moves only for Pokemon that came from an egg,
// tutor moves, Shedinja's Ninjask moves, events/trades/Colosseum with a known
// or unknown origin, and the same moves after Pal Park.

#include <cstdio>
#include <cstring>
#include <string>

#include "core/data/events3.h"
#include "core/data/gen3.h"
#include "core/data/learn3.h"
#include "core/data/moves.h"
#include "core/data/names.h"
#include "core/legality/events3_check.h"
#include "core/legality/legalize.h"
#include "core/legality/legality.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/edit4.h"
#include "core/pokemon/gift3.h"
#include "core/pokemon/pk3.h"
#include "core/pokemon/pk4.h"
#include "core/pokemon/transfer34.h"
#include "tests/test.h"

using namespace pktool;
namespace pk3 = pokemon::pk3;
using legality::Severity;

namespace {

pokemon::create4::Trainer trainer() {
    pokemon::create4::Trainer t;
    t.name = "PATE";
    t.tid = 12345;
    t.sid = 54321;
    t.gender = pokemon::Gender::Female;
    t.version = 4;  // FireRed
    t.language = 2;
    t.today = {26, 9, 29};
    return t;
}

// A legal wild Pokemon of the species at the level.
pokemon::Pokemon wild(uint16_t species, uint8_t level, uint32_t seed = 5) {
    pokemon::Pokemon p = pokemon::create4::newPokemon(trainer(), species, level, seed);
    return legality::legalizeGen3(p, 4).pokemon;
}

// True if a finding of at least this severity names the move.
bool flags(const legality::Report& r, Severity severity, uint16_t move) {
    const std::string name = data::moveName(move);
    for (const auto& f : r.findings) {
        if (f.severity >= severity && f.text.find(name) != std::string::npos) return true;
    }
    return false;
}

bool says(const legality::Report& r, Severity severity, const char* text) {
    for (const auto& f : r.findings) {
        if (f.severity == severity && f.text.find(text) != std::string::npos) return true;
    }
    return false;
}

void setMove(pokemon::Pokemon& p, int slot, uint16_t move) { pokemon::edit4::setMove(p, slot, move); }

// A species of a wild-catchable line with an egg-only move (no other way to learn it).
bool findEggOnly(uint16_t* species, uint16_t* move) {
    for (uint16_t s = 1; s <= 251; ++s) {
        if (legality::minCreateLevel3(s) == 0 || data::preEvolution4(s) != 0) continue;
        for (uint16_t m = 1; m <= data::kMaxMoveGen3; ++m) {
            const data::LearnHit e = data::findLearn3(s, m);
            if (e != nullptr && e->sources == data::kLearnEgg) {
                *species = s;
                *move = m;
                return true;
            }
        }
    }
    return false;
}

// A move no stage of the line can learn.
uint16_t impossibleMove(uint16_t species) {
    for (uint16_t m = 354; m >= 1; --m) {
        bool any = false;
        for (uint16_t s = species; s != 0; s = data::preEvolution4(s)) any = any || data::findLearn3(s, m) != nullptr;
        if (!any) return m;
    }
    return 0;
}

}  // namespace

TEST(moves3_egg_moves_need_an_egg) {
    uint16_t species = 0, move = 0;
    CHECK(findEggOnly(&species, &move));
    pokemon::Pokemon p = wild(species, 30);
    setMove(p, 3, move);
    legality::Report r = legality::checkGen3(p);
    CHECK(says(r, Severity::Invalid, "egg move"));
    CHECK(flags(r, Severity::Invalid, move));

    // The same move after hatching (met level 0, a hatch location) is fine.
    p.met.metLevel = 0;
    p.met.metLocation = legality::location3::kRoute117;
    p.met.ball = data::kBallPoke;
    r = legality::checkGen3(p);
    CHECK(!flags(r, Severity::Warning, move));
    // And an unhatched egg may carry it.
    p.isEgg = true;
    CHECK(!flags(legality::checkGen3(p), Severity::Warning, move));
}

TEST(moves3_tutor_and_machine_moves_are_open_to_every_origin) {
    int tutors = 0;
    for (uint16_t s = 1; s <= 251 && tutors < 12; ++s) {
        if (legality::minCreateLevel3(s) == 0 || data::preEvolution4(s) != 0) continue;
        for (uint16_t m = 1; m <= data::kMaxMoveGen3 && tutors < 12; ++m) {
            const data::LearnHit e = data::findLearn3(s, m);
            if (e == nullptr || !(e->sources & data::kLearnTutor)) continue;
            pokemon::Pokemon p = wild(s, 40, s);
            setMove(p, 2, m);
            CHECK(!flags(legality::checkGen3(p), Severity::Warning, m));
            ++tutors;
            break;
        }
    }
    CHECK(tutors >= 10);
    // Blast Burn, Hydro Cannon and Frenzy Plant: the starters' FR/LG tutors.
    for (uint16_t starter : {6, 9, 3}) {
        uint16_t move = starter == 6 ? 307 : starter == 9 ? 308 : 338;
        pokemon::Pokemon p = wild(starter, 50, starter);
        setMove(p, 0, move);
        CHECK(!flags(legality::checkGen3(p), Severity::Warning, move));
    }
}

TEST(moves3_unknown_events_warn_but_known_origins_do_not) {
    pokemon::Pokemon p = wild(25, 30);  // Pikachu
    const uint16_t bad = impossibleMove(25);
    setMove(p, 3, bad);
    CHECK(flags(legality::checkGen3(p), Severity::Invalid, bad));

    pokemon::Pokemon trade = p;  // an in-game trade is an ordinary Pokemon
    trade.met.metLocation = legality::location3::kTrade;
    legality::Report r = legality::checkGen3(trade);
    CHECK(flags(r, Severity::Invalid, bad));
    CHECK(!says(r, Severity::Warning, "unchecked"));

    pokemon::Pokemon event = p;  // a distribution PKTool does not know: may have any move
    event.met.metLocation = legality::location3::kFateful;
    event.met.fatefulEncounter = true;
    r = legality::checkGen3(event);
    CHECK(says(r, Severity::Warning, "not in the PKTool database"));
    CHECK(says(r, Severity::Warning, "unknown origin"));
    CHECK(!flags(r, Severity::Invalid, bad));

    pokemon::Pokemon colosseum = p;
    colosseum.originGame = legality::version::kColosseumXD;
    r = legality::checkGen3(colosseum);
    CHECK(says(r, Severity::Warning, "unknown origin"));
}

TEST(moves3_shedinja_keeps_the_moves_ninjask_learned_on_evolving) {
    // A move only the Ninjask learns, at Lv. 20 or later.
    uint16_t move = 0;
    for (uint16_t m = 1; m <= data::kMaxMoveGen3 && move == 0; ++m) {
        const data::LearnHit ninjask = data::findLearn3(291, m);
        if (ninjask != nullptr && (ninjask->sources & data::kLearnLevelUp) && ninjask->minLevel >= 20 && ninjask->minLevel <= 30 &&
            data::findLearn3(290, m) == nullptr && data::findLearn3(292, m) == nullptr) {
            move = m;
        }
    }
    CHECK(move != 0);
    pokemon::Pokemon shedinja = pokemon::create4::newPokemon(trainer(), 292, 40, 3);
    setMove(shedinja, 0, move);
    CHECK(!flags(legality::checkGen3(shedinja), Severity::Warning, move));
    // Not a Ninjask move: still impossible.
    const uint16_t bad = impossibleMove(291);
    setMove(shedinja, 1, bad);
    CHECK(flags(legality::checkGen3(shedinja), Severity::Invalid, bad));
    // Below Lv. 20 the Ninjask had not learned it yet.
    pokemon::Pokemon low = pokemon::create4::newPokemon(trainer(), 292, 19, 3);
    setMove(low, 0, move);
    CHECK(flags(legality::checkGen3(low), Severity::Invalid, move));
}

TEST(moves3_event_egg_moves_after_pal_park) {
    int row = -1;
    for (int i = data::generated::kEvents3Count; i < data::events3Count() && row < 0; ++i) {
        const data::Event3& e = data::event3(i);
        if ((e.flags & data::kEvEgg) && e.species == 172 && std::strstr(e.title, "Pokemon Box") != nullptr) row = i;
    }
    CHECK(row >= 0);
    uint8_t raw[pk3::kStoredSize];
    pokemon::Pokemon egg = pokemon::gift3::receive(data::event3(row), trainer(), 4, pokemon::gift3::Criteria(), raw);
    pokemon::Pokemon pichu = egg;
    pichu.isEgg = false;
    pichu.met.metLevel = 0;
    pichu.met.metLocation = 146;
    pichu.nickname = "PICHU";
    pichu.met.fatefulEncounter = false;
    pichu.level = 30;
    pokemon::edit4::setLevel(pichu, 30);
    pokemon::create4::encode(pichu, raw);
    CHECK(legality::checkGen3(pk3::toPokemon(raw, pk3::kStoredSize)).legal());

    uint8_t d4[pokemon::pk4::kStoredSize];
    pokemon::transfer34::convert(raw, d4, {26, 9, 30});
    pokemon::Pokemon q = pokemon::pk4::toPokemon(d4, pokemon::pk4::kStoredSize);
    const uint16_t surf = 57;
    CHECK(legality::gen3OriginHasMove(q, surf));
    legality::Report r = legality::checkGen4(q);
    CHECK(!flags(r, Severity::Warning, surf));
    q.trainer.name = "OTHER";  // another trainer's Pichu cannot have it
    CHECK(!legality::gen3OriginHasMove(q, surf));
}
