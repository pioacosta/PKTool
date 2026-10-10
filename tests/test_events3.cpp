// Gen III events and Colosseum/XD: the generated tables, the special PID/IV
// methods (B-A-C-D, Channel, XD RNG), the checker, the creator and LEGALIZE.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/data/events3.h"
#include "core/data/gen3.h"
#include "core/data/names.h"
#include "core/data/sorted_names.h"
#include "core/io/file_io.h"
#include "core/legality/events3_check.h"
#include "core/legality/legalize.h"
#include "core/legality/legality.h"
#include "core/legality/rng3.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/cxd3.h"
#include "core/pokemon/edit4.h"
#include "core/pokemon/gift3.h"
#include "core/pokemon/pk3.h"
#include "core/pokemon/pk4.h"
#include "core/pokemon/transfer34.h"
#include "core/saves/gen3_save.h"
#include "ds/app/app.h"
#include "tests/fake_saves.h"
#include "tests/test.h"

using namespace pktool;
namespace pk3 = pokemon::pk3;
namespace rng3 = legality::rng3;
using legality::Severity;

namespace {

pokemon::create4::Trainer frlgTrainer() {
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

int eventIndex(uint16_t species, const char* titlePart) {
    for (int i = data::findEvent3(species); i >= 0; i = data::findEvent3(species, i + 1)) {
        if (std::strstr(data::event3(i).title, titlePart) != nullptr) return i;
    }
    return -1;
}

pokemon::Pokemon receive(int index, uint32_t seed, pokemon::gift3::Criteria want = pokemon::gift3::Criteria(),
                         bool* satisfied = nullptr) {
    uint8_t raw[pk3::kStoredSize];
    return pokemon::gift3::receive(data::event3(index), frlgTrainer(), seed, want, raw, satisfied);
}

bool hasFinding(const legality::Report& r, Severity severity, const char* text) {
    for (const auto& f : r.findings) {
        if (f.severity == severity && f.text.find(text) != std::string::npos) return true;
    }
    return false;
}

void printInvalid(const char* what, const legality::Report& r) {
    std::printf("  %s:", what);
    for (const auto& f : r.findings) {
        if (f.severity == Severity::Invalid) std::printf(" [%s]", f.text.c_str());
    }
    std::printf("\n");
}

// ---- Colosseum / XD frames (XD RNG): IV, IV, ability, PID high, PID low.

struct CxdFrame {
    uint32_t pid;
    uint8_t ivs[pokemon::kStatCount];
};

uint32_t out16(uint32_t s) { return s >> 16; }

CxdFrame cxdFrame(uint32_t origin) {
    CxdFrame f = {};
    uint32_t s = rng3::xdNext(origin);
    uint32_t iv1 = (s >> 16) & 0x7FFF;
    s = rng3::xdNext(s);
    uint32_t iv2 = (s >> 16) & 0x7FFF;
    s = rng3::xdNext(s);  // ability
    s = rng3::xdNext(s);
    uint32_t hi = out16(s);
    s = rng3::xdNext(s);
    f.pid = (hi << 16) | out16(s);
    uint32_t iv32 = (iv2 << 15) | iv1;
    for (int k = 0; k < pokemon::kStatCount; ++k) f.ivs[k] = static_cast<uint8_t>((iv32 >> (5 * k)) & 31);
    return f;
}

// A Colosseum/XD Pokemon in a Gen III entity, round-tripped through the PK3 bytes.
pokemon::Pokemon gcPokemon(uint16_t species, uint8_t level, uint8_t location, bool fateful, const CxdFrame& frame,
                           uint16_t tid, uint16_t sid, const uint16_t* moves) {
    pokemon::Pokemon p = pokemon::create4::newPokemon(frlgTrainer(), species, level, 7);
    p.originGame = legality::version::kColosseumXD;
    p.trainer.name = "ASH";
    p.trainer.tid = tid;
    p.trainer.sid = sid;
    p.trainer.gender = pokemon::Gender::Male;
    p.pid = frame.pid;
    std::memcpy(p.ivs, frame.ivs, sizeof p.ivs);
    p.met.metLocation = location;
    p.met.metLevel = level;
    p.met.fatefulEncounter = fateful;
    p.met.ball = 4;
    for (int m = 0; m < 4; ++m) {
        p.moves[m] = 0;
        p.ppUps[m] = 0;
        p.pp[m] = 0;
    }
    for (int m = 0; m < 4; ++m) {
        if (moves != nullptr && moves[m] != 0) pokemon::edit4::setMove(p, m, moves[m]);
    }
    pokemon::edit4::refreshDerived(p);
    p.ability = data::ability3(species, static_cast<uint8_t>((p.pid >> 16) & 1));  // the RNG's ability call, not the PID
    uint8_t raw[pk3::kStoredSize];
    pokemon::create4::encode(p, raw);
    return pk3::toPokemon(raw, pk3::kStoredSize);
}

const data::Gc3* findGc(uint16_t species, uint8_t kind) {
    for (int i = 0; i < data::gc3Count(); ++i) {
        if (data::gc3(i).species == species && data::gc3(i).kind == kind) return &data::gc3(i);
    }
    return nullptr;
}

}  // namespace

TEST(events3_tables_are_sane) {
    CHECK(data::events3Count() > 100);
    for (int i = 0; i < data::events3Count(); ++i) {
        const data::Event3& e = data::event3(i);
        CHECK(e.species >= 1 && e.species <= 386);
        CHECK(e.level >= 1 && e.level <= 100);
        CHECK(e.versions != 0);
        CHECK(e.title[0] != '\0');
        CHECK(e.method <= data::kEvMethod2);
        for (int m = 0; m < 4; ++m) CHECK(e.moves[m] <= data::kMaxMoveGen3);
        if (i > 0 && i != data::generated::kEvents3Count) CHECK(data::event3(i - 1).species <= e.species);  // each part is sorted
    }
    CHECK(data::gc3Count() > 150);
    for (int i = 0; i < data::gc3Count(); ++i) {
        CHECK(data::gc3(i).species >= 1 && data::gc3(i).species <= 386);
        if (i > 0) CHECK(data::gc3(i - 1).species <= data::gc3(i).species);
    }
    CHECK_EQ(data::generated::kMystryMewSeedCount, 86);
    // Species the games only gave away in events.
    for (uint16_t s : {151, 251, 385, 386}) CHECK(data::findEvent3(s) >= 0);
    CHECK(findGc(197, data::kGcColoStarter) != nullptr);  // Umbreon
    CHECK(findGc(133, data::kGcXdGift) != nullptr);       // Eevee
    CHECK(findGc(244, data::kGcColoShadow) != nullptr);   // Entei
}

TEST(rng3_reverse_constants) {
    for (uint32_t s : {0u, 1u, 0x12345678u, 0xFFFFFFFFu, 0xE8D1A0BFu}) {
        CHECK_EQ(rng3::lcPrev(rng3::lcNext(s)), s);
        CHECK_EQ(rng3::xdPrev(rng3::xdNext(s)), s);
    }
}

// PKHeX: of the 65536 seeds, exactly 9 give a shiny WISHMKR Jirachi (TID 20043,
// SID 0), one per nature. This pins down the B-A-C-D PID order independently.
TEST(events3_wishmkr_jirachi_has_nine_shiny_seeds) {
    const uint16_t kShinySeeds[9] = {0x7236, 0xA030, 0xECDD, 0x7360, 0x9359, 0x3D60, 0xCF37, 0x353D, 0xF500};
    // Lonely, Naughty, Docile, Serious, Hasty, Timid, Jolly, Bashful, Careful. (PKHeX's own table swaps
    // Timid and Serious between 0x7360 and 0x3D60; the PID order is pinned by the other seven.)
    const uint8_t kNatures[9] = {1, 4, 6, 12, 11, 10, 13, 18, 23};
    int shinies = 0;
    for (uint32_t seed = 0; seed < 0x10000; ++seed) {
        uint32_t s = rng3::lcNext(seed);
        uint32_t a = out16(s);
        s = rng3::lcNext(s);
        uint32_t pid = rng3::bacdRegular(a, out16(s));
        if (!pokemon::isShiny(pid, 20043, 0)) continue;
        bool known = false;
        for (int k = 0; k < 9; ++k) {
            if (seed == kShinySeeds[k]) {
                known = true;
                CHECK_EQ(pid % 25, kNatures[k]);
            }
        }
        CHECK(known);
        ++shinies;
    }
    CHECK_EQ(shinies, 9);
}

TEST(events3_every_event_is_received_legal) {
    int checked = 0;
    for (int i = 0; i < data::events3Count(); ++i) {
        const data::Event3& e = data::event3(i);
        for (uint32_t seed : {1u, 0xC0FFEEu}) {
            pokemon::Pokemon p = receive(i, seed * 31 + static_cast<uint32_t>(i));
            legality::Report r = legality::checkGen3(p);
            if (!r.legal() || !hasFinding(r, Severity::Valid, "Event: ")) {
                std::printf("  event %d (%s)\n", i, e.title);
                printInvalid("received event", r);
            }
            CHECK(r.legal());
            CHECK(hasFinding(r, Severity::Valid, "Event: "));
            CHECK_EQ(p.species, e.species);
            CHECK_EQ(p.level, e.level);
            CHECK_EQ(p.met.metLocation, 255);
            CHECK_EQ(p.met.metLevel, e.metLevel);
            CHECK_EQ(p.met.fatefulEncounter, (e.flags & data::kEvFateful) != 0);
            CHECK(p.format == 3);
            if (e.ot[0]) {  // "a|b" lists alternatives
                CHECK(("|" + std::string(e.ot) + "|").find("|" + p.trainer.name + "|") != std::string::npos);
            } else {
                CHECK(p.trainer.name == ((e.flags & data::kEvEgg) ? "ＰＡＴＥ" : "PATE"));  // eggs: full-width
            }
            CHECK_EQ(p.isEgg, (e.flags & data::kEvEgg) != 0);
            if (e.flags & data::kEvTid3000) CHECK(p.trainer.tid >= 1 && p.trainer.tid <= 2999);
            if (e.tid != data::kEventIdAny) CHECK_EQ(p.trainer.tid, e.tid);
            ++checked;
        }
    }
    std::printf("  %d event Pokemon received and checked\n", checked);
}

TEST(events3_criteria_pick_frames_of_the_events_rng) {
    int wishmkr = eventIndex(385, "WISHMKR");
    CHECK(wishmkr >= 0);
    pokemon::gift3::Criteria c;
    c.shiny = 1;
    c.nature = 10;  // Timid: seed 0x7360
    bool ok = false;
    pokemon::Pokemon p = receive(wishmkr, 5, c, &ok);
    CHECK(ok);
    CHECK(p.shiny);
    CHECK_EQ(p.nature, 10);
    CHECK_EQ(p.trainer.tid, 20043);
    CHECK(p.heldItem == data::itemFromGen3(169) || p.heldItem == data::itemFromGen3(170));  // Ganlon or Salac
    CHECK(legality::checkGen3(p).legal());
    // Not every wish exists: the Berry Fix Zigzagoon is always shiny, Metang never.
    c = pokemon::gift3::Criteria();
    c.shiny = 1;
    receive(eventIndex(375, "ROCKS"), 5, c, &ok);
    CHECK(!ok);
    c.shiny = 0;
    c.nature = 3;
    receive(eventIndex(151, "MYSTRY"), 5, c, &ok);
    CHECK(ok);
}

TEST(events3_checker_rejects_changed_events) {
    int wishmkr = eventIndex(385, "WISHMKR");
    pokemon::Pokemon good = receive(wishmkr, 9);
    CHECK(legality::checkGen3(good).legal());

    pokemon::Pokemon bad = good;
    bad.ivs[pokemon::kAtk] = static_cast<uint8_t>((bad.ivs[pokemon::kAtk] + 1) & 31);
    CHECK(!legality::checkGen3(bad).legal());  // IVs no longer from the event's RNG
    bad = good;
    bad.pid ^= 0x00010000u;
    CHECK(!legality::checkGen3(bad).legal());
    bad = good;
    bad.met.ball = 1;  // Master Ball
    CHECK(!legality::checkGen3(bad).legal());
    bad = good;
    bad.met.fatefulEncounter = true;
    CHECK(!legality::checkGen3(bad).legal());
    bad = good;
    bad.met.metLevel = 6;
    CHECK(!legality::checkGen3(bad).legal());
    bad = good;
    bad.language = 1;  // the Wishmaker Jirachi was English
    CHECK(!legality::checkGen3(bad).legal());
    bad = good;
    bad.originGame = 5;  // LeafGreen: the gift is stamped Ruby
    CHECK(!legality::checkGen3(bad).legal());
    bad = good;
    bad.trainer.gender = pokemon::Gender::Female;  // OT gender is fixed to male
    CHECK(!legality::checkGen3(bad).legal());
    bad = good;
    bad.level = 4;
    bad.exp = 0;
    CHECK(!legality::checkGen3(bad).legal());

    // Another trainer ID no longer identifies the event: unknown, only a warning.
    bad = good;
    bad.trainer.tid = 1;
    bad.shiny = pokemon::isShiny(bad.pid, 1, 0);
    legality::Report r = legality::checkGen3(bad);
    CHECK(!hasFinding(r, Severity::Valid, "Event: "));
    CHECK(hasFinding(r, Severity::Warning, "not in the PKTool database"));

    // The Metang of the ROCKS event is never shiny.
    int metang = eventIndex(375, "ROCKS");
    pokemon::Pokemon rocks = receive(metang, 3);
    CHECK(legality::checkGen3(rocks).legal());
    bad = rocks;
    bad.pid = (0x1234u << 16) | (0x1234u ^ rocks.trainer.tid ^ rocks.trainer.sid);  // a shiny PID for these trainer IDs
    bad.shiny = true;
    CHECK(hasFinding(legality::checkGen3(bad), Severity::Invalid, "never shiny"));
}

TEST(events3_event_moves_are_legal_even_when_unlearnable) {
    // Every event Pokemon is legal with exactly the moves the event gave it,
    // even where its species could not learn them.
    int moveless = 0;
    for (int i = 0; i < data::events3Count(); ++i) {
        pokemon::Pokemon p = receive(i, 11 + static_cast<uint32_t>(i));
        // Gifts without listed moves (PCJP) know what the species knows at that level.
        if (data::event3(i).moves[0] != 0) {
            for (int m = 0; m < 4; ++m) CHECK_EQ(p.moves[m], data::event3(i).moves[m]);
        }
        legality::Report r = legality::checkGen3(p);
        CHECK(r.legal());
        CHECK(!hasFinding(r, Severity::Warning, "unchecked"));
        if (p.moves[0] == 0) ++moveless;
    }
    CHECK_EQ(moveless, 0);
}

TEST(events3_channel_jirachi) {
    int channel = eventIndex(385, "CHANNEL");
    CHECK(channel >= 0);
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        pokemon::Pokemon p = receive(channel, seed * 7919);
        CHECK(legality::checkGen3(p).legal());
        CHECK(hasFinding(legality::checkGen3(p), Severity::Valid, "Event: "));
        CHECK_EQ(p.trainer.tid, 40122);
        CHECK_EQ(p.met.metLevel, 0);
        CHECK(p.originGame == 1 || p.originGame == 2);  // Sapphire or Ruby, from the RNG
        CHECK(p.heldItem == data::itemFromGen3(169) || p.heldItem == data::itemFromGen3(170));

        pokemon::Pokemon bad = p;
        bad.trainer.sid = static_cast<uint16_t>(p.trainer.sid + 1);
        bad.shiny = pokemon::isShiny(bad.pid, bad.trainer.tid, bad.trainer.sid);
        CHECK(!legality::checkGen3(bad).legal());
        bad = p;
        bad.originGame = p.originGame == 1 ? 2 : 1;
        CHECK(!legality::checkGen3(bad).legal());  // the version comes from the RNG too
        bad = p;
        bad.ivs[pokemon::kHp] = static_cast<uint8_t>((bad.ivs[pokemon::kHp] + 3) & 31);
        CHECK(!legality::checkGen3(bad).legal());
    }
    // Shiny Channel Jirachi exist.
    pokemon::gift3::Criteria c;
    c.shiny = 1;
    bool ok = false;
    pokemon::Pokemon shiny = receive(channel, 3, c, &ok);
    CHECK(ok);
    CHECK(shiny.shiny);
    CHECK(legality::checkGen3(shiny).legal());
}

