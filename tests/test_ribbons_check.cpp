// Tests of the ribbon legality rules (core/legality/ribbons_check) and of the
// ribbon snapshot carried by Pokemon (round trip through decode/apply).

#include <cstring>
#include <string>
#include <vector>

#include "core/legality/legality.h"
#include "core/legality/legalize.h"
#include "core/legality/ribbons_check.h"
#include "core/pokemon/entity.h"
#include "core/pokemon/pk3.h"
#include "core/pokemon/pk4.h"
#include "core/pokemon/ribbons.h"
#include "tests/test.h"

using namespace pktool;
namespace rb = pokemon::ribbons;

namespace {

// Gen IV/V indices used below (ribbons.cpp order).
constexpr int kChampionSinnoh = 0, kAbility = 1, kAlert = 7, kGorgeous = 14, kRoyal = 15, kGorgeousRoyal = 16,
              kFootprint = 17, kRecord = 18, kEvent = 19, kLegend = 20, kClassic = 26;
constexpr int kCoolNormal = 28, kCoolSuper = 29, kCoolHyper = 30, kCoolMaster = 31, kChampionG3 = 48,
              kWinning = 49, kArtist = 51, kEffort = 52, kEarth = 58, kCountry = 56;
constexpr int kSinnohCoolGreat = 61;

pokemon::Pokemon make(uint8_t format, uint8_t origin, uint16_t species = 25) {
    pokemon::Pokemon p;
    p.format = format;
    p.species = species;
    p.originGame = origin;
    p.level = 30;
    p.met.metLevel = 5;
    p.ribbonFormat = format == 3 ? 3 : 4;
    return p;
}

void give(pokemon::Pokemon& p, int index, int value = 1) { rb::setWords(p.format, p.ribbonWords, index, value); }

bool flagged(const pokemon::Pokemon& p, int index, bool invalid, bool missing = false) {
    for (const legality::RibbonIssue& i : legality::analyzeRibbons(p)) {
        if (i.index == index && i.invalid == invalid && i.missing == missing) return true;
    }
    return false;
}

bool clean(const pokemon::Pokemon& p) { return legality::analyzeRibbons(p).empty(); }

}  // namespace

TEST(ribbons_check_gen4_native) {
    pokemon::Pokemon p = make(4, legality::version::kPlatinum);
    CHECK(clean(p));
    give(p, kChampionSinnoh);
    give(p, kAbility);
    give(p, kAlert);
    give(p, kFootprint);
    give(p, kLegend);
    give(p, kEffort);
    give(p, kSinnohCoolGreat - 1);  // Sinnoh Cool
    give(p, kSinnohCoolGreat);
    give(p, kGorgeous);
    give(p, kRoyal);
    give(p, kGorgeousRoyal);
    CHECK(clean(p));

    // Gen III only ribbons cannot be earned in Gen IV.
    give(p, kChampionG3);
    give(p, kArtist);
    give(p, kWinning);
    give(p, kCoolNormal);
    CHECK(flagged(p, kChampionG3, true));
    CHECK(flagged(p, kArtist, true));
    CHECK(flagged(p, kWinning, true));
    CHECK(flagged(p, kCoolNormal, true));
    give(p, kRecord);
    CHECK(flagged(p, kRecord, true));
    legality::Report r;
    legality::checkRibbons(p, r);
    CHECK(r.overall() == legality::Severity::Invalid);
}

TEST(ribbons_check_contest_chains_and_cosmetics) {
    pokemon::Pokemon p = make(4, legality::version::kDiamond);
    give(p, kSinnohCoolGreat + 1);  // Ultra without Cool/Great
    CHECK(flagged(p, kSinnohCoolGreat, true, true));
    CHECK(flagged(p, kSinnohCoolGreat - 1, true, true));
    p = make(4, legality::version::kDiamond);
    give(p, kRoyal);
    CHECK(flagged(p, kGorgeous, true, true));
    p = make(4, legality::version::kDiamond);
    give(p, kGorgeousRoyal);
    CHECK(flagged(p, kGorgeous, true, true));
    CHECK(flagged(p, kRoyal, true, true));

    // Ditto and Unown cannot enter contests; Mew cannot enter the Frontier.
    p = make(4, legality::version::kDiamond, 132);
    give(p, kSinnohCoolGreat - 1);
    CHECK(flagged(p, kSinnohCoolGreat - 1, true));
    p = make(4, legality::version::kPlatinum, 151);
    give(p, kAbility);
    CHECK(flagged(p, kAbility, true));
    p = make(4, legality::version::kPlatinum, 25);
    give(p, kAbility);
    CHECK(clean(p));
}

