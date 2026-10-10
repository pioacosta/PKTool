#include "core/legality/trade_check.h"

#include <string>
#include <vector>

#include "core/data/learn4.h"
#include "core/data/learn5.h"
#include "core/data/names.h"
#include "core/data/trades.h"

namespace pktool::legality {

using pokemon::Gender;
using pokemon::Pokemon;

namespace {

constexpr uint16_t kLocationTrade3 = 254;
constexpr uint16_t kLocationTrade4 = 2001;
constexpr uint16_t kLocationTrade5 = 30002;

bool inLine(const Pokemon& p, uint16_t base) {
    for (uint16_t s = p.species; s != 0; s = p.format == 5 ? data::preEvolution5(s) : data::preEvolution4(s)) {
        if (s == base) return true;
    }
    return false;
}

bool versionOk(const data::Trade345& t, const Pokemon& p) { return p.originGame < 32 && ((t.versions >> p.originGame) & 1u) != 0; }

// The HG/SS trades with their own met location (Webster's Spearow, Kirk's Shuckle).
bool fixedMetTrade(const Pokemon& p) {
    if (p.format != 4) return false;
    for (int i = 0; i < data::trades345Count(); ++i) {
        const data::Trade345& t = data::trade345(i);
        if (t.gen == 4 && t.metLocation != 0 && t.metLocation == p.met.metLocation && t.tid == p.trainer.tid &&
            versionOk(t, p) && inLine(p, t.species)) {
            return true;
        }
    }
    return false;
}

const char* genderName(uint8_t g) { return g == 0 ? "male" : g == 1 ? "female" : "genderless"; }

// Reasons the Pokemon does not fit the trade (hard ones first), and whether
// the OT name only differs by language.
void compare(const data::Trade345& t, const Pokemon& p, std::vector<std::string>& errors, std::vector<std::string>& warnings) {
    if (p.level < t.level) errors.push_back("level below the trade's level " + std::to_string(t.level));
    if (p.trainer.tid != t.tid || p.trainer.sid != t.sid) errors.push_back("trainer IDs differ from the trade's");
    if ((t.flags & data::kTradeHasPid) && p.pid != t.pid) errors.push_back("PID differs from the trade's");
    if (p.trainer.gender != (t.otGender == 0 ? Gender::Male : Gender::Female)) {
        errors.push_back(std::string("OT gender should be ") + (t.otGender == 0 ? "male" : "female"));
    }
    for (int i = 0; i < 6; ++i) {
        if (t.ivs[i] != 255 && p.ivs[i] != t.ivs[i]) {
            errors.push_back("IVs differ from the trade's");
            break;
        }
    }
    if (t.nature != 255 && p.nature != t.nature) errors.push_back(std::string("nature should be ") + data::natureName(t.nature));
    if (t.gender < 3 && static_cast<uint8_t>(p.gender) != t.gender) errors.push_back(std::string("gender should be ") + genderName(t.gender));
    if (p.met.ball != data::kBallPoke) errors.push_back("not in a Poke Ball");
    if (p.format == 5 && p.met.metLevel != t.level) errors.push_back("met level should be " + std::to_string(t.level));
    if (p.format == 4 && (t.metLocation != 0 ? p.met.metLevel != t.level : p.met.metLevel < t.level)) {
        errors.push_back("met level differs from the trade's");
    }

    // OT name and nickname, by language.
    bool otHere = false, otAny = false, nickHere = false, nickAny = false;
    for (int lang = 1; lang < 9; ++lang) {
        const bool otEq = t.ot[lang][0] != '\0' && p.trainer.name == t.ot[lang];
        otAny = otAny || otEq;
        if (lang == p.language) otHere = otEq;
        const bool nickEq = t.nickname[lang][0] != '\0' && p.nickname == t.nickname[lang];
        nickAny = nickAny || nickEq;
        if (lang == p.language) nickHere = nickEq;
    }
    if (p.format == 5 && p.language == 0) otHere = otAny;  // B/W forget the language ID of the trades
    if (!otAny) {
        errors.push_back("OT name does not match the trade's");
    } else if (!otHere) {
        warnings.push_back("OT name is the trade's in another language");
    }
    if ((t.flags & data::kTradeNickFixed) && !nickAny) warnings.push_back("nickname differs from the trade's");
    else if ((t.flags & data::kTradeNickFixed) && !nickHere && p.language != 0) warnings.push_back("nickname is the trade's in another language");
}

}  // namespace

bool isNpcTrade(const Pokemon& p) {
    switch (p.format) {
        case 3: return p.met.metLocation == kLocationTrade3;
        case 4: return p.met.metLocation == kLocationTrade4 || fixedMetTrade(p);
        case 5: return p.met.metLocation == kLocationTrade5;
        default: return false;
    }
}

void checkNpcTrade(const Pokemon& p, Report& r) {
    const data::Trade345* best = nullptr;
    std::vector<std::string> bestErrors, bestWarnings;
    bool speciesKnown = false;
    for (int i = 0; i < data::trades345Count(); ++i) {
        const data::Trade345& t = data::trade345(i);
        if (t.gen != p.format || !versionOk(t, p) || !inLine(p, t.species)) continue;
        if (p.format == 4 && t.metLocation != 0 && t.metLocation != p.met.metLocation) continue;
        if (p.format == 4 && t.metLocation == 0 && p.met.metLocation != kLocationTrade4) continue;
        speciesKnown = true;
        std::vector<std::string> errors, warnings;
        compare(t, p, errors, warnings);
        if (best == nullptr || errors.size() < bestErrors.size() ||
            (errors.size() == bestErrors.size() && warnings.size() < bestWarnings.size())) {
            best = &t;
            bestErrors = std::move(errors);
            bestWarnings = std::move(warnings);
        }
    }
    if (!speciesKnown) {
        r.findings.push_back({Severity::Warning, std::string("In-game trade of ") + data::speciesName(p.species) +
                                                      " not in the PKTool trade table"});
        return;
    }
    const std::string what = std::string("In-game trade (") + data::speciesName(best->species) + ")";
    for (const std::string& e : bestErrors) r.findings.push_back({Severity::Invalid, what + ": " + e});
    for (const std::string& w : bestWarnings) r.findings.push_back({Severity::Warning, what + ": " + w});
    if (bestErrors.empty() && bestWarnings.empty()) r.findings.push_back({Severity::Valid, what});
}

}  // namespace pktool::legality