TEST(events3_mystry_mew_needs_a_distribution_seed) {
    int mew = eventIndex(151, "MYSTRY");
    pokemon::Pokemon p = receive(mew, 2);
    CHECK(legality::checkGen3(p).legal());
    // A Method-1-like frame from an arbitrary seed is not one of the 86 distributed.
    uint32_t seed = 0x1234;
    uint32_t s = rng3::lcNext(seed);
    uint32_t a = out16(s);
    s = rng3::lcNext(s);
    pokemon::Pokemon bad = p;
    bad.pid = rng3::bacdRegular(a, out16(s));
    s = rng3::lcNext(s);
    uint32_t iv1 = (s >> 16) & 0x7FFF;
    s = rng3::lcNext(s);
    uint32_t iv2 = (s >> 16) & 0x7FFF;
    uint32_t iv32 = (iv2 << 15) | iv1;
    for (int k = 0; k < 6; ++k) bad.ivs[k] = static_cast<uint8_t>((iv32 >> (5 * k)) & 31);
    bad.shiny = pokemon::isShiny(bad.pid, bad.trainer.tid, bad.trainer.sid);
    pokemon::edit4::refreshDerived(bad);
    bad.ability = data::ability3(bad.species, static_cast<uint8_t>(bad.pid & 1));
    legality::Report r = legality::checkGen3(bad);
    CHECK(hasFinding(r, Severity::Invalid, "RNG"));
    CHECK(rng3::mystryMewOrigin(0x0652));
    CHECK(!rng3::mystryMewOrigin(0x1234));
    CHECK(!rng3::mystryMewOrigin(0x6065));  // unreleased
}

