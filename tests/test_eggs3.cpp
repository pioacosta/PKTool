// Gen III event eggs (Pokemon Box, PCJP 5th Anniversary, Wish and PokePark
// eggs) and the Pokemon Center New York / Japan machine gifts: the generated
// rows, receiving them, recognising them (unhatched and hatched) and rejecting
// tampered ones.

#include <cstdio>
#include <cstring>
#include <string>

#include "core/data/events3.h"
#include "core/data/names.h"
#include "core/legality/events3_check.h"
#include "core/legality/legality.h"
#include "core/legality/pcjp_table.h"
#include "core/legality/pid_iv.h"
#include "core/legality/rng3.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/gift3.h"
#include "core/pokemon/pk3.h"
#include "core/pokemon/pk4.h"
#include "core/pokemon/transfer34.h"
#include "tests/test.h"

using namespace pktool;
namespace pk3 = pokemon::pk3;
using legality::Severity;

namespace {

pokemon::create4::Trainer trainer(uint8_t version = 4) {
    pokemon::create4::Trainer t;
    t.name = "PATE";
    t.tid = 12345;
    t.sid = 54321;
    t.gender = pokemon::Gender::Female;
    t.version = version;
    t.language = 2;
    t.today = {26, 9, 29};
    return t;
}

pokemon::Pokemon receive(int index, uint32_t seed, pokemon::gift3::Criteria want = pokemon::gift3::Criteria(),
                         bool* satisfied = nullptr, uint8_t version = 4) {
    uint8_t raw[pk3::kStoredSize];
    return pokemon::gift3::receive(data::event3(index), trainer(version), seed, want, raw, satisfied);
}

bool has(const legality::Report& r, Severity s, const char* text) {
    for (const auto& f : r.findings) {
        if (f.severity == s && f.text.find(text) != std::string::npos) return true;
    }
    return false;
}

void show(const char* what, const legality::Report& r) {
    std::printf("  %s:", what);
    for (const auto& f : r.findings) {
        if (f.severity == Severity::Invalid) std::printf(" [%s]", f.text.c_str());
    }
    std::printf("\n");
}

bool isEgg(const data::Event3& e) { return (e.flags & data::kEvEgg) != 0; }
bool isPcny(const data::Event3& e) { return (e.flags & data::kEvTid3000) != 0; }
bool isPcjp(const data::Event3& e) { return !isEgg(e) && !isPcny(e); }

int firstRow(bool (*kind)(const data::Event3&), uint16_t species = 0, const char* titlePart = nullptr) {
    for (int i = data::generated::kEvents3Count; i < data::events3Count(); ++i) {
        const data::Event3& e = data::event3(i);
        if (!kind(e) || (species != 0 && e.species != species)) continue;
        if (titlePart != nullptr && std::strstr(e.title, titlePart) == nullptr) continue;
        return i;
    }
    return -1;
}

// An egg hatched in a Gen III game: the hatch location replaces 255 and the
// level-0 met data stay. Hatching in Ruby/Sapphire/Emerald clears the fateful flag.
pokemon::Pokemon hatch(pokemon::Pokemon p, bool clearFateful) {
    p.isEgg = false;
    p.met.metLevel = 0;
    p.met.metLocation = (p.originGame == 4 || p.originGame == 5) ? 146 : 32;
    if (clearFateful) p.met.fatefulEncounter = false;
    p.nickname = data::speciesName(p.species);
    p.isNicknamed = false;
    p.friendship = 70;
    return p;
}

}  // namespace

TEST(eggs3_tables_have_the_pkhex_rows) {
    int eggs = 0, pcny = 0, pcjp = 0;
    for (int i = data::generated::kEvents3Count; i < data::events3Count(); ++i) {
        const data::Event3& e = data::event3(i);
        if (isEgg(e)) {
            ++eggs;
            CHECK_EQ(e.level, 5);
        } else if (isPcny(e)) {
            ++pcny;
            CHECK(std::strstr(e.ot, "PCNY") != nullptr);
            CHECK_EQ(e.sid, 0);
            CHECK_EQ(e.method, data::kEvBacdUax);
            CHECK(e.moves[0] != 0);
        } else {
            ++pcjp;
            CHECK_EQ(e.level, 10);
            CHECK_EQ(e.language, 1);
            CHECK(e.tid >= 51126 && e.tid <= 60505);
        }
    }
    CHECK_EQ(eggs, 54);
    CHECK_EQ(pcny, 50);
    CHECK_EQ(pcjp, 58);
    CHECK_EQ(data::events3Count(), data::generated::kEvents3Count + data::generated::kEvents3ExtraCount);
    // Spot checks against PKHeX's tables.
    CHECK(firstRow(isEgg, 172, "PCJP 5th") >= 0);          // Pichu with Teeter Dance / Wish
    CHECK(firstRow(isEgg, 333, "Pokemon Box") >= 0);       // Swablu with False Swipe
    CHECK(firstRow(isPcny, 330) >= 0);                     // Flygon of the Dragon event
    CHECK(firstRow(isPcjp, 258) >= 0);                     // Mudkip of the first Japanese distribution
}

