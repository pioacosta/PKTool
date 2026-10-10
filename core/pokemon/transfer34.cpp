#include "core/pokemon/transfer34.h"

#include <cstring>

#include "core/data/moves.h"
#include "core/data/names.h"
#include "core/pokemon/create4.h"
#include "core/util/bytes.h"

namespace pktool::pokemon::transfer34 {
namespace {

constexpr uint16_t kLocationPalPark = 55;
constexpr uint16_t kUnown = 201;
constexpr size_t kPk4Contest = 0x1E;  // six contest stats, after the EVs

// HMs of Gen III, which Pal Park refuses (they are forgotten here).
const uint16_t kHms3[] = {15, 19, 57, 70, 148, 249, 127, 291};

bool isMail4(uint16_t item) {
    return item >= 137 && item <= 148;
}

void setBit(uint8_t* data, size_t offset, int bit) {
    data[offset] |= static_cast<uint8_t>(1u << bit);
}

// Unown's letter in Gen III comes from the PID; Gen IV stores it.
uint8_t unownForm(uint32_t pid) {
    uint32_t value = ((pid >> 24) & 3) << 6 | ((pid >> 16) & 3) << 4 | ((pid >> 8) & 3) << 2 | (pid & 3);
    return static_cast<uint8_t>(value % 28);
}

}  // namespace

void convert(const uint8_t in[pk3::kStoredSize], uint8_t out[pk4::kStoredSize], const Date& today,
             std::vector<std::string>* notes) {
    Pokemon p = pk3::toPokemon(in, pk3::kStoredSize);
    Pokemon q = p;
    q.format = 4;
    q.isEgg = false;
    q.friendship = 70;
    q.met.metLocation = kLocationPalPark;
    q.met.metLevel = p.level;
    q.met.metDate = today;
    q.met.eggLocation = 0;
    q.met.eggDate = Date{};
    if (p.species == kUnown) q.form = unownForm(p.pid);
    if (!p.isNicknamed) q.nickname = create4::defaultNickname(p.species, p.language, 4);
    if (isMail4(q.heldItem)) {
        if (notes) notes->push_back("Mail removed");
        q.heldItem = 0;
    }

    // HM moves are removed and the remaining moves shifted up.
    uint16_t moves[4] = {};
    uint8_t ups[4] = {};
    int count = 0;
    for (int i = 0; i < 4; ++i) {
        bool hm = false;
        for (uint16_t h : kHms3) hm = hm || p.moves[i] == h;
        if (hm && notes) notes->push_back(std::string("Forgot HM ") + data::moveName(p.moves[i]));
        if (p.moves[i] == 0 || hm) continue;
        moves[count] = p.moves[i];
        ups[count] = p.ppUps[i];
        ++count;
    }
    for (int i = 0; i < 4; ++i) {
        q.moves[i] = moves[i];
        q.ppUps[i] = ups[i];
        q.pp[i] = data::maxPp4(moves[i], ups[i]);
    }

    std::memset(out, 0, pk4::kStoredSize);
    pk4::applyPokemon(q, out);

    // Contest stats and ribbons (Gen III contest ribbons are counts per
    // category; Gen IV has one bit per rank).
    std::memcpy(out + kPk4Contest, in + pk3::offset::kContest, 6);
    uint32_t r = util::readU32(in + pk3::offset::kRibbons);
    for (int category = 0; category < 5; ++category) {
        uint32_t rank = (r >> (3 * category)) & 7;
        for (uint32_t k = 0; k < rank && k < 4; ++k) {
            int bit = category * 4 + static_cast<int>(k);  // Cool, Beauty, Cute, Smart, Tough from 0x3C
            setBit(out, 0x3C + bit / 8, bit % 8);
        }
    }
    const struct {
        int from;
        size_t offset;
        int bit;
    } kMap[] = {
        {15, 0x3E, 4},  // Champion (Hoenn)
        {16, 0x3E, 5},  // Winning
        {17, 0x3E, 6},  // Victory
        {18, 0x3E, 7},  // Artist
        {19, 0x3F, 0},  // Effort
        {20, 0x3F, 1},  // Battle Champion
        {21, 0x3F, 2},  // Regional Champion
        {22, 0x3F, 3},  // National Champion
        {23, 0x3F, 4},  // Country
        {24, 0x3F, 5},  // National
        {25, 0x3F, 6},  // Earth
        {26, 0x3F, 7},  // World
    };
    for (const auto& m : kMap) {
        if (r & (1u << m.from)) setBit(out, m.offset, m.bit);
    }
    pk4::refreshChecksum(out);
}

}  // namespace pktool::pokemon::transfer34