TEST(events3_japanese_events_use_the_japanese_character_set) {
    int tanabata = eventIndex(385, "Tanabata Jirachi (2004)");
    CHECK(tanabata >= 0);
    pokemon::Pokemon p = receive(tanabata, 4);
    CHECK_EQ(p.language, 1);
    CHECK(p.trainer.name == data::event3(tanabata).ot);
    CHECK(!p.nickname.empty());
    legality::Report r = legality::checkGen3(p);
    CHECK(r.legal());
    CHECK(hasFinding(r, Severity::Valid, "Event: "));
    // The same Pokemon with an English language flag is not that event.
    pokemon::Pokemon bad = p;
    bad.language = 2;
    CHECK(!hasFinding(legality::checkGen3(bad), Severity::Valid, "Event: ") || !legality::checkGen3(bad).legal());
}

// ---------------------------------------------------------------- Colosseum / XD

TEST(cxd_frames_are_recognised) {
    int plain = 0;
    for (uint32_t origin = 0x1000; origin < 0x1000 + 400; ++origin) {
        CxdFrame f = cxdFrame(origin * 2654435761u);
        if (pokemon::isShiny(f.pid, 100, 200)) continue;
        CHECK(rng3::findCxd(f.pid, f.ivs, 100, 200) == rng3::CxdType::Plain);
        ++plain;
    }
    CHECK(plain > 300);
    // A PID unrelated to the IVs is not a frame.
    CxdFrame f = cxdFrame(77);
    CHECK(rng3::findCxd(f.pid ^ 0x40000u, f.ivs, 100, 200) == rng3::CxdType::None);
    f.ivs[pokemon::kSpe] ^= 1;
    CHECK(rng3::findCxd(cxdFrame(77).pid, f.ivs, 100, 200) == rng3::CxdType::None);
}