TEST(eggs3_pcjp_table_roll_matches_the_machine) {
    int index[4] = {0, 0, 0, 0};
    int wish = 0, shinyPichu = 0, pichu = 0;
    for (uint32_t seed = 0; seed < 0x10000; ++seed) {
        legality::rng3::PcjpResult r = legality::rng3::pcjpResult(static_cast<uint16_t>(seed));
        CHECK(r.index < 4);
        if (r.index < 4) ++index[r.index];
        wish += r.wish;
        if (r.index == 0) {
            ++pichu;
            shinyPichu += r.shiny;
        } else {
            CHECK(!r.shiny);
        }
    }
    for (int i = 0; i < 4; ++i) CHECK(index[i] > 14000 && index[i] < 18800);  // about a quarter each
    CHECK(wish > 28000 && wish < 37000);                                       // about half
    CHECK(shinyPichu * 100 / pichu > 15 && shinyPichu * 100 / pichu < 25);     // 25 of 125
    CHECK_EQ(legality::rng3::pcjpIndex(172), 0u);
    CHECK_EQ(legality::rng3::pcjpIndex(371), 1u);
    CHECK_EQ(legality::rng3::pcjpIndex(359), 2u);
    CHECK_EQ(legality::rng3::pcjpIndex(280), 3u);
}

TEST(eggs3_every_extra_row_is_received_legal) {
    int checked = 0;
    for (int i = data::generated::kEvents3Count; i < data::events3Count(); ++i) {
        const data::Event3& e = data::event3(i);
        for (uint32_t seed : {3u, 0xBEEFu}) {
            pokemon::Pokemon p = receive(i, seed * 17 + static_cast<uint32_t>(i));
            legality::Report r = legality::checkGen3(p);
            if (!r.legal() || !has(r, Severity::Valid, "Event: ")) {
                std::printf("  row %d (%s)\n", i, e.title);
                show("received", r);
            }
            CHECK(r.legal());
            CHECK(has(r, Severity::Valid, "Event: "));
            CHECK(!has(r, Severity::Warning, "unchecked"));
            CHECK_EQ(p.isEgg, isEgg(e));
            CHECK_EQ(p.species, e.species);
            CHECK_EQ(p.met.metLocation, 255);
            CHECK_EQ(p.met.metLevel, e.metLevel);
            if (isEgg(e)) {
                CHECK_EQ(p.level, 5);
                CHECK_EQ(p.language, 1);
            }
            if (isPcny(e)) {
                CHECK(p.trainer.tid >= 1 && p.trainer.tid <= 2999);
                CHECK_EQ(p.trainer.sid, 0);
                CHECK(!p.shiny);
                CHECK_EQ(p.language, 2);
                CHECK(p.trainer.gender == pokemon::Gender::Female);  // the recipient's
            }
            if (isPcjp(e)) {
                CHECK_EQ(p.language, 1);
                CHECK_EQ(p.originGame, legality::version::kRuby);
                CHECK(p.moves[0] != 0);  // what the species knows at Lv. 10
                CHECK(!p.shiny);
            }
            ++checked;
        }
    }
    std::printf("  %d extra rows received and checked\n", checked);
}

TEST(eggs3_hatched_event_eggs_stay_legal) {
    int checked = 0;
    for (int i = data::generated::kEvents3Count; i < data::events3Count(); ++i) {
        const data::Event3& e = data::event3(i);
        if (!isEgg(e)) continue;
        for (bool clear : {false, true}) {
            if (clear && !(e.flags & data::kEvFateful)) continue;
            pokemon::Pokemon p = hatch(receive(i, 41 + static_cast<uint32_t>(i)), clear);
            legality::Report r = legality::checkGen3(p);
            if (!r.legal()) {
                std::printf("  hatched row %d (%s), flag %s\n", i, e.title, clear ? "cleared" : "kept");
                show("hatched", r);
            }
            CHECK(r.legal());
            CHECK(!has(r, Severity::Warning, "unchecked"));
            ++checked;
        }
    }
    std::printf("  %d hatched variants checked\n", checked);
}

