#include <cstdio>
#include <string>
#include <vector>

#include "core/data/encounters3.h"
#include "core/data/encounters4.h"
#include "core/data/learn4.h"
#include "core/data/names.h"
#include "core/data/personal.h"
#include "core/data/walker4.h"
#include "core/legality/legalize.h"
#include "core/legality/legality.h"
#include "core/legality/pid_iv.h"
#include "core/legality/pokewalker.h"
#include "core/legality/rng4.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/edit4.h"
#include "tests/test.h"

using namespace pktool;
using legality::Severity;
namespace v = legality::version;

namespace {

constexpr uint16_t kTid = 12345;
constexpr uint16_t kSid = 54321;
constexpr uint16_t kMonSid = 23456;  // 12345/54321 is one of the pairs the trainer check finds suspicious

// A Pokewalker catch from slot `index` of `course`, built the way the game does.
pokemon::Pokemon walkerMon(int course, int index, uint8_t nature, uint16_t tid = kTid, uint16_t sid = kSid) {
    const data::WalkerSlot& slot = data::walkerSlots(course)[index];
    pokemon::create4::Trainer t;
    t.name = "WALK";
    t.tid = tid;
    t.sid = sid;
    t.version = v::kHeartGold;
    pokemon::Pokemon p = pokemon::create4::newPokemon(t, slot.species, slot.level, 7);
    p.originGame = v::kHeartGold;
    p.met.metLocation = legality::kLocationPokewalker;
    p.met.metLevel = slot.level;
    p.met.eggLocation = 0;
    p.met.ball = data::kBallPoke;
    p.pid = legality::pokewalkerPid(tid, sid, nature, slot.gender, legality::pokewalkerGenderRatio(slot.species));
    legality::PidIvResult ivs = legality::generateConsecutiveIvs(p.ivs, 1);
    for (int s = 0; s < pokemon::kStatCount; ++s) p.ivs[s] = ivs.ivs[s];
    pokemon::edit4::refreshDerived(p);
    p.ability = pokemon::edit4::abilityForSlot(p.species, static_cast<uint8_t>(p.pid & 1));
    return p;
}

bool hasFinding(const legality::Report& r, Severity s, const char* needle) {
    for (const auto& f : r.findings) {
        if (f.severity == s && f.text.find(needle) != std::string::npos) return true;
    }
    return false;
}

void dump(const legality::Report& r) {
    for (const auto& f : r.findings) std::printf("    [%d] %s\n", static_cast<int>(f.severity), f.text.c_str());
}

}  // namespace

TEST(pokewalker_data_matches_pkhex) {
    CHECK_EQ(data::kWalkerCourses * data::kWalkerSlotsPerCourse, 162);
    const data::WalkerSlot* c0 = data::walkerSlots(0);  // Refreshing Field
    CHECK_EQ(c0[0].species, 115);
    CHECK_EQ(c0[0].level, 8);
    CHECK_EQ(c0[0].gender, 1);
    CHECK_EQ(c0[0].moves[0], 4);
    CHECK_EQ(data::walkerSlots(22)[3].species, 25);  // Yellow Forest: Pikachu
    CHECK(std::string(data::walkerCourseName(0)) == "Refreshing Field");
    CHECK(std::string(data::walkerCourseName(26)) == "Amity Meadow");
    CHECK(data::walkerSlots(27) == nullptr);
}

TEST(pokewalker_pid_follows_the_formula) {
    for (uint16_t ids : {0x0000u, 0x1234u, 0xFFFFu}) {
        for (uint8_t ratio : {uint8_t(31), uint8_t(63), uint8_t(127), uint8_t(191), uint8_t(225), uint8_t(0), uint8_t(254), uint8_t(255)}) {
            for (uint32_t nature = 0; nature < 24; ++nature) {
                for (uint8_t gender = 0; gender < 2; ++gender) {
                    if ((ratio == 0 && gender == 1) || (ratio == 254 && gender == 0)) continue;
                    uint32_t pid = legality::pokewalkerPid(kTid ^ ids, kSid, nature, gender, ratio);
                    CHECK_EQ(pid % 25, nature);
                    CHECK(!pokemon::isShiny(pid, kTid ^ ids, kSid));
                    uint32_t mid = pid & 0x00FFFF00u;
                    bool dual = ratio != 0 && ratio < 254;
                    if (dual && ratio <= 191) {  // gender follows the ratio (the 225 bump can wrap past 0xFF)
                        CHECK_EQ(((pid & 0xFF) < ratio) ? 1 : 0, gender);
                    }
                    if (!dual) CHECK(mid == 0 || mid == 0x00FFFF00u);
                }
            }
        }
    }
    // Quirky cannot be produced: the game builds Hardy instead.
    CHECK_EQ(legality::pokewalkerPid(kTid, kSid, 24, 0, 127) % 25, 0);
}

