#include "core/legality/pokewalker.h"

#include "core/data/learn4.h"
#include "core/data/moves.h"
#include "core/data/names.h"
#include "core/data/personal.h"
#include "core/legality/pid_iv.h"
#include "core/pokemon/edit4.h"

namespace pktool::legality {
namespace {

using pokemon::Gender;
using pokemon::Pokemon;

constexpr uint16_t kAzurill = 298;
constexpr uint16_t kMarill = 183;
constexpr uint16_t kAzumarill = 184;
constexpr uint16_t kFroslass = 478;
constexpr uint8_t kRatioAzurill = 191;  // 3 female : 1 male
constexpr uint8_t kRatioSnorunt = 127;

void add(Report& r, Severity s, std::string text) {
    r.findings.push_back({s, std::move(text)});
}

uint8_t genderCode(Gender g) {
    return static_cast<uint8_t>(g);
}

bool genderMatches(const Pokemon& p, const data::WalkerSlot& slot) {
    if (genderCode(p.gender) == slot.gender) return true;
    // A female Azurill whose evolution has a 50:50 ratio can read as male.
    return slot.species == kAzurill && p.species != kAzurill && slot.gender == 1 && (p.pid & 0xFF) < kRatioAzurill;
}

}  // namespace

uint8_t pokewalkerGenderRatio(uint16_t species) {
    if (species == kFroslass) return kRatioSnorunt;  // evolved from a 50:50 Snorunt line
    const data::PersonalInfo* info = data::personal4(species);
    return info ? info->genderRatio : data::kGenderRatioGenderless;
}

uint32_t pokewalkerPid(uint16_t tid, uint16_t sid, uint32_t nature, uint8_t gender, uint8_t ratio) {
    if (nature >= 24) nature = 0;
    // The top byte keeps the PID from ever being shiny.
    uint32_t pid = ((static_cast<uint32_t>(tid ^ sid) >> 8) ^ 0xFFu) << 24;
    pid += nature - (pid % 25);                    // wraps like the game's unsigned arithmetic
    if (ratio == 0 || ratio >= 0xFE) return pid;  // no gender to match

    const uint32_t pidGender = (pid & 0xFF) < ratio ? 1 : 0;
    if (gender == pidGender) return pid;
    if (gender == 0) {
        pid += (((ratio - (pid & 0xFF)) / 25) + 1) * 25;
        if ((nature & 1) != (pid & 1)) pid += 25;
    } else {
        pid -= ((((pid & 0xFF) - ratio) / 25) + 1) * 25;
        if ((nature & 1) != (pid & 1)) pid -= 25;
    }
    return pid;
}

bool isPokewalkerPid(const Pokemon& p) {
    // Bits 8-23 are all zero or all one.
    const uint32_t mid = p.pid & 0x00FFFF00u;
    if (mid != 0 && mid != 0x00FFFF00u) return false;
    const uint32_t nature = p.pid % 25;
    if (nature == 24) return false;  // Quirky cannot be produced

    const uint8_t gender = genderCode(p.gender);
    if (pokewalkerPid(p.trainer.tid, p.trainer.sid, nature, gender, pokewalkerGenderRatio(p.species)) == p.pid) return true;
    // Marill and Azumarill evolved from a female Azurill that became male.
    if (gender == 0 && (p.species == kMarill || p.species == kAzumarill) && (p.pid & 0xFF) < kRatioAzurill) {
        return pokewalkerPid(p.trainer.tid, p.trainer.sid, nature, 1, kRatioAzurill) == p.pid;
    }
    return false;
}

bool claimsPokewalker(const Pokemon& p) {
    return p.met.metLocation == kLocationPokewalker && !p.isEgg && p.met.eggLocation == 0 && isGen4Origin(p.originGame);
}

EncounterStatus findPokewalker(const Pokemon& p, WalkerMatch* match) {
    for (uint16_t s = p.species; s != 0; s = data::preEvolution4(s)) {
        for (int course = 0; course < data::kWalkerCourses; ++course) {
            const data::WalkerSlot* slots = data::walkerSlots(course);
            for (int i = 0; i < data::kWalkerSlotsPerCourse; ++i) {
                const data::WalkerSlot& slot = slots[i];
                if (slot.species != s || slot.level != p.met.metLevel || !genderMatches(p, slot)) continue;
                if (match != nullptr) {
                    match->course = course;
                    match->slot = &slot;
                    match->species = s;
                }
                return EncounterStatus::Found;
            }
        }
    }
    return EncounterStatus::NotFound;
}

bool applyPokewalkerOrigin(Pokemon& p, uint8_t abilitySlot, std::vector<std::string>& changes) {
    if (p.shiny || p.nature >= 24) return false;
    const uint8_t evolveLevel = data::evolveLevel4(p.species);
    const data::WalkerSlot* best = nullptr;
    int bestCourse = -1;
    uint16_t bestSpecies = 0;
    int bestScore = 1 << 30;
    for (uint16_t s = p.species; s != 0; s = data::preEvolution4(s)) {
        if (s != p.species && p.level < evolveLevel) break;  // could not have evolved yet
        for (int course = 0; course < data::kWalkerCourses; ++course) {
            const data::WalkerSlot* slots = data::walkerSlots(course);
            for (int i = 0; i < data::kWalkerSlotsPerCourse; ++i) {
                const data::WalkerSlot& slot = slots[i];
                if (slot.species != s || slot.level > p.level || slot.gender != genderCode(p.gender)) continue;
                const uint32_t pid = pokewalkerPid(p.trainer.tid, p.trainer.sid, p.nature, slot.gender,
                                                   pokewalkerGenderRatio(p.species));
                if (pokemon::edit4::abilityForSlot(p.species, static_cast<uint8_t>(pid & 1)) !=
                        pokemon::edit4::abilityForSlot(p.species, abilitySlot) ||
                    pokemon::edit4::genderFromPid(p.species, pid) != p.gender) {
                    continue;
                }
                int score = (s == p.species ? 0 : 100) + (p.level - slot.level);
                if (score < bestScore) {
                    bestScore = score;
                    best = &slot;
                    bestCourse = course;
                    bestSpecies = s;
                }
            }
        }
    }
    if (best == nullptr) return false;
    if (p.originGame != version::kHeartGold && p.originGame != version::kSoulSilver) {
        p.originGame = version::kHeartGold;
        changes.push_back("Origin game set to HeartGold");
    }
    p.met.metLocation = kLocationPokewalker;
    p.met.metLevel = best->level;
    p.met.eggLocation = 0;
    p.met.ball = data::kBallPoke;
    changes.push_back(std::string("Caught on the Pokewalker: ") + data::walkerCourseName(bestCourse) + " (as " +
                      data::speciesName(bestSpecies) + ")");
    return true;
}

bool walkerSlotHasMove(const data::WalkerSlot* slot, uint16_t move) {
    if (slot == nullptr) return false;
    for (uint16_t m : slot->moves) {
        if (m == move) return true;
    }
    return false;
}

void checkPokewalker(const Pokemon& p, Report& r, WalkerMatch* matchOut) {
    if (p.originGame != version::kHeartGold && p.originGame != version::kSoulSilver) {
        add(r, Severity::Invalid, "The Pokewalker only exists for HeartGold/SoulSilver");
        return;
    }
    if (p.met.fatefulEncounter) add(r, Severity::Invalid, "Pokewalker Pokemon are not fateful encounters");

    WalkerMatch match;
    if (findPokewalker(p, &match) != EncounterStatus::Found) {
        add(r, Severity::Invalid, std::string("No Pokewalker course offers ") + data::speciesName(p.species) +
                                      " at Lv. " + std::to_string(p.met.metLevel) + " with this gender");
        return;
    }
    if (matchOut != nullptr) *matchOut = match;
    add(r, Severity::Valid, std::string("Met: Pokewalker, ") + data::walkerCourseName(match.course) + " (" +
                                data::speciesName(match.species) + ")");

    if (match.species != p.species && p.level < data::evolveLevel4(p.species)) {
        add(r, Severity::Invalid, std::string(data::speciesName(p.species)) + " only evolves at level " +
                                      std::to_string(data::evolveLevel4(p.species)));
    }
    if (p.met.ball != data::kBallPoke) add(r, Severity::Invalid, "Pokewalker Pokemon are in a Poke Ball");
    if (p.shiny) {
        add(r, Severity::Invalid, "Pokewalker Pokemon are never shiny");
    } else if (!isPokewalkerPid(p)) {
        add(r, Severity::Invalid, "PID is not a Pokewalker PID");
    } else {
        add(r, Severity::Valid, "PID: Pokewalker");
    }
    if (!ivsFromConsecutiveCalls(p.ivs)) add(r, Severity::Warning, "IVs are not two consecutive RNG outputs");
}

}  // namespace pktool::legality
