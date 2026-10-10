#include "core/pokemon/pk5.h"

#include <algorithm>

#include "core/data/personal.h"
#include "core/pokemon/ribbons.h"
#include "core/pokemon/stats.h"
#include "core/text/gen4_text.h"
#include "core/util/bytes.h"

namespace pktool::pokemon::pk5 {
namespace {

using util::readU16;
using util::readU32;
using util::writeU16;
using util::writeU32;
namespace o4 = pk4::offset;

Date readDate(const uint8_t* p) {
    return Date{p[0], p[1], p[2]};
}

void writeDate(uint8_t* p, const Date& date) {
    p[0] = date.year;
    p[1] = date.month;
    p[2] = date.day;
}

}  // namespace

Pokemon toPokemon(const uint8_t* d, size_t size) {
    // The shared fields have the PK4 layout.
    Pokemon p;
    p.format = 5;
    p.pid = readU32(d + o4::kPid);
    p.species = readU16(d + o4::kSpecies);
    p.heldItem = readU16(d + o4::kHeldItem);
    p.trainer.tid = readU16(d + o4::kTid);
    p.trainer.sid = readU16(d + o4::kSid);
    p.exp = readU32(d + o4::kExp);
    p.friendship = d[o4::kFriendship];
    p.ability = d[o4::kAbility];
    p.language = d[o4::kLanguage];
    for (int s = 0; s < kStatCount; ++s) p.evs[s] = d[o4::kEvs + s];
    for (int m = 0; m < 4; ++m) {
        p.moves[m] = readU16(d + o4::kMoves + m * 2);
        p.pp[m] = d[o4::kPp + m];
        p.ppUps[m] = d[o4::kPpUps + m];
    }
    uint32_t iv32 = readU32(d + o4::kIv32);
    for (int s = 0; s < kStatCount; ++s) p.ivs[s] = static_cast<uint8_t>((iv32 >> (5 * s)) & 0x1F);
    p.isEgg = (iv32 >> 30) & 1;
    p.isNicknamed = (iv32 >> 31) & 1;

    uint8_t flags = d[o4::kFlags40];
    p.met.fatefulEncounter = flags & 1;
    p.gender = static_cast<Gender>(std::min((flags >> 1) & 3, 2));
    p.form = flags >> 3;
    p.nature = static_cast<uint8_t>(d[offset::kNature] % 25);
    p.hiddenAbility = d[offset::kFlags42] & 1;
    p.nSparkle = (d[offset::kFlags42] & 2) != 0;
    p.pokeStarFame = d[offset::kPokeStarFame];

    p.nickname = text::decodeGen5(d + o4::kNickname, pk4::kNicknameChars);
    p.originGame = d[o4::kOriginGame];
    p.trainer.name = text::decodeGen5(d + o4::kOtName, pk4::kOtNameChars);
    p.met.eggDate = readDate(d + o4::kEggDate);
    p.met.metDate = readDate(d + o4::kMetDate);
    p.met.eggLocation = readU16(d + offset::kEggLocation);
    p.met.metLocation = readU16(d + offset::kMetLocation);
    p.pokerus = d[o4::kPokerus];
    p.met.ball = d[offset::kBall];
    p.met.metLevel = d[o4::kMetLevelOtGender] & 0x7F;
    p.trainer.gender = (d[o4::kMetLevelOtGender] & 0x80) ? Gender::Female : Gender::Male;
    p.met.encounterType = d[o4::kEncounterType];
    p.shiny = isShiny(p.pid, p.trainer.tid, p.trainer.sid);

    if (const data::PersonalInfo* info = data::personal5(p.species)) {
        p.level = data::levelForExp(static_cast<data::GrowthRate>(info->growthRate), p.exp);
    } else if (size >= kPartySize && d[o4::kPartyLevel] != 0) {
        p.level = std::min<uint8_t>(d[o4::kPartyLevel], 100);
    } else {
        p.level = 1;
    }
    ribbons::snapshot(d, p);
    return p;
}

void applyPokemon(const Pokemon& p, uint8_t* d) {
    writeU32(d + o4::kPid, p.pid);
    writeU16(d + o4::kSpecies, p.species);
    writeU16(d + o4::kHeldItem, p.heldItem);
    writeU16(d + o4::kTid, p.trainer.tid);
    writeU16(d + o4::kSid, p.trainer.sid);
    writeU32(d + o4::kExp, p.exp);
    d[o4::kFriendship] = p.friendship;
    d[o4::kAbility] = static_cast<uint8_t>(p.ability);
    d[o4::kLanguage] = p.language;
    for (int s = 0; s < kStatCount; ++s) d[o4::kEvs + s] = p.evs[s];
    for (int m = 0; m < 4; ++m) {
        writeU16(d + o4::kMoves + m * 2, p.moves[m]);
        d[o4::kPp + m] = p.pp[m];
        d[o4::kPpUps + m] = p.ppUps[m];
    }
    uint32_t iv32 = 0;
    for (int s = 0; s < kStatCount; ++s) iv32 |= static_cast<uint32_t>(std::min<uint8_t>(p.ivs[s], 31)) << (5 * s);
    iv32 |= static_cast<uint32_t>(p.isEgg) << 30;
    iv32 |= static_cast<uint32_t>(p.isNicknamed) << 31;
    writeU32(d + o4::kIv32, iv32);

    d[o4::kFlags40] = static_cast<uint8_t>((p.met.fatefulEncounter ? 1 : 0) | (static_cast<uint8_t>(p.gender) << 1) |
                                           (p.form << 3));
    d[offset::kNature] = p.nature;
    d[offset::kFlags42] =
        static_cast<uint8_t>((d[offset::kFlags42] & ~3) | (p.hiddenAbility ? 1 : 0) | (p.nSparkle ? 2 : 0));
    d[offset::kPokeStarFame] = p.pokeStarFame;

    // Only rewrite names that changed (keeps the trash bytes).
    if (text::decodeGen5(d + o4::kNickname, pk4::kNicknameChars) != p.nickname) {
        text::encodeGen5(p.nickname, d + o4::kNickname, pk4::kNicknameChars, pk4::kNicknameMaxLength);
    }
    if (text::decodeGen5(d + o4::kOtName, pk4::kOtNameChars) != p.trainer.name) {
        text::encodeGen5(p.trainer.name, d + o4::kOtName, pk4::kOtNameChars, pk4::kOtNameMaxLength);
    }
    d[o4::kOriginGame] = p.originGame;
    writeDate(d + o4::kEggDate, p.met.eggDate);
    writeDate(d + o4::kMetDate, p.met.metDate);
    writeU16(d + offset::kEggLocation, p.met.eggLocation);
    writeU16(d + offset::kMetLocation, p.met.metLocation);
    d[o4::kPokerus] = p.pokerus;
    d[offset::kBall] = p.met.ball;
    d[o4::kMetLevelOtGender] = static_cast<uint8_t>((p.met.metLevel & 0x7F) |
                                                    (p.trainer.gender == Gender::Female ? 0x80 : 0));
    d[o4::kEncounterType] = p.met.encounterType;
    ribbons::restore(p, d);
    pk4::refreshChecksum(d);
}

void refreshPartyStats(uint8_t* d) {
    uint16_t species = readU16(d + o4::kSpecies);
    const data::PersonalInfo* info = data::personal5(species);
    if (info == nullptr) return;
    Pokemon p = toPokemon(d, kStoredSize);
    uint16_t stats[kStatCount];
    calcStats(species, info->baseStats, p.ivs, p.evs, p.level, p.nature, stats);
    uint16_t currentHp = readU16(d + o4::kPartyStats);
    uint16_t oldMaxHp = readU16(d + o4::kPartyStats + 2);
    if (currentHp >= oldMaxHp || currentHp > stats[kHp]) currentHp = stats[kHp];
    d[o4::kPartyLevel] = p.level;
    writeU16(d + o4::kPartyStats, currentHp);
    for (int s = 0; s < kStatCount; ++s) writeU16(d + o4::kPartyStats + 2 + s * 2, stats[s]);
    pk4::refreshChecksum(d);
}

}  // namespace pktool::pokemon::pk5