TEST(ribbons_check_pal_park_and_gen5) {
    // Gen III origin in a Gen IV entity: Hoenn ribbons and Effort are fine,
    // chains still apply.
    pokemon::Pokemon p = make(4, legality::version::kEmerald);
    give(p, kChampionG3);
    give(p, kArtist);
    give(p, kEffort);
    give(p, kCoolNormal);
    give(p, kCoolSuper);
    give(p, kEarth);
    CHECK(clean(p));
    give(p, kCoolMaster);
    CHECK(flagged(p, kCoolHyper, true, true));

    // Gen V native: nothing from the older generations.
    p = make(5, legality::version::kBlack);
    give(p, kChampionSinnoh);
    give(p, kEffort);
    give(p, kFootprint);
    give(p, kAlert);
    CHECK(flagged(p, kChampionSinnoh, true));
    CHECK(flagged(p, kEffort, true));
    CHECK(flagged(p, kFootprint, true));
    CHECK(flagged(p, kAlert, true));

    // Gen IV origin moved to Gen V keeps its ribbons.
    p = make(5, legality::version::kPlatinum);
    give(p, kChampionSinnoh);
    give(p, kEffort);
    give(p, kFootprint);
    CHECK(clean(p));
}

TEST(ribbons_check_event_ribbons) {
    pokemon::Pokemon p = make(4, legality::version::kPlatinum);
    give(p, kClassic);
    give(p, kEvent);
    CHECK(flagged(p, kClassic, true));
    p.met.fatefulEncounter = true;  // an event, but the card is unknown
    CHECK(flagged(p, kClassic, false));
    CHECK(flagged(p, kEvent, false));
    legality::Report r;
    legality::checkRibbons(p, r);
    CHECK(r.overall() == legality::Severity::Warning);
    give(p, kCountry);
    CHECK(flagged(p, kCountry, false));
    // The Earth ribbon needs a Gen III origin.
    p = make(4, legality::version::kPlatinum);
    give(p, kEarth);
    CHECK(flagged(p, kEarth, true));
}

TEST(ribbons_check_eggs_have_none) {
    pokemon::Pokemon p = make(4, legality::version::kPlatinum);
    p.isEgg = true;
    CHECK(clean(p));
    give(p, kChampionSinnoh);
    give(p, kCoolNormal);
    give(p, kEffort);
    CHECK(flagged(p, kChampionSinnoh, true));
    CHECK(flagged(p, kCoolNormal, true));
    CHECK(flagged(p, kEffort, true));
    pokemon::Pokemon e3 = make(3, legality::version::kEmerald);
    e3.isEgg = true;
    give(e3, 0, 2);
    CHECK(flagged(e3, 0, true));
}

TEST(ribbons_check_gen3) {
    pokemon::Pokemon p = make(3, legality::version::kEmerald);
    give(p, 0, 4);  // Cool Master
    give(p, 5);     // Champion
    give(p, 8);     // Artist
    give(p, 9);     // Effort
    give(p, 15);    // Earth
    CHECK(clean(p));
    give(p, 6);  // Winning at met level 5
    CHECK(clean(p));
    p.met.metLevel = 60;
    CHECK(flagged(p, 6, true));
    p.met.metLevel = 50;
    CHECK(clean(p));
    p.species = 151;  // Mew
    CHECK(flagged(p, 6, true));
    p = make(3, legality::version::kEmerald);
    p.ribbonWords[0] |= 7u << 3;  // rank 7 in the Beauty slot (edited)
    CHECK(flagged(p, 1, true));
    p = make(3, legality::version::kEmerald);
    give(p, 13);  // Country
    CHECK(flagged(p, 13, true));
    p.met.fatefulEncounter = true;
    CHECK(flagged(p, 13, false));
    p = make(3, legality::version::kColosseumXD);
    give(p, 14);  // National
    CHECK(clean(p));
}

