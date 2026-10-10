#include "core/pokemon/pk1.h"

#include <algorithm>
#include <cstring>

#include "core/data/gen12.h"
#include "core/data/language.h"
#include "core/pokemon/gb_common.h"
#include "core/text/gen12_text.h"

namespace pktool::pokemon::pk1 {
namespace {

uint8_t languageOf(const uint8_t* e) {
    return e[kLanguageOffset] == 0 ? 2 : e[kLanguageOffset];
}

size_t stringBytes(uint8_t language) {
    return language == 1 ? kJapaneseStringBytes : kStringBytes;
}

size_t maxNameChars(uint8_t language, bool nickname) {
    if (language == 1) return kJapaneseNameChars;
    return nickname ? kNicknameChars : kOtNameChars;
}

// The stored names of an entity use only the games' own string length.
std::string readName(const uint8_t* e, size_t offset, uint8_t language) {
    return text::decodeGb(1, e + offset, stringBytes(language), language);
}

void writeName(uint8_t* e, size_t offset, const std::string& name, uint8_t language, bool nickname) {
    uint8_t buffer[kStringBytes];
    if (!text::encodeGb(1, name, buffer, stringBytes(language), maxNameChars(language, nickname), language)) return;
    std::memset(e + offset, text::kGb12Terminator, kStringBytes);
    std::memcpy(e + offset, buffer, stringBytes(language));
}

}  // namespace

Pokemon toPokemon(const uint8_t* e, size_t) {
    Pokemon p;
    p.format = 1;
    p.species = data::speciesFromGen1(e[offset::kSpecies]);
    p.language = languageOf(e);
    p.nickname = readName(e, kNicknameOffset, p.language);
    p.trainer.name = readName(e, kOtOffset, p.language);
    p.trainer.tid = gb::readBe16(e + offset::kTid);
    p.trainer.gender = Gender::Male;
    p.exp = gb::readBe24(e + offset::kExp);
    p.catchRate = e[offset::kCatchRate];
    for (int m = 0; m < 4; ++m) {
        p.moves[m] = e[offset::kMoves + m];
        p.pp[m] = e[offset::kPp + m] & 0x3F;
        p.ppUps[m] = e[offset::kPp + m] >> 6;
    }
    gb::unpackDvs(gb::readBe16(e + offset::kDvs), p.ivs);
    for (int s = 0; s < 5; ++s) p.statExp[s == 4 ? kSpA : s] = gb::readBe16(e + offset::kStatExp + s * 2);
    p.statExp[kSpD] = p.statExp[kSpA];
    p.gender = Gender::Genderless;
    p.shiny = false;  // Gen I has no shiny Pokemon (the DVs become one in Gen II)
    p.types[0] = e[offset::kType1];
    p.types[1] = e[offset::kType2];
    // The level the game shows is the stored one; legality compares it with EXP.
    p.level = e[offset::kBoxLevel] >= 1 && e[offset::kBoxLevel] <= 100 ? e[offset::kBoxLevel]
                                                                           : gb::levelFor(1, p.species, p.exp);
    p.isNicknamed = p.species != 0 && p.nickname != data::defaultNickname(p.species, p.language, 1);
    return p;
}

void applyPokemon(const Pokemon& p, uint8_t* e) {
    const uint8_t language = p.language == 0 ? 2 : p.language;
    if (data::speciesFromGen1(e[offset::kSpecies]) != p.species) {
        e[offset::kSpecies] = data::speciesToGen1(p.species);
        if (const data::PersonalGb* info = data::personalGb1(p.species, false)) {
            e[offset::kType1] = info->types[0];
            e[offset::kType2] = info->types[1];
        }
    }
    if (p.types[0] != 0 || p.types[1] != 0) {  // the model carries the stored types
        e[offset::kType1] = p.types[0];
        e[offset::kType2] = p.types[1];
    }
    e[offset::kCatchRate] = p.catchRate;
    e[kLanguageOffset] = language;
    if (readName(e, kNicknameOffset, language) != p.nickname) writeName(e, kNicknameOffset, p.nickname, language, true);
    if (readName(e, kOtOffset, language) != p.trainer.name) writeName(e, kOtOffset, p.trainer.name, language, false);
    gb::writeBe16(e + offset::kTid, p.trainer.tid);
    if (gb::readBe24(e + offset::kExp) != p.exp || e[offset::kBoxLevel] == 0) {
        gb::writeBe24(e + offset::kExp, p.exp);
        e[offset::kBoxLevel] = p.level;
    }
    for (int m = 0; m < 4; ++m) {
        e[offset::kMoves + m] = static_cast<uint8_t>(p.moves[m]);
        e[offset::kPp + m] = static_cast<uint8_t>((std::min<uint8_t>(p.pp[m], 63)) | (std::min<uint8_t>(p.ppUps[m], 3) << 6));
    }
    for (int s = 0; s < 5; ++s) gb::writeBe16(e + offset::kStatExp + s * 2, p.statExp[s == 4 ? kSpA : s]);
    gb::writeBe16(e + offset::kDvs, gb::packDvs(p.ivs));
}

void fillPartyArea(uint8_t* e) {
    const uint16_t species = data::speciesFromGen1(e[offset::kSpecies]);
    const uint32_t exp = gb::readBe24(e + offset::kExp);
    uint8_t ivs[kStatCount];
    uint16_t statExp[kStatCount];
    gb::unpackDvs(gb::readBe16(e + offset::kDvs), ivs);
    for (int s = 0; s < 5; ++s) statExp[s == 4 ? kSpA : s] = gb::readBe16(e + offset::kStatExp + s * 2);
    statExp[kSpD] = statExp[kSpA];
    const uint8_t level = e[offset::kBoxLevel] >= 1 && e[offset::kBoxLevel] <= 100 ? e[offset::kBoxLevel]
                                                                                       : gb::levelFor(1, species, exp);
    uint16_t stats[kStatCount];
    gb::calcStats(1, species, false, ivs, statExp, level, stats);
    e[offset::kLevel] = level;
    // Max HP, Atk, Def, Spe, Spc.
    gb::writeBe16(e + offset::kMaxHp, stats[kHp]);
    gb::writeBe16(e + offset::kMaxHp + 2, stats[kAtk]);
    gb::writeBe16(e + offset::kMaxHp + 4, stats[kDef]);
    gb::writeBe16(e + offset::kMaxHp + 6, stats[kSpe]);
    gb::writeBe16(e + offset::kMaxHp + 8, stats[kSpA]);
}

void refreshPartyStats(uint8_t* e) {
    const uint16_t oldMax = gb::readBe16(e + offset::kMaxHp);
    fillPartyArea(e);
    const uint16_t newMax = gb::readBe16(e + offset::kMaxHp);
    uint16_t current = gb::readBe16(e + offset::kHp);
    if (current >= oldMax || current > newMax) current = newMax;
    gb::writeBe16(e + offset::kHp, current);
}

void clear(uint8_t* e, bool japanese) {
    std::memset(e, 0, kEntitySize);
    std::memset(e + kOtOffset, text::kGb12Terminator, 2 * kStringBytes);
    e[kLanguageOffset] = japanese ? 1 : 2;
}

}  // namespace pktool::pokemon::pk1
