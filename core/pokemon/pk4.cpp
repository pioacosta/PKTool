#include "core/pokemon/pk4.h"

#include <algorithm>

#include "core/data/names.h"
#include "core/data/personal.h"
#include "core/pokemon/ribbons.h"
#include "core/pokemon/stats.h"
#include "core/text/gen4_text.h"
#include "core/util/bytes.h"
#include "core/util/lcrng.h"

namespace pktool::pokemon::pk4 {
namespace {

using util::readU16;
using util::readU32;
using util::writeU16;
using util::writeU32;

constexpr size_t kBlockSize = 32;
constexpr size_t kBlockCount = 4;
constexpr size_t kShuffledStart = 8;

// kBlockPosition[sv][i] = index of the block that ends up at position i when
// un-shuffling. Rows 24-31 repeat rows 0-7 so that (PID >> 13) & 31 can be
// used directly without a modulo.
const uint8_t kBlockPosition[32][4] = {
    {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 3, 1, 2}, {0, 2, 3, 1}, {0, 3, 2, 1},
    {1, 0, 2, 3}, {1, 0, 3, 2}, {2, 0, 1, 3}, {3, 0, 1, 2}, {2, 0, 3, 1}, {3, 0, 2, 1},
    {1, 2, 0, 3}, {1, 3, 0, 2}, {2, 1, 0, 3}, {3, 1, 0, 2}, {2, 3, 0, 1}, {3, 2, 0, 1},
    {1, 2, 3, 0}, {1, 3, 2, 0}, {2, 1, 3, 0}, {3, 1, 2, 0}, {2, 3, 1, 0}, {3, 2, 1, 0},
    {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 3, 1, 2}, {0, 2, 3, 1}, {0, 3, 2, 1},
    {1, 0, 2, 3}, {1, 0, 3, 2},
};

// Inverse permutation index of each kBlockPosition row (used when shuffling).
const uint8_t kBlockPositionInvert[32] = {
    0,  1,  2,  4,  3,  5,  6,  7,  12, 18, 13, 19, 8, 10, 14, 20,
    16, 22, 9,  11, 15, 21, 17, 23, 0,  1,  2,  4,  3, 5,  6,  7,
};

uint32_t shuffleValue(uint32_t pid) {
    return (pid >> 13) & 31;
}

void shuffle(uint8_t* blocks, uint32_t sv) {
    uint8_t copy[kBlockSize * kBlockCount];
    std::copy(blocks, blocks + sizeof(copy), copy);
    for (size_t i = 0; i < kBlockCount; ++i) {
        const uint8_t* src = copy + kBlockPosition[sv][i] * kBlockSize;
        std::copy(src, src + kBlockSize, blocks + i * kBlockSize);
    }
}

void cryptArray(uint8_t* data, size_t size, uint32_t seed) {
    for (size_t i = 0; i + 1 < size; i += 2) {
        seed = util::lcrngNext(seed);
        writeU16(data + i, readU16(data + i) ^ static_cast<uint16_t>(seed >> 16));
    }
}

// Game version IDs of HeartGold and SoulSilver.
constexpr uint8_t kVersionHeartGold = 7;
constexpr uint8_t kVersionSoulSilver = 8;

constexpr uint8_t kVersionPlatinum = 12;
constexpr uint16_t kLocationFaraway = 3002;  // "Faraway place", shown by D/P for newer locations

// Writes a location to the D/P field and the Pt/HG/SS "extended" field, as
// the games do (mirrors PKHeX's G4PKM.MetLocation/EggLocation setters).
void writeLocation(uint8_t* d, size_t dpOffset, size_t extOffset, uint16_t location, bool ptHgss) {
    uint16_t dp = location;
    uint16_t ext = ptHgss ? location : 0;
    bool newerLocation = (location > 111 && location < 2000) || (location > 2010 && location < 3000);
    if (location == 0) {
        dp = ext = 0;
    } else if (newerLocation) {
        dp = kLocationFaraway;
        ext = location;
    }
    writeU16(d + dpOffset, dp);
    writeU16(d + extOffset, ext);
}

bool isValidSize(size_t size) {
    return size == kStoredSize || size == kPartySize || size == kPk5PartySize;
}

Date readDate(const uint8_t* p) {
    return Date{p[0], p[1], p[2]};
}

void writeDate(uint8_t* p, const Date& date) {
    p[0] = date.year;
    p[1] = date.month;
    p[2] = date.day;
}

}  // namespace

void decrypt(uint8_t* data, size_t size) {
    if (!isValidSize(size)) {
        return;
    }
    uint32_t pid = readU32(data + offset::kPid);
    uint16_t checksum = readU16(data + offset::kChecksum);
    uint8_t* blocks = data + kShuffledStart;
    cryptArray(blocks, kStoredSize - kShuffledStart, checksum);
    if (size > kStoredSize) {
        cryptArray(data + kStoredSize, size - kStoredSize, pid);
    }
    shuffle(blocks, shuffleValue(pid));
}

void encrypt(uint8_t* data, size_t size) {
    if (!isValidSize(size)) {
        return;
    }
    uint32_t pid = readU32(data + offset::kPid);
    uint16_t checksum = readU16(data + offset::kChecksum);
    uint8_t* blocks = data + kShuffledStart;
    shuffle(blocks, kBlockPositionInvert[shuffleValue(pid)]);
    cryptArray(blocks, kStoredSize - kShuffledStart, checksum);
    if (size > kStoredSize) {
        cryptArray(data + kStoredSize, size - kStoredSize, pid);
    }
}

uint16_t calcChecksum(const uint8_t* decrypted) {
    uint16_t sum = 0;
    for (size_t i = kShuffledStart; i < kStoredSize; i += 2) {
        sum = static_cast<uint16_t>(sum + readU16(decrypted + i));
    }
    return sum;
}

uint16_t storedChecksum(const uint8_t* decrypted) {
    return readU16(decrypted + offset::kChecksum);
}

void refreshChecksum(uint8_t* decrypted) {
    writeU16(decrypted + offset::kChecksum, calcChecksum(decrypted));
}

bool isEmptySlot(const uint8_t* encrypted, size_t size) {
    return std::all_of(encrypted, encrypted + size, [](uint8_t b) { return b == 0; });
}

Pokemon toPokemon(const uint8_t* d, size_t size) {
    Pokemon p;
    p.format = 4;
    p.pid = readU32(d + offset::kPid);
    p.species = readU16(d + offset::kSpecies);
    p.heldItem = readU16(d + offset::kHeldItem);
    p.trainer.tid = readU16(d + offset::kTid);
    p.trainer.sid = readU16(d + offset::kSid);
    p.exp = readU32(d + offset::kExp);
    p.friendship = d[offset::kFriendship];
    p.ability = d[offset::kAbility];
    p.language = d[offset::kLanguage];
    for (int s = 0; s < kStatCount; ++s) {
        p.evs[s] = d[offset::kEvs + s];
    }
    for (int m = 0; m < 4; ++m) {
        p.moves[m] = readU16(d + offset::kMoves + m * 2);
        p.pp[m] = d[offset::kPp + m];
        p.ppUps[m] = d[offset::kPpUps + m];
    }

    uint32_t iv32 = readU32(d + offset::kIv32);
    for (int s = 0; s < kStatCount; ++s) {
        p.ivs[s] = static_cast<uint8_t>((iv32 >> (5 * s)) & 0x1F);
    }
    p.isEgg = (iv32 >> 30) & 1;
    p.isNicknamed = (iv32 >> 31) & 1;

    uint8_t flags = d[offset::kFlags40];
    p.met.fatefulEncounter = flags & 1;
    p.gender = static_cast<Gender>(std::min((flags >> 1) & 3, 2));
    p.form = flags >> 3;

    p.nickname = text::decodeGen4(d + offset::kNickname, kNicknameChars);
    p.originGame = d[offset::kOriginGame];
    p.trainer.name = text::decodeGen4(d + offset::kOtName, kOtNameChars);

    p.met.eggDate = readDate(d + offset::kEggDate);
    p.met.metDate = readDate(d + offset::kMetDate);
    // Pt/HG/SS store their own locations in the "extended" fields and keep a
    // D/P-compatible value (e.g. "Faraway place") in the original ones.
    uint16_t eggExt = readU16(d + offset::kEggLocationExt);
    uint16_t metExt = readU16(d + offset::kMetLocationExt);
    p.met.eggLocation = eggExt != 0 ? eggExt : readU16(d + offset::kEggLocationDp);
    p.met.metLocation = metExt != 0 ? metExt : readU16(d + offset::kMetLocationDp);
    p.pokerus = d[offset::kPokerus];
    // HG/SS added new balls; the display ball is the higher of both fields.
    p.met.ball = std::max(d[offset::kBallDpPt], d[offset::kBallHgss]);
    p.met.metLevel = d[offset::kMetLevelOtGender] & 0x7F;
    p.trainer.gender = (d[offset::kMetLevelOtGender] & 0x80) ? Gender::Female : Gender::Male;
    p.met.encounterType = d[offset::kEncounterType];

    p.nature = natureFromPid(p.pid);
    p.shiny = isShiny(p.pid, p.trainer.tid, p.trainer.sid);

    // The level is defined by EXP; the party level byte is only a cache
    // (and is 0 in some exported files), so it is used only as a fallback.
    if (const data::PersonalInfo* info = data::personal4(p.species)) {
        p.level = data::levelForExp(static_cast<data::GrowthRate>(info->growthRate), p.exp);
    } else if (size >= kPartySize && d[offset::kPartyLevel] != 0) {
        p.level = std::min<uint8_t>(d[offset::kPartyLevel], 100);
    } else {
        p.level = 1;
    }
    ribbons::snapshot(d, p);
    return p;
}

void applyPokemon(const Pokemon& p, uint8_t* d) {
    writeU32(d + offset::kPid, p.pid);
    writeU16(d + offset::kSpecies, p.species);
    writeU16(d + offset::kHeldItem, p.heldItem);
    writeU16(d + offset::kTid, p.trainer.tid);
    writeU16(d + offset::kSid, p.trainer.sid);
    writeU32(d + offset::kExp, p.exp);
    d[offset::kFriendship] = p.friendship;
    d[offset::kAbility] = static_cast<uint8_t>(p.ability);
    d[offset::kLanguage] = p.language;
    for (int s = 0; s < kStatCount; ++s) {
        d[offset::kEvs + s] = p.evs[s];
    }
    for (int m = 0; m < 4; ++m) {
        writeU16(d + offset::kMoves + m * 2, p.moves[m]);
        d[offset::kPp + m] = p.pp[m];
        d[offset::kPpUps + m] = p.ppUps[m];
    }

    uint32_t iv32 = 0;
    for (int s = 0; s < kStatCount; ++s) {
        iv32 |= static_cast<uint32_t>(std::min<uint8_t>(p.ivs[s], 31)) << (5 * s);
    }
    iv32 |= static_cast<uint32_t>(p.isEgg) << 30;
    iv32 |= static_cast<uint32_t>(p.isNicknamed) << 31;
    writeU32(d + offset::kIv32, iv32);

    d[offset::kFlags40] = static_cast<uint8_t>((p.met.fatefulEncounter ? 1 : 0) |
                                               (static_cast<uint8_t>(p.gender) << 1) | (p.form << 3));

    // Only rewrite names that changed, so the bytes after the terminator
    // ("trash bytes", relevant for legality) survive untouched edits.
    if (text::decodeGen4(d + offset::kNickname, kNicknameChars) != p.nickname) {
        text::encodeGen4(p.nickname, d + offset::kNickname, kNicknameChars, kNicknameMaxLength, p.language);
    }
    if (text::decodeGen4(d + offset::kOtName, kOtNameChars) != p.trainer.name) {
        text::encodeGen4(p.trainer.name, d + offset::kOtName, kOtNameChars, kOtNameMaxLength, p.language);
    }
    d[offset::kOriginGame] = p.originGame;

    writeDate(d + offset::kEggDate, p.met.eggDate);
    writeDate(d + offset::kMetDate, p.met.metDate);
    d[offset::kPokerus] = p.pokerus;
    d[offset::kMetLevelOtGender] = static_cast<uint8_t>((p.met.metLevel & 0x7F) |
                                                        (p.trainer.gender == Gender::Female ? 0x80 : 0));
    d[offset::kEncounterType] = p.met.encounterType;

    // Locations are only rewritten when they changed, so the exact original
    // encoding of both fields is kept otherwise.
    Pokemon stored = toPokemon(d, kStoredSize);
    bool ptHgss = p.originGame == kVersionPlatinum || p.originGame == kVersionHeartGold ||
                  p.originGame == kVersionSoulSilver;
    if (stored.met.metLocation != p.met.metLocation) {
        writeLocation(d, offset::kMetLocationDp, offset::kMetLocationExt, p.met.metLocation, ptHgss);
    }
    if (stored.met.eggLocation != p.met.eggLocation) {
        writeLocation(d, offset::kEggLocationDp, offset::kEggLocationExt, p.met.eggLocation, ptHgss);
    }

    // HG/SS added new balls in a second field; D/P/Pt only read the first,
    // which must hold a ball they know. Mirrors PKHeX's G4PKM.Ball setter.
    if (p.met.ball != std::max(d[offset::kBallDpPt], d[offset::kBallHgss])) {
        uint8_t ball = p.met.ball;
        d[offset::kBallDpPt] = ball <= data::kBallCherish ? ball : data::kBallPoke;
        bool fromHgss = p.originGame == kVersionHeartGold || p.originGame == kVersionSoulSilver;
        d[offset::kBallHgss] = fromHgss ? (ball <= data::kBallSport ? ball : data::kBallPoke) : 0;
    }

    ribbons::restore(p, d);
    refreshChecksum(d);
}

void refreshPartyStats(uint8_t* d) {
    uint16_t species = readU16(d + offset::kSpecies);
    const data::PersonalInfo* info = data::personal4(species);
    if (info == nullptr) {
        return;
    }
    Pokemon p = toPokemon(d, kStoredSize);  // level from EXP
    uint16_t stats[kStatCount];
    calcStats(species, info->baseStats, p.ivs, p.evs, p.level, p.nature, stats);

    uint16_t currentHp = readU16(d + offset::kPartyStats);
    uint16_t oldMaxHp = readU16(d + offset::kPartyStats + 2);
    if (currentHp >= oldMaxHp || currentHp > stats[kHp]) {
        currentHp = stats[kHp];
    }
    d[offset::kPartyLevel] = p.level;
    writeU16(d + offset::kPartyStats, currentHp);
    for (int s = 0; s < kStatCount; ++s) {
        writeU16(d + offset::kPartyStats + 2 + s * 2, stats[s]);
    }
    refreshChecksum(d);
}

}  // namespace pktool::pokemon::pk4