TEST(cxd_anti_shiny_rerolls_are_recognised) {
    // IV, IV, ability, then PID pairs until one is not shiny.
    const uint16_t tid = 31337, sid = 4242;
    int found = 0;
    for (uint32_t origin = 1; origin < 3000000 && found < 2; origin += 1) {
        uint32_t s = origin * 2654435761u;
        uint32_t t = rng3::xdNext(s);
        uint32_t iv1 = (t >> 16) & 0x7FFF;
        t = rng3::xdNext(t);
        uint32_t iv2 = (t >> 16) & 0x7FFF;
        t = rng3::xdNext(t);  // ability
        uint32_t hi1 = out16(rng3::xdNext(t));
        uint32_t lo1 = out16(rng3::xdNext(rng3::xdNext(t)));
        if (!pokemon::isShiny((hi1 << 16) | lo1, tid, sid)) continue;  // the first pair must be shiny
        uint32_t hi2 = out16(rng3::xdNext(rng3::xdNext(rng3::xdNext(t))));
        uint32_t lo2 = out16(rng3::xdNext(rng3::xdNext(rng3::xdNext(rng3::xdNext(t)))));
        uint32_t pid = (hi2 << 16) | lo2;
        if (pokemon::isShiny(pid, tid, sid)) continue;
        uint32_t iv32 = (iv2 << 15) | iv1;
        uint8_t ivs[pokemon::kStatCount];
        for (int k = 0; k < 6; ++k) ivs[k] = static_cast<uint8_t>((iv32 >> (5 * k)) & 31);
        CHECK(rng3::findCxd(pid, ivs, tid, sid) == rng3::CxdType::Anti);
        ++found;
    }
    CHECK_EQ(found, 2);
}

