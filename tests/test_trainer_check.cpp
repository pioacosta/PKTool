// Tests of the original-trainer rules (core/legality/trainer_check).

#include <cstdint>
#include <string>

#include "core/legality/legality.h"
#include "core/legality/trainer_check.h"
#include "tests/test.h"

using namespace pktool;

namespace {

pokemon::Pokemon mon(uint8_t format, uint8_t origin) {
    pokemon::Pokemon p;
    p.format = format;
    p.species = 25;
    p.originGame = origin;
    p.language = 2;
    p.trainer.name = "ASH";
    p.trainer.tid = 12345;
    p.trainer.sid = 23456;
    p.met.metLevel = 5;
    p.met.metLocation = 16;
    return p;
}

legality::Report run(const pokemon::Pokemon& p) {
    legality::Report r;
    legality::checkTrainer(p, r);
    return r;
}

bool has(const legality::Report& r, legality::Severity s, const char* text) {
    for (const legality::Finding& f : r.findings) {
        if (f.severity == s && f.text.find(text) != std::string::npos) return true;
    }
    return false;
}

// Brute-force definition of the Ruby/Sapphire trainer ID: a state whose high
// half is the SID and whose successor's high half is the TID.
bool bruteForce(uint16_t tid, uint16_t sid) {
    for (uint32_t low = 0; low < 0x10000; ++low) {
        const uint32_t next = (static_cast<uint32_t>(sid) << 16 | low);
        if ((next * 0x41C64E6Du + 0x6073u) >> 16 == tid) return true;
    }
    return false;
}

}  // namespace

TEST(trainer_rs_ids_are_two_consecutive_rng_outputs) {
    uint32_t state = 0x12345678;
    int possible = 0;
    const int kPairs = 600;
    for (int i = 0; i < kPairs; ++i) {
        state = state * 0x41C64E6Du + 0x6073u;
        const uint16_t sid = static_cast<uint16_t>(state >> 16);
        const uint32_t nextState = state * 0x41C64E6Du + 0x6073u;
        const uint16_t tid = static_cast<uint16_t>(nextState >> 16);
        CHECK(legality::rsTrainerIdPossible(tid, sid));  // really produced by the RNG
        // Arbitrary pairs: the fast search agrees with the brute-force definition.
        const uint16_t t2 = static_cast<uint16_t>(state * 7919u >> 8);
        const uint16_t s2 = static_cast<uint16_t>(nextState >> 3);
        const bool fast = legality::rsTrainerIdPossible(t2, s2);
        CHECK_EQ(fast, bruteForce(t2, s2));
        possible += fast;
    }
    // Many arbitrary pairs have a seed but not all of them (the search is not a no-op).
    CHECK(possible > kPairs / 4 && possible < kPairs);
}

TEST(trainer_rs_ids_checked_for_ruby_sapphire_only) {
    // Find a pair no state produces.
    uint16_t badTid = 0, badSid = 0;
    for (uint16_t t = 1; t < 400 && badTid == 0; ++t) {
        if (!legality::rsTrainerIdPossible(t, 777)) {
            badTid = t;
            badSid = 777;
        }
    }
    CHECK(badTid != 0);
    pokemon::Pokemon p = mon(3, legality::version::kRuby);
    p.trainer.tid = badTid;
    p.trainer.sid = badSid;
    CHECK(has(run(p), legality::Severity::Invalid, "Ruby/Sapphire"));
    p.originGame = legality::version::kSapphire;
    CHECK(has(run(p), legality::Severity::Invalid, "Ruby/Sapphire"));
    p.originGame = legality::version::kEmerald;
    CHECK(run(p).findings.empty());
    p.originGame = legality::version::kRuby;
    p.met.metLevel = 0;  // hatched: could have been traded to Emerald
    CHECK(has(run(p), legality::Severity::Warning, "Ruby/Sapphire"));
    p.met.metLevel = 5;
    p.met.fatefulEncounter = true;  // event: fixed trainer
    CHECK(run(p).findings.empty());
}

