#include "core/legality/events3_check.h"

#include <string>
#include <vector>

#include "core/data/encounters3.h"
#include "core/data/learn4.h"
#include "core/data/names.h"
#include "core/data/locks3.h"
#include "core/legality/pcjp_table.h"
#include "core/legality/pid_iv.h"
#include "core/legality/rng3.h"
#include "core/legality/shadow_locks.h"

namespace pktool::legality {
namespace {

using pokemon::Gender;
using pokemon::Pokemon;
namespace rng3 = legality::rng3;

constexpr uint8_t kBallPoke = 4;
constexpr int kMaxBacd = 16;

const char* const kLanguageNames[] = {"", "Japanese", "English", "French", "Italian", "German", "", "Spanish"};

void add(Report& r, Severity s, std::string text) { r.findings.push_back({s, std::move(text)}); }

// True if `species` is `base` or one of its evolutions.
bool inLine(uint16_t species, uint16_t base) {
    for (uint16_t s = species; s != 0; s = data::preEvolution4(s)) {
        if (s == base) return true;
    }
    return false;
}

uint8_t gameBit(uint8_t originGame) { return data::versionBit3(originGame); }

// ------------------------------------------------------------------ events

bool isEggRow(const data::Event3& e) { return (e.flags & data::kEvEgg) != 0; }

// An egg, or a Pokemon hatched from one (Gen III has no egg location).
bool eggLike(const Pokemon& p) { return p.isEgg || (p.met.metLevel == 0 && p.met.metLocation != location3::kFateful); }

// True if `name` is one of the "a|b|c" alternatives of `list`.
bool otMatches(const char* list, const std::string& name) {
    const std::string all = list;
    size_t start = 0;
    while (start <= all.size()) {
        size_t end = all.find('|', start);
        if (end == std::string::npos) end = all.size();
        if (all.compare(start, end - start, name) == 0) return true;
        start = end + 1;
    }
    return false;
}

bool idsMatch(const data::Event3& e, const Pokemon& p) {
    if (e.flags & data::kEvTid3000) {
        if (p.trainer.tid == 0 || p.trainer.tid >= 3000) return false;
    } else if (e.tid != data::kEventIdAny && e.tid != p.trainer.tid) {
        return false;
    }
    return e.sid == data::kEventIdAny || e.sid == p.trainer.sid;
}

bool identifies(const data::Event3& e, const Pokemon& p) {
    if (!inLine(p.species, e.species)) return false;
    // Eggs only identify eggs and the Pokemon hatched from them; a hatched one
    // needs something that singles the event out (its OT, IDs or fateful flag).
    if (isEggRow(e) != eggLike(p)) return false;
    if (isEggRow(e) && !p.isEgg && e.ot[0] == '\0' && e.tid == data::kEventIdAny && !(e.flags & data::kEvFateful)) return false;
    if (!idsMatch(e, p)) return false;
    if (p.format == 3 && e.ot[0] != '\0' && !otMatches(e.ot, p.trainer.name)) return false;
    return true;
}

constexpr uint16_t kJirachi = 385;
constexpr uint16_t kMoveWish = 273;

bool hasMove(const data::Event3& e, uint16_t move) {
    for (uint16_t m : e.moves) {
        if (m == move) return true;
    }
    return false;
}

bool bacdAccepts(const data::Event3& e, const rng3::BacdMatch& m, const Pokemon& p) {
    bool method = false;
    switch (e.method) {
        case data::kEvBacdU:
            method = m.type == rng3::BacdType::Regular;
            break;
        case data::kEvBacdTs:
            method = m.type == rng3::BacdType::ForceShiny && rng3::lcPrev(rng3::lcPrev(m.origin)) <= 0xFFFF &&
                     rng3::pcjpTableMatch(e.species, hasMove(e, kMoveWish), true,
                                          static_cast<uint16_t>(rng3::lcPrev(rng3::lcPrev(m.origin))));
            break;
        case data::kEvBacdR:
            method = m.type == rng3::BacdType::Regular && m.origin <= 0xFFFF;
            break;
        case data::kEvBacdRA:
            method = (m.type == rng3::BacdType::Regular || m.type == rng3::BacdType::Anti) && m.origin <= 0xFFFF;
            break;
        case data::kEvBacdRbcd:
            method = m.type == rng3::BacdType::ForceShiny && m.origin <= 213;
            break;
        case data::kEvBacdTa:
            method = (m.type == rng3::BacdType::Regular || m.type == rng3::BacdType::Anti) &&
                     rng3::lcPrev(rng3::lcPrev(m.origin)) <= 0xFFFF;
            // The PCJP 5th Anniversary eggs came from the table roll; Negai Boshi Jirachi did not.
            if (method && e.species != kJirachi) {
                method = rng3::pcjpTableMatch(e.species, hasMove(e, kMoveWish), false,
                                              static_cast<uint16_t>(rng3::lcPrev(rng3::lcPrev(m.origin))));
            }
            break;
        case data::kEvBacdUax:
            method = m.type == rng3::BacdType::AntiXor;
            break;
        case data::kEvBacdM:
            method = m.type == rng3::BacdType::Regular && rng3::mystryMewOrigin(m.origin);
            break;
        default:
            return false;
    }
    if (!method) return false;
    int expected = (isEggRow(e) && !p.isEgg) ? -1 : rng3::bacdOtGender(e.otGender, m.origin);  // a hatched egg is not judged by its OT gender
    if (expected >= 0) {
        int actual = p.trainer.gender == Gender::Female ? 1 : 0;
        if (expected != actual) return false;
        if (e.otGender == data::kEvOtRandD3Male && actual != 0) return false;
        if (e.otGender == data::kEvOtRandD3Female && actual != 1) return false;
    }
    return true;
}

bool pidFits(const data::Event3& e, const Pokemon& p) {
    if (e.method == data::kEvMethod2) {  // Method 2 by design; Methods 1 and 4 come from the same script
        PidIvMethod m = findPidIvMethod3(p.pid, p.ivs, p.species);
        return m == PidIvMethod::Method1 || m == PidIvMethod::Method2 || m == PidIvMethod::Method4;
    }
    if (e.method == data::kEvChannel) {
        return rng3::matchChannel(p.pid, p.ivs, p.trainer.sid, p.originGame, p.trainer.gender == Gender::Female ? 1 : 0);
    }
    rng3::BacdMatch matches[kMaxBacd];
    uint32_t idXor = static_cast<uint32_t>(p.trainer.tid ^ p.trainer.sid);
    int count = rng3::findBacd(p.pid, p.ivs, idXor, matches, kMaxBacd);
    for (int i = 0; i < count; ++i) {
        if (bacdAccepts(e, matches[i], p)) return true;
    }
    return false;
}

std::vector<std::string> eventProblems(const data::Event3& e, const Pokemon& p) {
    std::vector<std::string> problems;
    const bool eggRow = isEggRow(e);
    const bool hatched = eggRow && !p.isEgg;  // hatched in some game: the hatch location replaces 255
    if (!(gameBit(p.originGame) & e.versions)) problems.push_back("Origin game does not fit this event");
    if (eggRow) {
        // An unhatched egg is always stored with the Japanese language.
        if (p.isEgg && p.language != 1) problems.push_back("Eggs are stored with the Japanese language");
    } else if (e.language != 0) {
        if (p.language != e.language) {
            problems.push_back(std::string("Language should be ") +
                               (e.language < 8 && kLanguageNames[e.language][0] ? kLanguageNames[e.language] : "that of the event"));
        }
    } else if (p.language == 1) {
        problems.push_back("This event was never given in Japanese");
    }
    if (hatched) {
        if (p.met.metLevel != 0) problems.push_back("A hatched egg has met level 0");
        if (p.met.metLocation == location3::kFateful) problems.push_back("A hatched egg has the hatch location");
    } else {
        if (p.met.metLocation != location3::kFateful) problems.push_back("Event Pokemon have met location 255");
        if (p.met.metLevel != e.metLevel) problems.push_back("Met level should be " + std::to_string(e.metLevel));
    }
    if (p.level < e.level) problems.push_back("Level cannot be below " + std::to_string(e.level));
    if (p.met.ball != kBallPoke) problems.push_back("Event Pokemon are in a Poke Ball");
    // Hatching in Ruby, Sapphire or Emerald clears the fateful flag of an egg.
    const bool fateful = (e.flags & data::kEvFateful) != 0;
    if (p.met.fatefulEncounter != fateful && !(hatched && fateful)) {
        problems.push_back(fateful ? "Fateful encounter flag should be set" : "Fateful encounter flag should be clear");
    }
    if (e.shiny == data::kEvShinyNever && p.shiny) problems.push_back("This event is never shiny");
    if (e.shiny == data::kEvShinyAlways && !p.shiny) problems.push_back("This event is always shiny");
    if (!hatched) {
        if (e.otGender == data::kEvOtOnly0 && p.trainer.gender != Gender::Male) problems.push_back("OT gender should be male");
        if (e.otGender == data::kEvOtOnly1 && p.trainer.gender != Gender::Female) problems.push_back("OT gender should be female");
    }
    if (!pidFits(e, p)) problems.push_back("PID/IVs do not come from the event's RNG");
    return problems;
}

// ------------------------------------------------------- Colosseum / XD

bool rebattleLocation(uint8_t location) {
    // XD: Mirror B. re-battles at Realgam Tower, the Poke Spots and Pyrite Town.
    return location == 59 || location == 90 || location == 91 || location == 92 || location == 113;
}

bool gcIdentifies(const data::Gc3& e, const Pokemon& p) {
    if (!inLine(p.species, e.species)) return false;
    if (e.tid != 0 && p.trainer.tid != e.tid) return false;
    if (p.format == 3) {
        if (p.met.metLevel != e.level) return false;
        bool shadowXd = e.kind == data::kGcXdShadow;
        if (p.met.metLocation != e.location && !(shadowXd && rebattleLocation(static_cast<uint8_t>(p.met.metLocation)))) return false;
    }
    return true;
}

bool gcAllowsAnti(uint8_t kind) {
    return kind == data::kGcColoGift || kind == data::kGcColoBonus || kind == data::kGcXdGift || kind == data::kGcXdShadow;
}

constexpr uint16_t kEevee = 133;

bool isShadow(uint8_t kind);

// Shadow Pokemon: some party the NPC trainer generates first must be able to
// lead to the shadow's frame (PKHeX LockFinder). Empty when it can.
std::string shadowLockProblem(const data::Gc3& e, const Pokemon& p) {
    const uint8_t game = e.kind == data::kGcColoShadow ? data::kShadowColo
                         : e.kind == data::kGcXdShadow ? data::kShadowXd
                                                       : data::kShadowEReader;
    const bool xd = game == data::kShadowXd;
    if (xd && p.shiny) return "";  // reported as "never shiny"
    const uint32_t tsv = xd ? static_cast<uint32_t>(p.trainer.tid ^ p.trainer.sid) >> 3 : shadow::kNoTsv;

    // Origin seeds the locks are tried from.
    uint32_t origins[16];
    int count = 0;
    if (game == data::kShadowEReader) {
        uint32_t starts[8];
        int n = rng3::pidOrigins(p.pid, starts, 8);
        for (int i = 0; i < n; ++i) {
            uint32_t s = starts[i];
            for (int k = 0; k < 4; ++k) s = rng3::xdNext(s);
            origins[count++] = s;
        }
    } else {
        rng3::CxdMatch matches[8];
        int n = rng3::findCxdAll(p.pid, p.ivs, p.trainer.tid, p.trainer.sid, matches, 8);
        for (int i = 0; i < n; ++i) {
            if (matches[i].type == rng3::CxdType::Anti && !gcAllowsAnti(e.kind)) continue;
            origins[count++] = matches[i].origin;
        }
    }
    bool known = false;
    for (int i = 0; i < data::shadowEnc3Count(); ++i) {
        const data::ShadowEnc3& row = data::shadowEnc3(i);
        if (row.species != e.species || row.level != e.level || row.game != game) continue;
        known = true;
        if (shadow::setOf(row).teamCount == 0) return "";
        for (int o = 0; o < count; ++o) {
            if (shadow::locksSatisfied(shadow::setOf(row), origins[o], tsv)) return "";
        }
    }
    if (!known) return "";
    return "PID/IVs cannot satisfy the nature/gender locks of the trainer's team before this Shadow Pokemon";
}

std::string pokeSpotProblem(const data::Gc3& e, const Pokemon& p) {
    bool known = false;
    for (int i = 0; i < data::pokeSpotArea3Count(); ++i) {
        const data::PokeSpotArea3& area = data::pokeSpotArea3(i);
        if (area.location != e.location) continue;
        for (uint8_t slot = 0; slot < 3; ++slot) {
            if (area.species[slot] != e.species || area.level[slot] != e.level) continue;
            known = true;
            if (rng3::matchPokeSpot(p.pid, p.ivs, slot)) return "";
        }
    }
    return known ? "PID/IVs do not come from a Poke Spot encounter of this slot" : "";
}

// Empty when the PID/IV frame fits, else the reason.
std::string gcPidProblem(const data::Gc3& e, const Pokemon& p) {
    switch (e.kind) {
        case data::kGcColoEReader:
            return shadowLockProblem(e, p);  // no IV correlation, only the locks
        case data::kGcXdPokeSpot:
            return pokeSpotProblem(e, p);
        case data::kGcColoStarter:
            if (!rng3::matchColoStarter(p.pid, p.ivs, p.trainer.tid, p.trainer.sid, e.species)) {
                return "PID/IVs do not match the Colosseum starter frame of these trainer IDs";
            }
            return "";
        default:
            break;
    }
    if (e.kind == data::kGcXdGift && e.species == kEevee) {
        if (!rng3::matchXdStarter(p.pid, p.ivs, p.trainer.tid, p.trainer.sid)) {
            return "PID/IVs do not match the XD starter frame of these trainer IDs";
        }
        return "";
    }
    rng3::CxdType cxd = rng3::findCxd(p.pid, p.ivs, p.trainer.tid, p.trainer.sid);
    if (cxd == rng3::CxdType::Plain || (cxd == rng3::CxdType::Anti && gcAllowsAnti(e.kind))) {
        return isShadow(e.kind) ? shadowLockProblem(e, p) : "";
    }
    return "PID/IVs do not come from the Colosseum/XD RNG";
}

const char* gcKindName(uint8_t kind) {
    switch (kind) {
        case data::kGcColoStarter: return "Colosseum starter";
        case data::kGcColoGift: return "Colosseum gift";
        case data::kGcColoBonus: return "Colosseum bonus-disc gift";
        case data::kGcColoShadow: return "Colosseum Shadow Pokemon";
        case data::kGcColoEReader: return "Colosseum e-Reader Shadow Pokemon";
        case data::kGcXdGift: return "XD gift";
        case data::kGcXdTrade: return "XD trade";
        case data::kGcXdShadow: return "XD Shadow Pokemon";
        default: return "XD Poke Spot";
    }
}

bool isShadow(uint8_t kind) {
    return kind == data::kGcColoShadow || kind == data::kGcXdShadow || kind == data::kGcColoEReader;
}

}  // namespace

bool checkEvent3(const Pokemon& p, Report& r, const uint16_t** moves) {
    const data::Event3* best = nullptr;
    std::vector<std::string> bestProblems;
    int bestScore = 1 << 20;
    for (int i = 0; i < data::events3Count(); ++i) {
        const data::Event3& e = data::event3(i);
        if (!identifies(e, p)) continue;
        std::vector<std::string> problems = eventProblems(e, p);
        // Rows that differ only by their moves (PCNY): prefer the one that gave this Pokemon's moves.
        int uncovered = 0;
        for (uint16_t move : p.moves) {
            if (move != 0 && !hasMove(e, move)) ++uncovered;
        }
        const int score = static_cast<int>(problems.size()) * 8 + uncovered;
        if (best == nullptr || score < bestScore) {
            best = &e;
            bestProblems = std::move(problems);
            bestScore = score;
        }
        if (bestScore == 0) break;
    }
    if (best == nullptr) return false;
    if (moves) *moves = best->moves;
    if (bestProblems.empty()) {
        add(r, Severity::Valid, std::string("Event: ") + best->title);
    } else {
        for (const std::string& problem : bestProblems) add(r, Severity::Invalid, std::string(best->title) + ": " + problem);
    }
    return true;
}

bool gcFemaleOtGift(const Pokemon& p) {
    for (int i = 0; i < data::gc3Count(); ++i) {
        const data::Gc3& e = data::gc3(i);
        if ((e.flags & data::kGcFemaleOt) && gcIdentifies(e, p)) return true;
    }
    return false;
}

bool checkGc3(const Pokemon& p, Report& r, const uint16_t** moves) {
    const data::Gc3* best = nullptr;
    int bestScore = 1 << 20;
    for (int i = 0; i < data::gc3Count(); ++i) {
        const data::Gc3& e = data::gc3(i);
        if (!gcIdentifies(e, p)) continue;
        int score = 0;
        if (((e.flags & data::kGcFateful) != 0) != p.met.fatefulEncounter) score += 2;
        if ((e.flags & data::kGcPokeBall) && p.met.ball != kBallPoke) score += 1;
        if (e.trainer != 0 && p.trainer.name != data::gcTrainerName(e.trainer, p.language)) score += 4;
        if (score < bestScore) {
            best = &e;
            bestScore = score;
        }
    }
    if (best == nullptr) return false;
    if (moves) *moves = best->moves;
    const data::Gc3& e = *best;
    const std::string what = gcKindName(e.kind);
    bool invalid = false;
    auto fail = [&](const std::string& text) {
        add(r, Severity::Invalid, what + ": " + text);
        invalid = true;
    };

    if (e.trainer != 0) {
        const char* expected = data::gcTrainerName(e.trainer, p.language);
        if (expected[0] != '\0' && p.trainer.name != expected) fail(std::string("OT should be ") + expected);
    }
    const bool femaleOt = (e.flags & data::kGcFemaleOt) != 0;
    if (femaleOt && p.trainer.gender != Gender::Female) fail("OT gender must be female");
    if (!femaleOt && p.trainer.gender == Gender::Female) fail("OT gender must be male");
    if ((e.flags & data::kGcJapanOnly) && p.language != 1) fail("only given in Japanese");
    if (e.kind == data::kGcColoBonus && p.trainer.sid != 0) fail("secret ID should be 0");
    if ((e.flags & data::kGcNeverShiny) && p.shiny) fail("never shiny");
    std::string pid = gcPidProblem(e, p);
    if (!pid.empty()) fail(pid);
    if (((e.flags & data::kGcFateful) != 0) != p.met.fatefulEncounter) {
        add(r, Severity::Warning, what + ": the fateful encounter flag is unusual");
    }
    if ((e.flags & data::kGcPokeBall) && p.met.ball != kBallPoke) {
        add(r, Severity::Warning, what + ": normally in a Poke Ball");
    }
    if (!invalid) add(r, Severity::Valid, std::string("Met: ") + what + " (" + data::speciesName(e.species) + ")");
    return true;
}

namespace {

// Pal Park keeps the trainer IDs, language, ball and fateful flag only, so an
// event is recognised by them. Eggs carry the owner's IDs and cannot be told
// apart afterwards.
bool palParkEvent(const data::Event3& e, const Pokemon& p) {
    if (!inLine(p.species, e.species)) return false;
    if (isEggRow(e) || (e.tid == data::kEventIdAny && !(e.flags & data::kEvTid3000))) return false;
    if (!idsMatch(e, p)) return false;
    if (e.language != 0 && e.language != p.language) return false;
    if (!(gameBit(p.originGame) & e.versions)) return false;
    return p.met.fatefulEncounter == ((e.flags & data::kEvFateful) != 0);
}

bool palParkGc(const data::Gc3& e, const Pokemon& p) {
    if (!inLine(p.species, e.species) || p.level < e.level) return false;
    return e.tid == 0 || p.trainer.tid == e.tid;
}

}  // namespace

std::string describeGen3Origin(const Pokemon& p) {
    if (p.originGame == 15) {  // Colosseum / XD
        for (int i = 0; i < data::gc3Count(); ++i) {
            const data::Gc3& e = data::gc3(i);
            if (!palParkGc(e, p)) continue;
            return std::string(gcKindName(e.kind)) + " (" + data::speciesName(e.species) + ")";
        }
        return "";
    }
    for (int i = 0; i < data::events3Count(); ++i) {
        const data::Event3& e = data::event3(i);
        if (!palParkEvent(e, p)) continue;
        return std::string("Event: ") + e.title;
    }
    return "";
}

bool gen3OriginHasMove(const Pokemon& p, uint16_t move) {
    if (p.originGame == 15) {
        for (int i = 0; i < data::gc3Count(); ++i) {
            const data::Gc3& e = data::gc3(i);
            if (!palParkGc(e, p)) continue;
            for (uint16_t m : e.moves) {
                if (m == move) return true;
            }
        }
        return false;
    }
    for (int i = 0; i < data::events3Count(); ++i) {
        const data::Event3& e = data::event3(i);
        if (palParkEvent(e, p) && hasMove(e, move)) return true;
    }
    std::vector<uint16_t> eggMoves;  // a Pokemon hatched from an event egg
    eventEggMoves3(p, eggMoves);
    for (uint16_t m : eggMoves) {
        if (m == move) return true;
    }
    return false;
}

void eventEggMoves3(const Pokemon& p, std::vector<uint16_t>& out) {
    for (int i = 0; i < data::events3Count(); ++i) {
        const data::Event3& e = data::event3(i);
        if (!isEggRow(e) || !inLine(p.species, e.species) || !(gameBit(p.originGame) & e.versions)) continue;
        if (e.ot[0] != '\0' && !otMatches(e.ot, p.trainer.name)) continue;
        if (e.tid != data::kEventIdAny && e.tid != p.trainer.tid) continue;
        for (uint16_t m : e.moves) {
            if (m != 0) out.push_back(m);
        }
    }
}

const data::Event3* findGen3Event(const Pokemon& p) {
    for (int i = 0; i < data::events3Count(); ++i) {
        const data::Event3& e = data::event3(i);
        if (!inLine(p.species, e.species)) continue;
        const bool tidOk = (e.flags & data::kEvTid3000) != 0 ? p.trainer.tid >= 1 && p.trainer.tid <= 2999
                                                             : e.tid == p.trainer.tid;
        if (!tidOk || (e.sid != data::kEventIdAny && e.sid != p.trainer.sid)) continue;
        if (e.language != 0 && e.language != p.language) continue;
        if (!(gameBit(p.originGame) & e.versions)) continue;
        return &e;
    }
    return nullptr;
}

}  // namespace pktool::legality