TEST(cxd_shadow_pokemon_are_recognised) {
    const data::Gc3* entei = findGc(244, data::kGcColoShadow);
    CHECK(entei != nullptr);
    CxdFrame f = cxdFrame(0xABCDEF);
    uint16_t tid = 11111, sid = 22222;
    pokemon::Pokemon p = gcPokemon(244, entei->level, entei->location, false, f, tid, sid, entei->moves);
    legality::Report r = legality::checkGen3(p);
    if (!r.legal()) printInvalid("Entei", r);
    CHECK(r.legal());
    CHECK(hasFinding(r, Severity::Valid, "Colosseum Shadow Pokemon"));
    CHECK(!hasFinding(r, Severity::Warning, "shadow locks"));  // Entei has no locks to verify

    pokemon::Pokemon bad = p;
    bad.pid ^= 0x00020000u;
    CHECK(hasFinding(legality::checkGen3(bad), Severity::Invalid, "Colosseum/XD RNG"));
    bad = p;
    bad.met.metLevel = 41;  // no such encounter: unknown, only a warning
    CHECK(hasFinding(legality::checkGen3(bad), Severity::Warning, "no known encounter"));
    bad = p;
    bad.trainer.gender = pokemon::Gender::Female;
    CHECK(!legality::checkGen3(bad).legal());

    // XD Shadow Pokemon are never shiny and keep the fateful flag.
    const data::Gc3* xd = findGc(216, data::kGcXdShadow);  // Teddiursa
    CHECK(xd != nullptr);
    CxdFrame g;
    uint32_t origin = 5;
    do g = cxdFrame(origin++ * 40503u); while (pokemon::isShiny(g.pid, tid, sid));
    pokemon::Pokemon t = gcPokemon(216, xd->level, xd->location, true, g, tid, sid, xd->moves);
    legality::Report rt = legality::checkGen3(t);
    if (!rt.legal()) printInvalid("Teddiursa", rt);
    CHECK(rt.legal());
    // A shiny one (same trainer, PID forced shiny) is rejected, whatever its frame.
    bool shinyFound = false;
    for (uint32_t o = 1; o < 400000 && !shinyFound; ++o) {
        CxdFrame h = cxdFrame(o * 2654435761u);
        if (!pokemon::isShiny(h.pid, tid, sid)) continue;
        pokemon::Pokemon s = gcPokemon(216, xd->level, xd->location, true, h, tid, sid, xd->moves);
        CHECK(hasFinding(legality::checkGen3(s), Severity::Invalid, "never shiny"));
        shinyFound = true;
    }
    CHECK(shinyFound);
}

