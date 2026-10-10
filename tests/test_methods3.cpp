// Gen III Methods 1/2/3/4: detection (Unown too) and generation of Method 2/4
// frames by LEGALIZE when they come closer to the requested IVs.

#include <cstdint>
#include <cstdio>

#include "core/legality/legalize.h"
#include "core/legality/legality.h"
#include "core/legality/pid_iv.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/edit4.h"
#include "core/util/lcrng.h"
#include "tests/test.h"

using namespace pktool;
using legality::PidIvMethod;

namespace {

uint32_t iv15(uint32_t s) { return (s >> 16) & 0x7FFF; }

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

}  // namespace

TEST(methods3_are_detected_including_method_3_and_unown) {
    const uint32_t s1 = util::lcrngNext(0x1234ABCDu), s2 = util::lcrngNext(s1), s3 = util::lcrngNext(s2);
    const uint32_t s4 = util::lcrngNext(s3), s5 = util::lcrngNext(s4);
    uint8_t ivs[pokemon::kStatCount];

    uint32_t pid = (s2 & 0xFFFF0000u) | (s1 >> 16);  // Methods 1, 2, 4
    legality::ivsFromWords(iv15(s3), iv15(s4), ivs);
    CHECK(legality::findPidIvMethod3(pid, ivs, 25) == PidIvMethod::Method1);
    legality::ivsFromWords(iv15(s4), iv15(s5), ivs);
    CHECK(legality::findPidIvMethod3(pid, ivs, 25) == PidIvMethod::Method2);
    legality::ivsFromWords(iv15(s3), iv15(s5), ivs);
    CHECK(legality::findPidIvMethod3(pid, ivs, 25) == PidIvMethod::Method4);

    pid = (s3 & 0xFFFF0000u) | (s1 >> 16);  // Method 3: the high half is the third call
    legality::ivsFromWords(iv15(s4), iv15(s5), ivs);
    CHECK(legality::findPidIvMethod3(pid, ivs, 25) == PidIvMethod::Method3);

    // Unown: the PID halves come in the other order.
    pid = (s1 & 0xFFFF0000u) | (s2 >> 16);
    legality::ivsFromWords(iv15(s3), iv15(s4), ivs);
    CHECK(legality::findPidIvMethod3(pid, ivs, 201) == PidIvMethod::Method1);
    CHECK(legality::findPidIvMethod3(pid, ivs, 25) == PidIvMethod::None);

    // A corrupted IV breaks it.
    pid = (s2 & 0xFFFF0000u) | (s1 >> 16);
    legality::ivsFromWords(iv15(s3), iv15(s4), ivs);
    ivs[pokemon::kSpe] ^= 1;
    CHECK(legality::findPidIvMethod3(pid, ivs, 25) == PidIvMethod::None);
}

TEST(methods3_generation_is_exact_and_never_worse_than_method_1) {
    int usedOther = 0;
    for (uint32_t trial = 0; trial < 24; ++trial) {
        legality::PidIvRequest req;
        req.species = 25;  // Pikachu
        req.tid = 12345;
        req.sid = 54321;
        req.nature = static_cast<uint8_t>(trial % 25);
        req.gender = (trial & 1) ? pokemon::Gender::Female : pokemon::Gender::Male;
        req.abilitySlot = static_cast<uint8_t>((trial >> 1) & 1);
        for (int s = 0; s < pokemon::kStatCount; ++s) req.targetIvs[s] = static_cast<uint8_t>((trial * 7 + s * 11) % 32);
        legality::PidIvResult only1 = legality::generatePidIv(req, 2, trial);
        legality::PidIvResult3 any = legality::generatePidIv3(req, 2, trial);
        CHECK(any.frame.found);
        if (!any.frame.found) continue;
        CHECK(only1.found);
        CHECK(any.frame.distance <= only1.distance);
        // The frame is a real frame of the reported method with every trait.
        CHECK(legality::findPidIvMethod3(any.frame.pid, any.frame.ivs, req.species) != PidIvMethod::None);
        CHECK_EQ(any.frame.pid % 25, req.nature);
        CHECK_EQ(any.frame.pid & 1, req.abilitySlot);
        CHECK(pokemon::edit4::genderFromPid(req.species, any.frame.pid) == req.gender);
        CHECK(!pokemon::isShiny(any.frame.pid, req.tid, req.sid));
        if (any.method == PidIvMethod::Method2 || any.method == PidIvMethod::Method4) {
            ++usedOther;
            CHECK(any.frame.distance < only1.distance);
            CHECK(legality::findPidIvMethod3(any.frame.pid, any.frame.ivs, req.species) == any.method ||
                  legality::findPidIvMethod3(any.frame.pid, any.frame.ivs, req.species) == PidIvMethod::Method1);
        } else {
            CHECK(any.method == PidIvMethod::Method1);
        }
        // Without Methods 2/4 allowed it is exactly Method 1.
        legality::PidIvResult3 m1 = legality::generatePidIv3(req, 2, trial, false);
        CHECK(m1.method == PidIvMethod::Method1);
        CHECK_EQ(m1.frame.pid, only1.pid);
    }
    std::printf("  %d of 24 requests used Method 2 or 4\n", usedOther);
    CHECK(usedOther > 0);
}

TEST(methods3_shiny_generation_reaches_closer_ivs) {
    legality::PidIvRequest req;
    req.species = 25;
    req.tid = 777;
    req.sid = 4242;
    req.nature = 3;
    req.shiny = true;
    req.gender = pokemon::Gender::Male;
    req.abilitySlot = 1;
    for (int s = 0; s < pokemon::kStatCount; ++s) req.targetIvs[s] = 31;
    legality::PidIvResult only1 = legality::generatePidIv(req, 2, 5);
    legality::PidIvResult3 any = legality::generatePidIv3(req, 2, 5);
    CHECK(any.frame.found);
    CHECK(only1.found);
    CHECK(any.frame.distance <= only1.distance);
    CHECK(pokemon::isShiny(any.frame.pid, req.tid, req.sid));
    CHECK(legality::findPidIvMethod3(any.frame.pid, any.frame.ivs, 25) != PidIvMethod::None);
}

TEST(gen3_legalize_may_use_method_2_or_4_and_stays_legal) {
    int other = 0;
    for (uint32_t trial = 0; trial < 12; ++trial) {
        pokemon::Pokemon p = pokemon::create4::newPokemon(trainer(), 25, 30, 100 + trial);  // Pikachu
        p.nature = static_cast<uint8_t>((trial * 5) % 25);
        for (int s = 0; s < pokemon::kStatCount; ++s) p.ivs[s] = static_cast<uint8_t>((trial * 3 + s * 13) % 32);
        pokemon::edit4::refreshDerived(p);
        legality::LegalizeResult r = legality::legalizeGen3(p, 3);
        CHECK(r.report.legal());
        CHECK_EQ(r.pokemon.nature, p.nature);
        PidIvMethod m = legality::findPidIvMethod3(r.pokemon.pid, r.pokemon.ivs, 25);
        CHECK(m != PidIvMethod::None);
        if (m == PidIvMethod::Method2 || m == PidIvMethod::Method4) ++other;
    }
    std::printf("  %d of 12 legalized with Method 2 or 4\n", other);
}