TEST(eggs3_event_egg_moves_survive_hatching_only_for_the_event) {
    // Pichu with Surf (Pokemon Box): fixed OT, so the hatched Pokemon is recognised by it.
    int row = firstRow(isEgg, 172, "Pokemon Box");
    CHECK(row >= 0);
    pokemon::Pokemon egg = receive(row, 5);
    pokemon::Pokemon pichu = hatch(egg, true);
    CHECK(legality::checkGen3(pichu).legal());
    CHECK(has(legality::checkGen3(pichu), Severity::Valid, "Hatched"));
    // The same Pichu under another OT (an ordinary Day-Care egg) cannot know Surf.
    pokemon::Pokemon other = pichu;
    other.trainer.name = "OTHER";
    legality::Report r = legality::checkGen3(other);
    CHECK(!r.legal());
    CHECK(has(r, Severity::Invalid, "Surf"));
}

TEST(eggs3_checker_rejects_tampered_extra_rows) {
    // PCNY: IDs, OT and PID/IVs.
    int ny = firstRow(isPcny, 330);
    CHECK(ny >= 0);
    pokemon::Pokemon good = receive(ny, 7);
    CHECK(legality::checkGen3(good).legal());
    pokemon::Pokemon bad = good;
    bad.trainer.tid = 3500;  // the machine only drew 1-2999
    CHECK(!has(legality::checkGen3(bad), Severity::Valid, "Event: "));
    bad = good;
    bad.trainer.tid = 0;
    CHECK(!has(legality::checkGen3(bad), Severity::Valid, "Event: "));
    bad = good;
    bad.trainer.name = "PCNYz";
    CHECK(!has(legality::checkGen3(bad), Severity::Valid, "Event: "));
    bad = good;
    bad.trainer.sid = 4;
    CHECK(!has(legality::checkGen3(bad), Severity::Valid, "Event: "));
    bad = good;
    bad.ivs[pokemon::kHp] = static_cast<uint8_t>((bad.ivs[pokemon::kHp] + 1) & 31);
    CHECK(has(legality::checkGen3(bad), Severity::Invalid, "RNG"));
    bad = good;
    bad.met.metLevel = static_cast<uint8_t>(good.met.metLevel + 1);
    CHECK(!legality::checkGen3(bad).legal());

    // PCJP: the trainer ID of each distribution and the cities.
    int jp = firstRow(isPcjp, 258);
    CHECK(jp >= 0);
    good = receive(jp, 9);
    CHECK(legality::checkGen3(good).legal());
    bad = good;
    bad.trainer.tid = 51127;
    CHECK(!has(legality::checkGen3(bad), Severity::Valid, "Event: "));
    bad = good;
    bad.trainer.name = "\xE3\x83\x88\xE3\x83\x88";  // not a city
    CHECK(!has(legality::checkGen3(bad), Severity::Valid, "Event: "));
    bad = good;
    bad.language = 2;
    CHECK(!legality::checkGen3(bad).legal());
    bad = good;
    bad.pid ^= 0x00010000u;
    CHECK(!legality::checkGen3(bad).legal());

    // Pokemon Box egg: OT gender and PID/IVs.
    int box = firstRow(isEgg, 263, "Pokemon Box");
    CHECK(box >= 0);
    good = receive(box, 11);
    CHECK(legality::checkGen3(good).legal());
    bad = good;
    bad.trainer.gender = pokemon::Gender::Male;
    CHECK(has(legality::checkGen3(bad), Severity::Invalid, "OT gender"));
    bad = good;
    bad.ivs[pokemon::kSpe] = static_cast<uint8_t>((bad.ivs[pokemon::kSpe] + 1) & 31);
    CHECK(has(legality::checkGen3(bad), Severity::Invalid, "RNG"));

    // PCJP 5th Anniversary eggs: the table roll decides species, moveset and shininess.
    int pichuShiny = firstRow(isEgg, 172, "PCJP 5th");
    CHECK(pichuShiny >= 0);
    int shinyRows = 0;
    for (int i = data::generated::kEvents3Count; i < data::events3Count(); ++i) {
        const data::Event3& e = data::event3(i);
        if (!isEgg(e) || e.method != data::kEvBacdTs) continue;
        ++shinyRows;
        pokemon::Pokemon egg = receive(i, 13);
        CHECK(egg.shiny);
        CHECK(legality::checkGen3(egg).legal());
    }
    CHECK_EQ(shinyRows, 2);

    // A Regular B-A-C-D frame whose 16-bit seed does not roll this Pichu in the table is not from the machine.
    int ta = -1;
    for (int i = data::generated::kEvents3Count; i < data::events3Count() && ta < 0; ++i) {
        const data::Event3& e = data::event3(i);
        if (isEgg(e) && e.method == data::kEvBacdTa && e.species == 172) ta = i;
    }
    CHECK(ta >= 0);
    pokemon::Pokemon egg = receive(ta, 19);
    CHECK(legality::checkGen3(egg).legal());
    uint16_t seed16 = 0;
    while (legality::rng3::pcjpTableMatch(172, data::event3(ta).moves[2] == 273, false, seed16)) ++seed16;
    uint32_t s = legality::rng3::lcNext(legality::rng3::lcNext(seed16));  // the origin seed, two table calls later
    uint32_t a = legality::rng3::lcNext(s);
    uint32_t b = legality::rng3::lcNext(a);
    egg.pid = legality::rng3::bacdRegular(a >> 16, b >> 16);
    uint32_t iv1 = legality::rng3::lcNext(b), iv2 = legality::rng3::lcNext(iv1);
    legality::ivsFromWords(static_cast<uint16_t>((iv1 >> 16) & 0x7FFF), static_cast<uint16_t>((iv2 >> 16) & 0x7FFF), egg.ivs);
    egg.shiny = pokemon::isShiny(egg.pid, egg.trainer.tid, egg.trainer.sid);
    CHECK(has(legality::checkGen3(egg), Severity::Invalid, "RNG"));

    // Method 2 (Wish eggs): a frame of Methods 1, 2 or 4 only.
    int wish = firstRow(isEgg, 83);
    CHECK(wish >= 0);
    good = receive(wish, 17);
    CHECK(legality::checkGen3(good).legal());
    bad = good;
    bad.ivs[pokemon::kAtk] = static_cast<uint8_t>((bad.ivs[pokemon::kAtk] + 3) & 31);
    CHECK(!legality::checkGen3(bad).legal());
}