TEST(cxd_starters_follow_the_trainer_ids) {
    // XD: TID, SID, fake PID x2, IV, IV, ability, PID, PID.
    uint32_t origin = 0x13579BDF;
    uint32_t s = rng3::xdNext(origin);
    uint16_t tid = static_cast<uint16_t>(out16(s));
    s = rng3::xdNext(s);
    uint16_t sid = static_cast<uint16_t>(out16(s));
    s = rng3::xdNext(rng3::xdNext(s));  // fake PID
    CxdFrame f = {};
    uint32_t t = rng3::xdNext(s);
    uint32_t iv1 = (t >> 16) & 0x7FFF;
    t = rng3::xdNext(t);
    uint32_t iv2 = (t >> 16) & 0x7FFF;
    uint32_t iv32 = (iv2 << 15) | iv1;
    for (int k = 0; k < 6; ++k) f.ivs[k] = static_cast<uint8_t>((iv32 >> (5 * k)) & 31);
    t = rng3::xdNext(t);  // ability
    uint32_t hi = out16(rng3::xdNext(t));
    uint32_t lo = out16(rng3::xdNext(rng3::xdNext(t)));
    f.pid = (hi << 16) | lo;

    const data::Gc3* eevee = findGc(133, data::kGcXdGift);
    CHECK(eevee != nullptr);
    pokemon::Pokemon p = gcPokemon(133, eevee->level, eevee->location, true, f, tid, sid, eevee->moves);
    legality::Report r = legality::checkGen3(p);
    if (!r.legal()) printInvalid("XD Eevee", r);
    CHECK(r.legal());
    CHECK(rng3::matchXdStarter(f.pid, f.ivs, tid, sid));
    // Other trainer IDs break the frame.
    pokemon::Pokemon bad = gcPokemon(133, eevee->level, eevee->location, true, f, static_cast<uint16_t>(tid + 1), sid, eevee->moves);
    CHECK(hasFinding(legality::checkGen3(bad), Severity::Invalid, "starter frame"));

    // Colosseum Umbreon: male and not shiny pairs, from the same call structure.
    uint32_t o2 = 0x2468ACE1;
    uint32_t u = rng3::xdNext(o2);
    uint16_t tid2 = static_cast<uint16_t>(out16(u));
    u = rng3::xdNext(u);
    uint16_t sid2 = static_cast<uint16_t>(out16(u));
    u = rng3::xdNext(rng3::xdNext(u));
    CxdFrame g = {};
    uint32_t v = rng3::xdNext(u);
    uint32_t jv1 = (v >> 16) & 0x7FFF;
    v = rng3::xdNext(v);
    uint32_t jv2 = (v >> 16) & 0x7FFF;
    uint32_t jv32 = (jv2 << 15) | jv1;
    for (int k = 0; k < 6; ++k) g.ivs[k] = static_cast<uint8_t>((jv32 >> (5 * k)) & 31);
    v = rng3::xdNext(v);  // ability
    for (;;) {
        v = rng3::xdNext(v);
        uint32_t a = out16(v);
        v = rng3::xdNext(v);
        uint32_t pid = (a << 16) | out16(v);
        if ((pid & 0xFF) >= 31 && !pokemon::isShiny(pid, tid2, sid2)) {
            g.pid = pid;
            break;
        }
    }
    const data::Gc3* umbreon = findGc(197, data::kGcColoStarter);
    CHECK(umbreon != nullptr);
    pokemon::Pokemon u1 = gcPokemon(197, umbreon->level, umbreon->location, false, g, tid2, sid2, umbreon->moves);
    legality::Report ru = legality::checkGen3(u1);
    if (!ru.legal()) printInvalid("Umbreon", ru);
    CHECK(ru.legal());
    CHECK(hasFinding(ru, Severity::Valid, "Colosseum starter"));
    pokemon::Pokemon u2 = gcPokemon(197, umbreon->level, umbreon->location, false, g, static_cast<uint16_t>(tid2 ^ 0x100), sid2, umbreon->moves);
    CHECK(!legality::checkGen3(u2).legal());
}

