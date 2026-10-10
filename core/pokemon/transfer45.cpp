#include "core/pokemon/transfer45.h"

#include <cstring>

#include "core/data/moves.h"
#include "core/data/names.h"
#include "core/pokemon/pk5.h"
#include "core/text/gen4_text.h"
#include "core/util/bytes.h"

namespace pktool::pokemon::transfer45 {
namespace {

constexpr uint16_t kLocationTransfer4 = 30001;
constexpr uint16_t kLocationTransferCelebi = 30010;
constexpr uint16_t kLocationTransferCrown = 30012;
constexpr uint16_t kArceus = 493;
constexpr uint16_t kShedinja = 292;
constexpr uint16_t kDefog = 432;

// HMs Poké Transfer refuses; with Defog known, Whirlpool goes instead.
const uint16_t kHmsDppt[] = {15, 19, 57, 70, 432, 249, 127, 431};
const uint16_t kHmsHgss[] = {15, 19, 57, 70, 250, 249, 127, 431};

bool isMail(uint16_t item) {
    return item >= 137 && item <= 148;
}

}  // namespace

void convert(const uint8_t in[pk4::kStoredSize], uint8_t out[pk4::kStoredSize], std::vector<std::string>* notes) {
    Pokemon p = pk4::toPokemon(in, pk4::kStoredSize);
    std::memcpy(out, in, pk4::kStoredSize);

    // Clear the Pt/HG/SS extended locations (0x44-0x47) and the HG/SS ball
    // (0x86-0x87); the model then reads the D/P fields.
    std::memset(out + 0x44, 0, 4);
    std::memset(out + 0x86, 0, 2);
    Pokemon q = pk5::toPokemon(out, pk4::kStoredSize);  // shared fields, Gen V view

    q.nature = natureFromPid(p.pid);
    q.nickname = p.nickname;
    q.trainer.name = p.trainer.name;
    q.friendship = 70;
    q.met.ball = p.met.ball;
    q.met.metLevel = p.level;
    q.met.metLocation = kLocationTransfer4;
    if (p.met.fatefulEncounter && p.species == 251) q.met.metLocation = kLocationTransferCelebi;
    if (p.met.fatefulEncounter && (p.species == 243 || p.species == 244 || p.species == 245)) {
        q.met.metLocation = kLocationTransferCrown;
    }
    if (p.species == kArceus) {
        q.form = 0;
        if (q.heldItem && notes) notes->push_back("Arceus plate removed");
        q.heldItem = 0;
    } else if (isMail(q.heldItem)) {
        if (notes) notes->push_back("Mail removed");
        q.heldItem = 0;
    }
    if (p.species == kShedinja) q.gender = Gender::Genderless;

    // HM moves are removed and the remaining moves shifted up.
    bool hasDefog = false;
    for (uint16_t m : p.moves) hasDefog = hasDefog || m == kDefog;
    const uint16_t* banned = hasDefog ? kHmsHgss : kHmsDppt;
    uint16_t moves[4] = {};
    uint8_t ups[4] = {};
    int count = 0;
    for (int i = 0; i < 4; ++i) {
        bool hm = false;
        for (int k = 0; k < 8; ++k) hm = hm || p.moves[i] == banned[k];
        if (hm && notes) notes->push_back(std::string("Forgot HM ") + data::moveName(p.moves[i]));
        if (p.moves[i] == 0 || hm) continue;
        moves[count] = p.moves[i];
        ups[count] = p.ppUps[i];
        ++count;
    }
    for (int i = 0; i < 4; ++i) {
        q.moves[i] = moves[i];
        q.ppUps[i] = ups[i];
        q.pp[i] = data::maxPp5(moves[i], ups[i]);
    }

    // Names: Gen IV glyphs -> UTF-16, with the buffers cleared first.
    std::memset(out + pk4::offset::kNickname, 0, pk4::kNicknameChars * 2);
    std::memset(out + pk4::offset::kOtName, 0, pk4::kOtNameChars * 2);
    text::encodeGen5(q.nickname, out + pk4::offset::kNickname, pk4::kNicknameChars, pk4::kNicknameMaxLength);
    text::encodeGen5(q.trainer.name, out + pk4::offset::kOtName, pk4::kOtNameChars, pk4::kOtNameMaxLength);
    pk5::applyPokemon(q, out);
}

}  // namespace pktool::pokemon::transfer45
