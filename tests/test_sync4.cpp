#include <cstdio>
#include <string>

#include "core/data/encounters4.h"
#include "core/data/learn4.h"
#include "core/data/names.h"
#include "core/data/personal.h"
#include "core/legality/legalize.h"
#include "core/legality/legality.h"
#include "core/legality/pid_iv.h"
#include "core/legality/rng4.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/edit4.h"
#include "core/pokemon/gift4.h"
#include "core/util/lcrng.h"
#include "tests/test.h"

using namespace pktool;
using legality::PidSource;
using legality::RngMethod;
using legality::Severity;
namespace v = legality::version;

namespace {

constexpr uint16_t kTid = 12345;
constexpr uint16_t kSid = 54321;
constexpr uint16_t kRoselia = 315;

bool hasFinding(const legality::Report& r, Severity s, const char* needle) {
    for (const auto& f : r.findings) {
        if (f.severity == s && f.text.find(needle) != std::string::npos) return true;
    }
    return false;
}

void dump(const legality::Report& r) {
    for (const auto& f : r.findings) std::printf("    [%d] %s\n", static_cast<int>(f.severity), f.text.c_str());
}

legality::PidIvRequest request(RngMethod method, uint8_t nature, pokemon::Gender gender, uint8_t slot) {
    legality::PidIvRequest req;
    req.species = kRoselia;  // 50:50-ish (127), two abilities
    req.tid = kTid;
    req.sid = kSid;
    req.nature = nature;
    req.gender = gender;
    req.abilitySlot = slot;
    req.method = method;
    return req;
}

// A wild Platinum Pokemon of `species` at its first wild encounter.
bool wildPlatinum(uint16_t species, pokemon::Pokemon& out, uint8_t wantKind = 255) {
    const data::Encounter4* begin;
    const data::Encounter4* end;
    data::encounters4(species, &begin, &end);
    for (const data::Encounter4* e = begin; e != end; ++e) {
        if (!(e->versions & data::kVerPlatinum) || e->kind >= data::kEncounterStatic) continue;
        if (wantKind != 255 && e->kind != wantKind) continue;
        pokemon::create4::Trainer t;
        t.name = "TEST";
        t.tid = kTid;
        t.sid = kSid;
        t.version = v::kPlatinum;
        out = pokemon::create4::newPokemon(t, species, e->maxLevel, 11);
        out.met.metLocation = e->location;
        out.met.metLevel = e->maxLevel;
        return true;
    }
    return false;
}

void setPid(pokemon::Pokemon& p, uint32_t pid) {
    p.pid = pid;
    pokemon::edit4::refreshDerived(p);
    p.ability = pokemon::edit4::abilityForSlot(p.species, static_cast<uint8_t>(pid & 1));
}

uint32_t next16(uint32_t& seed) {
    seed = util::lcrngNext(seed);
    return seed >> 16;
}

// The Poke Radar chain shiny recipe of PKHeX (ClassicEraRNG.GetChainShinyPID + IVs).
void chainShiny(uint32_t seed, uint16_t tid, uint16_t sid, uint32_t& pid, uint8_t ivs[pokemon::kStatCount]) {
    uint32_t lower = next16(seed) & 7;
    uint32_t upper = next16(seed) & 7;
    for (int i = 3; i < 16; ++i) lower |= (next16(seed) & 1) << i;
    upper = ((lower ^ tid ^ sid) & 0xFFF8u) | upper;
    pid = (upper << 16) | lower;
    uint32_t w1 = next16(seed) & 0x7FFF;
    uint32_t w2 = next16(seed) & 0x7FFF;
    legality::ivsFromWords(static_cast<uint16_t>(w1), static_cast<uint16_t>(w2), ivs);
}

}  // namespace

// ---------------------------------------------------------------- Synchronize

TEST(sync_widens_the_valid_nature_frames) {
    int natural = 0, synced = 0;
    for (RngMethod method : {RngMethod::MethodJ, RngMethod::MethodK}) {
        for (uint32_t i = 1; i <= 6000; ++i) {
            uint32_t low = i * 0x9E3779B1u;
            bool plain = legality::isNatureFrameValid(low, method);
            bool withSync = legality::isNatureFrameValid(low, method, true);
            CHECK(!plain || withSync);  // Synchronize only adds frames
            natural += plain;
            synced += withSync;
        }
    }
    std::printf("  valid frames: %d natural, %d with Synchronize (of 12000)\n", natural, synced);
    // Rejected PID pairs already make about half of all frames reachable; the
    // Synchronize coin flip makes almost every other one reachable too.
    CHECK(synced > natural + natural / 4);
}

