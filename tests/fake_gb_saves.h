#pragma once
// In-memory Game Boy saves (Gen I and Gen II) for tests: valid list headers and
// checksums, the trainer "ASH" and (optionally) a few Pokemon written the way
// the games lay lists out, so the readers are tested against the layout rather
// than against PKTool's own writer.

#include <cstdint>
#include <cstring>
#include <vector>

#include "core/pokemon/gb_common.h"
#include "core/pokemon/pk1.h"
#include "core/pokemon/pk2.h"
#include "core/saves/gen1_save.h"
#include "core/saves/gen2_save.h"
#include "core/text/gen12_text.h"
#include "tests/test.h"

namespace fakegb {

using namespace pktool;

// One list entry as the game stores it.
struct RawMon {
    uint8_t marker = 0;
    std::vector<uint8_t> body;
    std::string ot;
    std::string nick;
};

// Writes a list (count, markers, bodies, OT names, nicknames) at `offset`.
inline void writeList(std::vector<uint8_t>& d, size_t offset, int capacity, size_t bodyBytes, size_t stringBytes,
                      uint8_t format, uint8_t language, const std::vector<RawMon>& mons) {
    uint8_t* list = d.data() + offset;
    const size_t bodies = 1 + static_cast<size_t>(capacity) + 1;
    const size_t ots = bodies + bodyBytes * static_cast<size_t>(capacity);
    const size_t nicks = ots + stringBytes * static_cast<size_t>(capacity);
    std::memset(list, 0, nicks + stringBytes * static_cast<size_t>(capacity));
    list[0] = static_cast<uint8_t>(mons.size());
    std::memset(list + 1, 0xFF, static_cast<size_t>(capacity) + 1);
    for (size_t i = 0; i < mons.size(); ++i) {
        list[1 + i] = mons[i].marker;
        std::memcpy(list + bodies + bodyBytes * i, mons[i].body.data(), std::min(bodyBytes, mons[i].body.size()));
        text::encodeGb(format, mons[i].ot, list + ots + stringBytes * i, stringBytes, stringBytes - 1, language);
        text::encodeGb(format, mons[i].nick, list + nicks + stringBytes * i, stringBytes, stringBytes - 1, language);
    }
}

// A Gen I body (party layout, `boxOnly` keeps just the first 33 bytes).
inline std::vector<uint8_t> gen1Body(uint16_t species, uint8_t level, uint16_t tid, uint16_t dvs, size_t bytes) {
    std::vector<uint8_t> b(44, 0);
    b[0] = data::speciesToGen1(species);
    const data::PersonalGb* info = data::personalGb1(species, false);
    b[5] = info->types[0];
    b[6] = info->types[1];
    b[7] = info->catchRate;
    b[3] = level;
    b[8] = 33;  // Tackle
    b[9] = 45;  // Growl
    pokemon::gb::writeBe16(&b[12], tid);
    pokemon::gb::writeBe24(&b[14], pokemon::gb::expFor(1, species, level));
    pokemon::gb::writeBe16(&b[0x1B], dvs);
    b[0x1D] = 35;
    b[0x1E] = 40;
    b[0x21] = level;
    b.resize(bytes);
    return b;
}

inline std::vector<uint8_t> gen2Body(uint16_t species, uint8_t level, uint16_t tid, uint16_t dvs, size_t bytes,
                                     uint8_t item = 0) {
    std::vector<uint8_t> b(48, 0);
    b[0] = static_cast<uint8_t>(species);
    b[1] = item;
    b[2] = 33;
    b[3] = 45;
    pokemon::gb::writeBe16(&b[6], tid);
    pokemon::gb::writeBe24(&b[8], pokemon::gb::expFor(2, species, level));
    pokemon::gb::writeBe16(&b[0x15], dvs);
    b[0x17] = 35;
    b[0x18] = 40;
    b[0x1B] = 70;
    b[0x1F] = level;
    b.resize(bytes);
    return b;
}

constexpr uint16_t kTid = 12345;

// Trainer names that the Japanese character set can encode too.
inline std::string trainerName(bool japanese) { return japanese ? "アッシュ" : "ASH"; }
inline std::string otherName(bool japanese) { return japanese ? "レッド" : "RED"; }

struct Gen1Offsets {
    size_t party, currentBox, checksum, ot, tid, boxIndex;
    int capacity;
    size_t stringBytes;
};
inline const Gen1Offsets kGen1Int{0x2F2C, 0x30C0, 0x3523, 0x2598, 0x2605, 0x284C, 20, 11};
inline const Gen1Offsets kGen1Jpn{0x2ED5, 0x302D, 0x3594, 0x2598, 0x25FB, 0x2842, 30, 6};

inline void fixGen1Checksum(std::vector<uint8_t>& d, const Gen1Offsets& o) {
    uint8_t sum = 0;
    for (size_t i = o.ot; i < o.checksum; ++i) sum = static_cast<uint8_t>(sum + d[i]);
    d[o.checksum] = static_cast<uint8_t>(~sum);
}

// A Gen I save with empty lists; `yellow` sets the Pikachu starter.
inline std::vector<uint8_t> blankGen1(bool japanese, bool yellow) {
    const Gen1Offsets& o = japanese ? kGen1Jpn : kGen1Int;
    std::vector<uint8_t> d(0x8000, 0);
    const uint8_t language = japanese ? 1 : 2;
    text::encodeGb(1, trainerName(japanese), d.data() + o.ot, o.stringBytes, 7, language);
    d[o.tid] = static_cast<uint8_t>(kTid >> 8);
    d[o.tid + 1] = static_cast<uint8_t>(kTid);
    if (yellow) d[japanese ? 0x29B9 : 0x29C3] = 0x54;
    writeList(d, o.party, 6, 44, o.stringBytes, 1, language, {});
    writeList(d, o.currentBox, o.capacity, 33, o.stringBytes, 1, language, {});
    fixGen1Checksum(d, o);
    return d;
}

// Bank offsets of Gen I box lists (PKHeX GetBoxRawDataOffset).
inline size_t gen1BoxOffset(bool japanese, int box) {
    const int boxes = japanese ? 8 : 12;
    const size_t length = 2 + (static_cast<size_t>(japanese ? 6 : 11) * 2 + 33 + 1) * (japanese ? 30 : 20);
    return box < boxes / 2 ? 0x4000 + static_cast<size_t>(box) * length
                           : 0x6000 + static_cast<size_t>(box - boxes / 2) * length;
}

struct Gen2Setup {
    bool crystal, japanese;
    size_t party, currentBox, accumulatedEnd, checksum, checksum2, boxIndex, boxNames, trainerBytes;
    int boxes, capacity, split;
    size_t stringBytes;
};
inline const Gen2Setup kGen2Gs{false, false, 0x288A, 0x2D6C, 0x2D68, 0x2D69, 0x7E6D, 0x2724, 0x2727, 7, 14, 20, 7, 11};
inline const Gen2Setup kGen2Crystal{true, false, 0x2865, 0x2D10, 0x2B82, 0x2D0D, 0x1F0D, 0x2700, 0x2703, 7, 14, 20, 7, 11};
inline const Gen2Setup kGen2GsJpn{false, true, 0x283E, 0x2D10, 0x2C8B, 0x2D0D, 0x7F0D, 0x2705, 0x2708, 5, 9, 30, 6, 6};
inline const Gen2Setup kGen2CrystalJpn{true, true, 0x281A, 0x2D10, 0x2AE2, 0x2D0D, 0x7F0D, 0x26E2, 0x26E5, 5, 9, 30, 6, 6};

inline void fixGen2Checksum(std::vector<uint8_t>& d, const Gen2Setup& s) {
    uint16_t sum = 0;
    for (size_t i = 0x2009; i <= s.accumulatedEnd; ++i) sum = static_cast<uint16_t>(sum + d[i]);
    d[s.checksum] = static_cast<uint8_t>(sum);
    d[s.checksum + 1] = static_cast<uint8_t>(sum >> 8);
    d[s.checksum2] = static_cast<uint8_t>(sum);
    d[s.checksum2 + 1] = static_cast<uint8_t>(sum >> 8);
}

inline size_t gen2BoxOffset(const Gen2Setup& s, int box) {
    const size_t length = 2 + (s.stringBytes * 2 + 32 + 1) * static_cast<size_t>(s.capacity) + 2;
    return box < s.split ? 0x4000 + static_cast<size_t>(box) * length : 0x6000 + static_cast<size_t>(box - s.split) * length;
}

inline std::vector<uint8_t> blankGen2(const Gen2Setup& s) {
    std::vector<uint8_t> d(s.japanese ? 0x10000 : 0x8000, 0);
    const uint8_t language = s.japanese ? 1 : 2;
    text::encodeGb(2, trainerName(s.japanese), d.data() + 0x2009 + 2, s.stringBytes, 7, language);
    d[0x2009] = static_cast<uint8_t>(kTid >> 8);
    d[0x2009 + 1] = static_cast<uint8_t>(kTid);
    writeList(d, s.party, 6, 48, s.stringBytes, 2, language, {});
    for (int box = 0; box < s.boxes; ++box) writeList(d, gen2BoxOffset(s, box), s.capacity, 32, s.stringBytes, 2, language, {});
    writeList(d, s.currentBox, s.capacity, 32, s.stringBytes, 2, language, {});
    fixGen2Checksum(d, s);
    return d;
}

}  // namespace fakegb
