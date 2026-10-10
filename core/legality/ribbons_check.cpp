#include "core/legality/ribbons_check.h"

#include <algorithm>
#include <map>

#include "core/legality/events3_check.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/gift4.h"
#include "core/pokemon/gift5.h"
#include "core/pokemon/pk4.h"
#include "core/pokemon/ribbons.h"

namespace pktool::legality {
namespace {

using pokemon::Pokemon;
namespace rb = pokemon::ribbons;

// Gen IV / V indices (ribbons.cpp table order).
enum : int {
    kChampionSinnoh = 0, kAbilityFirst = 1, kAbilityLast = 6, kDailyFirst = 7, kDailyLast = 13,
    kGorgeous = 14, kRoyal = 15, kGorgeousRoyal = 16, kFootprint = 17, kRecord = 18, kEvent = 19,
    kLegend = 20, kChampionWorld = 21, kBirthday = 22, kSpecial = 23, kSouvenir = 24, kWishing = 25,
    kClassic = 26, kPremier = 27,
    kG3Contest = 28,  // 5 categories x 4 ranks
    kChampionG3 = 48, kWinning = 49, kVictory = 50, kArtist = 51, kEffort = 52, kChampionBattle = 53,
    kChampionRegional = 54, kChampionNational = 55, kCountry = 56, kNational = 57, kEarth = 58, kWorld = 59,
    kG4Contest = 60,
};

// Gen III table indices.
enum : int {
    k3Champion = 5, k3Winning = 6, k3Victory = 7, k3Artist = 8, k3Effort = 9, k3Battle = 10, k3Regional = 11,
    k3NationalChamp = 12, k3Country = 13, k3National = 14, k3Earth = 15, k3World = 16,
};

constexpr uint16_t kDitto = 132;
constexpr uint16_t kUnown = 201;
constexpr uint16_t kPichu = 172;

// Species the Battle Frontier / Battle Tower refuse (PKHeX BattleFrontierBanlist).
bool bannedFromFrontier(uint16_t s) {
    static const uint16_t kBan[] = {150, 151, 249, 250, 251, 382, 383, 384, 385, 386, 483, 484, 487, 489,
                                    490, 491, 492, 493, 494, 643, 644, 646, 647, 648, 649};
    return std::find(std::begin(kBan), std::end(kBan), s) != std::end(kBan);
}

constexpr const char* kEggWhy = "An egg has no ribbons";

struct Ctx {
    const Pokemon& p;
    bool visited3;
    bool visited4;
    bool spikyPichu;
    std::vector<RibbonIssue>& out;