TEST(sync_frames_are_method_one_and_marked) {
    for (RngMethod method : {RngMethod::MethodJ, RngMethod::MethodK}) {
        int syncUsed = 0;
        for (uint32_t salt = 1; salt <= 12; ++salt) {
            legality::PidIvRequest req = request(method, static_cast<uint8_t>(salt * 2 % 25), pokemon::Gender::Female,
                                                 static_cast<uint8_t>(salt & 1));
            req.shiny = (salt % 3 == 0);
            req.allowSync = true;
            legality::PidIvResult r = legality::generatePidIv(req, 1, salt);
            if (!r.found) continue;
            uint32_t low = 0;
            CHECK(legality::findPidIvMethod(r.pid, r.ivs, &low) == legality::PidIvMethod::Method1);
            CHECK_EQ(r.pid % 25, req.nature);
            CHECK(legality::isNatureFrameValid(low, method, true));
            if (r.source == PidSource::Synchronize) {
                ++syncUsed;
                CHECK(!legality::isNatureFrameValid(low, method, false));  // only reachable with the lead
            } else {
                CHECK(legality::isNatureFrameValid(low, method, false));
            }
        }
        std::printf("  method %d: %d of 12 frames need Synchronize\n", static_cast<int>(method), syncUsed);
    }
}

TEST(sync_reaches_shiny_requests_that_the_plain_search_misses) {
    // A Synchronize lead replaces the nature call, so the same small search
    // reaches frames the plain one cannot.
    int plain = 0, withSync = 0;
    for (uint32_t salt = 1; salt <= 40; ++salt) {
        legality::PidIvRequest req = request(RngMethod::MethodJ, static_cast<uint8_t>(salt % 25), pokemon::Gender::Female,
                                             static_cast<uint8_t>(salt & 1));
        req.shiny = true;
        plain += legality::generatePidIv(req, 1, salt).found;
        req.allowSync = true;
        withSync += legality::generatePidIv(req, 1, salt).found;
    }
    std::printf("  shiny requests found at effort 1: %d plain, %d with Synchronize (of 40)\n", plain, withSync);
    CHECK(withSync >= plain);
    CHECK(withSync > plain);
}

TEST(sync_off_by_default_keeps_the_old_frames) {
    legality::PidIvRequest req = request(RngMethod::MethodK, 11, pokemon::Gender::Male, 0);
    legality::PidIvResult r = legality::generatePidIv(req, 2);
    CHECK(r.found);
    CHECK(r.source == PidSource::Normal);
    uint32_t low = 0;
    CHECK(legality::findPidIvMethod(r.pid, r.ivs, &low) == legality::PidIvMethod::Method1);
    CHECK(legality::isNatureFrameValid(low, RngMethod::MethodK));
}

// ----------------------------------------------------------------- Cute Charm

TEST(cute_charm_pids_follow_the_gender_buffer) {
    CHECK_EQ(legality::cuteCharmPidMale(127, 3), 153);  // 25 * (127 / 25 + 1) + nature
    CHECK_EQ(legality::cuteCharmPidMale(31, 24), 74);
    CHECK(legality::isCuteCharmPid(kRoselia, pokemon::Gender::Female, 7));
    CHECK(!legality::isCuteCharmPid(kRoselia, pokemon::Gender::Female, 25));    // not a nature
    CHECK(legality::isCuteCharmPid(kRoselia, pokemon::Gender::Male, 153));
    CHECK(!legality::isCuteCharmPid(kRoselia, pokemon::Gender::Male, 200));     // male, but not the buffer
    CHECK(!legality::isCuteCharmPid(kRoselia, pokemon::Gender::Male, 7));
    CHECK(!legality::isCuteCharmPid(kRoselia, pokemon::Gender::Female, 0x100));
    CHECK(!legality::isCuteCharmPid(81, pokemon::Gender::Genderless, 5));       // Magnemite
    CHECK(legality::isCuteCharmPid(298, pokemon::Gender::Male, 200));           // male Azurill (ratio 191)
    CHECK(legality::isCuteCharmPid(183, pokemon::Gender::Male, 200));           // ... evolved into Marill
}