TEST(pokewalker_every_course_slot_is_legal) {
    int checked = 0;
    for (int course = 0; course < data::kWalkerCourses; ++course) {
        for (int i = 0; i < data::kWalkerSlotsPerCourse; ++i) {
            pokemon::Pokemon p = walkerMon(course, i, static_cast<uint8_t>((course * 6 + i) % 24), kTid, kMonSid);
            legality::Report r = legality::checkGen4(p);
            if (!r.legal()) {
                std::printf("  illegal slot: %s course %d slot %d (species %u)\n", data::walkerCourseName(course), course,
                            i, p.species);
                dump(r);
            }
            CHECK(r.legal());
            CHECK(hasFinding(r, Severity::Valid, "Pokewalker"));
            CHECK(!hasFinding(r, Severity::Warning, ""));
            ++checked;
        }
    }
    CHECK_EQ(checked, 162);
}

TEST(pokewalker_known_bad_cases_fail) {
    pokemon::Pokemon good = walkerMon(0, 0, 3);  // Refreshing Field: female Kangaskhan Lv. 8
    CHECK(legality::checkGen4(good).legal());

    pokemon::Pokemon p = good;
    p.pid ^= 0x00001000u;  // an ordinary PID: no longer a Pokewalker PID
    pokemon::edit4::refreshDerived(p);
    CHECK(hasFinding(legality::checkGen4(p), Severity::Invalid, "Pokewalker PID"));

    p = good;
    p.met.metLevel = static_cast<uint8_t>(good.met.metLevel + 1);
    p.level = static_cast<uint8_t>(p.level + 1);
    CHECK(hasFinding(legality::checkGen4(p), Severity::Invalid, "No Pokewalker course"));

    p = good;
    p.originGame = v::kPlatinum;  // the Pokewalker only exists for HG/SS
    CHECK(hasFinding(legality::checkGen4(p), Severity::Invalid, "only exists for HeartGold"));

    p = good;
    p.met.ball = 1;  // Master Ball
    CHECK(hasFinding(legality::checkGen4(p), Severity::Invalid, "Poke Ball"));

    p = good;
    p.shiny = true;
    CHECK(hasFinding(legality::checkGen4(p), Severity::Invalid, "never shiny"));
    // A shiny PID is also not a Pokewalker PID.
    pokemon::edit4::setShiny(p, true);
    p.shiny = pokemon::isShiny(p.pid, p.trainer.tid, p.trainer.sid);
    CHECK(!legality::checkGen4(p).legal());

    p = good;
    p.met.fatefulEncounter = true;
    CHECK(hasFinding(legality::checkGen4(p), Severity::Invalid, "fateful"));

    p = good;
    p.species = 4;  // Charmander is on no course with these details
    p.met.metLevel = 8;
    pokemon::edit4::refreshDerived(p);
    CHECK(hasFinding(legality::checkGen4(p), Severity::Invalid, "No Pokewalker course"));

    // Wrong gender for the slot (Kangaskhan is always female on this course).
    p = good;
    p.gender = pokemon::Gender::Male;
    CHECK(!legality::checkGen4(p).legal());

    // IVs that are not two consecutive outputs are only a warning (PKHeX: partial match).
    p = good;
    for (int s = 0; s < pokemon::kStatCount; ++s) p.ivs[s] = static_cast<uint8_t>((s * 7 + 3) & 31);
    if (!legality::ivsFromConsecutiveCalls(p.ivs)) {
        legality::Report r = legality::checkGen4(p);
        CHECK(r.legal());
        CHECK(hasFinding(r, Severity::Warning, "consecutive"));
    }
}

