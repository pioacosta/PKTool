#include "core/pokemon/pk3.h"

#include <algorithm>
#include <cstring>

#include "core/data/gen3.h"
#include "core/data/language.h"
#include "core/data/names.h"
#include "core/data/personal.h"
#include "core/pokemon/edit4.h"
#include "core/pokemon/ribbons.h"
#include "core/pokemon/stats.h"
#include "core/text/gen4_text.h"
#include "core/util/bytes.h"

namespace pktool::pokemon::pk3 {
namespace {

using util::readU16;
using util::readU32;
using util::writeU16;
using util::writeU32;

constexpr size_t kDataStart = 0x20;
constexpr size_t kBlockSize = 12;

// Substructure order for PID % 24 (same table as Gen IV's block order).
constexpr uint8_t kBlockPosition[24][4] = {
    {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 3, 1, 2}, {0, 2, 3, 1}, {0, 3, 2, 1},
    {1, 0, 2, 3}, {1, 0, 3, 2}, {2, 0, 1, 3}, {3, 0, 1, 2}, {2, 0, 3, 1}, {3, 0, 2, 1},
    {1, 2, 0, 3}, {1, 3, 0, 2}, {2, 1, 0, 3}, {3, 1, 0, 2}, {2, 3, 0, 1}, {3, 2, 0, 1},
    {1, 2, 3, 0}, {1, 3, 2, 0}, {2, 1, 3, 0}, {3, 1, 2, 0}, {2, 3, 1, 0}, {3, 2, 1, 0},
};

void crypt(uint8_t* data) {
    uint32_t key = readU32(data + offset::kPid) ^ readU32(data + offset::kTid);
    for (size_t i = kDataStart; i < kStoredSize; i += 4) writeU32(data + i, readU32(data + i) ^ key);
}

std::string upperSpeciesName(uint16_t species, uint8_t language) {
    return data::defaultNickname(species, language, 3);
}

}  // namespace

void decrypt(uint8_t* data) {
    crypt(data);
    const uint8_t* order = kBlockPosition[readU32(data + offset::kPid) % 24];
    uint8_t shuffled[4 * kBlockSize];
    std::memcpy(shuffled, data + kDataStart, sizeof shuffled);
    for (int b = 0; b < 4; ++b) std::memcpy(data + kDataStart + b * kBlockSize, shuffled + order[b] * kBlockSize, kBlockSize);
}

void encrypt(uint8_t* data) {
    const uint8_t* order = kBlockPosition[readU32(data + offset::kPid) % 24];
    uint8_t logical[4 * kBlockSize];
    std::memcpy(logical, data + kDataStart, sizeof logical);
    for (int b = 0; b < 4; ++b) std::memcpy(data + kDataStart + order[b] * kBlockSize, logical + b * kBlockSize, kBlockSize);
    crypt(data);
}

uint16_t calcChecksum(const uint8_t* d) {
    uint16_t sum = 0;
    for (size_t i = kDataStart; i < kStoredSize; i += 2) sum = static_cast<uint16_t>(sum + readU16(d + i));
    return sum;
}

uint16_t storedChecksum(const uint8_t* d) {
    return readU16(d + offset::kChecksum);
}

void refreshChecksum(uint8_t* d) {
    d[offset::kFlags] &= static_cast<uint8_t>(~1u);  // clear "bad egg"
    writeU16(d + offset::kChecksum, calcChecksum(d));
}

bool isEmptySlot(const uint8_t* data) {
    return (data[offset::kFlags] & 2) == 0;
}

Pokemon toPokemon(const uint8_t* d, size_t size) {
    Pokemon p;
    p.format = 3;
    p.pid = readU32(d + offset::kPid);
    p.trainer.tid = readU16(d + offset::kTid);
    p.trainer.sid = readU16(d + offset::kSid);
    p.language = d[offset::kLanguage];
    p.nickname = text::decodeGen3(d + offset::kNickname, kNicknameChars, p.language);
    p.trainer.name = text::decodeGen3(d + offset::kOtName, kOtNameChars, p.language);
    p.species = data::speciesFromGen3(readU16(d + offset::kSpecies));
    p.heldItem = data::itemFromGen3(readU16(d + offset::kHeldItem));
    p.exp = readU32(d + offset::kExp);
    p.friendship = d[offset::kFriendship];
    for (int m = 0; m < 4; ++m) {
        p.moves[m] = readU16(d + offset::kMoves + m * 2);
        p.pp[m] = d[offset::kPp + m];
        p.ppUps[m] = static_cast<uint8_t>((d[offset::kPpUps] >> (2 * m)) & 3);
    }
    for (int s = 0; s < kStatCount; ++s) p.evs[s] = d[offset::kEvs + s];
    p.pokerus = d[offset::kPokerus];
    p.met.metLocation = d[offset::kMetLocation];
    uint16_t origins = readU16(d + offset::kOrigins);
    p.met.metLevel = origins & 0x7F;
    p.originGame = static_cast<uint8_t>((origins >> 7) & 0xF);
    p.met.ball = static_cast<uint8_t>((origins >> 11) & 0xF);
    p.trainer.gender = (origins >> 15) ? Gender::Female : Gender::Male;
    uint32_t iv32 = readU32(d + offset::kIv32);
    for (int s = 0; s < kStatCount; ++s) p.ivs[s] = static_cast<uint8_t>((iv32 >> (5 * s)) & 0x1F);
    p.isEgg = (iv32 >> 30) & 1;
    p.ability = data::ability3(p.species, static_cast<uint8_t>(iv32 >> 31));
    p.met.fatefulEncounter = readU32(d + offset::kRibbons) >> 31;
    ribbons::snapshot(d, p);

    p.nature = natureFromPid(p.pid);
    p.gender = edit4::genderFromPid(p.species, p.pid);
    p.shiny = isShiny(p.pid, p.trainer.tid, p.trainer.sid);
    p.isNicknamed = !p.isEgg && p.species != 0 && p.nickname != upperSpeciesName(p.species, p.language);
    if (const data::PersonalInfo* info = data::personal4(p.species)) {
        p.level = data::levelForExp(static_cast<data::GrowthRate>(info->growthRate), p.exp);
    } else if (size >= kPartySize && d[offset::kPartyLevel] != 0) {
        p.level = std::min<uint8_t>(d[offset::kPartyLevel], 100);
    }
    return p;
}

void applyPokemon(const Pokemon& p, uint8_t* d) {
    writeU32(d + offset::kPid, p.pid);
    writeU16(d + offset::kTid, p.trainer.tid);
    writeU16(d + offset::kSid, p.trainer.sid);
    d[offset::kLanguage] = p.language;
    if (text::decodeGen3(d + offset::kNickname, kNicknameChars, p.language) != p.nickname) {
        std::memset(d + offset::kNickname, text::kGen3Terminator, kNicknameChars);
        text::encodeGen3(p.nickname, d + offset::kNickname, kNicknameChars, kNicknameChars, p.language);
    }
    if (text::decodeGen3(d + offset::kOtName, kOtNameChars, p.language) != p.trainer.name) {
        std::memset(d + offset::kOtName, text::kGen3Terminator, kOtNameChars);
        text::encodeGen3(p.trainer.name, d + offset::kOtName, kOtNameChars, kOtNameChars, p.language);
    }
    if (data::speciesFromGen3(readU16(d + offset::kSpecies)) != p.species) {
        writeU16(d + offset::kSpecies, data::speciesToGen3(p.species));
    }
    if (data::itemFromGen3(readU16(d + offset::kHeldItem)) != p.heldItem) {
        writeU16(d + offset::kHeldItem, data::itemToGen3(p.heldItem));
    }
    uint8_t flags = static_cast<uint8_t>(d[offset::kFlags] & ~6u);
    if (p.species != 0) flags |= 2;
    if (p.isEgg) flags |= 4;
    d[offset::kFlags] = flags;

    writeU32(d + offset::kExp, p.exp);
    d[offset::kFriendship] = p.friendship;
    uint8_t ppUps = 0;
    for (int m = 0; m < 4; ++m) {
        writeU16(d + offset::kMoves + m * 2, p.moves[m]);
        d[offset::kPp + m] = p.pp[m];
        ppUps |= static_cast<uint8_t>(std::min<uint8_t>(p.ppUps[m], 3) << (2 * m));
    }
    d[offset::kPpUps] = ppUps;
    for (int s = 0; s < kStatCount; ++s) d[offset::kEvs + s] = p.evs[s];
    d[offset::kPokerus] = p.pokerus;
    d[offset::kMetLocation] = static_cast<uint8_t>(p.met.metLocation);
    uint16_t origins = static_cast<uint16_t>((p.met.metLevel & 0x7F) | ((p.originGame & 0xF) << 7) |
                                             ((p.met.ball & 0xF) << 11) |
                                             (p.trainer.gender == Gender::Female ? 0x8000 : 0));
    writeU16(d + offset::kOrigins, origins);

    // Ability slot: the second ability if the species has two and it was
    // picked; species with one ability keep their stored bit.
    uint32_t oldIv32 = readU32(d + offset::kIv32);
    uint32_t abilityBit = oldIv32 >> 31;
    uint16_t first = data::ability3(p.species, 0);
    uint16_t second = data::ability3(p.species, 1);
    if (first != second) abilityBit = p.ability == second ? 1 : 0;
    uint32_t iv32 = 0;
    for (int s = 0; s < kStatCount; ++s) iv32 |= static_cast<uint32_t>(std::min<uint8_t>(p.ivs[s], 31)) << (5 * s);
    iv32 |= static_cast<uint32_t>(p.isEgg) << 30;
    iv32 |= abilityBit << 31;
    writeU32(d + offset::kIv32, iv32);
    ribbons::restore(p, d);
    uint32_t ribbonBits = readU32(d + offset::kRibbons) & 0x7FFFFFFFu;
    writeU32(d + offset::kRibbons, ribbonBits | (p.met.fatefulEncounter ? 0x80000000u : 0));
    refreshChecksum(d);
}

void refreshPartyStats(uint8_t* d) {
    Pokemon p = toPokemon(d, kStoredSize);
    const data::PersonalInfo* info = data::personal4(p.species);
    if (info == nullptr) return;
    uint16_t stats[kStatCount];
    calcStats(p.species, info->baseStats, p.ivs, p.evs, p.level, p.nature, stats);
    uint16_t currentHp = readU16(d + offset::kPartyStats);
    uint16_t oldMaxHp = readU16(d + offset::kPartyStats + 2);
    if (currentHp >= oldMaxHp || currentHp > stats[kHp]) currentHp = stats[kHp];
    d[offset::kPartyLevel] = p.level;
    writeU16(d + offset::kPartyStats, currentHp);
    for (int s = 0; s < kStatCount; ++s) writeU16(d + offset::kPartyStats + 2 + s * 2, stats[s]);
}

}  // namespace pktool::pokemon::pk3
