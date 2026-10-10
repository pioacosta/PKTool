#include "core/pokemon/pk2.h"

#include <algorithm>
#include <cstring>

#include "core/data/gen12.h"
#include "core/data/language.h"
#include "core/pokemon/gb_common.h"
#include "core/text/gen12_text.h"

namespace pktool::pokemon::pk2 {
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

std::string readName(const uint8_t* e, size_t offset, uint8_t language) {
    return text::decodeGb(2, e + offset, stringBytes(language), language);
}

void writeName(uint8_t* e, size_t offset, const std::string& name, uint8_t language, bool nickname) {
    uint8_t buffer[kStringBytes];
    if (!text::encodeGb(2, name, buffer, stringBytes(language), maxNameChars(language, nickname), language)) return;
    std::memset(e + offset, text::kGb12Terminator, kStringBytes);
    std::memcpy(e + offset, buffer, stringBytes(language));
}

}  // namespace

Pokemon toPokemon(const uint8_t* e, size_t) {
    Pokemon p;
    p.format = 2;
    p.species = e[offset::kSpecies];
    p.language = languageOf(e);
    p.isEgg = e[kEggOffset] != 0;
    p.nickname = readName(e, kNicknameOffset, p.language);
    p.trainer.name = readName(e, kOtOffset, p.language);
    p.trainer.tid = gb::readBe16(e + offset::kTid);
    p.heldItem = data::itemFromGen2(e[offset::kHeldItem]);
    p.exp = gb::readBe24(e + offset::kExp);
    for (int m = 0; m < 4; ++m) {
        p.moves[m] = e[offset::kMoves + m];
        p.pp[m] = e[offset::kPp + m] & 0x3F;
        p.ppUps[m] = e[offset::kPp + m] >> 6;
    }
    gb::unpackDvs(gb::readBe16(e + offset::kDvs), p.ivs);
    for (int s = 0; s < 5; ++s) p.statExp[s] = gb::readBe16(e + offset::kStatExp + s * 2);
    p.statExp[kSpD] = p.statExp[kSpA];
    p.friendship = e[offset::kFriendship];
    p.pokerus = e[offset::kPokerus];
    const uint16_t caught = gb::readBe16(e + offset::kCaughtData);
    p.met.encounterType = static_cast<uint8_t>(caught >> 14);  // time of day
    p.met.metLevel = (caught >> 8) & 0x3F;
    p.trainer.gender = (caught & 0x80) ? Gender::Female : Gender::Male;
    p.met.metLocation = caught & 0x7F;
    p.gender = gb::genderFromDvs(p.species, p.ivs);
    p.shiny = gb::isShinyDvs(p.ivs);
    if (p.species == 201) p.form = gb::unownForm(p.ivs);
    p.level = e[offset::kLevel] >= 1 && e[offset::kLevel] <= 100 ? e[offset::kLevel] : gb::levelFor(2, p.species, p.exp);
    p.isNicknamed = !p.isEgg && p.species != 0 && p.nickname != data::defaultNickname(p.species, p.language, 2);
    return p;
}

void applyPokemon(const Pokemon& p, uint8_t* e) {
    const uint8_t language = p.language == 0 ? 2 : p.language;
    e[offset::kSpecies] = static_cast<uint8_t>(p.species);
    e[kLanguageOffset] = language;
    e[kEggOffset] = p.isEgg ? 1 : 0;
    if (readName(e, kNicknameOffset, language) != p.nickname) writeName(e, kNicknameOffset, p.nickname, language, true);
    if (readName(e, kOtOffset, language) != p.trainer.name) writeName(e, kOtOffset, p.trainer.name, language, false);
    gb::writeBe16(e + offset::kTid, p.trainer.tid);
    if (data::itemFromGen2(e[offset::kHeldItem]) != p.heldItem) e[offset::kHeldItem] = data::itemToGen2(p.heldItem);
    if (gb::readBe24(e + offset::kExp) != p.exp || e[offset::kLevel] == 0) {
        gb::writeBe24(e + offset::kExp, p.exp);
        e[offset::kLevel] = p.level;
    }
    for (int m = 0; m < 4; ++m) {
        e[offset::kMoves + m] = static_cast<uint8_t>(p.moves[m]);
        e[offset::kPp + m] = static_cast<uint8_t>((std::min<uint8_t>(p.pp[m], 63)) | (std::min<uint8_t>(p.ppUps[m], 3) << 6));
    }
    for (int s = 0; s < 5; ++s) gb::writeBe16(e + offset::kStatExp + s * 2, p.statExp[s]);
    gb::writeBe16(e + offset::kDvs, gb::packDvs(p.ivs));
    e[offset::kFriendship] = p.friendship;
    e[offset::kPokerus] = p.pokerus;
    const uint16_t caught = static_cast<uint16_t>(((p.met.encounterType & 3) << 14) | ((p.met.metLevel & 0x3F) << 8) |
                                                  (p.trainer.gender == Gender::Female ? 0x80 : 0) |
                                                  (p.met.metLocation & 0x7F));
    gb::writeBe16(e + offset::kCaughtData, caught);
}

void fillPartyArea(uint8_t* e) {
    const uint16_t species = e[offset::kSpecies];
    const uint32_t exp = gb::readBe24(e + offset::kExp);
    uint8_t ivs[kStatCount];
    uint16_t statExp[kStatCount];
    gb::unpackDvs(gb::readBe16(e + offset::kDvs), ivs);
    for (int s = 0; s < 5; ++s) statExp[s] = gb::readBe16(e + offset::kStatExp + s * 2);
    statExp[kSpD] = statExp[kSpA];
    uint8_t level = e[offset::kLevel];
    if (level == 0) level = gb::levelFor(2, species, exp);
    uint16_t stats[kStatCount];
    gb::calcStats(2, species, true, ivs, statExp, level, stats);
    for (int s = 0; s < kStatCount; ++s) gb::writeBe16(e + offset::kMaxHp + s * 2, stats[s]);
    if (gb::readBe16(e + offset::kHp) == 0 && e[offset::kStatus] == 0) gb::writeBe16(e + offset::kHp, stats[kHp]);
}

void refreshPartyStats(uint8_t* e) {
    const uint16_t oldMax = gb::readBe16(e + offset::kMaxHp);
    uint16_t current = gb::readBe16(e + offset::kHp);
    fillPartyArea(e);
    const uint16_t newMax = gb::readBe16(e + offset::kMaxHp);
    if (oldMax == 0 || current >= oldMax || current > newMax) current = newMax;
    gb::writeBe16(e + offset::kHp, current);
}

void clear(uint8_t* e, bool japanese) {
    std::memset(e, 0, kEntitySize);
    std::memset(e + kOtOffset, text::kGb12Terminator, 2 * kStringBytes);
    e[kLanguageOffset] = japanese ? 1 : 2;
}

}  // namespace pktool::pokemon::pk2