TEST(cxd_gifts_and_trades_check_the_trainer) {
    const data::Gc3* plusle = findGc(311, data::kGcColoGift);
    CHECK(plusle != nullptr);
    uint16_t tid = plusle->tid, sid = 999;
    CxdFrame f;
    uint32_t o = 3;
    do f = cxdFrame(o++ * 69069u); while (pokemon::isShiny(f.pid, tid, sid));
    pokemon::Pokemon p = gcPokemon(311, plusle->level, plusle->location, false, f, tid, sid, plusle->moves);
    p.trainer.name = "DUKING";
    uint8_t raw[pk3::kStoredSize];
    pokemon::create4::encode(p, raw);
    p = pk3::toPokemon(raw, pk3::kStoredSize);
    legality::Report r = legality::checkGen3(p);
    if (!r.legal()) printInvalid("Plusle", r);
    CHECK(r.legal());
    CHECK(hasFinding(r, Severity::Valid, "Colosseum gift"));
    p.trainer.name = "BRENDAN";
    CHECK(!legality::checkGen3(p).legal());
}

// ------------------------------------------------------------- Pal Park, LEGALIZE

TEST(events3_events_stay_legal_through_pal_park) {
    for (const char* title : {"WISHMKR", "CHANNEL", "MYSTRY"}) {
        int index = eventIndex(title[0] == 'M' ? 151 : 385, title);
        CHECK(index >= 0);
        uint8_t raw[pk3::kStoredSize];
        pokemon::Pokemon g = pokemon::gift3::receive(data::event3(index), frlgTrainer(), 17, pokemon::gift3::Criteria(), raw);
        uint8_t d4[pokemon::pk4::kStoredSize];
        pokemon::transfer34::convert(raw, d4, {26, 9, 30});
        pokemon::Pokemon q = pokemon::pk4::toPokemon(d4, pokemon::pk4::kStoredSize);
        legality::Report r = legality::checkGen4(q);
        if (!r.legal()) printInvalid(title, r);
        CHECK(r.legal());
        CHECK(hasFinding(r, Severity::Valid, "Gen III origin"));
        CHECK(!hasFinding(r, Severity::Warning, "No Gen III encounter"));
        (void)g;
    }
}

TEST(cxd_pokemon_stay_recognised_through_pal_park) {
    const data::Gc3* entei = findGc(244, data::kGcColoShadow);
    CHECK(entei != nullptr);
    pokemon::Pokemon p = gcPokemon(244, entei->level, entei->location, false, cxdFrame(0xABCDEF), 11111, 22222, entei->moves);
    uint8_t raw[pk3::kStoredSize];
    pokemon::create4::encode(p, raw);
    uint8_t d4[pokemon::pk4::kStoredSize];
    pokemon::transfer34::convert(raw, d4, {26, 9, 30});
    pokemon::Pokemon q = pokemon::pk4::toPokemon(d4, pokemon::pk4::kStoredSize);
    CHECK_EQ(q.originGame, legality::version::kColosseumXD);
    legality::Report r = legality::checkGen4(q);
    if (!r.legal()) printInvalid("Entei in Gen IV", r);
    CHECK(r.legal());
    CHECK(hasFinding(r, Severity::Valid, "Colosseum/XD origin"));
    CHECK(legality::describeGen3Origin(q).find("Colosseum Shadow Pokemon") == 0);
}