TEST(cute_charm_generation_hits_exact_ivs) {
    const struct {
        pokemon::Gender gender;
        uint8_t nature;
    } cases[] = {{pokemon::Gender::Female, 7}, {pokemon::Gender::Female, 12}, {pokemon::Gender::Male, 3},
                 {pokemon::Gender::Male, 18}};
    for (const auto& c : cases) {
        legality::PidIvRequest req = request(RngMethod::MethodJ, c.nature, c.gender, 0);
        uint32_t pid = c.gender == pokemon::Gender::Female ? c.nature : legality::cuteCharmPidMale(127, c.nature);
        req.abilitySlot = static_cast<uint8_t>(pid & 1);  // the buffer fixes the ability slot
        const uint8_t target[6] = {17, 4, 29, 23, 9, 30};
        for (int s = 0; s < 6; ++s) req.targetIvs[s] = target[s];
        req.allowCuteCharm = true;
        legality::PidIvResult r = legality::generatePidIv(req, 2);
        CHECK(r.found);
        CHECK_EQ(r.distance, 0);  // the IVs are free: any two consecutive calls
        CHECK(r.source == PidSource::CuteCharm);
        CHECK_EQ(r.pid, pid);
        for (int s = 0; s < 6; ++s) CHECK_EQ(r.ivs[s], target[s]);
        legality::PidIvMethod method = legality::findPidIvMethod(r.pid, r.ivs);
        CHECK(method == legality::PidIvMethod::CuteCharm || method == legality::PidIvMethod::Method1);
        CHECK(legality::isCuteCharmPid(kRoselia, c.gender, r.pid));
        CHECK(pokemon::edit4::genderFromPid(kRoselia, r.pid) == c.gender);
    }
}

TEST(cute_charm_is_not_used_when_it_cannot_satisfy_the_request) {
    // The Cute Charm PID fixes the ability slot (nature 7 female = PID 7 = slot 1).
    legality::PidIvRequest req = request(RngMethod::MethodJ, 7, pokemon::Gender::Female, 0);
    req.allowCuteCharm = true;
    legality::PidIvResult r = legality::generatePidIv(req, 2);
    CHECK(r.found);
    CHECK(r.source != PidSource::CuteCharm);
    CHECK_EQ(r.pid & 1, 0);

    // Shiny: a PID below 0x100 is only shiny for very particular trainer IDs.
    req = request(RngMethod::MethodJ, 7, pokemon::Gender::Female, 1);
    req.allowCuteCharm = true;
    req.shiny = true;
    r = legality::generatePidIv(req, 1, 5);
    CHECK(!r.found || r.source != PidSource::CuteCharm);

    // Species without two genders have no Cute Charm.
    req = request(RngMethod::MethodJ, 7, pokemon::Gender::Male, 1);
    req.species = 128;  // Tauros
    req.allowCuteCharm = true;
    r = legality::generatePidIv(req, 1);
    CHECK(!r.found || r.source != PidSource::CuteCharm);
}

TEST(checker_applies_the_strict_cute_charm_rule) {
    pokemon::Pokemon p;
    CHECK(wildPlatinum(kRoselia, p));
    uint8_t ivs[6] = {12, 31, 7, 20, 5, 28};
    for (int s = 0; s < 6; ++s) p.ivs[s] = ivs[s];
    legality::PidIvResult frames = legality::generateConsecutiveIvs(p.ivs, 1);
    for (int s = 0; s < 6; ++s) p.ivs[s] = frames.ivs[s];

    setPid(p, 7);  // female Roselia, nature 7: the Cute Charm PID
    CHECK(p.gender == pokemon::Gender::Female);
    legality::Report ok = legality::checkGen4(p);
    if (!ok.legal()) dump(ok);
    CHECK(ok.legal());
    CHECK(hasFinding(ok, Severity::Valid, "Cute Charm"));

    setPid(p, 153);  // male Roselia, nature 3: the male buffer
    CHECK(p.gender == pokemon::Gender::Male);
    CHECK(legality::checkGen4(p).legal());

    setPid(p, 200);  // male, below 0x100, but not buffer + nature
    CHECK(p.gender == pokemon::Gender::Male);
    legality::Report bad = legality::checkGen4(p);
    CHECK(!bad.legal());
    CHECK(hasFinding(bad, Severity::Invalid, "Cute Charm"));
}

// ------------------------------------------------------------------- LEGALIZE

