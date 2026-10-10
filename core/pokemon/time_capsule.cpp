#include "core/pokemon/time_capsule.h"

#include <cstring>

#include "core/data/gen12.h"
#include "core/data/names.h"
#include "core/pokemon/gb_common.h"

namespace pktool::pokemon::timecapsule {
namespace {

constexpr uint8_t kLanguageKorean = 8;

void note(std::vector<std::string>* notes, const std::string& text) {
    if (notes != nullptr) notes->push_back(text);
}

}  // namespace

Result toGen2(const uint8_t* src, uint8_t* out, std::vector<std::string>* notes) {
    Result result;
    const uint16_t species = data::speciesFromGen1(src[pk1::offset::kSpecies]);
    if (species == 0) {
        result.reason = "It is not a real Pok\xC3\xA9mon (glitch species).";
        return result;
    }
    std::memset(out, 0, pk2::kEntitySize);
    // Name fields and language carry over; the species and the stored fields follow.
    std::memcpy(out + pk2::kOtOffset, src + pk1::kOtOffset, 2 * pk1::kStringBytes);
    out[pk2::kLanguageOffset] = src[pk1::kLanguageOffset];
    out[pk2::offset::kSpecies] = static_cast<uint8_t>(species);
    // PK1 bytes 7-0x20 (catch rate, moves, ID, EXP, Stat Exp, DVs, PP) are PK2 bytes 1-0x1A.
    std::memcpy(out + 1, src + pk1::offset::kCatchRate, 0x1A);
    const uint8_t rate = src[pk1::offset::kCatchRate];
    const uint8_t item = data::itemFromCatchRate(rate);
    out[pk2::offset::kHeldItem] = item;
    out[pk2::offset::kFriendship] = 70;  // the base friendship of every Gen I/II species
    out[pk2::offset::kLevel] = src[pk1::offset::kBoxLevel] != 0 ? src[pk1::offset::kBoxLevel] : src[pk1::offset::kLevel];
    pk2::refreshPartyStats(out);
    if (item != 0) {
        note(notes, std::string("Its catch rate became the held item ") + data::itemNameGen2(item));
    }
    note(notes, "Friendship starts at 70");
    uint8_t ivs[kStatCount];
    gb::unpackDvs(gb::readBe16(src + pk1::offset::kDvs), ivs);
    if (gb::isShinyDvs(ivs)) note(notes, "Its DVs make it shiny in Generation II");
    result.ok = true;
    return result;
}

Result toGen1(const uint8_t* src, uint8_t* out, std::vector<std::string>* notes) {
    Result result;
    const uint16_t species = src[pk2::offset::kSpecies];
    if (src[pk2::kEggOffset] != 0) {
        result.reason = "Eggs cannot be traded: hatch it first.";
        return result;
    }
    if (src[pk2::kLanguageOffset] == kLanguageKorean) {
        result.reason = "There are no Korean Generation I games.";
        return result;
    }
    if (species == 0 || species > data::kMaxSpeciesGen1) {
        result.reason = std::string(data::speciesName(species)) + " did not exist in Generation I.";
        return result;
    }
    for (int m = 0; m < 4; ++m) {
        const uint8_t move = src[pk2::offset::kMoves + m];
        if (move > data::kMaxMoveGen1) {
            result.reason = std::string("It knows ") + data::moveName(move) + ", a Generation II move.";
            return result;
        }
    }
    std::memset(out, 0, pk1::kEntitySize);
    std::memcpy(out + pk1::kOtOffset, src + pk2::kOtOffset, 2 * pk2::kStringBytes);
    out[pk1::kLanguageOffset] = src[pk2::kLanguageOffset];
    out[pk1::offset::kSpecies] = data::speciesToGen1(species);
    // PK2 bytes 1-0x1A (item, moves, ID, EXP, Stat Exp, DVs, PP) are PK1 bytes 7-0x20.
    std::memcpy(out + pk1::offset::kCatchRate, src + 1, 0x1A);
    const uint8_t item = src[pk2::offset::kHeldItem];
    out[pk1::offset::kCatchRate] = data::catchRateFromItem2(item);
    if (const data::PersonalGb* info = data::personalGb1(species, false)) {
        out[pk1::offset::kType1] = info->types[0];
        out[pk1::offset::kType2] = info->types[1];
    }
    out[pk1::offset::kBoxLevel] = src[pk2::offset::kLevel];
    pk1::fillPartyArea(out);
    // Keep the current HP when the Gen II party data had it.
    const uint16_t hp = gb::readBe16(src + pk2::offset::kHp);
    const uint16_t maxHp = gb::readBe16(out + pk1::offset::kMaxHp);
    gb::writeBe16(out + pk1::offset::kHp, hp != 0 && hp <= maxHp ? hp : maxHp);
    if (item != 0) {
        note(notes, std::string("The held item ") + data::itemNameGen2(item) + " became its catch rate");
    }
    if (src[pk2::offset::kFriendship] != 0) note(notes, "Friendship and Pokerus are lost");
    if (gb::readBe16(src + pk2::offset::kCaughtData) != 0) note(notes, "Crystal's caught data is lost");
    result.ok = true;
    return result;
}

}  // namespace pktool::pokemon::timecapsule
