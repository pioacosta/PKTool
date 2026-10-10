#pragma once

#include <cstdint>
#include <string>

namespace pktool::pokemon {

// Stat order used everywhere in PKTool. It matches the order the games store
// IVs/EVs in and the order nature modifiers apply to (Atk, Def, Spe, SpA, SpD).
enum Stat : uint8_t { kHp = 0, kAtk, kDef, kSpe, kSpA, kSpD, kStatCount };

enum class Gender : uint8_t { Male = 0, Female = 1, Genderless = 2 };

struct Date {
    uint8_t year = 0;  // years since 2000
    uint8_t month = 0;
    uint8_t day = 0;
};

struct TrainerInfo {
    std::string name;
    uint16_t tid = 0;
    uint16_t sid = 0;
    Gender gender = Gender::Male;
};

struct MetInfo {
    uint16_t metLocation = 0;
    uint16_t eggLocation = 0;
    uint8_t metLevel = 0;
    uint8_t ball = 0;
    uint8_t encounterType = 0;
    bool fatefulEncounter = false;
    Date metDate;
    Date eggDate;
};

// Generation-independent view of a Pokémon.
//
// This is NOT a copy of any binary format. Codecs (e.g. core/pokemon/pk4.h)
// convert raw entities to this view and apply edits back onto the raw bytes,
// so fields PKTool does not model are preserved.
struct Pokemon {
    uint16_t species = 0;
    uint8_t form = 0;
    std::string nickname;
    bool isNicknamed = false;
    bool isEgg = false;

    uint32_t pid = 0;
    uint32_t exp = 0;
    uint8_t level = 1;
    uint8_t nature = 0;
    uint16_t ability = 0;
    Gender gender = Gender::Male;
    bool shiny = false;

    uint16_t heldItem = 0;
    uint16_t moves[4] = {0, 0, 0, 0};
    uint8_t pp[4] = {0, 0, 0, 0};
    uint8_t ppUps[4] = {0, 0, 0, 0};

    uint8_t ivs[kStatCount] = {0, 0, 0, 0, 0, 0};
    uint8_t evs[kStatCount] = {0, 0, 0, 0, 0, 0};

    uint8_t friendship = 0;
    uint8_t pokerus = 0;
    uint8_t language = 0;
    uint8_t originGame = 0;

    TrainerInfo trainer;
    MetInfo met;

    // Entity format: 1 = PK1 (R/B/Y), 2 = PK2 (G/S/C), 3 = PK3 (R/S/E/FR/LG),
    // 4 = PK4 (D/P/Pt/HG/SS), 5 = PK5 (B/W/B2/W2). Gen V stores the nature on
    // its own (not PID % 25) and has hidden abilities.
    // Formats 1 and 2 have no PID, nature, ability, ball or SID: ivs[] holds
    // the 0-15 DVs and statExp[] the 16-bit Stat Exp (see gb_common.h),
    // shininess and Gen II gender come from the DVs. Gen I has no held item,
    // gender or friendship; its catch rate byte lives in catchRate. A Gen II
    // Crystal Pokemon's caught data uses met.metLocation, met.metLevel,
    // trainer.gender and met.encounterType (time of day 1-3, 0 = none).
    uint8_t format = 4;
    uint16_t statExp[kStatCount] = {0, 0, 0, 0, 0, 0};
    uint8_t catchRate = 0;
    uint8_t types[2] = {0, 0};  // format 1: the types stored in the entity (the games' own IDs)
    bool hiddenAbility = false;

    // Gen V only: the N's Pokémon flag (B2/W2 N's Sparkling Pokémon) and the
    // Pokéstar Studios fame counter.
    bool nSparkle = false;
    uint8_t pokeStarFame = 0;

    // Snapshot of the entity's ribbon bits (see ribbons.h), used by the
    // legality checker and LEGALIZE. ribbonFormat is 0 when unknown (a Pokémon
    // not decoded from raw data: applying it leaves the ribbons alone), 3 for
    // the Gen III layout (word 0) and 4 for the Gen IV/V layout (three words).
    uint8_t ribbonFormat = 0;
    uint32_t ribbonWords[3] = {0, 0, 0};
};

// Field-by-field equality. Keep it in step with the struct above: the UI uses it to know a
// Pokémon did not change (the legality badge is only recomputed when it did).
inline bool operator==(const Date& a, const Date& b) {
    return a.year == b.year && a.month == b.month && a.day == b.day;
}
inline bool operator==(const TrainerInfo& a, const TrainerInfo& b) {
    return a.tid == b.tid && a.sid == b.sid && a.gender == b.gender && a.name == b.name;
}
inline bool operator==(const MetInfo& a, const MetInfo& b) {
    return a.metLocation == b.metLocation && a.eggLocation == b.eggLocation && a.metLevel == b.metLevel &&
           a.ball == b.ball && a.encounterType == b.encounterType && a.fatefulEncounter == b.fatefulEncounter &&
           a.metDate == b.metDate && a.eggDate == b.eggDate;
}
inline bool operator==(const Pokemon& a, const Pokemon& b) {
    for (int i = 0; i < 4; ++i) {
        if (a.moves[i] != b.moves[i] || a.pp[i] != b.pp[i] || a.ppUps[i] != b.ppUps[i]) return false;
    }
    for (int i = 0; i < kStatCount; ++i) {
        if (a.ivs[i] != b.ivs[i] || a.evs[i] != b.evs[i]) return false;
    }
    for (int i = 0; i < 3; ++i) {
        if (a.ribbonWords[i] != b.ribbonWords[i]) return false;
    }
    return a.species == b.species && a.form == b.form && a.isNicknamed == b.isNicknamed && a.isEgg == b.isEgg &&
           a.pid == b.pid && a.exp == b.exp && a.level == b.level && a.nature == b.nature && a.ability == b.ability &&
           a.gender == b.gender && a.shiny == b.shiny && a.heldItem == b.heldItem && a.friendship == b.friendship &&
           a.pokerus == b.pokerus && a.language == b.language && a.originGame == b.originGame &&
           a.format == b.format && a.hiddenAbility == b.hiddenAbility && a.nSparkle == b.nSparkle &&
           a.pokeStarFame == b.pokeStarFame && a.ribbonFormat == b.ribbonFormat && a.trainer == b.trainer &&
           a.met == b.met && a.nickname == b.nickname;
}
inline bool operator!=(const Pokemon& a, const Pokemon& b) { return !(a == b); }

// Shiny rule for Gen III-V: (TID ^ SID ^ PID_hi ^ PID_lo) < 8.
inline bool isShiny(uint32_t pid, uint16_t tid, uint16_t sid) {
    uint32_t xorValue = tid ^ sid ^ (pid >> 16) ^ (pid & 0xFFFF);
    return xorValue < 8;
}

// Gen III/IV nature rule: PID % 25.
inline uint8_t natureFromPid(uint32_t pid) {
    return static_cast<uint8_t>(pid % 25);
}

}  // namespace pktool::pokemon
