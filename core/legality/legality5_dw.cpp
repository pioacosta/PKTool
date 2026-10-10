// Gen V Dream World (Entree Forest) and N's Pokémon legality, after PKHeX's
// EncounterStatic5Entree / EncounterStatic5N and MiscVerifierPK5.

#include "core/legality/legality5_dw.h"

#include <string>

#include "core/data/encounters5.h"
#include "core/data/learn5.h"
#include "core/data/names.h"
#include "core/data/personal.h"
#include "core/pokemon/edit4.h"

namespace pktool::legality {
namespace {

using pokemon::Gender;
using pokemon::Pokemon;

constexpr uint8_t kBallDream = 25;
constexpr uint16_t kZorua = 570;
constexpr uint8_t kLanguageJapanese = 1;
constexpr uint8_t kLanguageKorean = 8;

void add(Report& r, Severity s, std::string text) {
    r.findings.push_back({s, std::move(text)});
}

bool inSpeciesLine(const Pokemon& p, uint16_t species) {
    for (uint16_t s = p.species; s != 0; s = data::preEvolution5(s)) {
        if (s == species) return true;
    }
    return false;
}

// Bit of data::DreamWorld5::languages for a language ID (1 Japanese, 8 Korean).
uint8_t languageBit(uint8_t language) {
    return language == kLanguageJapanese ? 1 : language == kLanguageKorean ? 2 : 4;
}

// How far an entry got through the checks (a higher rank is a closer match);
// kRankOk means the Pokémon fits it completely.
enum Rank : int {
    kRankVersion,
    kRankLevel,
    kRankForm,
    kRankGender,
    kRankLanguage,
    kRankShiny,
    kRankAbility,
    kRankPid,
    kRankOk,
};

struct Fit {
    int rank = -1;
    std::string problem;
};

Fit fitEntry(const Pokemon& p, const data::DreamWorld5& e) {
    Fit fit;
    auto fail = [&](Rank rank, const char* text) {
        fit.rank = rank;
        fit.problem = text;
        return fit;
    };
    if (!(e.versions & data::versionBit5(p.originGame))) {
        return fail(kRankVersion, "This Dream World Pokemon was not available in this game");
    }
    if (p.met.metLevel != e.level) {
        fit.rank = kRankLevel;
        fit.problem = "Dream World Pokemon are met at level " + std::to_string(e.level);
        return fit;
    }
    if (e.form != data::kDwAny && p.form != e.form) return fail(kRankForm, "Form does not match the Dream World Pokemon");
    if (e.gender != data::kDwAny && static_cast<uint8_t>(p.gender) != e.gender) {
        return fail(kRankGender, e.gender == 0   ? "This Dream World Pokemon is always male"
                                 : e.gender == 1 ? "This Dream World Pokemon is always female"
                                                 : "This Dream World Pokemon is genderless");
    }
    if (!(e.languages & languageBit(p.language))) {
        return fail(kRankLanguage, "This Pokemon Global Link Pokemon was not given in this language");
    }
    if (p.shiny) return fail(kRankShiny, "Dream World Pokemon are never shiny");
    const data::PersonalInfo* info = data::personal5(e.species);
    bool hidden = info && hasHiddenAbility5(*info);
    if (hidden != p.hiddenAbility) {
        return fail(kRankAbility, hidden ? "Dream World Pokemon have their hidden ability"
                                         : "This Dream World Pokemon has no hidden ability");
    }
    if (!hidden && ((p.pid >> 16) & 1) != 0) {
        return fail(kRankAbility, "Dream World Pokemon without a hidden ability have their first ability");
    }
    // A fixed gender is forced on a random PID: the gender value cannot be the extreme one.
    if (e.gender == 0 && ((p.pid & 0xFF) >= 254)) return fail(kRankPid, "PID does not match the Dream World's forced gender");
    if (e.gender == 1 && ((p.pid & 0xFF) == 0)) return fail(kRankPid, "PID does not match the Dream World's forced gender");
    fit.rank = kRankOk;
    return fit;
}

}  // namespace

bool isDreamWorldBall5(uint8_t ball) {
    if (ball >= 1 && ball <= 4) return true;   // Master, Ultra, Great, Poke
    if (ball >= 6 && ball <= 15) return true;  // Net ... Quick (not Safari)
    return ball == kBallDream;
}

void checkDreamWorld5(const Pokemon& p, Report& r, SpecialMoves* special) {
    Fit best;
    bool any = false;
    bool found = false;
    for (uint16_t s = p.species; s != 0; s = data::preEvolution5(s)) {
        const data::DreamWorld5* begin;
        const data::DreamWorld5* end;
        data::dreamWorld5(s, &begin, &end);
        for (const data::DreamWorld5* e = begin; e != end; ++e) {
            any = true;
            Fit fit = fitEntry(p, *e);
            if (fit.rank > best.rank) best = fit;
            if (fit.rank != kRankOk) continue;
            found = true;
            for (int m = 0; special && m < 3; ++m) {
                if (e->moves[m] != 0 && !special->contains(e->moves[m]) && special->count < 48) {
                    special->moves[special->count++] = e->moves[m];
                }
            }
        }
    }
    if (!any) {
        add(r, Severity::Invalid, std::string(data::speciesName(p.species)) + " cannot be found in the Dream World");
        return;
    }
    if (found) {
        add(r, Severity::Valid, "Met: Entree Forest (Dream World)");
    } else {
        add(r, Severity::Invalid, best.problem);
    }
    if (!isDreamWorldBall5(p.met.ball)) add(r, Severity::Invalid, "Ball impossible for a Dream World Pokemon");
    if (p.met.fatefulEncounter) add(r, Severity::Invalid, "Dream World Pokemon have no fateful encounter");
    if (p.met.eggLocation != 0) add(r, Severity::Invalid, "Dream World Pokemon cannot come from an egg");
}

const data::DreamWorld5* findDreamWorldFor5(const Pokemon& p, bool anyGame, uint16_t* encounterSpecies) {
    if (!p.hiddenAbility || p.shiny) return nullptr;
    const uint8_t gameBit = data::versionBit5(p.originGame);
    const uint8_t evolveLevel = data::evolveLevel5(p.species);
    const data::DreamWorld5* best = nullptr;
    int bestScore = -1;
    for (uint16_t s = p.species; s != 0; s = data::preEvolution5(s)) {
        if (s != p.species && p.level < evolveLevel) break;
        const data::PersonalInfo* info = data::personal5(s);
        if (info == nullptr || !hasHiddenAbility5(*info)) continue;
        const data::DreamWorld5* begin;
        const data::DreamWorld5* end;
        data::dreamWorld5(s, &begin, &end);
        for (const data::DreamWorld5* e = begin; e != end; ++e) {
            if (e->level > p.level) continue;
            if (!(e->versions & gameBit) && !anyGame) continue;
            if (!(e->languages & languageBit(p.language))) continue;
            if (e->gender != data::kDwAny && static_cast<uint8_t>(p.gender) != e->gender) continue;
            if (e->form != data::kDwAny && p.form != e->form) continue;
            int score = (e->versions & gameBit) ? 100 : 0;
            for (int i = 0; i < 4; ++i) {
                for (int m = 0; m < 3; ++m) score += p.moves[i] != 0 && p.moves[i] == e->moves[m] ? 10 : 0;
            }
            if (s == p.species) score += 1;
            if (score > bestScore) {
                bestScore = score;
                best = e;
                if (encounterSpecies) *encounterSpecies = s;
            }
        }
    }
    return best;
}

const data::DreamRadar5* findDreamRadarFor5(const Pokemon& p) {
    for (uint16_t s = p.species; s != 0; s = data::preEvolution5(s)) {
        if (const data::DreamRadar5* e = data::dreamRadar5(s)) return e;
    }
    return nullptr;
}

bool isTherianForme5(const Pokemon& p) {
    return p.form == 1 && (p.species == 641 || p.species == 642 || p.species == 645);
}

uint16_t hiddenAbilityOf5(const Pokemon& p, const data::PersonalInfo& info) {
    if (isTherianForme5(p)) return p.species == 641 ? 144 : p.species == 642 ? 10 : 22;
    return info.hiddenAbility;
}

void checkDreamRadar5(const Pokemon& p, Report& r) {
    constexpr uint16_t kRotom = 479;
    const data::DreamRadar5* e = findDreamRadarFor5(p);
    if (e == nullptr) {
        add(r, Severity::Invalid, std::string(data::speciesName(p.species)) + " cannot be found at the Dream Radar");
        return;
    }
    add(r, Severity::Valid, std::string("Met: Dream Radar (") + data::speciesName(e->species) + ")");
    if (p.originGame != 22 && p.originGame != 23) add(r, Severity::Invalid, "The Dream Radar only exists for Black 2 / White 2");
    const uint8_t level = p.met.metLevel;
    if (level < 5 || level > 40 || level % 5 != 0) {
        add(r, Severity::Invalid, "Dream Radar Pokemon are met at a level that is a multiple of 5 (5-40)");
    }
    if (p.form != e->form && e->species != kRotom) add(r, Severity::Invalid, "Form does not match the Dream Radar Pokemon");
    if (p.species != e->species && p.level < data::evolveLevel5(p.species)) {
        add(r, Severity::Invalid, std::string(data::speciesName(p.species)) + " only evolves at level " +
                                      std::to_string(data::evolveLevel5(p.species)));
    }
    if (p.shiny) add(r, Severity::Invalid, "Dream Radar Pokemon are never shiny");
    if (p.met.ball != kBallDream) add(r, Severity::Invalid, "Dream Radar Pokemon are in a Dream Ball");
    if (e->anyAbility ? p.hiddenAbility : !p.hiddenAbility) {
        add(r, Severity::Invalid, e->anyAbility ? "This Dream Radar Pokemon has no hidden ability"
                                                : "Dream Radar Pokemon have their hidden ability");
    }
    if (p.met.fatefulEncounter) add(r, Severity::Invalid, "Dream Radar Pokemon have no fateful encounter");
    if (p.met.eggLocation != 0) add(r, Severity::Invalid, "Dream Radar Pokemon cannot come from an egg");
}

const data::NPokemon5* findNPokemon5(const Pokemon& p) {
    int count = 0;
    const data::NPokemon5* table = data::nPokemon5(&count);
    for (int i = 0; i < count; ++i) {
        if (table[i].pid == p.pid && inSpeciesLine(p, table[i].species)) return &table[i];
    }
    return nullptr;
}

void checkNPokemon5(const Pokemon& p, const data::NPokemon5& n, Report& r) {
    add(r, Severity::Valid, "N's Pokemon");
    if (!p.nSparkle) add(r, Severity::Invalid, "N's Pokemon must have the sparkle flag");
    if (p.originGame != 22 && p.originGame != 23) add(r, Severity::Invalid, "N's Pokemon only exist in Black 2 / White 2");
    if (p.met.metLocation != n.location) {
        add(r, Severity::Invalid, std::string("N's ") + data::speciesName(n.species) + " is met at " +
                                      data::locationName5(n.location));
    }
    if (p.met.metLevel != n.level) {
        add(r, Severity::Invalid, "N's Pokemon are met at level " + std::to_string(n.level));
    }
    if (p.met.eggLocation != 0) add(r, Severity::Invalid, "N's Pokemon cannot come from an egg");
    if (p.met.fatefulEncounter) add(r, Severity::Invalid, "N's Pokemon have no fateful encounter");
    if (p.trainer.tid != 2 || p.trainer.sid != 0) add(r, Severity::Invalid, "N's Pokemon have trainer ID 2 / secret ID 0");
    const char* ot = p.language == kLanguageJapanese ? "\xEF\xBC\xAE" : "N";  // full-width N in Japanese
    if (p.trainer.name != ot) add(r, Severity::Invalid, "The original trainer of N's Pokemon is N");
    if (p.trainer.gender != Gender::Male) add(r, Severity::Invalid, "N's original trainer is male");
    for (int s = 0; s < pokemon::kStatCount; ++s) {
        if (p.ivs[s] != 30) {
            add(r, Severity::Invalid, "N's Pokemon have 30 in every IV");
            break;
        }
    }
    if (p.nature != n.nature) add(r, Severity::Invalid, "Nature does not match N's Pokemon");
    if (p.hiddenAbility != (n.ability == 2)) {
        add(r, Severity::Invalid, n.ability == 2 ? "This N's Pokemon has its hidden ability" : "N's Pokemon have no hidden ability");
    }
    const data::PersonalInfo* info = data::personal5(p.species);
    Gender gender = info && info->genderRatio == data::kGenderRatioGenderless ? Gender::Genderless : Gender::Male;
    if (p.gender != gender) add(r, Severity::Invalid, "N's Pokemon are male (or genderless)");
    if (p.shiny) add(r, Severity::Invalid, "N's Pokemon are never shiny");
    if (n.species == kZorua ? p.met.ball != data::kBallPoke
                            : !isDreamWorldBall5(p.met.ball) || p.met.ball == kBallDream) {
        add(r, Severity::Invalid, n.species == kZorua ? "N's Zorua is in a Poke Ball" : "Ball impossible for N's Pokemon");
    }
}

}  // namespace pktool::legality