TEST(eggs3_generation_honours_the_requested_traits) {
    int ny = firstRow(isPcny, 330);
    CHECK(ny >= 0);
    pokemon::gift3::Criteria want;
    want.nature = 12;
    bool ok = false;
    pokemon::Pokemon p = receive(ny, 3, want, &ok);
    CHECK(ok);
    CHECK_EQ(p.nature, 12);
    CHECK(legality::checkGen3(p).legal());

    int shinyPichu = -1;
    for (int i = data::generated::kEvents3Count; i < data::events3Count(); ++i) {
        if (isEgg(data::event3(i)) && data::event3(i).method == data::kEvBacdTs && data::event3(i).species == 172) {
            shinyPichu = i;
            break;
        }
    }
    CHECK(shinyPichu >= 0);
    want = pokemon::gift3::Criteria();
    want.shiny = 1;
    p = receive(shinyPichu, 4, want, &ok);
    CHECK(ok);
    CHECK(p.shiny);
    CHECK(legality::checkGen3(p).legal());

    // Pokemon Box eggs follow the recipient's IDs and may be shiny or not.
    int box = firstRow(isEgg, 300, "Pokemon Box");
    CHECK(box >= 0);
    want = pokemon::gift3::Criteria();
    want.shiny = 0;
    want.gender = static_cast<int8_t>(pokemon::Gender::Female);
    p = receive(box, 8, want, &ok);
    CHECK(ok);
    CHECK(p.gender == pokemon::Gender::Female);
    CHECK(legality::checkGen3(p).legal());
}

TEST(eggs3_pal_park_recognises_pcny_and_pcjp_pokemon_and_their_moves) {
    for (int row : {firstRow(isPcny, 330), firstRow(isPcjp, 258)}) {
        CHECK(row >= 0);
        const data::Event3& e = data::event3(row);
        uint8_t raw[pk3::kStoredSize];
        pokemon::Pokemon g = pokemon::gift3::receive(e, trainer(), 21, pokemon::gift3::Criteria(), raw);
        uint8_t d4[pokemon::pk4::kStoredSize];
        pokemon::transfer34::convert(raw, d4, {26, 9, 30});
        pokemon::Pokemon q = pokemon::pk4::toPokemon(d4, pokemon::pk4::kStoredSize);
        CHECK(legality::describeGen3Origin(q).find("Event: ") == 0);
        for (uint16_t m : e.moves) {
            if (m != 0) CHECK(legality::gen3OriginHasMove(q, m));
        }
        legality::Report r = legality::checkGen4(q);
        if (!r.legal()) show("Pal Park", r);
        CHECK(r.legal());
        (void)g;
    }
}