TEST(legalize_uses_a_lead_and_says_so) {
    pokemon::Pokemon base;
    CHECK(wildPlatinum(kRoselia, base));
    int leads = 0;
    for (uint8_t nature : {uint8_t(7), uint8_t(2), uint8_t(20)}) {
        for (bool female : {true, false}) {
            pokemon::Pokemon p = base;
            for (auto& iv : p.ivs) iv = 31;
            // Ask for the traits through the editor rules, then LEGALIZE.
            pokemon::Gender g = female ? pokemon::Gender::Female : pokemon::Gender::Male;
            CHECK(pokemon::edit4::findPid(p.species, p.trainer.tid, p.trainer.sid, nature, false, g, 0, 1, p.pid));
            pokemon::edit4::refreshDerived(p);
            p.ability = pokemon::edit4::abilityForSlot(p.species, 0);
            legality::LegalizeResult r = legality::legalizeGen4(p, 4);
            if (!r.report.legal() || !r.unresolved.empty()) dump(r.report);
            CHECK(r.report.legal());
            CHECK_EQ(r.pokemon.nature, nature);
            CHECK(r.pokemon.gender == g);
            bool mentionsLead = false;
            for (const auto& c : r.changes) {
                if (c.find("Lead with") != std::string::npos) mentionsLead = true;
            }
            leads += mentionsLead;
        }
    }
    std::printf("  %d of 6 LEGALIZE results needed a lead\n", leads);
}

TEST(legalize_never_uses_leads_for_static_or_egg_origins) {
    // Hatched Pokemon keep their IVs and use no encounter RNG at all.
    pokemon::Pokemon p;
    CHECK(wildPlatinum(kRoselia, p));
    p.met.eggLocation = 2000;
    p.met.metLevel = 0;
    p.level = 5;
    for (auto& iv : p.ivs) iv = 31;
    legality::LegalizeResult r = legality::legalizeGen4(p, 2);
    for (const auto& c : r.changes) CHECK(c.find("Lead with") == std::string::npos);
}

// ----------------------------------------------------------------- Poke Radar

TEST(radar_chain_shiny_frames_are_recognised) {
    for (uint32_t seed : {0x1234u, 0xDEADBEEFu, 0x0u, 0xFFFFFFFFu, 0x31415926u}) {
        uint32_t pid;
        uint8_t ivs[pokemon::kStatCount];
        chainShiny(seed, kTid, kSid, pid, ivs);
        CHECK(pokemon::isShiny(pid, kTid, kSid));
        CHECK(legality::isChainShinyFrame(pid, ivs, kTid, kSid));
        uint8_t other[pokemon::kStatCount];
        for (int s = 0; s < pokemon::kStatCount; ++s) other[s] = ivs[s];
        other[pokemon::kSpe] ^= 4;  // IVs no longer follow the PID calls
        CHECK(!legality::isChainShinyFrame(pid, other, kTid, kSid));
        CHECK(!legality::isChainShinyFrame(pid ^ 0x8, ivs, kTid, kSid));  // PID bits no longer match the calls
        CHECK(!legality::isChainShinyFrame(pid, ivs, kTid ^ 0x40, kSid));
    }
}

TEST(radar_chain_shiny_is_valid_in_dppt_grass_only) {
    pokemon::Pokemon p;
    CHECK(wildPlatinum(399, p, 0));  // Bidoof in Platinum grass
    uint32_t pid;
    uint8_t ivs[pokemon::kStatCount];
    chainShiny(0xBADC0DE, kTid, kSid, pid, ivs);
    for (int s = 0; s < 6; ++s) p.ivs[s] = ivs[s];
    setPid(p, pid);
    CHECK(p.shiny);
    legality::Report grass = legality::checkGen4(p);
    if (!grass.legal()) dump(grass);
    CHECK(grass.legal());
    CHECK(hasFinding(grass, Severity::Valid, "chain shiny"));

    // HG/SS has no Poke Radar.
    pokemon::Pokemon hgss = p;
    hgss.originGame = v::kHeartGold;
    legality::Report r = legality::checkGen4(hgss);
    CHECK(hasFinding(r, Severity::Invalid, "chain shinies only exist in D/P/Pt grass"));

    // Surf and fishing spots are not grass.
    pokemon::Pokemon surf;
    bool found = false;
    for (uint16_t s = 1; s <= data::kMaxSpeciesGen4 && !found; ++s) {
        const data::Encounter4* b;
        const data::Encounter4* e;
        data::encounters4(s, &b, &e);
        for (const data::Encounter4* x = b; x != e && !found; ++x) {
            if (!(x->versions & data::kVerPlatinum) || x->kind != 1 || data::preEvolution4(s) != 0) continue;
            bool grassHere = false;
            for (const data::Encounter4* y = b; y != e; ++y) {
                grassHere |= y->kind == 0 && y->location == x->location && (y->versions & data::kVerPlatinum);
            }
            if (grassHere) continue;
            found = wildPlatinum(s, surf, 1);
        }
    }
    CHECK(found);
    if (found) {
        for (int s = 0; s < 6; ++s) surf.ivs[s] = ivs[s];
        chainShiny(0xBADC0DE, kTid, kSid, pid, ivs);
        setPid(surf, pid);
        CHECK(surf.shiny);
        CHECK(hasFinding(legality::checkGen4(surf), Severity::Invalid, "chain shinies only exist in D/P/Pt grass"));
    }

    // The Great Marsh (Pt) does not allow the Poke Radar.
    pokemon::Pokemon marsh = p;
    marsh.met.metLocation = 52;
    CHECK(hasFinding(legality::checkGen4(marsh), Severity::Invalid, "chain shinies only exist"));
}

