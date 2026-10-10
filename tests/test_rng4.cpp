#include <chrono>
#include <cstring>
#include <cstdio>

#include "core/legality/legalize.h"
#include "core/legality/legality.h"
#include "core/legality/pid_iv.h"
#include "core/legality/rng4.h"
#include "core/pokemon/edit4.h"
#include "core/pokemon/pk4.h"
#include "tests/fixtures.h"
#include "tests/test.h"

using namespace pktool;
using namespace pktool::legality;
namespace pk4 = pokemon::pk4;

namespace {

PidIvRequest baseRequest(RngMethod method) {
    PidIvRequest req;
    req.species = 445;  // Garchomp (both genders, one ability)
    req.tid = 12345;
    req.sid = 54321;
    req.nature = 13;  // Jolly
    req.gender = pokemon::Gender::Male;
    req.abilitySlot = 0;
    req.method = method;
    for (auto& iv : req.targetIvs) iv = 31;
    return req;
}

void checkFrame(const PidIvRequest& req, const PidIvResult& r) {
    CHECK(r.found);
    CHECK_EQ(r.pid % 25, req.nature);
    CHECK_EQ(r.pid & 1, req.abilitySlot);
    CHECK_EQ(pokemon::isShiny(r.pid, req.tid, req.sid), req.shiny);
    CHECK(pokemon::edit4::genderFromPid(req.species, r.pid) == req.gender);
    uint32_t lowState = 0;
    CHECK(findPidIvMethod(r.pid, r.ivs, &lowState) == PidIvMethod::Method1);
    CHECK(isNatureFrameValid(lowState, req.method));
}

}  // namespace

TEST(rng4_method_j_and_k_frames_close_to_target) {
    for (RngMethod method : {RngMethod::MethodJ, RngMethod::MethodK, RngMethod::Method1}) {
        PidIvRequest req = baseRequest(method);
        auto start = std::chrono::steady_clock::now();
        PidIvResult r = generatePidIv(req, 2);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        checkFrame(req, r);
        std::printf("  method %d: IVs %u/%u/%u/%u/%u/%u (distance %d) in %lld ms\n", static_cast<int>(method), r.ivs[0],
                    r.ivs[1], r.ivs[2], r.ivs[3], r.ivs[4], r.ivs[5], r.distance, static_cast<long long>(ms));
        CHECK(r.distance < 20);  // close to 6x31
    }
}

TEST(rng4_shiny_frames) {
    PidIvRequest req = baseRequest(RngMethod::MethodJ);
    req.shiny = true;
    req.gender = pokemon::Gender::Female;
    PidIvResult r = generatePidIv(req, 2, 7);
    checkFrame(req, r);
}

TEST(rng4_impossible_requests) {
    PidIvRequest req = baseRequest(RngMethod::MethodJ);
    req.species = 128;  // Tauros: male only
    req.gender = pokemon::Gender::Female;
    CHECK(!generatePidIv(req, 1).found);
    req.gender = pokemon::Gender::Male;
    req.nature = 25;
    CHECK(!generatePidIv(req, 1).found);
}

TEST(rng4_nature_frame_validation_rejects_plain_method1) {
    // A Method 1 frame whose preceding call does not give the nature is not a
    // valid Method J frame unless rejected pairs explain it.
    int rejected = 0;
    for (uint32_t seed = 1; seed < 200; ++seed) {
        uint32_t low = seed * 0x10001u;
        if (!isNatureFrameValid(low, RngMethod::MethodJ)) ++rejected;
    }
    CHECK(rejected > 0);
}

TEST(legalize_fixes_edited_pokemon_and_keeps_requests) {
    // Wild Platinum Roserade-like Pokémon edited into an illegal state.
    pokemon::Pokemon p = pk4::toPokemon(test::kRoseradePk4, pk4::kPartySize);
    p.met.eggLocation = 0;
    p.met.metLocation = 50;
    p.originGame = version::kPlatinum;
    pokemon::edit4::setLevel(p, 50);
    p.met.metLevel = 60;                                   // above level
    CHECK(pokemon::edit4::setNature(p, 13) == pokemon::edit4::Result::Ok);  // random PID
    CHECK(pokemon::edit4::setShiny(p, true) == pokemon::edit4::Result::Ok);
    p.met.ball = 23;                                       // Moon Ball (HG/SS only)
    for (int s = 0; s < pokemon::kStatCount; ++s) p.evs[s] = 200;
    for (auto& iv : p.ivs) iv = 31;
    CHECK(!checkGen4(p).legal());

    LegalizeResult r = legalizeGen4(p, 2);
    for (const auto& c : r.changes) std::printf("  change: %s\n", c.c_str());
    for (const auto& u : r.unresolved) std::printf("  unresolved: %s\n", u.c_str());
    CHECK(r.unresolved.empty());
    CHECK(r.report.legal());
    CHECK_EQ(r.pokemon.nature, 13);
    CHECK(r.pokemon.shiny);
    CHECK(r.pokemon.gender == p.gender);
    CHECK_EQ(r.pokemon.species, p.species);
    CHECK_EQ(r.pokemon.level, 50);
    CHECK(r.pokemon.moves[0] == p.moves[0]);
}

TEST(legalize_reports_what_it_cannot_fix) {
    pokemon::Pokemon p = pk4::toPokemon(test::kRoseradePk4, pk4::kPartySize);
    p.moves[1] = 89;  // Earthquake: Roserade cannot learn it
    LegalizeResult r = legalizeGen4(p, 1);
    CHECK(!r.unresolved.empty());
    CHECK_EQ(r.pokemon.moves[1], 89);  // requested moves are never removed silently
}

TEST(reroll_keeps_hatched_pokemon_ivs) {
    pokemon::Pokemon p = pk4::toPokemon(test::kRoseradePk4, pk4::kPartySize);  // hatched
    uint8_t before[pokemon::kStatCount];
    for (int s = 0; s < pokemon::kStatCount; ++s) before[s] = p.ivs[s];
    CHECK(rerollPidGen4(p, 3, false, p.gender, p.pid & 1, 1));
    CHECK_EQ(p.nature, 3);
    for (int s = 0; s < pokemon::kStatCount; ++s) CHECK_EQ(p.ivs[s], before[s]);
    CHECK(checkGen4(p).legal());
}

TEST(legalize_result_survives_writing_to_raw_bytes) {
    pokemon::Pokemon p = pk4::toPokemon(test::kRoseradePk4, pk4::kPartySize);
    p.met.eggLocation = 0;
    p.met.metLocation = 16;  // Route 201: no Roserade line there
    p.originGame = version::kPlatinum;
    pokemon::edit4::setLevel(p, 50);
    LegalizeResult r = legalizeGen4(p, 2);
    CHECK(r.report.legal());

    uint8_t raw[pk4::kPartySize];
    std::memcpy(raw, test::kRoseradePk4, sizeof(raw));
    pk4::applyPokemon(r.pokemon, raw);
    pk4::refreshPartyStats(raw);
    pokemon::Pokemon back = pk4::toPokemon(raw, pk4::kPartySize);
    CHECK_EQ(back.met.metLocation, r.pokemon.met.metLocation);
    CHECK_EQ(back.met.eggLocation, r.pokemon.met.eggLocation);
    CHECK_EQ(back.met.metLevel, r.pokemon.met.metLevel);
    CHECK_EQ(back.pid, r.pokemon.pid);
    CHECK(checkGen4(back).legal());
}
