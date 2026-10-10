#include "core/legality/encounters_gb.h"

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <string>

#include "core/data/gen12.h"
#include "core/data/learn12.h"
#include "core/data/learn4.h"
#include "core/data/moves.h"
#include "core/pokemon/gb_common.h"

namespace pktool::legality::gb {
namespace {

using namespace pktool::data;

constexpr uint8_t kEggGroupUndiscovered = 15;

uint16_t preEvo(uint8_t format, uint16_t species) {
    const uint16_t pre = data::preEvolution4(species);
    const uint16_t max = format == 1 ? kMaxSpeciesGen1 : kMaxSpeciesGen2;
    return pre != 0 && pre <= max ? pre : 0;
}

bool inLine(const Line& line, uint16_t species, int* index = nullptr) {
    for (int i = 0; i < line.count; ++i) {
        if (line.species[i] == species) {
            if (index) *index = i;
            return true;
        }
    }
    return false;
}

}  // namespace

Line lineOf(uint8_t format, uint16_t species) {
    Line line;
    uint16_t chain[3];
    int n = 0;
    for (uint16_t s = species; s != 0 && n < 3; s = preEvo(format, s)) chain[n++] = s;
    line.count = n;
    for (int i = 0; i < n; ++i) line.species[i] = chain[n - 1 - i];
    return line;
}

uint8_t evolutionLevel(const Line& line, int from) {
    uint8_t level = 0;
    for (int i = from + 1; i < line.count; ++i) level = std::max(level, data::evolveLevel4(line.species[i]));
    return level;
}

uint8_t versionMask(uint8_t format, uint8_t hint, bool japanese) {
    if (format == 1) {
        uint8_t mask = hint == 38 ? kGbYellow : hint == 35 ? (kGbRed | kGbBlue | kGbBlueJp) : 15;
        if (!japanese) mask &= static_cast<uint8_t>(~kGbBlueJp);
        return mask;
    }
    return hint == 41 ? kGbCrystal : hint == 39 ? (kGbGold | kGbSilver) : 7;
}

bool canHatchGb(uint16_t species) {
    switch (species) {
        case 172: case 173: case 174: case 175: case 236: case 238: case 239: case 240:
            return true;  // babies hatch although their egg group is "undiscovered"
        default:
            break;
    }
    const PersonalGb* info = personalGb2(species, true);
    return info != nullptr && info->eggGroups[0] != kEggGroupUndiscovered && info->eggGroups[0] != 13;  // not Ditto
}

void findCandidates(uint8_t format, const Line& line, uint8_t versions, std::vector<Candidate>& out) {
    if (line.count == 0) return;
    const bool gen1 = format == 1;
    const WildGb* wild = gen1 ? generated::kWild1 : generated::kWild2;
    const int wildCount = gen1 ? generated::kWild1Count : generated::kWild2Count;
    for (int i = 0; i < wildCount; ++i) {
        const WildGb& w = wild[i];
        if (!(w.versions & versions) || !inLine(line, w.species)) continue;
        Candidate c;
        c.source = Source::Wild;
        c.species = w.species;
        c.levelMin = w.levelMin;
        c.levelMax = w.levelMax;
        c.versions = static_cast<uint8_t>(w.versions & versions);
        c.location = w.location;
        c.time = w.time;
        c.slotKind = w.kind;
        out.push_back(c);
    }
    const StaticGb* statics = gen1 ? generated::kStatic1 : generated::kStatic2;
    const int staticCount = gen1 ? generated::kStatic1Count : generated::kStatic2Count;
    for (int i = 0; i < staticCount; ++i) {
        const StaticGb& s = statics[i];
        if (!(s.versions & versions) || !inLine(line, s.species)) continue;
        Candidate c;
        c.source = (s.flags & kStaticEgg) ? Source::Egg : Source::Static;
        c.species = s.species;
        c.levelMin = c.levelMax = s.level;
        c.versions = static_cast<uint8_t>(s.versions & versions);
        c.location = s.location;
        c.flags = s.flags;
        c.gender = s.gender;
        c.dvs = s.dvs;
        std::memcpy(c.moves, s.moves, 4);
        c.row = i;
        out.push_back(c);
    }
    if (gen1) {
        for (int i = 0; i < generated::kTrades1Count; ++i) {
            const TradeGb1& t = generated::kTrades1[i];
            if (!(t.versions & versions) || !inLine(line, t.species)) continue;
            Candidate c;
            c.source = Source::Trade;
            c.species = t.species;
            c.levelMin = t.levelRby;
            c.levelMax = 100;
            c.versions = static_cast<uint8_t>(t.versions & versions);
            c.row = i;
            out.push_back(c);
        }
        for (int i = 0; i < generated::kGifts1Count; ++i) {
            const GiftGb1& g = generated::kGifts1[i];
            if (!inLine(line, g.species)) continue;
            Candidate c;
            c.source = Source::Gift;
            c.species = g.species;
            c.levelMin = c.levelMax = g.level;
            c.versions = versions;
            std::memcpy(c.moves, g.moves, 4);
            c.row = i;
            c.fromGift = true;
            out.push_back(c);
        }
        return;
    }
    for (int i = 0; i < generated::kTrades2Count; ++i) {
        const TradeGb2& t = generated::kTrades2[i];
        if (!inLine(line, t.species)) continue;
        Candidate c;
        c.source = Source::Trade;
        c.species = t.species;
        c.levelMin = t.level;
        c.levelMax = 100;
        c.versions = versions;
        c.gender = t.gender;
        c.dvs = t.dvs;
        c.row = i;
        out.push_back(c);
    }
    for (int i = 0; i < generated::kGifts2Count; ++i) {
        const GiftGb2& g = generated::kGifts2[i];
        if (!inLine(line, g.species)) continue;
        const uint8_t version = g.location != 0 ? kGbCrystal : (kGbGold | kGbSilver);
        if (!(version & versions)) continue;
        Candidate c;
        c.source = g.egg ? Source::Egg : Source::Gift;
        c.species = g.species;
        c.levelMin = c.levelMax = g.level;
        c.versions = static_cast<uint8_t>(version & versions);
        c.location = g.location;
        c.flags = g.shinyAlways ? kStaticShinyAlways : 0;
        std::memcpy(c.moves, g.moves, 4);
        c.row = i;
        c.fromGift = true;
        out.push_back(c);
    }
    const uint16_t base = line.species[0];
    if (canHatchGb(base)) {
        Candidate c;
        c.source = Source::Egg;
        c.species = base;
        c.levelMin = c.levelMax = 5;
        c.versions = versions;
        c.row = -1;
        out.push_back(c);
    }
}

uint8_t lowestEncounterLevel(uint8_t format, uint16_t species, uint8_t versions) {
    const Line line = lineOf(format, species);
    std::vector<Candidate> found;
    findCandidates(format, line, versions, found);
    uint8_t best = 0;
    for (const Candidate& c : found) {
        if (c.source == Source::Gift || c.source == Source::Trade) continue;  // not an ordinary new Pokemon
        int index = 0;
        inLine(line, c.species, &index);
        uint8_t level = std::max<uint8_t>(c.levelMin, evolutionLevel(line, index));
        if (c.source == Source::Egg) level = std::max<uint8_t>(5, evolutionLevel(line, index));
        if (level == 0) level = 1;
        if (best == 0 || level < best) best = level;
    }
    return best;
}

void defaultMoves(uint8_t format, uint16_t species, uint8_t level, bool yellowOrCrystal, uint16_t moves[4]) {
    for (int i = 0; i < 4; ++i) moves[i] = 0;
    auto learn = [&](uint16_t move) {
        if (move == 0) return;
        for (int i = 0; i < 4; ++i) {
            if (moves[i] == move) return;
        }
        for (int i = 0; i < 4; ++i) {
            if (moves[i] == 0) {
                moves[i] = move;
                return;
            }
        }
        for (int i = 0; i < 3; ++i) moves[i] = moves[i + 1];  // the oldest move is forgotten
        moves[3] = move;
    };
    if (format == 1) {
        if (const PersonalGb* info = personalGb1(species, yellowOrCrystal)) {
            for (int i = 0; i < 4; ++i) learn(info->initMoves[i]);
        }
    }
    const GbGame game = format == 1 ? (yellowOrCrystal ? GbGame::Yellow : GbGame::RedBlue)
                                    : (yellowOrCrystal ? GbGame::Crystal : GbGame::GoldSilver);
    const LevelMoveGb* begin;
    const LevelMoveGb* end;
    levelUpGb(game, species, &begin, &end);
    for (const LevelMoveGb* m = begin; m != end; ++m) {
        if (m->level <= level) learn(m->move);
    }
}

bool startOf(uint8_t format, const Line& line, uint8_t versions, uint8_t level, Start* out) {
    std::vector<Candidate> found;
    findCandidates(format, line, versions, found);
    bool have = false;
    Start best;
    for (const Candidate& c : found) {
        if (c.source == Source::Gift || c.source == Source::Trade) continue;
        int index = 0;
        for (int i = 0; i < line.count; ++i) {
            if (line.species[i] == c.species) index = i;
        }
        const uint8_t need = std::max<uint8_t>(c.source == Source::Egg ? 5 : c.levelMin, evolutionLevel(line, index));
        if (level < need) continue;
        Start start;
        start.index = index;
        start.candidate = c;
        start.level = c.source == Source::Egg ? 5 : std::min<uint8_t>(level, std::max<uint8_t>(c.levelMax, c.levelMin));
        if (start.level < c.levelMin) start.level = c.levelMin;
        // The most evolved stage met directly wins; among equals the lowest level.
        if (!have || start.index > best.index || (start.index == best.index && start.level < best.level)) {
            best = start;
            have = true;
        }
    }
    if (have && out != nullptr) *out = best;
    return have;
}

void movesFrom(uint8_t format, const Line& line, const Start& start, uint8_t level, bool yellowOrCrystal,
               uint16_t moves[4]) {
    defaultMoves(format, line.species[start.index], start.level, yellowOrCrystal, moves);
    auto learn = [&](uint16_t move) {
        if (move == 0) return;
        for (int i = 0; i < 4; ++i) {
            if (moves[i] == move) return;
        }
        for (int i = 0; i < 4; ++i) {
            if (moves[i] == 0) {
                moves[i] = move;
                return;
            }
        }
        for (int i = 0; i < 3; ++i) moves[i] = moves[i + 1];
        moves[3] = move;
    };
    const GbGame game = format == 1 ? (yellowOrCrystal ? GbGame::Yellow : GbGame::RedBlue)
                                    : (yellowOrCrystal ? GbGame::Crystal : GbGame::GoldSilver);
    for (int j = start.index; j < line.count; ++j) {
        const LevelMoveGb* begin;
        const LevelMoveGb* end;
        levelUpGb(game, line.species[j], &begin, &end);
        // Moves of a later stage are learned after it evolved.
        const uint8_t from = j == start.index ? start.level : std::max<uint8_t>(evolutionLevel(line, j - 1), start.level);
        for (const LevelMoveGb* m = begin; m != end; ++m) {
            if (m->level > from && m->level <= level) learn(m->move);
        }
    }
}

bool hasCaughtData(const pokemon::Pokemon& p) {
    return p.format == 2 && (p.met.metLevel != 0 || p.met.metLocation != 0 || p.met.encounterType != 0);
}

bool catchRateFits(const pokemon::Pokemon& p, const Line& line) {
    for (int i = 0; i < line.count; ++i) {
        const PersonalGb* rb = personalGb1(line.species[i], false);
        const PersonalGb* y = personalGb1(line.species[i], true);
        if ((rb && rb->catchRate == p.catchRate) || (y && y->catchRate == p.catchRate)) return true;
    }
    return p.catchRate == 167 || p.catchRate == 168;  // Stadium gifts
}

namespace {

constexpr uint8_t kLanguageJapanese = 1;
constexpr uint8_t kLanguageKorean = 8;
constexpr uint8_t kSlotHeadbuttKind = 6;
constexpr uint8_t kSlotHeadbuttSpecialKind = 7;
// Trainer data constants of PKHeX's EncounterGift1/EncounterGift2.
constexpr uint16_t kTidStadiumJp1 = 1999;
constexpr uint16_t kTidStadiumInt1 = 2000;
constexpr uint16_t kTidVirtualConsoleMew = 22796;  // Red/Green (Japan) release date
constexpr uint16_t kTidStadium2 = 2000;
constexpr uint16_t kTidStadiumInt2 = 2001;
constexpr uint16_t kTourDvs = 0xA1C5;  // Atk 10, Def 1, Spe 12, Spc 5

bool oneOf(const std::string& name, std::initializer_list<const char*> names) {
    for (const char* n : names) {
        if (name == n) return true;
    }
    return false;
}

// EncounterSlot2.IsTreeAvailable: trees depend on the last digit of the trainer ID.
bool headbuttTreeAvailable(uint8_t location, uint8_t kind, uint16_t tid) {
    for (int i = 0; i < generated::kHeadbuttCount; ++i) {
        if (generated::kHeadbuttLocations[i] != location) continue;
        const uint32_t permissions = generated::kHeadbuttMasks[i];
        const int pivot = tid % 10;
        return kind == kSlotHeadbuttKind ? (permissions & (1u << pivot)) != 0 : (permissions & (1u << (pivot + 12))) != 0;
    }
    return false;
}

// EncounterTrade2: trainer ID, and the nickname and OT of one language's version of the trade.
bool trade2Fits(const pokemon::Pokemon& p, const Candidate& c) {
    if (c.row < 0 || c.row >= generated::kTrades2Count) return true;
    const TradeGb2& t = generated::kTrades2[c.row];
    if (t.dvs != 0xFFFF && p.trainer.tid != t.tid) return false;
    auto named = [&](int index) { return p.nickname == t.nicknames[index] && p.trainer.name == t.otNames[index]; };
    if (p.language == kLanguageJapanese) return named(1);
    if (p.language == kLanguageKorean) return named(6);
    return named(0) || named(2) || named(3) || named(4) || named(5);  // en, fr, de, it, es
}

// EncounterGift1.IsMatchExact: language, fixed DVs, trainer ID and name.
bool gift1Fits(const pokemon::Pokemon& p, const Candidate& c, uint16_t dv) {
    if (c.row < 0 || c.row >= generated::kGifts1Count) return true;
    const GiftGb1& g = generated::kGifts1[c.row];
    const bool jp = p.language == kLanguageJapanese;
    if (g.language != 0 && jp != (g.language == 1)) return false;
    const std::string& ot = p.trainer.name;
    switch (g.trainer) {
        case 1:  // Virtual Console Mew
            return dv == 0xFFFF && p.trainer.tid == kTidVirtualConsoleMew && ot == (jp ? "ゲーフリ" : "GF");
        case 2:  // Stadium
            if (p.trainer.tid != (jp ? kTidStadiumJp1 : kTidStadiumInt1)) return false;
            return jp ? ot == "スタジアム" : oneOf(ot, {"STADIUM", "STADE", "STADIO", "ESTADIO"});
        case 3:  // Europe tour
            return dv == kTourDvs && oneOf(ot, {"YOSHIRA", "YOSHIRB", "YOSHIBA", "YOSHIBB", "LINKE", "LINKW", "LUIGE",
                                                "LUIGW", "LUIGIC", "YOSHIC", "EUROPE", "SWEDEN", "NORWAY", "FINLAND",
                                                "DENMARK", "AUSTRIA", "UK", "NAL", "MARIO"});
        case 4:  // Japan tour
            return dv == kTourDvs && oneOf(ot, {"マクハリ", "フクオカ", "トウキョー", "オーサカ", "サッポロ", "ナゴヤ", "マリオ",
                                                "クッパ", "ルイージ", "ピーチ", "ヨッシー", "ドンキー"});
        default:
            return true;
    }
}

// EncounterGift2.IsMatchExact: language, level reached, trainer ID and name.
bool gift2Fits(const pokemon::Pokemon& p, const Candidate& c) {
    if (c.row < 0 || c.row >= generated::kGifts2Count) return true;
    const GiftGb2& g = generated::kGifts2[c.row];
    if ((p.language == kLanguageJapanese) != (g.language == 1)) return false;
    if (g.currentLevel != 0 && g.currentLevel > p.level) return false;
    if (g.egg && !p.isEgg) return true;
    const std::string& ot = p.trainer.name;
    switch (g.trainer) {
        case 1: return p.trainer.tid == kTidStadium2 && ot == "スタジアム";
        case 2: return p.trainer.tid == kTidStadium2 && ot == "Stadium";
        case 3: return p.trainer.tid == kTidStadiumInt2 && oneOf(ot, {"Stadion", "Stade", "Stadio", "Estadio"});
        case 4: return oneOf(ot, {"PCNYa", "PCNYb", "PCNYc", "PCNYd"});
        default: return true;
    }
}

}  // namespace

void analyse(const pokemon::Pokemon& p, Analysis& a) {
    const uint8_t format = p.format;
    a.line = lineOf(format, p.species);
    a.tradeOt = p.trainer.name == "*";
    a.crystalData = hasCaughtData(p);
    a.tradeback = format == 1 && !catchRateFits(p, a.line) && !(inLine(a.line, 25) && p.catchRate == 0xA3) &&
                  (p.catchRate == 0 || catchRateIsItem(p.catchRate));
    a.versions = versionMask(format, p.originGame, p.language == 1);
    if (a.crystalData) a.versions &= kGbCrystal;
    a.found.clear();
    a.usable.clear();
    a.pool.clear();
    a.ordered.clear();
    a.dataMismatch = false;
    a.levelTooLow = false;
    a.lowest = 0;
    findCandidates(format, a.line, a.versions, a.found);
    const uint16_t dv = dvWord(p);
    for (const Candidate& c : a.found) {
        int index = 0;
        for (int i = 0; i < a.line.count; ++i) {
            if (a.line.species[i] == c.species) index = i;
        }
        uint8_t need = std::max<uint8_t>(c.levelMin, evolutionLevel(a.line, index));
        if (c.source == Source::Egg) need = std::max<uint8_t>(5, evolutionLevel(a.line, index));
        if (a.lowest == 0 || need < a.lowest) a.lowest = need;
        if (p.level < need) {
            a.levelTooLow = true;
            continue;
        }
        if (c.source == Source::Egg) {
            if (p.isEgg && p.species != a.line.species[0]) continue;
        } else if (p.isEgg) {
            continue;
        }
        if (format == 2 && c.source == Source::Trade) {
            // Gen II NPC trades keep their own OT name (only Gen I trades are "*").
            if (!trade2Fits(p, c)) continue;
        } else if (a.tradeOt != (c.source == Source::Trade)) {
            continue;
        }
        if (c.fromGift && !(format == 1 ? gift1Fits(p, c, dv) : gift2Fits(p, c))) continue;
        if (format == 2) {
            if (c.dvs != 0xFFFF && c.dvs != dv) continue;
            if ((c.flags & kStaticShinyAlways) && !p.shiny) continue;
            if ((c.flags & kStaticShinyNever) && p.shiny) continue;
            if (c.gender != 255 && static_cast<uint8_t>(p.gender) != c.gender) continue;
        }
        a.usable.push_back(c);
    }
    for (const Candidate& c : a.usable) {
        if (a.crystalData) {
            if (c.source == Source::Wild || c.source == Source::Static) {
                if (p.met.metLevel < c.levelMin || p.met.metLevel > c.levelMax) continue;
                if (c.location != 0 && c.location != p.met.metLocation) continue;
                if (c.source == Source::Wild && c.time != 0 && p.met.encounterType >= 1 && p.met.encounterType <= 3 &&
                    (c.time & (1 << p.met.encounterType)) == 0) {
                    continue;
                }
            } else if (c.source == Source::Egg) {
                if (p.isEgg || p.met.metLocation != kHatchLocationCrystal || p.met.metLevel != 1) continue;
            } else if (c.source == Source::Trade) {
                if (p.met.metLocation != kTradeLocationCrystal || p.met.metLevel != 0) continue;
                if (c.row >= 0 && c.row < generated::kTrades2Count && generated::kTrades2[c.row].dvs != 0xFFFF &&
                    static_cast<uint8_t>(p.trainer.gender) != generated::kTrades2[c.row].otGender) {
                    continue;
                }
            } else if (c.location != 0 && c.location != p.met.metLocation) {
                continue;
            }
        }
        a.pool.push_back(c);
    }
    a.dataMismatch = a.pool.empty() && !a.usable.empty();
    if (a.pool.empty()) a.pool = a.usable;
    for (Candidate& c : a.pool) {
        // A headbutt slot is only reachable with a trainer ID whose tree exists (a deferred error in PKHeX).
        if (c.source == Source::Wild && (c.slotKind == kSlotHeadbuttKind || c.slotKind == kSlotHeadbuttSpecialKind)) {
            c.treeBad = !headbuttTreeAvailable(c.location, c.slotKind, p.trainer.tid);
        }
    }
    static const Source kOrder[] = {Source::Wild, Source::Static, Source::Trade, Source::Egg, Source::Gift};
    for (bool treeBad : {false, true}) {  // encounters needing an unreachable headbutt tree come last
        for (Source source : kOrder) {
            for (const Candidate& c : a.pool) {
                if (c.source == source && c.treeBad == treeBad) a.ordered.push_back(&c);
            }
        }
    }
}

namespace {

constexpr uint8_t kCrystalTutors[3] = {53, 85, 58};  // Flamethrower, Thunderbolt, Ice Beam (machine bits 57-59)

// Level-up, TM/HM and tutor sources (`egg` is set for an egg move).
bool generalSource(const pokemon::Pokemon& p, const Analysis& a, uint16_t move, bool* egg) {
    const bool canUseGen1 = p.format == 1 || (!a.crystalData && !p.isEgg && p.species <= kMaxSpeciesGen1);
    const bool canUseGen2 = p.format == 2 || a.tradeback;
    bool general = false;
    for (int s = 0; s < a.line.count; ++s) {
        const uint16_t species = a.line.species[s];
        if (canUseGen1 && species <= kMaxSpeciesGen1 && move <= kMaxMoveGen1) {
            for (GbGame game : {GbGame::RedBlue, GbGame::Yellow}) {
                const uint8_t level = levelUpLevelGb(game, species, move);
                if (level != 0 && level <= p.level) general = true;
            }
            for (bool yellow : {false, true}) {
                const PersonalGb* info = personalGb1(species, yellow);
                if (info != nullptr && learnsMachineGb(*info, 1, move)) general = true;
            }
        }
        if (canUseGen2 && species <= kMaxSpeciesGen2 && (p.format == 2 || move <= kMaxMoveGen1)) {
            for (GbGame game : {GbGame::GoldSilver, GbGame::Crystal}) {
                const uint8_t level = levelUpLevelGb(game, species, move);
                if (level != 0 && level <= p.level) general = true;
                if (isEggMoveGb(game, species, move)) *egg = true;
            }
            for (bool crystal : {false, true}) {
                const PersonalGb* info = personalGb2(species, crystal);
                if (info == nullptr) continue;
                if (learnsMachineGb(*info, 2, move)) general = true;
                if (crystal) {
                    for (int k = 0; k < 3; ++k) {
                        if (kCrystalTutors[k] == move && ((info->machines[(57 + k) >> 3] >> ((57 + k) & 7)) & 1)) general = true;
                    }
                }
            }
        }
    }
    return general;
}

}  // namespace

bool moveLearnable(const pokemon::Pokemon& p, const Analysis& a, uint16_t move, const Candidate* c, bool* eggMoveOnly) {
    if (move == 0) return true;
    if (move > maxMove(p.format)) return false;
    bool egg = false;
    if (generalSource(p, a, move, &egg)) return true;
    if (egg && (p.isEgg || (c != nullptr && c->source == Source::Egg))) return true;
    if (eggMoveOnly != nullptr) *eggMoveOnly = egg;
    if (c != nullptr) {
        for (int k = 0; k < 4; ++k) {
            if (c->moves[k] == move) return true;
        }
        // A wild Gen I Pokemon starts with its species' initial moves.
        if (p.format == 1 && (c->source == Source::Wild || c->source == Source::Static)) {
            for (bool yellow : {false, true}) {
                const PersonalGb* info = personalGb1(c->species, yellow);
                for (int k = 0; info != nullptr && k < 4; ++k) {
                    if (info->initMoves[k] == move) return true;
                }
            }
        }
    }
    return false;
}

uint16_t dvWord(const pokemon::Pokemon& p) {
    return pokemon::gb::packDvs(p.ivs);
}

}  // namespace pktool::legality::gb