TEST(shiny_pids_outside_every_known_recipe_are_invalid) {
    pokemon::Pokemon p;
    CHECK(wildPlatinum(399, p, 0));
    uint32_t pid;
    uint8_t ivs[pokemon::kStatCount];
    chainShiny(0x5EED, kTid, kSid, pid, ivs);
    for (int s = 0; s < 6; ++s) p.ivs[s] = ivs[s];
    p.ivs[pokemon::kHp] ^= 1;  // breaks both the chain and Method 1
    setPid(p, pid);
    legality::Report r = legality::checkGen4(p);
    CHECK(hasFinding(r, Severity::Invalid, "Shiny PID/IVs"));
}

TEST(shiny_method_one_frames_stay_valid) {
    pokemon::Pokemon p;
    CHECK(wildPlatinum(399, p, 0));
    legality::PidIvRequest req;
    req.species = p.species;
    req.tid = kTid;
    req.sid = kSid;
    req.nature = 4;
    req.shiny = true;
    req.gender = pokemon::Gender::Male;
    req.abilitySlot = 0;
    req.method = RngMethod::MethodJ;
    legality::PidIvResult r = legality::generatePidIv(req, 3, 9);
    CHECK(r.found);
    for (int s = 0; s < 6; ++s) p.ivs[s] = r.ivs[s];
    setPid(p, r.pid);
    CHECK(legality::checkGen4(p).legal());
}

// ------------------------------------------------------------- Manaphy egg

namespace {

pokemon::create4::Trainer manaphyTrainer(uint8_t version) {
    pokemon::create4::Trainer t;
    t.name = "TEST";
    t.tid = kTid;
    t.sid = kSid;
    t.version = version;
    t.today = pokemon::Date{9, 10, 9};
    return t;
}

}  // namespace

TEST(manaphy_egg_is_received_and_legal) {
    for (uint8_t version : {v::kDiamond, v::kPlatinum, v::kSoulSilver}) {
        for (uint32_t seed = 1; seed <= 20; ++seed) {
            uint8_t raw[pokemon::pk4::kStoredSize];
            pokemon::Pokemon p = pokemon::gift4::receiveManaphyEgg(manaphyTrainer(version), seed, raw);
            CHECK_EQ(p.species, 490);
            CHECK(p.isEgg);
            CHECK(p.met.fatefulEncounter);
            CHECK_EQ(p.met.eggLocation, 3001);
            CHECK_EQ(p.level, 1);
            CHECK(p.gender == pokemon::Gender::Genderless);
            CHECK(!p.shiny);
            CHECK_EQ(p.moves[0], 294);
            CHECK_EQ(p.moves[1], 145);
            CHECK_EQ(p.moves[2], 346);
            CHECK_EQ(p.met.ball, data::kBallPoke);
            CHECK_EQ(p.originGame, version);
            legality::Report r = legality::checkGen4(p);
            if (!r.legal()) dump(r);
            CHECK(r.legal());
            CHECK(hasFinding(r, Severity::Valid, "Manaphy egg"));
        }
    }
}

TEST(manaphy_egg_from_a_gen_v_save_or_odd_language_is_made_playable) {
    pokemon::create4::Trainer t = manaphyTrainer(v::kBlack2);
    t.language = 8;  // Korean: the egg only exists in the other languages
    uint8_t raw[pokemon::pk4::kStoredSize];
    pokemon::Pokemon p = pokemon::gift4::receiveManaphyEgg(t, 3, raw);
    CHECK_EQ(p.format, 4);
    CHECK_EQ(p.originGame, v::kDiamond);
    CHECK_EQ(p.language, 2);
    CHECK(legality::checkGen4(p).legal());
}