    int get(int index) const { return rb::getWords(p.format, p.ribbonWords, index); }
    void add(int index, bool invalid, const char* why) const { out.push_back({index, invalid, false, why}); }
    void need(int index, const char* why) const { out.push_back({index, true, true, why}); }
};

// ----- Gen III entities ---------------------------------------------------------

void analyze3(const Ctx& c) {
    const Pokemon& p = c.p;
    const bool gc = p.originGame == version::kColosseumXD;
    const data::Event3* event = gc ? nullptr : findGen3Event(p);
    const bool eventLike = p.met.fatefulEncounter || gc || event != nullptr;
    for (int i = 0; i < rb::count(3); ++i) {
        const int v = c.get(i);
        if (v == 0) continue;
        if (p.isEgg) {
            c.add(i, true, kEggWhy);
            continue;
        }
        if (rb::isRank(3, i)) {
            if (v > 4) c.add(i, true, "Contest rank above Master");
            continue;
        }
        switch (i) {
            case k3Champion:
            case k3Artist:
            case k3Effort:
            case k3Earth:  // PKHeX: any Gen III origin may have it
                break;
            case k3Winning:
                if (bannedFromFrontier(p.species)) {
                    c.add(i, true, "This species cannot enter the Battle Tower");
                } else if (p.met.metLevel > 50) {
                    c.add(i, true, "Winning needs a Pokemon of level 50 or less");
                }
                break;
            case k3Victory:
                if (bannedFromFrontier(p.species)) c.add(i, true, "This species cannot enter the Battle Tower");
                break;
            case k3National:
                if (event != nullptr && (event->flags & data::kEvNationalRibbon)) break;
                if (!gc) {
                    if (eventLike) {
                        c.add(i, false, "Event ribbon, not verified");
                    } else {
                        c.add(i, true, "Needs a purified Colosseum/XD Pokemon or an event");
                    }
                }
                break;
            default:  // Battle/Regional/National champion, Country, World
                if (eventLike) {
                    c.add(i, false, "Event ribbon, not verified");
                } else {
                    c.add(i, true, "Event ribbon on a Pokemon that is not from an event");
                }
        }
    }
}

// ----- Gen IV / Gen V entities --------------------------------------------------

// Expected ribbon words of the Wonder Card that produced `p`, if known.
bool cardWords(const Pokemon& p, uint32_t out[3]) {
    if (!p.met.fatefulEncounter) return false;
    if (p.format == 4) {
        const data::Event4* card = pokemon::gift4::matchingEvent(p);
        if (card == nullptr) return false;
        Pokemon t = pokemon::pk4::toPokemon(card->templatePk4, pokemon::pk4::kStoredSize);
        std::copy(t.ribbonWords, t.ribbonWords + 3, out);
        return true;
    }
    if (p.format == 5 && isGen5Origin(p.originGame)) {
        const data::Event5* card = pokemon::gift5::matchingEvent(p);
        if (card == nullptr) return false;
        pokemon::create4::Trainer tr;
        tr.name = p.trainer.name;
        tr.tid = p.trainer.tid;
        tr.sid = p.trainer.sid;
        tr.gender = p.trainer.gender;
        tr.language = p.language;
        tr.version = p.originGame;
        uint8_t raw[pokemon::pk5::kStoredSize];
        Pokemon t = pokemon::gift5::receive(*card, tr, 1, raw);
        std::copy(t.ribbonWords, t.ribbonWords + 3, out);
        return true;
    }
    return false;
}

// A contest rank needs the lower ranks (Master needs Hyper/Ultra, Super/Great, the first).
void chain(const Ctx& c, int base, const char* why) {
    for (int cat = 0; cat < 5; ++cat) {
        const int b = base + cat * 4;
        bool top = c.get(b + 3) != 0;
        if (c.get(b + 2)) {
            top = true;
        } else if (top) {
            c.need(b + 2, why);
        }
        if (c.get(b + 1)) {
            top = true;
        } else if (top) {
            c.need(b + 1, why);
        }
        if (top && !c.get(b)) c.need(b, why);
    }
}

void flagAll(const Ctx& c, int first, int last, const char* why) {
    for (int i = first; i <= last; ++i) {
        if (c.get(i)) c.add(i, true, why);
    }
}

void analyze45(const Ctx& c) {
    const Pokemon& p = c.p;
    const bool egg = p.isEgg;
    const bool eventFlag = p.met.fatefulEncounter;

    // Ribbons an event can carry: the Gen IV event set and the Gen III event
    // set (Earth and National have their own rules below).
    static const int kEventRibbons[] = {kEvent,        kChampionWorld,    kBirthday,         kSpecial, kSouvenir,
                                        kWishing,      kClassic,          kPremier,          kChampionBattle,
                                        kChampionRegional, kChampionNational, kCountry,      kWorld};
    uint32_t card[3];
    const bool haveCard = cardWords(p, card);
    for (int i : kEventRibbons) {
        const bool now = c.get(i) != 0;
        if (haveCard) {
            const bool want = rb::getWords(p.format, card, i) != 0;
            if (now && !want) c.add(i, true, "The event does not give this ribbon");
            if (!now && want) c.out.push_back({i, false, true, "The event gives this ribbon"});
        } else if (now) {
            if (eventFlag) {
                c.add(i, false, "Event ribbon, event not in the database");
            } else {
                c.add(i, true, egg ? kEggWhy : "Event ribbon on a Pokemon that is not from an event");
            }
        }
    }

    // Gen III only ribbons.
    if (c.get(kNational)) {
        if (egg) {
            c.add(kNational, true, kEggWhy);
        } else if (!c.visited3) {
            c.add(kNational, true, "Needs a Gen III Pokemon");
        } else if (p.originGame != version::kColosseumXD) {
            const data::Event3* event = findGen3Event(p);
            if (event != nullptr && (event->flags & data::kEvNationalRibbon)) {
                // the event gives it
            } else if (eventFlag || event != nullptr) {
                c.add(kNational, false, "Event ribbon, not verified");
            } else {
                c.add(kNational, true, "Needs a purified Colosseum/XD Pokemon or an event");
            }
        }
    }
    if (c.get(kEarth) && (egg || !isGen3Origin(p.originGame))) {
        c.add(kEarth, true, egg ? kEggWhy : "Needs a Gen III Pokemon");
    }
    if (c.get(kChampionG3) && (egg || !c.visited3)) c.add(kChampionG3, true, egg ? kEggWhy : "Needs a Gen III Pokemon");
    if (c.get(kArtist) && (egg || !c.visited3)) c.add(kArtist, true, egg ? kEggWhy : "Needs a Gen III Pokemon");
    if (c.get(kEffort) && (egg || !(c.visited3 || c.visited4))) {
        c.add(kEffort, true, egg ? kEggWhy : "Needs a Gen III or IV Pokemon");
    }
    for (int i : {kWinning, kVictory}) {
        if (!c.get(i)) continue;
        if (egg) {
            c.add(i, true, kEggWhy);
        } else if (!c.visited3) {
            c.add(i, true, "Needs a Gen III Pokemon");
        } else if (bannedFromFrontier(p.species)) {
            c.add(i, true, "This species cannot enter the Battle Tower");
        }
    }

    // Gen III contests.
    if (egg || !c.visited3) {
        flagAll(c, kG3Contest, kG3Contest + 19, egg ? kEggWhy : "Hoenn contests need a Gen III Pokemon");
    } else {
        chain(c, kG3Contest, "A contest rank needs the lower ranks");
    }
    // Gen IV contests.
    const bool contest4 = c.visited4 && p.species != kDitto && p.species != kUnown && !c.spikyPichu;
    if (egg || !contest4) {
        flagAll(c, kG4Contest, kG4Contest + 19, egg ? kEggWhy : "Sinnoh contests need a Gen IV Pokemon that can enter");
    } else {
        chain(c, kG4Contest, "A contest rank needs the lower ranks");
    }

    // Battle Frontier abilities.
    const bool frontier4 = c.visited4 && !bannedFromFrontier(p.species) && !c.spikyPichu;
    if (egg || !frontier4) {
        flagAll(c, kAbilityFirst, kAbilityLast, egg ? kEggWhy : "Needs a Gen IV Pokemon the Battle Frontier accepts");
    }

    // Gen IV common set.
    const bool sinnoh4 = c.visited4 && !c.spikyPichu;
    if (c.get(kFootprint) && (egg || !(c.visited3 || c.visited4))) {
        c.add(kFootprint, true, egg ? kEggWhy : "Footprint needs Gen III or IV");
    }
    if (c.get(kRecord)) c.add(kRecord, true, "The Record ribbon cannot be obtained");
    if (c.get(kLegend) && (egg || !c.visited4)) c.add(kLegend, true, egg ? kEggWhy : "Needs a Gen IV Pokemon");
    if (c.get(kChampionSinnoh) && (egg || !sinnoh4)) {
        c.add(kChampionSinnoh, true, egg ? kEggWhy : "Needs a Gen IV Pokemon");
    }
    if (egg || !c.visited4) flagAll(c, kDailyFirst, kDailyLast, egg ? kEggWhy : "Needs a Gen IV Pokemon");
    if (egg || !sinnoh4) {
        flagAll(c, kGorgeous, kGorgeousRoyal, egg ? kEggWhy : "Needs a Gen IV Pokemon");
    } else {
        // Gorgeous -> Royal -> Gorgeous Royal (Ribbon Syndicate order).
        bool pre;
        if (!c.get(kRoyal)) {
            pre = false;
        } else if (!(pre = c.get(kGorgeous) != 0)) {
            c.need(kGorgeous, "Royal needs Gorgeous");
        }
        if (!pre && c.get(kGorgeousRoyal)) {
            c.need(kGorgeous, "Gorgeous Royal needs Gorgeous and Royal");
            c.need(kRoyal, "Gorgeous Royal needs Gorgeous and Royal");
        }
    }
}

}  // namespace

std::vector<RibbonIssue> analyzeRibbons(const Pokemon& p) {
    std::vector<RibbonIssue> issues;
    if (p.ribbonFormat == 0 || p.format < 3 || p.format > 5) return issues;
    const bool g3Format = p.format == 3;
    Ctx c{p,
          g3Format || isGen3Origin(p.originGame),
          p.format == 4 || (p.format == 5 && (isGen3Origin(p.originGame) || isGen4Origin(p.originGame))),
          p.species == kPichu && p.form == 1,
          issues};
    if (g3Format) {
        analyze3(c);
    } else {
        analyze45(c);
    }
    // An index may be flagged by two rules: keep the first.
    std::vector<RibbonIssue> unique;
    for (const RibbonIssue& i : issues) {
        bool seen = false;
        for (const RibbonIssue& u : unique) seen = seen || (u.index == i.index && u.missing == i.missing);
        if (!seen) unique.push_back(i);
    }
    return unique;
}

void checkRibbons(const Pokemon& p, Report& r) {
    std::vector<RibbonIssue> issues = analyzeRibbons(p);
    if (issues.empty()) return;
    // One finding per severity and reason: "reason (A, B)".
    struct Key {
        Severity severity;
        std::string reason;
        bool operator<(const Key& o) const {
            return severity != o.severity ? severity < o.severity : reason < o.reason;
        }
    };
    std::map<Key, std::string> groups;
    for (const RibbonIssue& i : issues) {
        Key key{i.invalid ? Severity::Invalid : Severity::Warning,
                std::string(i.missing ? "Ribbon missing: " : "Ribbon: ") + i.why};
        std::string& list = groups[key];
        if (!list.empty()) list += ", ";
        list += rb::name(p.format, i.index);
    }
    for (const auto& g : groups) r.findings.push_back({g.first.severity, g.first.reason + " (" + g.second + ")"});
}

bool fixRibbons(Pokemon& p, std::vector<std::string>& changes) {
    if (p.ribbonFormat == 0) return false;
    bool changed = false;
    for (const RibbonIssue& i : analyzeRibbons(p)) {
        if (!i.invalid || i.missing) continue;
        rb::setWords(p.format, p.ribbonWords, i.index, 0);
        changes.push_back(std::string("Removed the ") + rb::name(p.format, i.index) + " ribbon: " + i.why);
        changed = true;
    }
    for (const RibbonIssue& i : analyzeRibbons(p)) {
        if (!i.missing) continue;
        rb::setWords(p.format, p.ribbonWords, i.index, 1);
        changes.push_back(std::string("Added the ") + rb::name(p.format, i.index) + " ribbon: " + i.why);
        changed = true;
    }
    return changed;
}

}  // namespace pktool::legality