TEST(pokewalker_evolved_catches_keep_the_slot_species_rules) {
    // Lv. 8 Kangaskhan has no evolution; use a course with a pre-evolution:
    // Night Sky's Edge has Clefairy-like slots, Resort has Pikachu.
    for (int course = 0; course < data::kWalkerCourses; ++course) {
        for (int i = 0; i < data::kWalkerSlotsPerCourse; ++i) {
            const data::WalkerSlot& slot = data::walkerSlots(course)[i];
            uint8_t evolve = 0;
            uint16_t evolved = 0;
            for (uint16_t s = 1; s <= data::kMaxSpeciesGen4; ++s) {
                if (data::preEvolution4(s) == slot.species && data::evolveLevel4(s) > slot.level) {
                    evolve = data::evolveLevel4(s);
                    evolved = s;
                    break;
                }
            }
            if (evolved == 0) continue;
            // The evolved species below its evolution level cannot be a Pokewalker catch...
            pokemon::Pokemon p = walkerMon(course, i, 5);
            p.species = evolved;
            pokemon::edit4::refreshDerived(p);
            p.level = slot.level;
            p.ability = pokemon::edit4::abilityForSlot(p.species, static_cast<uint8_t>(p.pid & 1));
            legality::Report r = legality::checkGen4(p);
            CHECK(!r.legal());
            // ...and at the evolution level it can.
            pokemon::edit4::setLevel(p, evolve);
            p.pid = legality::pokewalkerPid(kTid, kSid, 5, slot.gender, legality::pokewalkerGenderRatio(evolved));
            pokemon::edit4::refreshDerived(p);
            p.ability = pokemon::edit4::abilityForSlot(p.species, static_cast<uint8_t>(p.pid & 1));
            if (p.gender == static_cast<pokemon::Gender>(slot.gender)) {
                legality::Report ok = legality::checkGen4(p);
                if (!hasFinding(ok, Severity::Invalid, "Pokewalker PID") && !hasFinding(ok, Severity::Invalid, "No Pokewalker")) {
                    CHECK(!hasFinding(ok, Severity::Invalid, "only evolves"));
                }
            }
            return;
        }
    }
    CHECK(false);  // the table always has an evolving species
}

namespace {

// A request in the shape of `slot`: its species, level and gender, any nature.
pokemon::Pokemon requestFor(const data::WalkerSlot& slot, uint8_t version, uint8_t nature, uint8_t abilitySlot) {
    pokemon::create4::Trainer t;
    t.name = "WALK";
    t.tid = kTid;
    t.sid = kSid;
    t.version = version;
    pokemon::Pokemon p = pokemon::create4::newPokemon(t, slot.species, slot.level, 3);
    uint32_t pid = 0;
    if (pokemon::edit4::findPid(slot.species, kTid, kSid, nature, false, static_cast<pokemon::Gender>(slot.gender),
                                abilitySlot, 3, pid)) {
        p.pid = pid;
        pokemon::edit4::refreshDerived(p);
        p.ability = pokemon::edit4::abilityForSlot(slot.species, abilitySlot);
    }
    return p;
}

}  // namespace

TEST(legalize_applies_a_pokewalker_origin_when_asked) {
    // The LEGALIZE fallback (used when no wild, static, egg or Pal Park origin
    // fits): every slot can be chosen back for its own species and level.
    int applied = 0, tried = 0;
    for (int course = 0; course < data::kWalkerCourses; ++course) {
        for (int i = 0; i < data::kWalkerSlotsPerCourse; ++i) {
            const data::WalkerSlot& slot = data::walkerSlots(course)[i];
            if (slot.gender > 1) continue;
            for (uint8_t nature : {uint8_t(2), uint8_t(9)}) {
                pokemon::Pokemon p = requestFor(slot, v::kPlatinum, nature, 0);
                if (p.nature != nature) continue;
                ++tried;
                std::vector<std::string> changes;
                if (!legality::applyPokewalkerOrigin(p, 0, changes)) continue;  // ability tied to nature
                ++applied;
                CHECK_EQ(p.met.metLocation, legality::kLocationPokewalker);
                CHECK(p.originGame == v::kHeartGold);
                CHECK(p.met.metLevel <= p.level);
                CHECK_EQ(p.met.ball, data::kBallPoke);
                CHECK(!changes.empty());
                // The PID is derived next; then the whole Pokemon is legal.
                CHECK(legality::rerollPidGen4(p, nature, false, p.gender, 0, 1));
                legality::Report r = legality::checkGen4(p);
                if (!r.legal()) dump(r);
                CHECK(r.legal());
            }
        }
    }
    std::printf("  %d of %d requests moved to the Pokewalker\n", applied, tried);
    CHECK(applied > tried / 4);
    CHECK(applied < tried);  // the ability can rule a slot out

    // Shiny and Quirky requests, or a gender no slot has, are refused untouched.
    const data::WalkerSlot& slot = data::walkerSlots(0)[0];  // female Kangaskhan, Lv. 8
    pokemon::Pokemon shiny = requestFor(slot, v::kPlatinum, 2, 0);
    shiny.shiny = true;
    std::vector<std::string> changes;
    CHECK(!legality::applyPokewalkerOrigin(shiny, 0, changes));
    pokemon::Pokemon male = requestFor(slot, v::kPlatinum, 2, 0);
    male.gender = pokemon::Gender::Male;
    CHECK(!legality::applyPokewalkerOrigin(male, 0, changes));
    pokemon::Pokemon low = requestFor(slot, v::kPlatinum, 2, 0);
    pokemon::edit4::setLevel(low, 5);  // below the slot's level
    CHECK(!legality::applyPokewalkerOrigin(low, 0, changes));
    CHECK(changes.empty());
    CHECK_EQ(shiny.originGame, v::kPlatinum);
}