TEST(manaphy_egg_hatched_and_traded_variants) {
    uint8_t raw[pokemon::pk4::kStoredSize];
    pokemon::Pokemon egg = pokemon::gift4::receiveManaphyEgg(manaphyTrainer(v::kPlatinum), 4, raw);

    pokemon::Pokemon hatched = egg;  // hatched at Solaceon Town
    hatched.isEgg = false;
    hatched.nickname = "MANAPHY";
    hatched.met.metLocation = 4;
    hatched.met.metLevel = 0;
    hatched.met.metDate = pokemon::Date{9, 10, 20};
    hatched.friendship = 70;
    legality::Report r = legality::checkGen4(hatched);
    if (!r.legal()) dump(r);
    CHECK(r.legal());

    pokemon::Pokemon traded = hatched;  // traded as an egg: the egg location becomes the link trade
    traded.met.eggLocation = 2002;
    CHECK(legality::checkGen4(traded).legal());

    pokemon::Pokemon tradedEgg = egg;  // unhatched traded egg
    tradedEgg.met.metLocation = 2002;
    CHECK(legality::checkGen4(tradedEgg).legal());
}

TEST(manaphy_egg_known_bad_cases_fail) {
    uint8_t raw[pokemon::pk4::kStoredSize];
    pokemon::Pokemon egg = pokemon::gift4::receiveManaphyEgg(manaphyTrainer(v::kPlatinum), 5, raw);
    pokemon::Pokemon p = egg;
    p.met.ball = 1;
    CHECK(hasFinding(legality::checkGen4(p), Severity::Invalid, "Poke Ball"));

    p = egg;
    p.language = 8;
    CHECK(hasFinding(legality::checkGen4(p), Severity::Invalid, "Language"));

    p = egg;
    p.ivs[pokemon::kAtk] ^= 1;  // IVs no longer follow the PID
    CHECK(hasFinding(legality::checkGen4(p), Severity::Invalid, "Manaphy egg's RNG"));

    p = egg;  // an untraded Manaphy cannot be shiny: make the PID shiny for its trainer
    uint32_t lo = p.pid & 0xFFFF;
    p.pid = ((lo ^ kTid ^ kSid) << 16) | lo;
    pokemon::edit4::refreshDerived(p);
    CHECK(p.shiny);
    CHECK(!legality::checkGen4(p).legal());

    p = egg;  // an egg that is not from the Ranger is just an event Pokemon without a card
    p.met.eggLocation = 2000;
    CHECK(!legality::checkGen4(p).findings.empty());
    CHECK(!hasFinding(legality::checkGen4(p), Severity::Valid, "Manaphy egg"));
}

TEST(manaphy_anti_shiny_reroll_is_accepted) {
    // A Method 1 frame that is shiny for the trainer is re-rolled by the game's
    // ARNG until it is not.
    uint8_t raw[pokemon::pk4::kStoredSize];
    pokemon::Pokemon egg = pokemon::gift4::receiveManaphyEgg(manaphyTrainer(v::kPlatinum), 6, raw);
    const uint32_t trainerValue = (kTid ^ kSid) >> 3;
    uint32_t seed = 0x2468ACE0;
    for (int attempt = 0; attempt < 400000; ++attempt) {
        uint32_t a = next16(seed);
        uint32_t b = next16(seed);
        uint32_t c = next16(seed) & 0x7FFF;
        uint32_t d = next16(seed) & 0x7FFF;
        if (((a ^ b) >> 3) != trainerValue) continue;
        uint32_t pid = (b << 16) | a;
        while ((((pid >> 16) ^ (pid & 0xFFFF)) >> 3) == trainerValue) pid = pid * 0x6C078965u + 1u;
        pokemon::Pokemon p = egg;
        legality::ivsFromWords(static_cast<uint16_t>(c), static_cast<uint16_t>(d), p.ivs);
        p.pid = pid;
        pokemon::edit4::refreshDerived(p);
        legality::Report r = legality::checkGen4(p);
        if (!r.legal()) dump(r);
        CHECK(r.legal());
        CHECK(hasFinding(r, Severity::Valid, "anti-shiny"));
        // The same PID for another trainer would not be explained by the re-roll.
        p.trainer.tid = static_cast<uint16_t>(kTid + 0x100);
        CHECK(!legality::checkGen4(p).legal());
        return;
    }
    CHECK(false);
}