TEST(trainer_name_rules) {
    pokemon::Pokemon p = mon(4, legality::version::kPlatinum);
    CHECK(run(p).findings.empty());
    p.trainer.name = "";
    CHECK(has(run(p), legality::Severity::Invalid, "empty"));
    p.trainer.name = "ABCDEFGHIJKLM";  // 13 characters
    CHECK(has(run(p), legality::Severity::Invalid, "too long"));
    p.trainer.name = "ABCDEFGHIJKL";
    CHECK(run(p).findings.empty());
    p.language = 1;  // Japanese: 6
    p.trainer.name = "ABCDEFG";
    CHECK(has(run(p), legality::Severity::Invalid, "too long"));
    p.trainer.name = "\xE3\x81\x82\xE3\x81\x84\xE3\x81\x86\xE3\x81\x88\xE3\x81\x8A\xE3\x81\x8B";  // 6 kana
    CHECK(run(p).findings.empty());
    p.language = 2;
    p.trainer.name = "pkhex Joe";
    CHECK(has(run(p), legality::Severity::Warning, "Suspicious OT"));
    // Fixed trainers are exempt.
    p.trainer.name = "";
    p.met.fatefulEncounter = true;
    CHECK(run(p).findings.empty());
    p.met.fatefulEncounter = false;
    p.met.metLocation = 2001;  // in-game trade
    CHECK(run(p).findings.empty());
}

TEST(trainer_id_rules) {
    pokemon::Pokemon p = mon(5, legality::version::kBlack);
    CHECK(run(p).findings.empty());
    p.trainer.tid = 0;
    p.trainer.sid = 0;
    CHECK(has(run(p), legality::Severity::Warning, "both 0"));
    p.trainer.tid = 4321;
    p.trainer.sid = 4321;
    CHECK(has(run(p), legality::Severity::Warning, "equals SID"));
    p.trainer.tid = 0;
    p.trainer.sid = 5;
    CHECK(has(run(p), legality::Severity::Warning, "TID is 0"));
    p.trainer.tid = 5;
    p.trainer.sid = 0;
    CHECK(has(run(p), legality::Severity::Warning, "SID is 0"));
    p.trainer.tid = 12345;
    p.trainer.sid = 54321;
    CHECK(has(run(p), legality::Severity::Warning, "Suspicious trainer"));
    p.trainer.tid = 15040;
    p.trainer.sid = 18831;
    CHECK(has(run(p), legality::Severity::Warning, "Suspicious trainer"));
    CHECK(run(p).overall() == legality::Severity::Warning);
    // N's Pokemon use N's fixed IDs.
    p.trainer.name = "N";
    p.trainer.tid = 0;
    p.trainer.sid = 0;
    CHECK(run(p).findings.empty());
}

TEST(trainer_language_rules) {
    pokemon::Pokemon p = mon(4, legality::version::kPlatinum);
    for (uint8_t lang : {1, 2, 3, 4, 5, 7, 8}) {
        p.language = lang;
        CHECK(run(p).findings.empty());
    }
    p.language = 0;
    CHECK(has(run(p), legality::Severity::Invalid, "Language"));
    p.language = 6;
    CHECK(has(run(p), legality::Severity::Invalid, "Language"));
    p.language = 9;
    CHECK(has(run(p), legality::Severity::Invalid, "Language"));
    // Korean did not exist in Gen III.
    p = mon(3, legality::version::kEmerald);
    p.language = 8;
    CHECK(has(run(p), legality::Severity::Invalid, "Language"));
    p.language = 7;
    CHECK(run(p).findings.empty());
    // ... nor for a Gen III Pokemon moved to Gen IV.
    p = mon(4, legality::version::kFireRed);
    p.language = 8;
    CHECK(has(run(p), legality::Severity::Invalid, "Language"));
}

TEST(trainer_colosseum_xd_trainer_is_male) {
    pokemon::Pokemon p = mon(3, legality::version::kColosseumXD);
    CHECK(run(p).findings.empty());
    p.trainer.gender = pokemon::Gender::Female;
    CHECK(has(run(p), legality::Severity::Invalid, "female"));
    // Other games have a female trainer.
    pokemon::Pokemon q = mon(3, legality::version::kFireRed);
    q.trainer.gender = pokemon::Gender::Female;
    CHECK(run(q).findings.empty());
}