TEST(legalize_repairs_a_pokewalker_catch) {
    // Walker location, but an ordinary PID and a Platinum origin: LEGALIZE keeps
    // the Pokewalker origin (moving it to HeartGold) and derives the PID.
    int fixed = 0, tried = 0;
    for (int course = 0; course < data::kWalkerCourses; course += 3) {
        for (int i = 0; i < data::kWalkerSlotsPerCourse; ++i) {
            const data::WalkerSlot& slot = data::walkerSlots(course)[i];
            if (slot.gender > 1) continue;
            pokemon::Pokemon p = requestFor(slot, v::kPlatinum, 7, 0);
            if (p.nature != 7) continue;
            p.originGame = v::kPlatinum;
            p.met.metLocation = legality::kLocationPokewalker;
            p.met.metLevel = slot.level;
            p.met.eggLocation = 0;
            p.met.ball = 12;
            ++tried;
            legality::LegalizeResult r = legality::legalizeGen4(p, 2);
            if (!r.unresolved.empty()) continue;  // ability could not be kept with this nature
            ++fixed;
            CHECK(r.report.legal());
            CHECK_EQ(r.pokemon.met.metLocation, legality::kLocationPokewalker);
            CHECK(r.pokemon.originGame == v::kHeartGold);
            CHECK_EQ(r.pokemon.met.ball, data::kBallPoke);
            CHECK_EQ(r.pokemon.nature, 7);
            CHECK(legality::isPokewalkerPid(r.pokemon));
            CHECK(legality::ivsFromConsecutiveCalls(r.pokemon.ivs));
        }
    }
    std::printf("  %d of %d Pokewalker catches repaired\n", fixed, tried);
    CHECK(fixed > tried / 4);
}

TEST(legalize_pokewalker_claim_with_a_wrong_level_looks_for_another_origin) {
    // Kangaskhan Lv. 30 cannot be a Lv. 8 Pokewalker catch: it becomes an ordinary encounter or egg.
    const data::WalkerSlot& slot = data::walkerSlots(0)[0];
    pokemon::Pokemon p = requestFor(slot, v::kHeartGold, 2, 0);
    pokemon::edit4::setLevel(p, 30);
    p.met.metLocation = legality::kLocationPokewalker;
    p.met.metLevel = 30;
    legality::LegalizeResult r = legality::legalizeGen4(p, 2);
    CHECK(r.report.legal());
}

TEST(reroll_keeps_the_pokewalker_pid_rules) {
    pokemon::Pokemon p = walkerMon(0, 0, 3);
    // Find a nature whose Pokewalker PID has the same ability slot (Kangaskhan has two abilities).
    uint8_t slot = static_cast<uint8_t>(p.pid & 1);
    uint8_t nature = 0;
    for (; nature < 24; ++nature) {
        if (nature == 3) continue;
        uint32_t pid = legality::pokewalkerPid(kTid, kSid, nature, 1, legality::pokewalkerGenderRatio(115));
        if ((pid & 1) == slot) break;
    }
    CHECK(nature < 24);
    CHECK(legality::rerollPidGen4(p, nature, false, pokemon::Gender::Female, slot, 1));
    CHECK_EQ(p.nature, nature);
    CHECK(legality::isPokewalkerPid(p));
    CHECK(legality::checkGen4(p).legal());
    // Shiny and Quirky cannot be Pokewalker Pokemon.
    CHECK(!legality::rerollPidGen4(p, nature, true, pokemon::Gender::Female, slot, 1));
    CHECK(!legality::rerollPidGen4(p, 24, false, pokemon::Gender::Female, slot, 1));
    // A nature whose Pokewalker PID has the other ability slot cannot keep this one.
    for (uint8_t other = 0; other < 24; ++other) {
        uint32_t pid = legality::pokewalkerPid(kTid, kSid, other, 1, legality::pokewalkerGenderRatio(115));
        if ((pid & 1) != slot) {
            CHECK(!legality::rerollPidGen4(p, other, false, pokemon::Gender::Female, slot, 1));
            break;
        }
    }
}

TEST(pokewalker_no_regression_on_ordinary_legal_pokemon) {
    // A wild Platinum catch must not be mistaken for a Pokewalker one.
    pokemon::create4::Trainer t;
    t.name = "TEST";
    t.tid = kTid;
    t.sid = kSid;
    t.version = v::kPlatinum;
    pokemon::Pokemon p = pokemon::create4::newPokemon(t, 399, 50, 9);  // Bidoof
    legality::LegalizeResult r = legality::legalizeGen4(p, 2);
    CHECK(r.report.legal());
    CHECK(r.pokemon.met.metLocation != legality::kLocationPokewalker);
    CHECK(!legality::claimsPokewalker(r.pokemon));
}
