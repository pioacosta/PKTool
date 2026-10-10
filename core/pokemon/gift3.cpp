#include "core/pokemon/gift3.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "core/data/encounters3.h"
#include "core/data/gen3.h"
#include "core/data/learn3.h"
#include "core/data/moves.h"
#include "core/data/personal.h"
#include "core/data/names.h"
#include "core/legality/legality.h"
#include "core/legality/pcjp_table.h"
#include "core/legality/rng3.h"
#include "core/pokemon/edit4.h"
#include "core/pokemon/entity.h"
#include "core/util/bytes.h"

namespace pktool::pokemon::gift3 {
namespace {

using legality::rng3::lcNext;
namespace rng3 = legality::rng3;

constexpr uint8_t kBallPoke = 4;
constexpr uint32_t kWishmakerTid = 20043;
constexpr uint32_t kReleasedMewSeed = 0xE8D1A0BFu;  // MYSTRY seed 0x6065: only this sibling was released

struct Frame {
    uint32_t pid = 0;
    uint8_t ivs[kStatCount] = {};
    uint8_t otGender = 0;
    uint8_t version = 0;
    uint16_t sid = 0;
    uint16_t heldItem3 = 0;
};

uint32_t out16(uint32_t s) { return s >> 16; }

uint32_t mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

uint8_t versionFor(const data::Event3& e, uint8_t recipientVersion, uint32_t pick) {
    int count = 0;
    uint8_t only = 0;
    for (uint8_t bit = 1; bit <= 16; bit = static_cast<uint8_t>(bit << 1)) {
        if (e.versions & bit) {
            ++count;
            only = bit;
        }
    }
    if (count == 1) return only == data::kVerSapphire ? 1 : only == data::kVerRuby ? 2 : only == data::kVerEmerald ? 3 : only == data::kVerFireRed ? 4 : 5;
    if (data::versionBit3(recipientVersion) & e.versions) return recipientVersion;
    // Ruby/Sapphire gifts: either, at random; Emerald/FireRed/LeafGreen eggs: FireRed when the recipient's game is not allowed.
    if (e.versions & (data::kVerRuby | data::kVerSapphire)) return static_cast<uint8_t>(1 + (pick & 1));
    return (e.versions & data::kVerFireRed) ? 4 : 3;
}

// B-A-C-D frame generated from `origin` (see PKHeX EncounterGift3.SetPINGA).
Frame bacdFrame(const data::Event3& e, uint32_t origin, uint32_t idXor) {
    Frame f;
    uint32_t s = origin;
    uint32_t pid;
    if (e.method == data::kEvMethod2) {  // A, B (PID low, high), a VBlank call, then the IV words
        s = lcNext(s);
        uint32_t a16 = out16(s);
        s = lcNext(s);
        pid = (out16(s) << 16) | a16;
        s = lcNext(s);
    } else if (e.shiny == data::kEvShinyNever && e.method == data::kEvBacdUax) {
        uint32_t a16;
        do {
            s = lcNext(s);
            a16 = out16(s);
        } while ((a16 & ~7u) == 0);
        s = lcNext(s);
        pid = rng3::bacdAntiXor(a16, out16(s), idXor);
    } else if (e.shiny == data::kEvShinyAlways) {
        s = lcNext(s);
        uint32_t x16 = out16(s);
        s = lcNext(s);
        s = lcNext(s);
        pid = rng3::bacdForceShiny(x16, out16(s), idXor);
    } else {
        s = lcNext(s);
        uint32_t a16 = out16(s);
        s = lcNext(s);
        pid = rng3::bacdRegular(a16, out16(s));
        if (e.shiny == data::kEvShinyNever) pid = rng3::bacdAntiShiny(pid, idXor);
    }
    f.pid = pid;
    s = lcNext(s);
    uint32_t iv1 = (s >> 16) & 0x7FFF;
    s = lcNext(s);
    uint32_t iv2 = (s >> 16) & 0x7FFF;
    uint32_t iv32 = (iv2 << 15) | iv1;
    for (int k = 0; k < kStatCount; ++k) f.ivs[k] = static_cast<uint8_t>((iv32 >> (5 * k)) & 31);
    if (e.tid == kWishmakerTid) {
        s = lcNext(s);
        f.heldItem3 = static_cast<uint16_t>(170 - ((out16(s) / 3) & 1));
    }
    return f;
}

// Origin seeds of a restricted event, in the order they are tried.
struct Domain {
    uint32_t size;
    uint32_t (*origin)(uint32_t index);
};

uint32_t originU16(uint32_t index) { return index; }
uint32_t originBcd(uint32_t index) { return index; }
uint32_t originTable(uint32_t index) { return lcNext(lcNext(index)); }
uint32_t originMystry(uint32_t index) {
    const int count = data::generated::kMystryMewSeedCount;
    uint32_t seed = data::generated::kMystryMewSeeds[index % static_cast<uint32_t>(count)];
    if (seed == 0x6065) return kReleasedMewSeed;
    uint32_t steps = 5 * (1 + (index / static_cast<uint32_t>(count)) % 4);
    for (uint32_t i = 0; i < steps; ++i) seed = lcNext(seed);
    return seed;
}

bool restricted(const data::Event3& e, Domain* d) {
    switch (e.method) {
        case data::kEvBacdR:
        case data::kEvBacdRA: *d = {0x10000, originU16}; return true;
        case data::kEvBacdRbcd: *d = {214, originBcd}; return true;
        case data::kEvBacdTa:
        case data::kEvBacdTs: *d = {0x10000, originTable}; return true;
        case data::kEvBacdM: *d = {static_cast<uint32_t>(data::generated::kMystryMewSeedCount) * 4, originMystry}; return true;
        default: return false;
    }
}

bool wanted(const Criteria& c, uint16_t species, uint32_t pid, uint16_t tid, uint16_t sid) {
    if (c.nature >= 0 && pid % 25 != static_cast<uint32_t>(c.nature)) return false;
    if (c.shiny >= 0 && isShiny(pid, tid, sid) != (c.shiny != 0)) return false;
    if (c.gender >= 0 && edit4::genderFromPid(species, pid) != static_cast<Gender>(c.gender)) return false;
    return true;
}

// The OT gender must follow the frame for the random rules; for the dual-OT
// rules it must also equal the entry's fixed value.
bool otGenderOk(const data::Event3& e, uint32_t origin) {
    int rng = rng3::bacdOtGender(e.otGender, origin);
    if (rng < 0) return true;
    if (e.otGender == data::kEvOtRandD3Male) return rng == 0;
    if (e.otGender == data::kEvOtRandD3Female) return rng == 1;
    return true;
}

// PCJP 5th Anniversary eggs: the 16-bit seed must roll this species, moveset and shininess in the table.
bool tableRollFits(const data::Event3& e, uint32_t seed16) {
    constexpr uint16_t kJirachi = 385;  // Negai Boshi Jirachi used the seed range without the table
    constexpr uint16_t kWish = 273;
    if (e.species == kJirachi) return true;
    bool wish = false;
    for (uint16_t m : e.moves) wish = wish || m == kWish;
    return rng3::pcjpTableMatch(e.species, wish, e.method == data::kEvBacdTs, static_cast<uint16_t>(seed16));
}

// Searches the event's RNG for a frame with the wanted traits (the first
// frame when there is no preference); `ok` reports whether one was found.
Frame searchFrame(const data::Event3& e, const Criteria& c, uint16_t tid, uint16_t sid, uint32_t seed, bool* ok,
                  uint32_t* originOut) {
    const uint32_t idXor = static_cast<uint32_t>(tid) ^ sid;
    Frame firstFrame;
    uint32_t firstOrigin = 0;
    bool haveFirst = false;
    Domain d{0, nullptr};
    uint32_t start = mix(seed ^ 0x6A09E667u);
    const bool isRestricted = restricted(e, &d);
    const bool isTable = e.method == data::kEvBacdTa || e.method == data::kEvBacdTs;
    const uint32_t attempts = isRestricted ? d.size : 30000;
    for (uint32_t i = 0; i < attempts; ++i) {
        const uint32_t index = isRestricted ? (start + i) % d.size : 0;
        if (isTable && !tableRollFits(e, index)) continue;
        uint32_t origin = isRestricted ? d.origin(index) : mix(start + i * 0x9E3779B9u);
        Frame f = bacdFrame(e, origin, idXor);
        if (!otGenderOk(e, origin)) continue;
        if (!haveFirst) {
            firstFrame = f;
            firstOrigin = origin;
            haveFirst = true;
        }
        if (wanted(c, e.species, f.pid, tid, sid)) {
            *ok = true;
            *originOut = origin;
            return f;
        }
        if (c.nature < 0 && c.shiny < 0 && c.gender < 0) break;
    }
    *ok = false;
    *originOut = firstOrigin;
    return firstFrame;
}

Frame searchChannel(const Criteria& c, uint32_t seed, bool* ok) {
    Frame firstFrame;
    bool haveFirst = false;
    uint32_t rng = mix(seed ^ 0xBB67AE85u);
    for (int i = 0; i < 400000; ++i) {
        rng = mix(rng + 0x9E3779B9u);
        uint32_t origin = rng3::channelSkipToPidIv(rng);
        rng3::ChannelFrame cf = rng3::channelFrame(origin);
        // matchChannel() is the expensive part (it searches 65536 RNG origins):
        // frames that are neither the first one nor a wanted one skip it, so a
        // request no Channel frame can satisfy (shiny Jirachi) ends in a
        // moment instead of 400000 full searches.
        const bool isWanted = wanted(c, 385, cf.pid, 40122, cf.sid);
        if (haveFirst && !isWanted) continue;
        if (!rng3::matchChannel(cf.pid, cf.ivs, cf.sid, cf.version, cf.otGender)) continue;  // edge cases of the shiny lock
        Frame f;
        f.pid = cf.pid;
        std::memcpy(f.ivs, cf.ivs, sizeof f.ivs);
        f.otGender = cf.otGender;
        f.version = cf.version;
        f.sid = cf.sid;
        f.heldItem3 = cf.heldItem3;
        if (!haveFirst) {
            firstFrame = f;
            haveFirst = true;
        }
        if (isWanted) {
            *ok = true;
            return f;
        }
        if (c.nature < 0 && c.shiny < 0 && c.gender < 0) break;
    }
    *ok = false;
    return firstFrame;
}

// The Japanese character set only has full-width Latin letters.
std::string fullWidth(const std::string& ascii) {
    std::string out;
    for (char ch : ascii) {
        uint32_t cp = static_cast<unsigned char>(ch);
        if (cp >= 0x21 && cp <= 0x7E) cp += 0xFEE0;
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

const char* const kEggName = "\xE3\x82\xBF\xE3\x83\x9E\xE3\x82\xB4";  // タマゴ

// One of the "a|b|c" alternatives of an OT list.
std::string pickOt(const char* list, uint32_t pick) {
    std::vector<std::string> names;
    std::string all = list;
    size_t start = 0;
    while (start <= all.size()) {
        size_t end = all.find('|', start);
        if (end == std::string::npos) end = all.size();
        names.push_back(all.substr(start, end - start));
        start = end + 1;
    }
    return names[pick % names.size()];
}

// The last four level-up moves a wild `species` of `level` has learned.
void defaultMoves(uint16_t species, uint8_t level, uint16_t out[4]) {
    struct Learned {
        uint8_t level;
        uint16_t move;
    };
    std::vector<Learned> learned;
    for (uint16_t move = 1; move <= data::kMaxMoveGen3; ++move) {
        const data::LearnHit entry = data::findLearn3(species, move);
        if (entry != nullptr && (entry->sources & data::kLearnLevelUp) && entry->minLevel <= level) {
            learned.push_back({entry->minLevel, move});
        }
    }
    std::sort(learned.begin(), learned.end(), [](const Learned& a, const Learned& b) {
        return a.level != b.level ? a.level < b.level : a.move < b.move;
    });
    for (int i = 0; i < 4; ++i) out[i] = 0;
    const size_t count = learned.size() < 4 ? learned.size() : 4;
    for (size_t i = 0; i < count; ++i) out[i] = learned[learned.size() - count + i].move;
}

}  // namespace

bool canReceive(const data::Event3& event, uint8_t gameVersion) {
    (void)event;
    return gameVersion >= 1 && gameVersion <= 5;
}

Pokemon receive(const data::Event3& e, const create4::Trainer& recipient, uint32_t seed, const Criteria& want,
                uint8_t out[pk3::kStoredSize], bool* satisfied) {
    Frame f;
    bool ok = false;
    uint32_t origin = 0;
    // Trainer IDs the event leaves open: random in 1-2999 (PCNY) or the recipient's.
    const uint16_t tid = e.tid != data::kEventIdAny ? e.tid
                         : (e.flags & data::kEvTid3000) ? static_cast<uint16_t>(1 + mix(seed ^ 0x51ED270Bu) % 2999)
                                                         : recipient.tid;
    const uint16_t sid = e.sid != data::kEventIdAny ? e.sid : recipient.sid;
    if (e.method == data::kEvChannel) {
        f = searchChannel(want, seed, &ok);
    } else {
        f = searchFrame(e, want, tid, sid, seed, &ok, &origin);
    }
    if (satisfied) *satisfied = ok;

    // Trainer data: the event's, or the recipient's where the gift leaves it open.
    create4::Trainer t = recipient;
    t.version = e.method == data::kEvChannel ? f.version : versionFor(e, recipient.version, mix(seed));
    if (e.ot[0] != '\0') {
        t.name = pickOt(e.ot, mix(seed ^ 0x2545F491u));
    } else if (e.flags & data::kEvEgg) {
        t.name = fullWidth(t.name);  // an egg is stored in Japanese, whose character set has no Latin letters
    }
    t.tid = tid;
    t.sid = e.method == data::kEvChannel ? f.sid : sid;
    if (e.flags & data::kEvEgg) {
        t.language = 1;  // an egg is stored with the Japanese language
    } else if (e.language != 0) {
        t.language = e.language;
    } else if (recipient.language == 1 || recipient.language > 7 || recipient.language == 0) {
        t.language = 2;
    }
    switch (e.otGender) {
        case data::kEvOtRecipient: t.gender = recipient.gender; break;
        case data::kEvOtRandAlgo: t.gender = f.otGender ? Gender::Female : Gender::Male; break;
        default: t.gender = rng3::bacdOtGender(e.otGender, origin) == 1 ? Gender::Female : Gender::Male; break;
    }

    Pokemon p = create4::newPokemon(t, e.species, e.level, seed);
    p.format = 3;
    p.pid = f.pid;
    std::memcpy(p.ivs, f.ivs, sizeof p.ivs);
    p.originGame = t.version;
    p.language = t.language;
    p.trainer.name = t.name;
    p.trainer.tid = t.tid;
    p.trainer.sid = t.sid;
    p.trainer.gender = t.gender;
    p.nickname = create4::defaultNickname(e.species, p.language, 3);
    bool ascii = true;
    for (char ch : p.nickname) ascii = ascii && static_cast<unsigned char>(ch) < 0x80;
    // The Japanese pack gives katakana; without it the English name is written full width.
    if (p.language == 1 && ascii) p.nickname = fullWidth(p.nickname);
    p.isNicknamed = false;
    p.met.ball = kBallPoke;
    p.met.metLocation = legality::location3::kFateful;
    p.met.metLevel = e.metLevel;
    p.met.fatefulEncounter = (e.flags & data::kEvFateful) != 0;
    p.heldItem = f.heldItem3 ? data::itemFromGen3(f.heldItem3) : 0;
    for (int m = 0; m < 4; ++m) {
        p.moves[m] = 0;
        p.pp[m] = 0;
        p.ppUps[m] = 0;
    }
    uint16_t moves[4] = {e.moves[0], e.moves[1], e.moves[2], e.moves[3]};
    if (moves[0] == 0) defaultMoves(e.species, e.level, moves);  // the gift keeps what the species knows at that level
    for (int m = 0; m < 4; ++m) {
        if (moves[m] != 0) edit4::setMove(p, m, moves[m]);
    }
    if (e.flags & data::kEvEgg) {
        const data::PersonalInfo* info = data::personal4(e.species);
        p.isEgg = true;
        p.nickname = kEggName;
        p.friendship = info ? info->hatchCycles : 20;
    }
    edit4::refreshDerived(p);
    p.ability = data::ability3(p.species, static_cast<uint8_t>(p.pid & 1));

    uint8_t buffer[entity::kStoredSize];
    create4::encode(p, buffer);
    if (e.flags & data::kEvNationalRibbon) {
        uint32_t ribbons = util::readU32(buffer + pk3::offset::kRibbons);
        util::writeU32(buffer + pk3::offset::kRibbons, ribbons | (1u << 24));
        pk3::refreshChecksum(buffer);
    }
    std::memcpy(out, buffer, pk3::kStoredSize);
    return pk3::toPokemon(out, pk3::kStoredSize);
}

}  // namespace pktool::pokemon::gift3
