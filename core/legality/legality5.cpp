// Gen V (B/W/B2/W2) legality checks. Same conservative approach as the
// Gen IV checker: only what is impossible under every known rule is Invalid.

#include "core/data/encounters5.h"
#include "core/data/learn3.h"
#include "core/data/learn4.h"
#include "core/data/learn5.h"
#include "core/data/moves.h"
#include "core/data/names.h"
#include "core/data/personal.h"
#include "core/legality/legality.h"
#include "core/legality/legality5_dw.h"
#include "core/legality/roamer.h"
#include "core/legality/ribbons_check.h"
#include "core/legality/trainer_check.h"
#include "core/legality/trade_check.h"
#include "core/pokemon/edit4.h"
#include "core/pokemon/gift5.h"
#include "core/util/bytes.h"

namespace pktool::legality {
namespace {

using pokemon::Pokemon;

constexpr uint16_t kShedinja = 292;
constexpr uint16_t kMaxEvTotal = 510;
constexpr uint16_t kMaxItemGen5 = 638;
constexpr uint8_t kBallDream = 25;
constexpr uint16_t kDitto = 132;

constexpr uint16_t kLocationTransfer4 = 30001;
constexpr uint16_t kLocationTradeNpc = 30002;
constexpr uint16_t kLocationLinkTrade = 30003;
constexpr uint16_t kLocationEntreeForest = 75;  // Dream World / Entree Forest captures
constexpr uint16_t kLocationDreamWorld = 30015;

void add(Report& r, Severity s, std::string text) {
    r.findings.push_back({s, std::move(text)});
}

bool hatched(const Pokemon& p) {
    return p.met.eggLocation != 0;
}

bool isTransferred(const Pokemon& p) {
    return isGen3Origin(p.originGame) || isGen4Origin(p.originGame);
}

bool isTransferLocation(uint16_t location) {
    return location >= kLocationTransfer4 && location <= 30013 && location != kLocationTradeNpc &&
           location != kLocationLinkTrade;
}

bool isEntreeForest(const Pokemon& p) {
    return p.met.metLocation == kLocationEntreeForest;
}

bool isDreamWorld(const Pokemon& p) {
    return isEntreeForest(p) || p.met.metLocation == kLocationDreamWorld;
}

// True if the species line has a Gen V encounter at the met location (any level).
bool speciesAtLocation5(const Pokemon& p) {
    uint8_t bit = data::versionBit5(p.originGame);
    for (uint16_t s = p.species; s != 0; s = data::preEvolution5(s)) {
        const data::Encounter4* begin;
        const data::Encounter4* end;
        data::encounters5(s, &begin, &end);
        for (const data::Encounter4* e = begin; e != end; ++e) {
            if ((e->versions & bit) && (e->kind == data::kEncounterRoamer ? roamer::metOk5(p.met.metLocation) : e->location == p.met.metLocation)) {
                return true;
            }
        }
    }
    return false;
}

void checkMoves5(const Pokemon& p, const data::Event5* card, const SpecialMoves* special, Report& r) {
    int known = 0;
    int specialUsed = 0;
    const bool bred = p.isEgg || hatched(p);
    const bool transferred = isTransferred(p);
    const uint16_t base = data::baseSpecies5(p.species);
    for (int i = 0; i < 4; ++i) {
        uint16_t move = p.moves[i];
        if (move == 0) continue;
        ++known;
        if (move > data::kMaxMoveGen5) {
            add(r, Severity::Invalid, "Move " + std::to_string(i + 1) + " does not exist in Gen V");
            continue;
        }
        for (int j = 0; j < i; ++j) {
            if (p.moves[j] == move) add(r, Severity::Invalid, std::string("Duplicate move: ") + data::moveName(move));
        }
        if (p.ppUps[i] > 3) add(r, Severity::Invalid, std::string("Too many PP Ups on ") + data::moveName(move));
        if (p.pp[i] > data::maxPp5(move, p.ppUps[i])) {
            add(r, Severity::Invalid, std::string("PP above maximum on ") + data::moveName(move));
        }

        bool learnable = false;
        bool levelTooLow = false;
        for (uint16_t s = p.species; s != 0 && !learnable; s = data::preEvolution5(s)) {
            const data::LearnHit e = data::findLearn5(s, move);
            if (e == nullptr) continue;
            if (e->sources & (data::kLearnMachine | data::kLearnTutor | data::kLearnEgg)) {
                learnable = true;
            } else if (e->minLevel <= p.level || (bred && s == base)) {
                learnable = true;
            } else {
                levelTooLow = true;
            }
        }
        // Transferred Pokémon keep what they learned in Gen III/IV.
        for (uint16_t s = p.species; transferred && s != 0 && !learnable; s = data::preEvolution4(s)) {
            if (data::findLearn4(s, move)) learnable = true;
            if (isGen3Origin(p.originGame) && data::findLearn3(s, move)) learnable = true;
        }
        if (learnable || (card && pokemon::gift5::cardHasMove(*card, move))) continue;
        std::string name = data::moveName(move);
        if (special && special->contains(move)) {  // a Dream World special move: only one fits
            if (++specialUsed > 1) add(r, Severity::Invalid, name + ": a Dream World Pokemon has only one special move");
            continue;
        }
        if (card == nullptr && (p.met.fatefulEncounter || isGen3Origin(p.originGame))) {
            add(r, Severity::Warning, name + ": event/Dream World/Gen III move, unchecked");
        } else if (levelTooLow) {
            add(r, Severity::Invalid, name + " is learned at a higher level");
        } else {
            add(r, Severity::Invalid, std::string(data::speciesName(p.species)) + " cannot learn " + name);
        }
    }
    if (known == 0 && !p.isEgg) add(r, Severity::Invalid, "No moves");
}

// What the matched Wonder Card fixes (PKHeX's PGF.IsMatchExact/IsMatchPartial).
void checkEvent5(const Pokemon& p, const data::Event5& card, Report& r) {
    namespace g = data::pgf;
    const uint8_t* c = card.pgf;
    add(r, Severity::Valid, std::string("Event: ") + card.title);
    if (c[g::kPidType] == 0 && p.shiny) add(r, Severity::Invalid, "This event is never shiny");
    if (c[g::kIsEgg] == 1) return;  // the rest belongs to whoever hatched it

    if (c[g::kPidType] == 2 && !p.shiny) add(r, Severity::Invalid, "This event is always shiny");
    if (p.met.ball != c[g::kBall]) add(r, Severity::Invalid, "Ball does not match the event");
    if (c[g::kNature] < 25 && p.nature != c[g::kNature]) add(r, Severity::Invalid, "Nature does not match the event");
    if (c[g::kGender] <= 1 && static_cast<uint8_t>(p.gender) != c[g::kGender]) {
        add(r, Severity::Invalid, "Gender does not match the event");
    }
    if (c[g::kOriginGame] != 0 ? p.originGame != c[g::kOriginGame] : !data::canReceiveEvent5(card, p.originGame)) {
        add(r, Severity::Invalid, "This event was not available in the origin game");
    }
    uint8_t language = c[g::kLanguage] ? c[g::kLanguage] : card.restrictLanguage;
    if (language != 0 && p.language != language) add(r, Severity::Invalid, "This event was distributed in another language");
    uint8_t abilityType = c[g::kAbilityType];
    if (abilityType == 2 && !p.hiddenAbility) add(r, Severity::Invalid, "This event has its hidden ability");
    if (abilityType <= 1 || abilityType == 3) {
        if (p.hiddenAbility) add(r, Severity::Invalid, "This event cannot have its hidden ability");
    }
    if (abilityType <= 1 && !p.hiddenAbility && p.ability != pokemon::edit4::abilityForSlot(p.species, abilityType)) {
        add(r, Severity::Invalid, std::string("Ability should be ") +
                                      data::abilityName(pokemon::edit4::abilityForSlot(p.species, abilityType)));
    }
}

}  // namespace

EncounterStatus findEncounter5(const Pokemon& p, const data::Encounter4** match, uint16_t* matchedSpecies) {
    uint8_t bit = data::versionBit5(p.originGame);
    if (bit == 0 || p.isEgg || hatched(p) || p.met.fatefulEncounter || isDreamWorld(p) ||
        findNPokemon5(p) != nullptr || p.met.metLocation == kLocationTradeNpc || p.met.metLocation == kLocationLinkTrade) {
        return EncounterStatus::NotApplicable;
    }
    for (uint16_t s = p.species; s != 0; s = data::preEvolution5(s)) {
        const data::Encounter4* begin;
        const data::Encounter4* end;
        data::encounters5(s, &begin, &end);
        const data::Encounter4* fallback = nullptr;
        for (const data::Encounter4* e = begin; e != end; ++e) {
            if (!(e->versions & bit)) continue;
            if (e->kind == data::kEncounterRoamer ? !roamer::metOk5(p.met.metLocation) : e->location != p.met.metLocation) continue;
            if (p.met.metLevel < e->minLevel || p.met.metLevel > e->maxLevel) continue;
            // Prefer an encounter that allows the Pokémon's hidden ability.
            bool hiddenOk = e->kind == data::kSlotHiddenGrotto || e->kind == data::kEncounterStaticHidden;
            if (p.hiddenAbility != hiddenOk) {
                if (!fallback) fallback = e;
                continue;
            }
            if (match) *match = e;
            if (matchedSpecies) *matchedSpecies = s;
            return EncounterStatus::Found;
        }
        if (fallback) {
            if (match) *match = fallback;
            if (matchedSpecies) *matchedSpecies = s;
            return EncounterStatus::Found;
        }
    }
    return EncounterStatus::NotFound;
}

Report checkGen5(const Pokemon& p) {
    Report r;
    const data::PersonalInfo* info = data::personal5(p.species);
    if (info == nullptr) {
        add(r, Severity::Invalid, "Species does not exist in Gen V");
        return r;
    }
    const bool transferred = isTransferred(p);
    const data::Event5* card = p.met.fatefulEncounter && !transferred ? pokemon::gift5::matchingEvent(p) : nullptr;
    if (!transferred && !isGen5Origin(p.originGame)) add(r, Severity::Invalid, "Unknown origin game");
    if (p.isEgg) checkEgg(p, r);

    auto growth = static_cast<data::GrowthRate>(info->growthRate);
    if (p.exp > data::expForLevel(growth, 100)) add(r, Severity::Invalid, "EXP above the level 100 maximum");
    if (!p.isEgg && p.met.metLevel > p.level) add(r, Severity::Invalid, "Met level is above the current level");

    // Where it comes from.
    const data::NPokemon5* nPokemon = transferred ? nullptr : findNPokemon5(p);
    SpecialMoves dwMoves;
    if (nPokemon) {
        checkNPokemon5(p, *nPokemon, r);
    } else if (transferred) {
        if (isTransferLocation(p.met.metLocation)) {
            add(r, Severity::Valid, "Transferred from Gen IV (Poke Transfer)");
        } else {
            add(r, Severity::Invalid, "Gen III/IV Pokemon must be met at Poke Transfer");
        }
    } else if (p.isEgg || hatched(p)) {
        if (!p.isEgg) add(r, Severity::Valid, "Hatched from an egg");
        if (card) checkEvent5(p, *card, r);
        if (!p.met.fatefulEncounter && p.met.ball != data::kBallPoke) {
            add(r, Severity::Invalid, "Hatched eggs must be in a Poke Ball in Gen V");
        }
    } else if (isEntreeForest(p)) {
        checkDreamWorld5(p, r, &dwMoves);
    } else if (p.met.metLocation == kLocationDreamRadar5) {
        checkDreamRadar5(p, r);
    } else if (p.met.fatefulEncounter) {
        if (card) {
            checkEvent5(p, *card, r);
        } else {
            add(r, Severity::Warning, "Gen V event not in the Wonder Card database");
        }
    } else if (isNpcTrade(p)) {
        checkNpcTrade(p, r);
    } else if (p.met.metLocation == kLocationLinkTrade) {
        add(r, Severity::Valid, "In-game trade");
    } else {
        const data::Encounter4* e = nullptr;
        uint16_t species = 0;
        std::string where = data::locationName5(p.met.metLocation);
        if (findEncounter5(p, &e, &species) == EncounterStatus::Found) {
            const char* type = e->kind == data::kSlotHiddenGrotto ? "Hidden Grotto"
                               : e->kind >= data::kEncounterStatic ? "static"
                                                                   : "wild";
            add(r, Severity::Valid, "Met: " + (e->kind == data::kEncounterRoamer ? std::string("roaming") : where) +
                                        " (" + type + ", " + data::speciesName(species) + ")");
            bool hiddenOk = e->kind == data::kSlotHiddenGrotto || e->kind == data::kEncounterStaticHidden;
            if (p.hiddenAbility && !hiddenOk) add(r, Severity::Invalid, "Hidden ability not available from this encounter");
            if (e->kind == data::kEncounterStaticHidden && !p.hiddenAbility) {
                add(r, Severity::Invalid, "This encounter always has its hidden ability");
            }
            if (e->kind == data::kSlotHiddenGrotto && p.shiny) add(r, Severity::Invalid, "Hidden Grotto Pokemon are never shiny");
            bool correlated = e->kind < data::kEncounterStatic ? e->kind != data::kSlotHiddenGrotto
                                                               : e->kind == data::kEncounterStatic;
            if (correlated && !pokemon::edit4::wildPidCorrelation5(p.pid, p.trainer.tid, p.trainer.sid)) {
                add(r, Severity::Invalid, "PID not from the game's RNG (bit 31)");
            }
        } else if (speciesAtLocation5(p)) {
            add(r, Severity::Warning, "Unusual met level " + std::to_string(p.met.metLevel) + " at " + where);
        } else {
            add(r, Severity::Invalid, std::string(data::speciesName(p.species)) + " cannot be found at " + where);
        }
    }

    if (isTherianForme5(p) && p.met.metLocation != kLocationDreamRadar5) {
        add(r, Severity::Invalid, "The Therian Forme only comes from the Dream Radar in Gen V");
    }

    // PID-derived traits. Transferred Gen III/IV Pokémon keep their old rules
    // (nature = PID % 25, ability slot = bit 0); Gen V uses bit 16.
    if (p.species != kShedinja) {
        if (!pokemon::edit4::canHaveGender(p.species, p.gender)) {
            add(r, Severity::Invalid, "Gender impossible for this species");
        } else if (pokemon::edit4::genderFromPid(p.species, p.pid) != p.gender) {
            add(r, Severity::Invalid, "Gender does not match the PID");
        }
    }
    if (transferred && p.nature != pokemon::natureFromPid(p.pid)) {
        add(r, Severity::Invalid, "Nature does not match the PID (transferred Pokemon)");
    }
    if (p.hiddenAbility) {
        if (!hasHiddenAbility5(*info)) {
            add(r, Severity::Invalid, "This species has no hidden ability");
        } else if (p.ability != hiddenAbilityOf5(p, *info)) {
            add(r, Severity::Invalid, std::string("Hidden ability should be ") + data::abilityName(hiddenAbilityOf5(p, *info)));
        }
        if (transferred) add(r, Severity::Invalid, "Transferred Pokemon cannot have a hidden ability");
    } else if (!isTherianForme5(p)) {  // (reported by the Dream Radar check)
        uint8_t slot = static_cast<uint8_t>(transferred ? p.pid & 1 : (p.pid >> 16) & 1);
        uint16_t expected = pokemon::edit4::abilityForSlot(p.species, slot);
        bool either = p.ability == info->abilities[0] || p.ability == info->abilities[1];
        // Cards with a fixed PID set the ability directly (checked by checkEvent5).
        bool eventOk = card && util::readU32(card->pgf + data::pgf::kPid) != 0 && either;
        if (p.ability != expected && !(isGen3Origin(p.originGame) && either) && !eventOk) {
            add(r, Severity::Invalid, std::string("Ability should be ") + data::abilityName(expected));
        }
    }

    // Evolution level.
    uint8_t evolveLevel = data::evolveLevel5(p.species);
    if (!p.isEgg && p.level < evolveLevel && hatched(p) && !p.met.fatefulEncounter) {
        add(r, Severity::Invalid, std::string(data::speciesName(p.species)) + " only evolves at level " +
                                      std::to_string(evolveLevel));
    }

    int evTotal = 0;
    for (int s = 0; s < pokemon::kStatCount; ++s) evTotal += p.evs[s];
    if (evTotal > kMaxEvTotal) add(r, Severity::Invalid, "EV total above 510");
    if (p.heldItem > kMaxItemGen5) add(r, Severity::Invalid, "Held item does not exist in Gen V");

    checkMoves5(p, card, isEntreeForest(p) ? &dwMoves : nullptr, r);

    // Gen V-only fields.
    if (!nPokemon && p.nSparkle) add(r, Severity::Invalid, "N's Pokemon flag on a Pokemon that is not one of N's");
    if (p.pokeStarFame != 0) {
        if (p.isEgg) {
            add(r, Severity::Invalid, "An egg cannot have Pokestar Studios fame");
        } else if (p.species == kDitto) {
            add(r, Severity::Invalid, "Ditto cannot take part in Pokestar Studios");
        } else if (p.pokeStarFame % 25 != 0 && p.pokeStarFame % 25 != 5) {
            add(r, Severity::Invalid, "Impossible Pokestar Studios fame value");
        }
    }

    uint8_t ball = p.met.ball;
    if (ball == 0 || ball > kBallDream) add(r, Severity::Invalid, "Invalid ball");
    if (ball == data::kBallCherish && !p.met.fatefulEncounter) add(r, Severity::Invalid, "Cherish Ball outside an event");
    if (ball == kBallDream && !isDreamWorld(p) && !p.met.fatefulEncounter) {
        add(r, Severity::Invalid, "Dream Ball outside the Dream World");
    }
    checkRibbons(p, r);
    checkTrainer(p, r);

    if (r.overall() == Severity::Valid && r.findings.empty()) add(r, Severity::Valid, "No problems found");
    return r;
}

}  // namespace pktool::legality