TEST(events3_legalize_makes_event_only_species_from_events) {
    for (uint16_t species : {151, 251, 385}) {  // Mew, Celebi, Jirachi
        CHECK(legality::minCreateLevel3(species) > 0);
        pokemon::Pokemon p = pokemon::create4::newPokemon(frlgTrainer(), species, 50, species * 31u);
        legality::LegalizeResult r = legality::legalizeGen3(p, 4);
        for (const std::string& u : r.unresolved) std::printf("  unresolved: %s\n", u.c_str());
        CHECK(r.unresolved.empty());
        CHECK(r.report.legal());
        CHECK_EQ(r.pokemon.species, species);
        CHECK(r.pokemon.level >= 50);  // the event's own level when that is higher (English Celebi: 70)
        CHECK(r.pokemon.level == 50 || !r.changes.empty());
        CHECK_EQ(r.pokemon.met.metLocation, 255);
        CHECK_EQ(r.pokemon.nature, p.nature);  // the requested nature is kept: the event's RNG has every nature
        CHECK(hasFinding(r.report, Severity::Valid, "Event: "));
    }
    // A species with wild encounters never turns into an event.
    pokemon::Pokemon pikachu = pokemon::create4::newPokemon(frlgTrainer(), 25, 20, 3);
    legality::LegalizeResult r = legality::legalizeGen3(pikachu, 4);
    CHECK(r.report.legal());
    CHECK(r.pokemon.met.metLocation != 255);
}

// ---------------------------------------------------------------------- the UI

namespace {

void press3(ui::App& app, uint32_t keys) {
    ui::Input in;
    in.pressed = keys;
    in.repeated = keys;
    app.update(in);
    app.update(ui::Input());
}

}  // namespace

TEST(ui_adds_a_gen3_event_to_an_empty_slot) {
    char pattern[] = "/tmp/pktool-ev3-XXXXXX";
    std::string dir = mkdtemp(pattern);
    std::string path = io::joinPath(dir, "Pokemon FireRed.sav");
    CHECK(io::safeWriteFile(path, fake::blankGen3(saves::Game::FireRedLeafGreen)) == io::WriteResult::Ok);
    ui::Config config;
    config.scanRoot = dir;
    config.dataDir = io::joinPath(dir, "PKTOOL");
    {
        ui::App app(config);
        CHECK(app.openSave(path));
        press3(app, ui::kKeyA);      // save menu: Boxes
        press3(app, ui::kKeyR);      // box 1, slot 1
        press3(app, ui::kKeyA);      // Add menu
        press3(app, ui::kKeyDown);
        press3(app, ui::kKeyDown);   // Event
        press3(app, ui::kKeyA);      // the event list
        press3(app, ui::kKeyA);      // receive the first one (alphabetical by species name)
        press3(app, ui::kKeyA);      // close the result dialog
        press3(app, ui::kKeyStart);  // put it in the slot
        press3(app, ui::kKeySelect); // write
        press3(app, ui::kKeyA);      // confirm
    }
    std::vector<uint8_t> written;
    CHECK(io::readFile(path, written));
    saves::Gen3Save save;
    CHECK(save.load(written) == saves::LoadResult::Ok);
    uint8_t slot[pk3::kPartySize] = {};
    CHECK(save.readBoxSlot(0, 0, slot) == saves::SlotState::Valid);
    pokemon::Pokemon p = pk3::toPokemon(slot, pk3::kStoredSize);
    uint16_t first = data::event3(0).species;
    for (int i = 1; i < data::events3Count(); ++i) {
        if (data::foldName(data::speciesName(data::event3(i).species)) < data::foldName(data::speciesName(first))) {
            first = data::event3(i).species;
        }
    }
    // The list also holds the Colosseum/XD Pokemon (labelled as such).
    bool firstIsEvent = true;
    for (int i : pokemon::cxd3::menuEntries()) {
        if (data::foldName(data::speciesName(data::gc3(i).species)) < data::foldName(data::speciesName(first))) {
            first = data::gc3(i).species;
            firstIsEvent = false;
        }
    }
    CHECK_EQ(p.species, first);
    legality::Report r = legality::checkGen3(p);
    if (!r.legal()) printInvalid("UI event", r);
    CHECK(r.legal());
    if (firstIsEvent) {
        CHECK_EQ(p.met.metLocation, 255);
        CHECK(hasFinding(r, Severity::Valid, "Event: "));
    } else {
        CHECK_EQ(p.originGame, legality::version::kColosseumXD);
    }
}