TEST(ribbons_check_reaches_the_checkers) {
    pokemon::Pokemon p = make(4, legality::version::kPlatinum);
    p.species = 25;
    legality::Report before = legality::checkGen4(p);
    give(p, kRecord);
    legality::Report after = legality::checkGen4(p);
    CHECK(after.findings.size() == before.findings.size() + 1);
    CHECK(after.overall() == legality::Severity::Invalid);
    // Without a snapshot (a Pokemon not decoded from raw data) nothing is checked.
    p.ribbonFormat = 0;
    CHECK(legality::checkGen4(p).findings.size() == before.findings.size());
}

TEST(ribbons_fix_removes_and_adds) {
    pokemon::Pokemon p = make(4, legality::version::kPlatinum);
    give(p, kChampionG3);
    give(p, kRecord);
    give(p, kRoyal);
    give(p, kClassic);  // event ribbon, no event: removed
    p.met.fatefulEncounter = true;
    give(p, kEvent);  // fateful: only a warning, kept
    std::vector<std::string> changes;
    CHECK(legality::fixRibbons(p, changes));
    CHECK(!changes.empty());
    CHECK_EQ(rb::getWords(4, p.ribbonWords, kChampionG3), 0);
    CHECK_EQ(rb::getWords(4, p.ribbonWords, kRecord), 0);
    CHECK_EQ(rb::getWords(4, p.ribbonWords, kGorgeous), 1);  // prerequisite added
    CHECK_EQ(rb::getWords(4, p.ribbonWords, kRoyal), 1);
    CHECK_EQ(rb::getWords(4, p.ribbonWords, kEvent), 1);
    std::vector<std::string> again;
    CHECK(!legality::fixRibbons(p, again));
    CHECK(clean(p) || flagged(p, kEvent, false) || flagged(p, kClassic, false));
}

TEST(ribbons_snapshot_round_trip) {
    for (uint8_t format : {3, 4, 5}) {
        std::vector<uint8_t> raw(pokemon::entity::kMaxPartySize, 0);
        pokemon::Pokemon p;
        p.format = format;
        p.species = 25;
        p.originGame = format == 3 ? 3 : format == 4 ? 12 : 23;
        p.nickname = "A";
        p.trainer.name = "B";
        p.met.ball = 4;
        if (format == 3) {
            std::memset(raw.data() + pokemon::pk3::offset::kNickname, 0xFF, pokemon::pk3::kNicknameChars);
            std::memset(raw.data() + pokemon::pk3::offset::kOtName, 0xFF, pokemon::pk3::kOtNameChars);
        }
        pokemon::entity::apply(p, raw.data());
        // Ribbons through the raw API, then a decode/apply round trip changes nothing.
        rb::set(format, raw.data(), 0, format == 3 ? 3 : 1);
        rb::set(format, raw.data(), format == 3 ? 12 : 30, 1);
        std::vector<uint8_t> before = raw;
        pokemon::Pokemon q = pokemon::entity::decode(format, raw.data(), 100);
        CHECK(q.ribbonFormat != 0);
        CHECK_EQ(rb::getWords(format, q.ribbonWords, 0), format == 3 ? 3 : 1);
        pokemon::entity::apply(q, raw.data());
        CHECK(raw == before);
        // Changing the snapshot changes the raw ribbon and keeps the rest valid.
        rb::setWords(format, q.ribbonWords, 0, 0);
        pokemon::entity::apply(q, raw.data());
        CHECK_EQ(rb::get(format, raw.data(), 0), 0);
        CHECK_EQ(rb::get(format, raw.data(), format == 3 ? 12 : 30), 1);
        // A Pokemon without a snapshot leaves the raw ribbons alone.
        q.ribbonFormat = 0;
        rb::setWords(format, q.ribbonWords, 0, 1);
        pokemon::entity::apply(q, raw.data());
        CHECK_EQ(rb::get(format, raw.data(), 0), 0);
    }
}
