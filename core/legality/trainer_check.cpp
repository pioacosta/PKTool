#include "core/legality/trainer_check.h"

#include <string>

#include "core/legality/events3_check.h"
#include "core/legality/trade_check.h"

namespace pktool::legality {
namespace {

using pokemon::Gender;
using pokemon::Pokemon;

constexpr uint16_t kLocationNpcTrade4 = 2001;
constexpr uint16_t kLocationNpcTrade5 = 30002;

// Language IDs: 1 Japanese, 2 English, 3 French, 4 Italian, 5 German, 6 unused,
// 7 Spanish, 8 Korean.
constexpr uint8_t kJapanese = 1;
constexpr uint8_t kKorean = 8;

// PKHeX Legal.MaxLengthTrainer*: 12 characters, 6 for Japanese and Korean.
int maxLengthOt(uint8_t language) { return language == kJapanese || language == kKorean ? 6 : 12; }

// Length in characters of a UTF-8 string.
size_t charCount(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) n += (c & 0xC0) != 0x80;
    return n;
}

bool startsWithNoCase(const std::string& s, const char* prefix) {
    for (size_t i = 0; prefix[i] != 0; ++i) {
        if (i >= s.size()) return false;
        char a = s[i], b = prefix[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + 32);
        if (a != b) return false;
    }
    return true;
}

bool suspiciousName(const std::string& name) {
    // The full-width spelling is 5 three-byte characters.
    return startsWithNoCase(name, "PKHeX") || name.compare(0, 15, "\xEF\xBC\xB0\xEF\xBC\xAB\xEF\xBC\xA8\xEF\xBD\x85\xEF\xBC\xB8") == 0;
}

bool addIfNew(Report& r, Severity s, const std::string& text) {
    for (const Finding& f : r.findings) {
        if (f.text == text) return false;
    }
    r.findings.push_back({s, text});
    return true;
}

// The trainer is a fixed, scripted one: events, in-game trades, N, Colosseum
// and XD. PKHeX does not apply the OT name and ID rules to those.
bool fixedTrainer(const Pokemon& p) {
    if (p.met.fatefulEncounter || p.originGame == version::kColosseumXD) return true;
    if (p.format == 3) {
        if (p.met.metLocation == location3::kTrade || p.met.metLocation == location3::kFateful) return true;
        if (findGen3Event(p) != nullptr) return true;  // events without the fateful flag
    } else {
        if (p.met.metLocation == kLocationNpcTrade4 || p.met.metLocation == kLocationNpcTrade5) return true;
        if (isGen3Origin(p.originGame) && findGen3Event(p) != nullptr) return true;
    }
    if (isNpcTrade(p)) return true;
    if (p.nSparkle) return true;
    // N (full-width in Japanese): "N" / U+FF2E.
    return p.format == 5 && (p.trainer.name == "N" || p.trainer.name == "\xEF\xBC\xAE");
}

bool suspiciousIds(uint16_t tid, uint16_t sid) {
    return (tid == 12345 && sid == 54321) || (tid == 15040 && sid == 18831);
}

}  // namespace

bool rsTrainerIdPossible(uint16_t tid, uint16_t sid) {
    // PKHeX LCRNGReversal.GetSeeds(first = SID, second = TID): the LCRNG state Z
    // with high half SID whose successor has high half TID.
    constexpr uint32_t kRMult = 0xEEB9EB65u;
    constexpr uint32_t kRAdd = 0x0A3561A1u;
    constexpr uint32_t kLag0 = 0x7ED7u;
    constexpr uint32_t kLag1 = 0xD33u;
    constexpr uint32_t kLower = 0x50F5A0Bu;
    constexpr uint32_t kUpper = 0x50F40B4u;
    const uint32_t first = static_cast<uint32_t>(sid) << 16;
    const uint32_t second = static_cast<uint32_t>(tid) << 16;
    const uint32_t tmp = (((second * kRMult) - first) >> 16) * kLag0;
    if (((tmp + kLower) >> 16) != ((tmp + kUpper) >> 16)) return false;
    uint32_t low = (((tmp + kLower) >> 16) * kLag1) % kLag0;
    do {
        const uint32_t seed = (second | low) * kRMult + kRAdd;
        if ((seed & 0xFFFF0000u) == first) return true;
    } while ((low += kLag0) < 0x10000u);
    return false;
}

void checkTrainer(const Pokemon& p, Report& r) {
    // Language.
    const uint8_t maxLanguage = isGen3Origin(p.originGame) ? 7 : kKorean;
    if (p.language == 0 || p.language == 6) {
        addIfNew(r, Severity::Invalid, "Language ID does not exist");
    } else if (p.language > maxLanguage) {
        addIfNew(r, Severity::Invalid, "Language not available in the game of origin");
    }
    // Colosseum and XD only have a male trainer.
    if (p.originGame == version::kColosseumXD && p.trainer.gender == Gender::Female && !gcFemaleOtGift(p)) {
        bool already = false;
        for (const Finding& f : r.findings) already = already || f.text.find("OT gender") != std::string::npos;
        if (!already) addIfNew(r, Severity::Invalid, "OT from Colosseum/XD cannot be female");
    }

    if (fixedTrainer(p)) return;

    // Name.
    if (p.trainer.name.empty()) {
        addIfNew(r, Severity::Invalid, "OT name is empty");
    } else {
        // A traded egg keeps the OT of a game in any language: use the longest limit.
        const bool egg = p.isEgg || p.met.eggLocation != 0;
        const int limit = egg ? 12 : maxLengthOt(p.language);
        if (static_cast<int>(charCount(p.trainer.name)) > limit) addIfNew(r, Severity::Invalid, "OT name is too long");
        if (suspiciousName(p.trainer.name)) addIfNew(r, Severity::Warning, "Suspicious OT name");
    }

    // IDs.
    const uint16_t tid = p.trainer.tid;
    const uint16_t sid = p.trainer.sid;
    if (p.originGame == version::kRuby || p.originGame == version::kSapphire) {
        if (!rsTrainerIdPossible(tid, sid)) {
            // Eggs of R/S hatched in Emerald can carry the Emerald trainer IDs.
            const bool hatched = p.format == 3 ? (!p.isEgg && p.met.metLevel == 0) : p.met.eggLocation != 0;
            addIfNew(r, hatched ? Severity::Warning : Severity::Invalid,
                     "Trainer IDs no Ruby/Sapphire new game can produce");
        }
    }
    if (tid == 0 && sid == 0) {
        addIfNew(r, Severity::Warning, "TID and SID are both 0");
    } else if (tid == sid) {
        addIfNew(r, Severity::Warning, "TID equals SID");
    } else if (tid == 0) {
        addIfNew(r, Severity::Warning, "TID is 0");
    } else if (sid == 0) {
        addIfNew(r, Severity::Warning, "SID is 0");
    } else if (suspiciousIds(tid, sid)) {
        addIfNew(r, Severity::Warning, "Suspicious trainer IDs");
    }
}

}  // namespace pktool::legality
