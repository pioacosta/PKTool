#include "ds/app/app.h"

#include "ds/app/ui_common.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "core/data/events3.h"
#include "core/data/gen12.h"
#include "core/data/events4.h"
#include "core/data/gen3.h"
#include "core/data/language.h"
#include "core/data/events5.h"
#include "core/data/moves.h"
#include "core/data/names.h"
#include "core/data/personal.h"
#include "core/data/sorted_names.h"
#include "core/io/file_io.h"
#include "ds/gfx/cjk_font.h"
#include "core/legality/legalize.h"
#include "core/pokemon/cxd3.h"
#include "core/legality/encounters_gb.h"
#include "core/pokemon/edit4.h"
#include "core/pokemon/edit_gb.h"
#include "core/pokemon/edit_origin.h"
#include "core/pokemon/gift3.h"
#include "core/pokemon/gift4.h"
#include "core/pokemon/gb_common.h"
#include "core/pokemon/gift5.h"
#include "core/pokemon/stats.h"
#include "core/pokemon/transfer34.h"
#include "core/pokemon/transfer45.h"
#include "core/saves/save_commit.h"
#include "core/showdown/import4.h"
#include "core/text/gen12_text.h"

namespace pktool::ui {
using namespace detail;
namespace {

namespace pk4 = pokemon::pk4;
namespace edit4 = pokemon::edit4;
namespace editgb = pokemon::editgb;
namespace editorigin = pokemon::editorigin;
const char* const kStatNames[] = {"HP", "Atk", "Def", "Spe", "SpA", "SpD"};

const char* genderText(pokemon::Gender gender) {
    switch (gender) {
        case pokemon::Gender::Male:
            return "♂ Male";
        case pokemon::Gender::Female:
            return "♀ Female";
        case pokemon::Gender::Genderless:
            break;
    }
    return "Genderless";
}

// A neutral capsule emblem used when no sprite pack is installed.
void drawBallEmblem(Canvas& c, int cx, int cy, int r, Color top, Color bottom) {
    c.fillCircle(cx, cy, r, bottom);
    for (int dy = -r; dy < 0; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            if (dx * dx + dy * dy <= r * r + r / 2) {
                c.pixel(cx + dx, cy + dy, top);
            }
        }
    }
    c.hline(cx - r, cy, 2 * r + 1, kTitleBar);
    c.circle(cx, cy, r, kTitleBar);
    c.fillCircle(cx, cy, std::max(1, r / 3), kText);
    c.circle(cx, cy, std::max(1, r / 3), kTitleBar);
}

// Stable per-species colour for placeholder icons.
Color speciesColor(uint16_t species) {
    uint32_t h = species * 2654435761u;
    return rgb(static_cast<uint8_t>(90 + (h >> 24) % 140), static_cast<uint8_t>(90 + (h >> 16) % 140),
               static_cast<uint8_t>(90 + (h >> 8) % 140));
}

// ----- Editor fields -------------------------------------------------------------

enum Field {
    kFieldNickname,
    kFieldSpecies,
    kFieldLevel,
    kFieldNature,
    kFieldShiny,
    kFieldGender,
    kFieldAbility,
    kFieldItem,
    kFieldBall,
    kFieldMove1,
    kFieldMove4 = kFieldMove1 + 3,
    kFieldIv1,
    kFieldIv6 = kFieldIv1 + 5,
    kFieldEv1,
    kFieldEv6 = kFieldEv1 + 5,
    kFieldCatchRate,  // Gen I only
    kFieldFriendship,
    kFieldOtName,
    kFieldOtGender,
    kFieldTid,
    kFieldSid,
    kFieldMetLocation,
    kFieldMetLevel,
    kFieldMetTime,      // Crystal's caught data only
    kFieldMetDate,      // Gen IV/V only
    kFieldEggLocation,  // Gen IV/V only
    kFieldEggDate,      // Gen IV/V only
    kFieldFateful,
    kFieldPokerusStrain,
    kFieldPokerusDays,
    kFieldShowdown,  // action: export the set to /PKTOOL/showdown/export.txt
    kFieldCount,
};

std::string fieldLabel(int field) {
    if (field >= kFieldMove1 && field <= kFieldMove4) return "Move " + std::to_string(field - kFieldMove1 + 1);
    if (field >= kFieldIv1 && field <= kFieldIv6) return std::string("IV ") + kStatNames[field - kFieldIv1];
    if (field >= kFieldEv1 && field <= kFieldEv6) return std::string("EV ") + kStatNames[field - kFieldEv1];
    switch (field) {
        case kFieldNickname: return "Nickname";
        case kFieldSpecies: return "Species";
        case kFieldLevel: return "Level";
        case kFieldNature: return "Nature";
        case kFieldShiny: return "Shiny";
        case kFieldGender: return "Gender";
        case kFieldAbility: return "Ability";
        case kFieldItem: return "Held item";
        case kFieldBall: return "Ball";
        case kFieldCatchRate: return "Catch rate";
        case kFieldMetTime: return "Met time";
        case kFieldFriendship: return "Friendship";  // "Egg cycles" for eggs (see drawEdit)
        case kFieldOtName: return "OT name";
        case kFieldOtGender: return "OT gender";
        case kFieldTid: return "TID";
        case kFieldSid: return "SID";
        case kFieldMetLocation: return "Met location";
        case kFieldMetLevel: return "Met level";
        case kFieldMetDate: return "Met date";
        case kFieldEggLocation: return "Egg location";
        case kFieldEggDate: return "Egg date";
        case kFieldFateful: return "Fateful";
        case kFieldPokerusStrain: return "Pokérus";
        case kFieldPokerusDays: return "Days left";  // below "Pokérus"
        case kFieldShowdown: return "Showdown";
    }
    return "";
}

// Labels that depend on the format: DVs and Stat Exp in the Game Boy games.
std::string fieldLabelFor(const pokemon::Pokemon& p, int field) {
    if (p.format <= 2) {
        static const char* const kGbStats[] = {"HP", "Atk", "Def", "Spe", "Spc", "Spc"};
        if (field >= kFieldIv1 && field <= kFieldIv6) return std::string("DV ") + kGbStats[field - kFieldIv1];
        if (field >= kFieldEv1 && field <= kFieldEv6) return std::string("Stat Exp ") + kGbStats[field - kFieldEv1];
    }
    if (field == kFieldFriendship && p.isEgg) return "Egg cycles";
    return fieldLabel(field);
}

bool hasCaughtData(const pokemon::Pokemon& p) {
    return p.format == 2 && (p.met.metLevel != 0 || p.met.metLocation != 0 || p.met.encounterType != 0);
}

// Gen I/II: no nature, ability, ball, SID or fateful flag; the HP DV follows
// the others and Special is one stat; Crystal alone keeps caught data.
bool gbFieldVisible(const pokemon::Pokemon& p, int field) {
    const bool gen1 = p.format == 1;
    const bool crystal = p.format == 2 && (p.originGame == 41 || hasCaughtData(p));
    if (field >= kFieldMove1 && field <= kFieldMove4) return true;
    if (field >= kFieldIv1 && field <= kFieldIv6) {
        const int stat = field - kFieldIv1;
        return stat != pokemon::kHp && stat != pokemon::kSpD;
    }
    if (field >= kFieldEv1 && field <= kFieldEv6) return field - kFieldEv1 != pokemon::kSpD;
    switch (field) {
        case kFieldNickname:
        case kFieldSpecies:
        case kFieldLevel:
        case kFieldOtName:
        case kFieldTid:
        case kFieldShowdown:
            return true;
        case kFieldShiny:
        case kFieldGender:
        case kFieldItem:
        case kFieldFriendship:
        case kFieldPokerusStrain:
        case kFieldPokerusDays:
            return !gen1;
        case kFieldCatchRate:
            return gen1;
        case kFieldOtGender:
            return crystal;
        case kFieldMetLocation:
        case kFieldMetLevel:
        case kFieldMetTime:
            return crystal && !p.isEgg;
        default:
            return false;
    }
}

// Fields the Pokémon's format really has (Gen III has no met date nor egg
// data; eggs have not been met yet).
bool fieldVisible(const pokemon::Pokemon& p, int field) {
    if (p.format <= 2) return gbFieldVisible(p, field);
    switch (field) {
        case kFieldCatchRate:
        case kFieldMetTime:
            return false;
        case kFieldMetDate:
            return editorigin::hasMetDate(p.format) && !p.isEgg;
        case kFieldEggLocation:
        case kFieldEggDate:
            return editorigin::hasEggData(p.format);
        case kFieldMetLocation:
        case kFieldMetLevel:
            return !(p.isEgg && p.format >= 4);
        default:
            return true;
    }
}

// Next visible field from `field` in direction `step` (+1 / -1), wrapping.
int nextVisibleField(const pokemon::Pokemon& p, int field, int step) {
    for (int i = 0; i < kFieldCount; ++i) {
        field = wrap(field + step, kFieldCount);
        if (fieldVisible(p, field)) return field;
    }
    return field;
}

std::string pokerusDaysText(uint8_t value) {
    if (editorigin::pokerusStrain(value) == 0) return "-";
    uint8_t days = editorigin::pokerusDays(value);
    if (days == 0) return "Cured";
    return std::to_string(days) + (days == 1 ? " day" : " days");
}

std::string fieldValue(const pokemon::Pokemon& p, int field) {
    if (field >= kFieldMove1 && field <= kFieldMove4) return data::moveName(p.moves[field - kFieldMove1]);
    if (field >= kFieldIv1 && field <= kFieldIv6) return std::to_string(p.ivs[field - kFieldIv1]);
    if (field >= kFieldEv1 && field <= kFieldEv6) {
        return std::to_string(p.format <= 2 ? p.statExp[field - kFieldEv1] : p.evs[field - kFieldEv1]);
    }
    switch (field) {
        case kFieldCatchRate: return std::to_string(p.catchRate);
        case kFieldMetTime: {
            static const char* const kTimes[] = {"None", "Morning", "Day", "Night"};
            return kTimes[p.met.encounterType & 3];
        }
        case kFieldNickname: return p.nickname;
        case kFieldSpecies: return data::speciesName(p.species);
        case kFieldLevel: return std::to_string(p.level);
        case kFieldNature: return data::natureName(p.nature);
        case kFieldShiny: return p.shiny ? "Yes" : "No";
        case kFieldGender: return genderText(p.gender);
        case kFieldAbility: return data::abilityName(p.ability);
        case kFieldItem: return data::itemName(p.heldItem);
        case kFieldBall: return data::ballName(p.met.ball);
        case kFieldFriendship: return std::to_string(p.friendship);
        case kFieldOtName: return p.trainer.name;
        case kFieldOtGender: return p.trainer.gender == pokemon::Gender::Female ? "Female" : "Male";
        case kFieldTid: return std::to_string(p.trainer.tid);
        case kFieldSid: return std::to_string(p.trainer.sid);
        case kFieldMetLocation: return editorigin::locationText(p.format, p.met.metLocation);
        case kFieldMetLevel: return std::to_string(p.met.metLevel);
        case kFieldMetDate: return editorigin::dateText(p.met.metDate);
        case kFieldEggLocation: return editorigin::locationText(p.format, p.met.eggLocation);
        case kFieldEggDate: return editorigin::dateText(p.met.eggDate);
        case kFieldFateful: return p.met.fatefulEncounter ? "Yes" : "No";
        case kFieldPokerusStrain: {
            uint8_t strain = editorigin::pokerusStrain(p.pokerus);
            return strain == 0 ? "None" : "Strain " + std::to_string(strain);
        }
        case kFieldPokerusDays: return pokerusDaysText(p.pokerus);
        case kFieldShowdown: return "A: export set";
    }
    return "";
}

// Gen I/II summary text: the types of the species and the four DVs.
std::string gbTypeText(const pokemon::Pokemon& p) {
    uint8_t types[2] = {p.types[0], p.types[1]};
    if (p.format == 2) {
        if (const data::PersonalGb* info = data::personalGb2(p.species, p.originGame == 41)) {
            types[0] = info->types[0];
            types[1] = info->types[1];
        }
    }
    std::string text = data::typeNameGb(types[0]);
    if (types[1] != types[0]) text += std::string("/") + data::typeNameGb(types[1]);
    return text;
}

std::string gbDvText(const pokemon::Pokemon& p) {
    return "A" + std::to_string(p.ivs[pokemon::kAtk]) + " D" + std::to_string(p.ivs[pokemon::kDef]) + " S" +
           std::to_string(p.ivs[pokemon::kSpe]) + " C" + std::to_string(p.ivs[pokemon::kSpA]);
}

// Search effort for PID changes made while editing (kept short so the UI
// stays responsive on a DS) and for LEGALIZE.
constexpr int kEditEffort = 1;
constexpr int kLegalizeEffort = 4;
// Event picker row that is not a Wonder Card of the table: the Gen IV Pokémon Ranger Manaphy egg.
constexpr int kManaphyEggCard = -2;
// Event picker rows for cards read from /PKTOOL/events/ are kFileCardBase + index into fileCards_.
constexpr int kFileCardBase = 0x10000;
constexpr int kMaxCardFiles = 64;
constexpr int kColoXdCard = 1000;  // Gen III: card = kColoXdCard + index in data::gc3()

uint8_t minCreateLevel(uint8_t format, uint16_t species) {
    if (format <= 2) return legality::minCreateLevelGb(format, species);
    if (format == 3) return legality::minCreateLevel3(species);
    return format >= 5 ? legality::minCreateLevel5(species) : legality::minCreateLevel4(species);
}

// Fields that choose a name from a long list: they step in alphabetical order
// (L/R jump to the next initial letter instead of +-10).
bool isNameField(int field) {
    return (field >= kFieldMove1 && field <= kFieldMove4) || field == kFieldSpecies || field == kFieldNature ||
           field == kFieldItem || field == kFieldBall;
}

// Next value of a name field in alphabetical order: one step, or (letterJump)
// the first value of the next/previous initial letter. `delta` gives the direction.
uint16_t nextName(data::NameKind kind, uint8_t format, uint16_t current, int delta, bool letterJump) {
    if (letterJump) return data::jumpSorted(kind, format, current, delta > 0 ? 1 : -1);
    return data::stepSorted(kind, format, current, delta > 0 ? 1 : -1);
}

// Changes the PID-derived traits keeping the Pokémon consistent with its
// origin's RNG (IVs may move slightly). Returns a status message.
std::string rerollTraits(pokemon::Pokemon& p, uint8_t nature, bool shiny, pokemon::Gender gender, uint8_t slot) {
    if (p.format >= 5) {  // Gen V: no PID/IV correlation checked yet; keep the IVs
        edit4::Result r = nature != p.nature               ? edit4::setNature(p, nature)
                          : slot != edit4::abilitySlot(p) ? edit4::setAbilitySlot(p, slot)
                          : shiny != p.shiny ? edit4::setShiny(p, shiny)
                                             : edit4::setGender(p, gender);
        return r == edit4::Result::Ok ? "" : "No valid PID for that combination";
    }
    uint8_t before[pokemon::kStatCount];
    std::copy(p.ivs, p.ivs + pokemon::kStatCount, before);
    if (!legality::rerollPidGen4(p, nature, shiny, gender, slot, kEditEffort)) {
        return "No valid PID for that combination";
    }
    return std::equal(before, before + pokemon::kStatCount, p.ivs) ? "" : "IVs adjusted to keep it legal";
}

// Gen I/II: the fields whose rules differ (no PID: DVs decide shininess and gender).
// Returns true if the field was handled; the others share the common code below.
bool adjustFieldGb(pokemon::Pokemon& p, int field, int delta, bool letterJump, std::string& message) {
    if (field >= kFieldMove1 && field <= kFieldMove4) {
        const int index = field - kFieldMove1;
        uint16_t move = p.moves[index];
        const int tries = static_cast<int>(data::sortedIds(data::NameKind::Move, p.format).size());
        for (int t = 0; t < tries; ++t) {
            move = nextName(data::NameKind::Move, p.format, move, delta, letterJump && t == 0);
            if (editgb::setMove(p, index, move) == editgb::Result::Ok) break;
        }
        return true;
    }
    if (field >= kFieldIv1 && field <= kFieldIv6) {
        const int s = field - kFieldIv1;
        editgb::setDv(p, static_cast<pokemon::Stat>(s), static_cast<uint8_t>(clampInt(p.ivs[s] + delta, 0, 15)));
        return true;
    }
    if (field >= kFieldEv1 && field <= kFieldEv6) {
        const int s = field - kFieldEv1;
        // One step is 256 (a full square-root step of the games' Stat Exp), L/R jump 4096.
        const int unit = (delta >= 10 || delta <= -10) ? 4096 : 256;
        editgb::setStatExp(p, static_cast<pokemon::Stat>(s),
                           static_cast<uint16_t>(clampInt(p.statExp[s] + (delta < 0 ? -unit : unit), 0, 65535)));
        return true;
    }
    switch (field) {
        case kFieldSpecies:
            editgb::setSpecies(p, nextName(data::NameKind::Species, p.format, p.species, delta, letterJump));
            return true;
        case kFieldLevel:
            editgb::setLevel(p, static_cast<uint8_t>(clampInt(p.level + delta, 1, 100)));
            return true;
        case kFieldShiny:
            if (editgb::setShiny(p, !p.shiny) != editgb::Result::Ok) message = "No DVs give that gender and shininess";
            return true;
        case kFieldGender: {
            auto other = p.gender == pokemon::Gender::Male ? pokemon::Gender::Female : pokemon::Gender::Male;
            if (!editgb::canHaveGender(p.species, other)) {
                message = "This species has a fixed gender";
            } else if (editgb::setGender(p, other) != editgb::Result::Ok) {
                message = "No DVs give that gender";
            }
            return true;
        }
        case kFieldCatchRate:
            p.catchRate = static_cast<uint8_t>(clampInt(p.catchRate + delta, 0, 255));
            return true;
        case kFieldMetTime:
            p.met.encounterType = static_cast<uint8_t>(wrap(p.met.encounterType - 1 + (delta < 0 ? -1 : 1), 3) + 1);
            return true;
        default:
            return false;
    }
}

// Changes `field` by `delta` steps; `letterJump` makes name fields jump by
// initial letter. Returns a status message ("" if none).
std::string adjustField(pokemon::Pokemon& p, int field, int delta, bool letterJump, const pokemon::Date& today) {
    if (p.format <= 2) {
        std::string message;
        if (adjustFieldGb(p, field, delta, letterJump, message)) return message;
    }
    if (field >= kFieldMove1 && field <= kFieldMove4) {
        int index = field - kFieldMove1;
        uint16_t move = p.moves[index];
        const int tries = static_cast<int>(data::sortedIds(data::NameKind::Move, p.format).size());
        for (int t = 0; t < tries; ++t) {
            move = nextName(data::NameKind::Move, p.format, move, delta, letterJump && t == 0);
            // A move known in another slot is skipped.
            if (edit4::setMove(p, index, move) == edit4::Result::Ok) break;
        }
        return "";
    }
    if (field >= kFieldIv1 && field <= kFieldIv6) {
        int s = field - kFieldIv1;
        edit4::setIv(p, static_cast<pokemon::Stat>(s), static_cast<uint8_t>(clampInt(p.ivs[s] + delta, 0, 31)));
        return "";
    }
    if (field >= kFieldEv1 && field <= kFieldEv6) {
        int s = field - kFieldEv1;
        uint8_t before = p.evs[s];
        edit4::setEv(p, static_cast<pokemon::Stat>(s), static_cast<uint8_t>(clampInt(p.evs[s] + delta, 0, 255)));
        return (delta > 0 && p.evs[s] == before && before < 255) ? "EV total is limited to 510" : "";
    }
    switch (field) {
        case kFieldNickname:
            return "Press A to type a nickname";
        case kFieldShowdown:
            return "Press A to export";
        case kFieldSpecies:
            edit4::setSpecies(p, nextName(data::NameKind::Species, p.format, p.species, delta, letterJump));
            return "";
        case kFieldLevel:
            edit4::setLevel(p, static_cast<uint8_t>(clampInt(p.level + delta, 1, 100)));
            return "";
        case kFieldNature:
            return rerollTraits(p, static_cast<uint8_t>(nextName(data::NameKind::Nature, p.format, p.nature, delta, letterJump)),
                                p.shiny, p.gender, edit4::abilitySlot(p));
        case kFieldShiny:
            return rerollTraits(p, p.nature, !p.shiny, p.gender, edit4::abilitySlot(p));
        case kFieldGender: {
            auto other = p.gender == pokemon::Gender::Male ? pokemon::Gender::Female : pokemon::Gender::Male;
            if (!edit4::canHaveGender(p.species, other)) return "This species has a fixed gender";
            return rerollTraits(p, p.nature, p.shiny, other, edit4::abilitySlot(p));
        }
        case kFieldAbility: {
            // Gen V cycles slot 1 -> slot 2 -> hidden ability.
            const data::PersonalInfo* info = data::personalFor(p.format, p.species);
            int slots = edit4::abilityCountFor(p);
            bool hidden = p.format >= 5 && info && info->hiddenAbility != 0;
            if (slots < 2 && !hidden) return "This species has only one ability";
            std::vector<uint8_t> order;
            for (int i = 0; i < slots; ++i) order.push_back(static_cast<uint8_t>(i));
            if (hidden) order.push_back(2);
            uint8_t current = edit4::abilitySlot(p);
            size_t at = static_cast<size_t>(std::find(order.begin(), order.end(), current) - order.begin());
            uint8_t next = order[(at + 1) % order.size()];
            return rerollTraits(p, p.nature, p.shiny, p.gender, next);
        }
        case kFieldItem:
            p.heldItem = nextName(data::NameKind::Item, p.format, p.heldItem, delta, letterJump);
            return "";
        case kFieldBall:
            p.met.ball = static_cast<uint8_t>(nextName(data::NameKind::Ball, p.format, p.met.ball, delta, letterJump));
            return "";
        case kFieldFriendship:
            p.friendship = static_cast<uint8_t>(clampInt(p.friendship + delta, 0, 255));
            return "";
        case kFieldOtName:
            return "Press A to type the OT name";
        case kFieldOtGender:
            p.trainer.gender = p.trainer.gender == pokemon::Gender::Female ? pokemon::Gender::Male : pokemon::Gender::Female;
            return "";
        case kFieldTid:
            editorigin::setTid(p, static_cast<uint16_t>(clampInt(p.trainer.tid + delta, 0, 65535)));
            return "";
        case kFieldSid:
            editorigin::setSid(p, static_cast<uint16_t>(clampInt(p.trainer.sid + delta, 0, 65535)));
            return "";
        case kFieldMetLocation:
            p.met.metLocation = editorigin::stepLocation(p, p.met.metLocation, delta);
            return "";
        case kFieldEggLocation:
            p.met.eggLocation = editorigin::stepLocation(p, p.met.eggLocation, delta);
            return "";
        case kFieldMetLevel:
            editorigin::setMetLevel(p, p.met.metLevel + delta);
            return "";
        case kFieldMetDate:
        case kFieldEggDate: {
            // +-1: one day; L/R (+-10): one month.
            pokemon::Date& date = field == kFieldMetDate ? p.met.metDate : p.met.eggDate;
            date = (delta >= 10 || delta <= -10) ? editorigin::stepMonth(date, delta / 10, today)
                                                 : editorigin::stepDay(date, delta, today);
            return "";
        }
        case kFieldFateful:
            p.met.fatefulEncounter = !p.met.fatefulEncounter;
            return "";
        case kFieldPokerusStrain:
            editorigin::setPokerusStrain(p, editorigin::pokerusStrain(p.pokerus) + delta);
            return editorigin::pokerusText(p.pokerus);
        case kFieldPokerusDays:
            if (!editorigin::setPokerusDays(p, editorigin::pokerusDays(p.pokerus) + delta)) {
                return "Set a Pok\xC3\xA9rus strain first";
            }
            return editorigin::pokerusText(p.pokerus);
    }
    return "";
}

void setExtreme(pokemon::Pokemon& p, int field, bool maximum, const pokemon::Date& today) {
    if (p.format <= 2) {
        if (field >= kFieldIv1 && field <= kFieldIv6) {
            editgb::setDv(p, static_cast<pokemon::Stat>(field - kFieldIv1), maximum ? 15 : 0);
            return;
        }
        if (field >= kFieldEv1 && field <= kFieldEv6) {
            editgb::setStatExp(p, static_cast<pokemon::Stat>(field - kFieldEv1), maximum ? 65535 : 0);
            return;
        }
        if (field == kFieldLevel) {
            editgb::setLevel(p, maximum ? 100 : 1);
            return;
        }
        if (field == kFieldCatchRate) {
            p.catchRate = maximum ? 255 : 0;
            return;
        }
    }
    if (field >= kFieldIv1 && field <= kFieldIv6) {
        edit4::setIv(p, static_cast<pokemon::Stat>(field - kFieldIv1), maximum ? 31 : 0);
    } else if (field >= kFieldEv1 && field <= kFieldEv6) {
        edit4::setEv(p, static_cast<pokemon::Stat>(field - kFieldEv1), maximum ? 252 : 0);
    } else if (field == kFieldLevel) {
        edit4::setLevel(p, maximum ? 100 : 1);
    } else if (field == kFieldFriendship) {
        p.friendship = maximum ? 255 : 0;
    } else if (field == kFieldTid) {
        editorigin::setTid(p, maximum ? 65535 : 0);
    } else if (field == kFieldSid) {
        editorigin::setSid(p, maximum ? 65535 : 0);
    } else if (field == kFieldMetLevel) {
        editorigin::setMetLevel(p, maximum ? 100 : 1);
    } else if (field == kFieldMetDate || field == kFieldEggDate) {
        // Y: today.
        (field == kFieldMetDate ? p.met.metDate : p.met.eggDate) = today;
    } else if (field == kFieldPokerusStrain) {
        editorigin::setPokerusStrain(p, maximum ? 15 : 0);
    } else if (field == kFieldPokerusDays) {
        editorigin::setPokerusDays(p, maximum ? 15 : 0);
    }
}

bool isToggleField(int field) {
    return field == kFieldShiny || field == kFieldGender || field == kFieldAbility || field == kFieldOtGender ||
           field == kFieldFateful;
}

// L/R step of numeric fields (the TID/SID range is 65536 values wide).
int bigStep(int field) {
    return field == kFieldTid || field == kFieldSid ? 1000 : 10;
}

const char* loadErrorText(saves::LoadResult result) {
    switch (result) {
        case saves::LoadResult::Ok:
            return "OK";
        case saves::LoadResult::WrongSize:
            return "Unsupported save size.\nSupported: R/B/Y, G/S/C, R/S/E/FR/LG,\nD/P/Pt/HG/SS and B/W/B2/W2.";
        case saves::LoadResult::UnknownFormat:
            return "Unrecognised save format.\nSupported: R/B/Y, G/S/C, R/S/E/FR/LG,\nD/P/Pt/HG/SS and B/W/B2/W2.";
        case saves::LoadResult::BadChecksum:
            return "The save is corrupted: both\ncopies failed the checksum.";
    }
    return "Unknown error.";
}

// "Pokemon Platinum.sav.20260928-214510.bak" -> "2026-09-28  21:45:10"
std::string backupLabel(const std::string& saveName, const std::string& backupPath) {
    std::string name = io::fileName(backupPath);
    if (name.compare(0, saveName.size() + 1, saveName + ".") == 0) {
        name = name.substr(saveName.size() + 1);
    }
    if (name.size() >= 15 && name[8] == '-') {
        std::string pretty = name.substr(0, 4) + "-" + name.substr(4, 2) + "-" + name.substr(6, 2) + "  " +
                             name.substr(9, 2) + ":" + name.substr(11, 2) + ":" + name.substr(13, 2);
        if (name.size() > 15 && name[15] == '_') {
            pretty += "  #" + name.substr(16, name.find('.', 16) - 16);
        }
        return pretty;
    }
    return name;
}

// Word-wraps `text` to `width` pixels (at most `maxLines` lines).
std::vector<std::string> wrapText(const std::string& text, int width, size_t maxLines) {
    std::vector<std::string> lines;
    std::string line;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find(' ', start);
        std::string word = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && Canvas::textWidth(kFontSmall, candidate) > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (!line.empty()) lines.push_back(line);
    if (lines.size() > maxLines) {
        lines.resize(maxLines);
        lines.back() = Canvas::ellipsize(kFontSmall, lines.back() + "...", width);
    }
    return lines;
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

}  // namespace

// =============================================================================
// Construction and top-level flow

App::App(Config config)
    : config_(std::move(config)),
      sprites_(config_.spriteDirs.empty() ? std::vector<std::string>{io::joinPath(config_.dataDir, "sprites")}
                                          : config_.spriteDirs) {
    const std::vector<std::string> assets =
        config_.assetDirs.empty() ? std::vector<std::string>{config_.dataDir} : config_.assetDirs;
    data::setLanguageDirs(assets);
    gfx::openCjkFont(assets);
    io::loadSettings(settingsPath(), settings_);
    if (!data::setNameLanguage(settings_.language)) {  // pack missing: English
        data::setNameLanguage(data::kLangEnglish);
        settings_.language = data::kLangEnglish;
    }
    setSaved_ = settings_;
    data::setOrder(settings_.sortById ? data::Order::ById : data::Order::Alphabetical);
}

std::string App::backupDir() const {
    return io::joinPath(config_.dataDir, "backups");
}

std::string App::saveTitle() const {
    return std::string("Pok\xC3\xA9mon ") + saves::gameName(save_->game());
}

namespace {
constexpr int kFadeFrames = 5;   // screen fade-in, in frames
constexpr int kBobPeriod = 24;   // the selected sprite moves up/down every this many frames
constexpr int kEaseFrac = 16;    // selection bar position unit (1/16 pixel)
}  // namespace

// One frame: input, then the animation clock. A screen change starts a fade-in;
// the fade, a gliding selection bar and the sprite bob keep the screens redrawing.
bool App::update(const Input& input) {
    const Screen before = screen_;
    const bool keepRunning = updateFrame(input);
    ++frame_;
    tickAnimations(before);
    return keepRunning;
}

void App::tickAnimations(Screen before) {
    if (!settings_.animations) {
        if (fadeLeft_ > 0 || easing_) redraw_ = true;
        fadeLeft_ = 0;
        easing_ = false;
        return;
    }
    if (screen_ != before) {
        fadeLeft_ = kFadeFrames;
        easeKey_ = nullptr;
        easing_ = false;
        redraw_ = true;
    } else if (fadeLeft_ > 0) {
        --fadeLeft_;
        if (!config_.hardwareFade) redraw_ = true;
    }
    if (easing_) redraw_ = true;
    const bool bobbing = screen_ == Screen::Box || screen_ == Screen::Bank || screen_ == Screen::Picker ||
                         screen_ == Screen::Edit;
    if (bobbing && dialog_ == Dialog::None && pending_ == Pending::None && frame_ % kBobPeriod == 0) redraw_ = true;
}

int App::fadeAlpha() const {
    // Fade in from the background colour: alpha 4 per frame left (of 32).
    return fadeLeft_ > 0 && settings_.animations ? std::min(32, fadeLeft_ * 4) : 0;
}

int App::bobOffset() const {
    return settings_.animations && (frame_ / kBobPeriod) % 2 == 1 ? -1 : 0;
}

bool App::updateFrame(const Input& rawInput) {
    if (pending_ != Pending::None) {
        runPending();
        redraw_ = true;
        return true;
    }

    Input in = rawInput;
    if (in.touched) {
        int zone = touchedZone(in);
        if (zone > 0 && zone < kZoneItem) {
            in.pressed |= static_cast<uint32_t>(zone);
            in.repeated |= static_cast<uint32_t>(zone);
        }
    }
    if (in.pressed == 0 && in.repeated == 0 && !in.touched) {
        return true;
    }
    redraw_ = true;

    if (dialog_ != Dialog::None) {
        updateDialog(in);
        return true;
    }
    switch (screen_) {
        case Screen::Home:
            return updateHome(in);
        case Screen::Browser:
            updateBrowser(in);
            break;
        case Screen::SaveMenu:
            updateSaveMenu(in);
            break;
        case Screen::Box:
            updateBox(in);
            break;
        case Screen::Edit:
            updateEdit(in);
            break;
        case Screen::Backups:
            updateBackups(in);
            break;
        case Screen::Picker:
            updatePicker(in);
            break;
        case Screen::Bank:
            updateBank(in);
            break;
        case Screen::Keyboard:
            updateKeyboard(in);
            break;
        case Screen::Trainer:
            updateTrainer(in);
            break;
        case Screen::Bag:
            updateBag(in);
            break;
        case Screen::ItemPicker:
            updateItemPicker(in);
            break;
        case Screen::Ribbons:
            updateRibbons(in);
            break;
        case Screen::Settings:
            updateSettings(in);
            break;
    }
    return true;
}

int App::touchedZone(const Input& in) const {
    for (const TouchZone& z : zones_) {
        if (in.touchX >= z.x && in.touchX < z.x + z.w && in.touchY >= z.y && in.touchY < z.y + z.h) {
            return z.id;
        }
    }
    return -1;
}

void App::showLegality(const pokemon::Pokemon& p) {
    dialogReport_ = legality::check(p);
    // Problems first, then warnings, then passed checks.
    std::stable_sort(dialogReport_.findings.begin(), dialogReport_.findings.end(),
                     [](const legality::Finding& a, const legality::Finding& b) { return a.severity > b.severity; });
    switch (dialogReport_.overall()) {
        case legality::Severity::Invalid:
            dialogTitle_ = "Illegal";
            break;
        case legality::Severity::Warning:
            dialogTitle_ = "Legal, with warnings";
            break;
        case legality::Severity::Valid:
            dialogTitle_ = "Legal";
            break;
    }
    dialog_ = Dialog::Legality;
}

void App::showMessage(const std::string& title, const std::string& text) {
    dialog_ = Dialog::Message;
    dialogTitle_ = title;
    dialogText_ = text;
}

void App::runPending() {
    Pending action = pending_;
    pending_ = Pending::None;
    // Writing needs a 512 KB save in memory plus a validation copy: drop the
    // sprite cache first so the DS heap cannot run out halfway through.
    if (action == Pending::Write || action == Pending::Restore) sprites_.clear();
    switch (action) {
        case Pending::Scan:
            // A rescan (Y) only reads the files that changed since the last one.
            saveFiles_ = saves::findPokemonSaves(config_.scanRoot, 3, &saveFiles_);
            scanned_ = true;
            browserCursor_ = 0;
            browserScroll_ = 0;
            screen_ = Screen::Browser;
            break;
        case Pending::GbaBackup:
            backUpGbaCartridge();
            break;
        case Pending::Write: {
            // Bank first (deposits), then the save; withdrawn Pokémon leave the
            // Bank only once the save holding them is on the SD card.
            if (bankDirty_ && bank_.save() != io::WriteResult::Ok) {
                showMessage("Save failed", "The Bank could not be written.\n\nNothing was modified.");
                break;
            }
            bankDirty_ = false;
            saves::CommitResult result = saves::commitSave(*save_, savePath_, backupDir());
            if (result.status == saves::CommitResult::Status::Ok && !bankWithdrawn_.empty()) {
                for (const auto& slot : bankWithdrawn_) bank_.clear(slot.first, slot.second);
                bankWithdrawn_.clear();
                if (bank_.save() != io::WriteResult::Ok) {
                    showMessage("Bank not updated", "The save was written, but the Bank\nstill lists the withdrawn Pok\xC3\xA9mon.");
                    unsaved_ = false;
                    break;
                }
            }
            if (result.status == saves::CommitResult::Status::Ok) {
                unsaved_ = false;
                showMessage("Saved", "Your changes were written to the SD card.\n\nBackup of the previous file:\n" +
                                         io::fileName(result.backupPath));
            } else {
                showMessage("Save failed", std::string(saves::describe(result)) +
                                               ".\n\nThe file on the SD card was not modified.");
            }
            break;
        }
        case Pending::Restore: {
            io::WriteResult result = io::restoreBackup(backups_[backupCursor_], savePath_, backupDir());
            if (result != io::WriteResult::Ok) {
                showMessage("Restore failed", io::describe(result));
                break;
            }
            if (backupsReturn_ == Screen::SaveMenu) {
                openSave(savePath_);
            }
            backups_ = io::listBackups(savePath_, backupDir());
            showMessage("Restored", "The backup was restored.\nThe replaced save was backed up too.");
            break;
        }
        case Pending::Legalize: {
            legality::LegalizeResult result = legality::legalize(edit_.current, kLegalizeEffort);
            edit_.current = result.pokemon;
            dialogReport_ = legality::Report();
            for (const std::string& c : result.changes) dialogReport_.findings.push_back({legality::Severity::Valid, c});
            for (const std::string& u : result.unresolved) {
                dialogReport_.findings.insert(dialogReport_.findings.begin(), {legality::Severity::Invalid, u});
            }
            if (result.changes.empty() && result.unresolved.empty()) {
                dialogReport_.findings.push_back({legality::Severity::Valid, "No changes needed"});
            }
            dialogTitle_ = result.unresolved.empty() ? "Legalized" : "Could not fully legalize";
            dialog_ = Dialog::Legality;
            break;
        }
        case Pending::Create:
            createPokemon();
            break;
        case Pending::None:
            break;
    }
}

const char* App::screenName() const {
    switch (screen_) {
        case Screen::Home: return "Home";
        case Screen::Browser: return "Browser";
        case Screen::SaveMenu: return "SaveMenu";
        case Screen::Box: return "Box";
        case Screen::Edit: return "Edit";
        case Screen::Backups: return "Backups";
        case Screen::Picker: return "Picker";
        case Screen::Bank: return "Bank";
        case Screen::Keyboard: return "Keyboard";
        case Screen::Trainer: return "Trainer";
        case Screen::Bag: return "Bag";
        case Screen::ItemPicker: return "ItemPicker";
        case Screen::Ribbons: return "Ribbons";
        case Screen::Settings: return "Settings";
    }
    return "";
}

bool App::openSave(const std::string& path) {
    // Free the previous save first: two saves at once are 1 MB of the DS heap.
    // A failure below then goes back to the browser: no screen can use a save
    // that is gone.
    const bool hadSave = save_ != nullptr;
    save_.reset();
    unsaved_ = false;
    menuOpen_ = false;
    moving_ = false;
    sprites_.clear();
    std::vector<uint8_t> bytes;
    if (!io::readFile(path, bytes)) {
        if (hadSave) screen_ = Screen::Browser;
        showMessage("Cannot open", "Could not read the file.");
        return false;
    }
    saves::LoadResult result;
    std::unique_ptr<saves::SaveFile> loaded = saves::loadSaveFile(std::move(bytes), &result);
    if (!loaded) {
        if (hadSave) screen_ = Screen::Browser;
        showMessage("Cannot open", loadErrorText(result));
        return false;
    }
    save_ = std::move(loaded);
    savePath_ = path;
    box_ = -1;
    slotCursor_ = 0;
    unsaved_ = false;
    menuOpen_ = false;
    moving_ = false;
    saveMenuCursor_ = 0;
    screen_ = Screen::SaveMenu;
    redraw_ = true;
    return true;
}

// =============================================================================
// Input

void App::updateDialog(const Input& in) {
    bool yes = in.pressed & kKeyA;
    bool no = in.pressed & kKeyB;
    if (dialog_ == Dialog::Message || dialog_ == Dialog::Legality) {
        if (yes || no) dialog_ = Dialog::None;
        return;
    }
    if (!yes && !no) {
        return;
    }
    Dialog which = dialog_;
    dialog_ = Dialog::None;
    if (!yes) {
        return;
    }
    switch (which) {
        case Dialog::ConfirmWrite:
            pending_ = Pending::Write;
            break;
        case Dialog::ConfirmClose:
            unsaved_ = false;
            screen_ = Screen::Browser;
            save_.reset();  // the browser scans other saves: do not keep this one in memory
            if (bankLoaded_ && (bankDirty_ || !bankWithdrawn_.empty())) {
                bank_.load(bankPath());  // drop the Bank moves too
                bankDirty_ = false;
                bankWithdrawn_.clear();
            }
            break;
        case Dialog::ConfirmDiscard:
            // The trainer and bag editors belong to the save menu, the Pokémon editor to the box view.
            screen_ = (screen_ == Screen::Trainer || screen_ == Screen::Bag) ? Screen::SaveMenu : Screen::Box;
            break;
        case Dialog::ConfirmRestore:
            pending_ = Pending::Restore;
            break;
        case Dialog::ConfirmRelease:
            if (screen_ == Screen::Bank) {
                bankRelease();
            } else {
                releaseSlot();
            }
            break;
        case Dialog::None:
        case Dialog::Message:
        case Dialog::Legality:
            break;
    }
}

bool App::updateHome(const Input& in) {
    constexpr int kTiles = 4;
    int zone = in.touched ? touchedZone(in) : -1;
    bool activate = in.pressed & kKeyA;
    if (zone >= kZoneItem && zone < kZoneItem + kTiles) {
        homeCursor_ = zone - kZoneItem;
        activate = true;
    }
    if (in.repeated & kKeyLeft) homeCursor_ = (homeCursor_ & 2) | ((homeCursor_ + 1) & 1);
    if (in.repeated & kKeyRight) homeCursor_ = (homeCursor_ & 2) | ((homeCursor_ + 1) & 1);
    if (in.repeated & (kKeyUp | kKeyDown)) homeCursor_ ^= 2;
    if (in.pressed & kKeyStart) return false;
    if (!activate) {
        return true;
    }
    switch (homeCursor_) {
        case 0:
            startBrowser();
            break;
        case 1:
            startBrowser();
            if (screen_ == Screen::Browser || pending_ == Pending::Scan) {
                showMessage("Backups", "Select a save and press X\nto see and restore its backups.");
            }
            break;
        case 2:
            openBank(false, Screen::Home);
            break;
        default:
            openSettings();  // the About box is its last row
            break;
    }
    return true;
}

void App::startBrowser() {
    if (!config_.storageReady) {
        showMessage("No SD card", "The SD card could not be\ninitialised (DLDI/FAT).");
        return;
    }
    if (!scanned_) {
        pending_ = Pending::Scan;  // runs next frame, after "Scanning..." is shown
        return;
    }
    screen_ = Screen::Browser;
}

void App::updateBrowser(const Input& in) {
    int count = static_cast<int>(saveFiles_.size());
    int zone = in.touched ? touchedZone(in) : -1;
    bool activate = in.pressed & kKeyA;
    if (zone >= kZoneItem && zone < kZoneItem + count) {
        int index = zone - kZoneItem;
        activate = index == browserCursor_;
        browserCursor_ = index;
    }
    if (in.repeated & kKeyUp) browserCursor_ = wrap(browserCursor_ - 1, count);
    if (in.repeated & kKeyDown) browserCursor_ = wrap(browserCursor_ + 1, count);
    if (in.repeated & kKeyL) browserCursor_ = clampInt(browserCursor_ - 8, 0, std::max(0, count - 1));
    if (in.repeated & kKeyR) browserCursor_ = clampInt(browserCursor_ + 8, 0, std::max(0, count - 1));
    if (in.pressed & kKeyB) screen_ = Screen::Home;
    if (in.pressed & kKeyY) {
        scanned_ = false;
        startBrowser();
    }
    if ((in.pressed & kKeySelect) && config_.gbaSlot != nullptr) {
        pending_ = Pending::GbaBackup;  // runs next frame, after "Reading..." is shown
        return;
    }
    if (count == 0) {
        return;
    }
    if (activate) openSave(saveFiles_[browserCursor_].path);
    if (in.pressed & kKeyX) {
        savePath_ = saveFiles_[browserCursor_].path;
        openBackups(Screen::Browser);
    }
}

// Beta: copies the save of the Gen III cartridge in the GBA slot to
// <dataDir>/gba/ and opens it. Read-only for the cartridge; the copy is kept
// only if two reads agree and it loads as a Gen III save.
void App::backUpGbaCartridge() {
    const char* title = "GBA cartridge (beta)";
    saves::gba::CartInfo info;
    std::vector<uint8_t> data;
    const saves::gba::ReadResult read = saves::gba::readSave(*config_.gbaSlot, info, data);
    if (read != saves::gba::ReadResult::Ok) {
        showMessage(title, saves::gba::describe(read));
        return;
    }
    saves::LoadResult loaded = saves::LoadResult::UnknownFormat;
    std::unique_ptr<saves::SaveFile> check = saves::loadSaveFile(data, &loaded);
    if (!check || loaded != saves::LoadResult::Ok || check->format() != 3) {
        showMessage(title, std::string(info.gameName) + ":\nthe cartridge has no valid save\n(new game, or a damaged save).\nNothing was written.");
        return;
    }
    check.reset();
    const std::string dir = io::joinPath(config_.dataDir, "gba");
    const std::string name = saves::gba::backupFileName(info, io::currentTimestamp());
    const std::string path = io::joinPath(dir, name);
    io::WriteResult written = io::makeDirectories(dir) ? io::safeWriteFile(path, data) : io::WriteResult::TempWriteFailed;
    if (written != io::WriteResult::Ok) {
        showMessage(title, std::string("Could not write the copy:\n") + io::describe(written));
        return;
    }
    scanned_ = false;  // the next browser visit lists the copy
    if (openSave(path)) {
        showMessage(title, std::string(info.gameName) + " backed up to\ngba/" + name +
                               "\n\nEdits are written to this copy,\nnot to the cartridge.");
    }
}

// ----- Save menu ----------------------------------------------------------------
//
// The first screen after choosing a save, like the main menu of the game. B
// closes the save; Boxes, Trainer, Bag, Bank and Backups come back here with B.

namespace {

enum SaveMenuItem { kSmBoxes, kSmTrainer, kSmBag, kSmBank, kSmSave, kSmBackups, kSmClose, kSmCount };

}  // namespace

void App::closeSave() {
    if (unsaved_) {
        dialog_ = Dialog::ConfirmClose;
    } else {
        screen_ = Screen::Browser;
        save_.reset();  // the browser scans other saves: do not keep this one in memory
    }
}

void App::runSaveMenuAction(int item) {
    switch (item) {
        case kSmBoxes:
            screen_ = Screen::Box;
            break;
        case kSmTrainer:
            openTrainer();
            break;
        case kSmBag:
            openBag();
            break;
        case kSmBank:
            openBank(false, Screen::SaveMenu);
            break;
        case kSmSave:
            if (unsaved_) {
                dialog_ = Dialog::ConfirmWrite;
            } else {
                showMessage("Nothing to save", "There are no changes to write.");
            }
            break;
        case kSmBackups:
            openBackups(Screen::SaveMenu);
            break;
        case kSmClose:
            closeSave();
            break;
        default:
            break;
    }
}

void App::updateSaveMenu(const Input& in) {
    int zone = in.touched ? touchedZone(in) : -1;
    bool activate = in.pressed & kKeyA;
    if (zone >= kZoneItem && zone < kZoneItem + kSmCount) {
        saveMenuCursor_ = zone - kZoneItem;
        activate = true;
    }
    if (in.repeated & kKeyUp) saveMenuCursor_ = wrap(saveMenuCursor_ - 1, kSmCount);
    if (in.repeated & kKeyDown) saveMenuCursor_ = wrap(saveMenuCursor_ + 1, kSmCount);
    if (in.pressed & kKeySelect) {
        runSaveMenuAction(kSmSave);
        return;
    }
    if (in.pressed & kKeyB) {
        closeSave();
        return;
    }
    if (activate) runSaveMenuAction(saveMenuCursor_);
}

int App::slotCount() const {
    return isParty() ? saves::SaveFile::kPartyMax : save_->boxSlotCount();
}

saves::SlotState App::readSlot(int box, int slot, pokemon::Pokemon& out) const {
    if (box < 0) {
        uint8_t buffer[pokemon::entity::kMaxPartySize];
        auto state = save_->readPartySlot(slot, buffer);
        if (state == saves::SlotState::Valid) {
            out = pokemon::entity::decode(save_->format(), buffer, save_->partySize());
            if (save_->format() <= 2) out.originGame = save_->gameVersion();  // Game Boy entities do not store it
        }
        return state;
    }
    uint8_t buffer[pokemon::entity::kStoredSize];
    auto state = save_->readBoxSlot(box, slot, buffer);
    if (state == saves::SlotState::Valid) {
        out = pokemon::entity::decode(save_->format(), buffer, pokemon::entity::kStoredSize);
        if (save_->format() <= 2) out.originGame = save_->gameVersion();
    }
    return state;
}

// Appended to export.txt, which the Showdown import lists too.
std::string App::exportShowdown(const pokemon::Pokemon& p) {
    std::string dir = showdownDir();
    std::string path = io::joinPath(dir, "export.txt");
    std::vector<uint8_t> file;
    io::readFile(path, file);
    if (!file.empty() && file.back() != '\n') file.push_back('\n');
    if (!file.empty()) file.push_back('\n');
    std::string set = showdown::format(p);
    file.insert(file.end(), set.begin(), set.end());
    io::makeDirectories(dir);
    return io::safeWriteFile(path, file) == io::WriteResult::Ok ? "Set added to showdown/export.txt"
                                                                  : "Could not write export.txt";
}

// ----- Slot menu ---------------------------------------------------------------

namespace {

const char* menuLabel(int action) {
    static const char* const kLabels[] = {"Edit",    "Move",  "Check legality", "Export Showdown",
                                          "To Bank", "Clone", "Release",        "Ribbons", "Cancel"};
    return kLabels[action];
}

}  // namespace

void App::openMenu(const pokemon::Pokemon& p) {
    menuItems_.clear();
    menuItems_.push_back(MenuAction::Edit);
    menuItems_.push_back(MenuAction::Move);
    menuItems_.push_back(MenuAction::Legality);
    if (!p.isEgg) menuItems_.push_back(MenuAction::Showdown);  // an egg has no battle set
    menuItems_.push_back(MenuAction::Bank);
    menuItems_.push_back(MenuAction::Clone);
    if (save_->format() >= 3) menuItems_.push_back(MenuAction::Ribbons);
    if (!isParty() || save_->partyCount() > 1) menuItems_.push_back(MenuAction::Release);  // never an empty party
    menuItems_.push_back(MenuAction::Cancel);
    menuCursor_ = 0;
    menuOpen_ = true;
}

void App::backupNow() {
    std::string backup = io::createBackup(savePath_, backupDir());
    if (backup.empty()) {
        showMessage("Backup failed", "Could not create the backup.");
    } else {
        showMessage("Backup created", io::fileName(backup));
        backups_ = io::listBackups(savePath_, backupDir());  // the Backups screen lists it
    }
}

void App::updateMenu(const Input& in) {
    int zone = in.touched ? touchedZone(in) : -1;
    int count = static_cast<int>(menuItems_.size());
    bool activate = in.pressed & kKeyA;
    if (in.repeated & kKeyUp) menuCursor_ = wrap(menuCursor_ - 1, count);
    if (in.repeated & kKeyDown) menuCursor_ = wrap(menuCursor_ + 1, count);
    if (zone >= kZoneItem && zone < kZoneItem + count) {
        menuCursor_ = zone - kZoneItem;
        activate = true;
    } else if (in.touched && zone < 0) {
        menuOpen_ = false;  // tapping outside closes the menu
        return;
    }
    if (in.pressed & kKeyB) {
        menuOpen_ = false;
        return;
    }
    if (activate) {
        MenuAction action = menuItems_[menuCursor_];
        menuOpen_ = false;
        runMenuAction(action);
    }
}

void App::runMenuAction(MenuAction action) {
    pokemon::Pokemon p;
    if (readSlot(box_, slotCursor_, p) != saves::SlotState::Valid) return;
    switch (action) {
        case MenuAction::Edit:
            startEdit();
            break;
        case MenuAction::Move:
            moving_ = true;
            moveBox_ = box_;
            moveSlot_ = slotCursor_;
            break;
        case MenuAction::Legality:
            showLegality(p);
            break;
        case MenuAction::Showdown:
            showMessage("Showdown", exportShowdown(p));
            break;
        case MenuAction::Bank:
            openBank(true, Screen::Box);
            break;
        case MenuAction::Clone:
            cloneSlot();
            break;
        case MenuAction::Ribbons:
            if (p.isEgg) {
                showMessage("Ribbons", "An egg cannot hold ribbons\nuntil it hatches.");
            } else {
                openRibbons();
            }
            break;
        case MenuAction::Release:
            dialog_ = Dialog::ConfirmRelease;
            break;
        case MenuAction::Cancel:
            break;
    }
}

// Drops the picked-up Pokémon on the cursor slot: moves it to an empty slot or
// swaps it with the occupant. Party and box data differ in size, so the stored
// part is cut out or the battle stats are recomputed when crossing over.
void App::dropHeld() {
    moving_ = false;
    const int srcBox = moveBox_;
    const int srcSlot = moveSlot_;
    const int dstBox = box_;
    const int dstSlot = slotCursor_;
    if (srcBox == dstBox && srcSlot == dstSlot) return;

    uint8_t a[pokemon::entity::kMaxPartySize] = {};
    uint8_t b[pokemon::entity::kMaxPartySize] = {};
    auto readRaw = [&](int box, int slot, uint8_t* raw) {
        return box < 0 ? save_->readPartySlot(slot, raw) : save_->readBoxSlot(box, slot, raw);
    };
    if (readRaw(srcBox, srcSlot, a) != saves::SlotState::Valid) return;
    auto dstState = readRaw(dstBox, dstSlot, b);
    if (dstState == saves::SlotState::BadChecksum) {
        showMessage("Move", "The target slot holds bad data.");
        return;
    }
    const bool dstEmpty = dstState == saves::SlotState::Empty;
    const uint8_t format = save_->format();

    auto writeParty = [&](int slot, const uint8_t* stored) {
        uint8_t party[pokemon::entity::kMaxPartySize] = {};
        std::memcpy(party, stored, save_->storedSize());
        pokemon::entity::refreshParty(format, party);
        save_->writePartySlot(slot, party);
    };

    if (srcBox >= 0 && dstBox >= 0) {  // box -> box
        save_->writeBoxSlot(dstBox, dstSlot, a);
        if (dstEmpty) {
            save_->clearBoxSlot(srcBox, srcSlot);
        } else {
            save_->writeBoxSlot(srcBox, srcSlot, b);
        }
    } else if (srcBox < 0 && dstBox >= 0) {  // party -> box
        if (dstEmpty) {
            if (save_->partyCount() <= 1) {
                showMessage("Move", "The party needs at least\none Pok\xC3\xA9mon.");
                return;
            }
            save_->writeBoxSlot(dstBox, dstSlot, a);
            save_->removePartySlot(srcSlot);
        } else {
            writeParty(srcSlot, b);
            save_->writeBoxSlot(dstBox, dstSlot, a);
        }
    } else if (srcBox >= 0) {  // box -> party
        if (dstEmpty) {
            uint8_t party[pokemon::entity::kMaxPartySize] = {};
            std::memcpy(party, a, save_->storedSize());
            pokemon::entity::refreshParty(format, party);
            if (!save_->appendParty(party)) {
                showMessage("Move", "The party is full.");
                return;
            }
            save_->clearBoxSlot(srcBox, srcSlot);
            slotCursor_ = save_->partyCount() - 1;
        } else {
            writeParty(dstSlot, a);
            save_->writeBoxSlot(srcBox, srcSlot, b);
        }
    } else {  // party -> party: reorder
        if (dstEmpty) {
            showMessage("Move", "Drop it on another party member\nto swap places.");
            return;
        }
        save_->writePartySlot(dstSlot, a);
        save_->writePartySlot(srcSlot, b);
    }
    unsaved_ = true;
}

// Copies the Pokémon into the first empty box slot (same box first).
void App::cloneSlot() {
    uint8_t raw[pokemon::entity::kMaxPartySize] = {};
    auto state = isParty() ? save_->readPartySlot(slotCursor_, raw) : save_->readBoxSlot(box_, slotCursor_, raw);
    if (state != saves::SlotState::Valid) return;
    int boxes = save_->boxCount();
    int first = isParty() ? 0 : box_;
    for (int i = 0; i < boxes; ++i) {
        int box = (first + i) % boxes;
        for (int slot = 0; slot < save_->boxSlotCount(); ++slot) {
            uint8_t probe[pokemon::entity::kMaxPartySize];
            if (save_->readBoxSlot(box, slot, probe) != saves::SlotState::Empty) continue;
            save_->writeBoxSlot(box, slot, raw);
            unsaved_ = true;
            showMessage("Cloned", "Copied to box " + twoDigits(box + 1) + ", slot " + std::to_string(slot + 1) + ".");
            return;
        }
    }
    showMessage("Clone", "No empty box slot left.");
}

void App::releaseSlot() {
    if (isParty()) {
        if (!save_->removePartySlot(slotCursor_)) {
            showMessage("Release", "The party needs at least\none Pok\xC3\xA9mon.");
            return;
        }
        slotCursor_ = std::min(slotCursor_, save_->partyCount() - 1);
    } else {
        save_->clearBoxSlot(box_, slotCursor_);
    }
    unsaved_ = true;
}

void App::updateBox(const Input& in) {
    if (menuOpen_) {
        updateMenu(in);
        return;
    }
    int zone = in.touched ? touchedZone(in) : -1;
    int boxDelta = 0;
    if (in.repeated & kKeyR || zone == kZoneNextBox) boxDelta = 1;
    if (in.repeated & kKeyL || zone == kZonePrevBox) boxDelta = -1;
    if (zone == kZoneBoxTitle && !isParty() && !moving_ && save_->hasBoxNames()) {
        openKeyboard(KeyboardTarget::BoxName, save_->boxName(box_), saves::SaveFile::kBoxNameMaxLength);
        return;
    }
    if (boxDelta != 0) {
        // Cycle PARTY, BOX 1 ... BOX 18.
        box_ = wrap(box_ + 1 + boxDelta, save_->boxCount() + 1) - 1;
        slotCursor_ = std::min(slotCursor_, slotCount() - 1);
        return;
    }

    bool activate = in.pressed & kKeyA;
    if (zone >= kZoneItem && zone < kZoneItem + slotCount()) {
        int slot = zone - kZoneItem;
        activate = slot == slotCursor_;
        slotCursor_ = slot;
    }
    int columns = isParty() ? 2 : 6;
    int count = slotCount();
    if (in.repeated & kKeyLeft) slotCursor_ = wrap(slotCursor_ - 1, count);
    if (in.repeated & kKeyRight) slotCursor_ = wrap(slotCursor_ + 1, count);
    if (in.repeated & kKeyUp) slotCursor_ = wrap(slotCursor_ - columns, count);
    if (in.repeated & kKeyDown) slotCursor_ = wrap(slotCursor_ + columns, count);

    if (moving_) {
        // A drops the picked-up Pokémon here, B puts it back.
        if (activate) {
            dropHeld();
        } else if (in.pressed & kKeyB) {
            moving_ = false;
        }
        return;
    }

    if (activate) {
        pokemon::Pokemon p;
        auto state = readSlot(box_, slotCursor_, p);
        if (state == saves::SlotState::Valid) {
            openMenu(p);
        } else if (state == saves::SlotState::Empty && !isParty()) {
            openPicker(PickerKind::AddMenu);
        } else if (state == saves::SlotState::Empty) {
            showMessage("Empty slot", "New Pok\xC3\xA9mon go into a box:\npick an empty box slot (L/R).");
        }
    }
    if (in.pressed & kKeyStart) {
        pokemon::Pokemon p;
        if (readSlot(box_, slotCursor_, p) == saves::SlotState::Valid) showLegality(p);
    }
    if (in.pressed & kKeySelect) {
        if (unsaved_) {
            dialog_ = Dialog::ConfirmWrite;
        } else {
            showMessage("Nothing to save", "There are no changes to write.");
        }
    }
    if (in.pressed & kKeyB) screen_ = Screen::SaveMenu;  // back to the save menu; closing happens there
    if (in.pressed & kKeyY) openBank(true, Screen::Box);
    // X does nothing here: Trainer, Bag and Backups live in the save menu.
}

bool App::startEdit() {
    EditState& e = edit_;
    e = EditState();
    e.party = isParty();
    e.box = box_;
    e.slot = slotCursor_;
    auto state = e.party ? save_->readPartySlot(e.slot, e.raw) : save_->readBoxSlot(box_, e.slot, e.raw);
    if (state != saves::SlotState::Valid) {
        return false;
    }
    e.original = pokemon::entity::decode(save_->format(), e.raw, e.party ? save_->partySize() : pokemon::entity::kStoredSize);
    if (save_->format() <= 2) e.original.originGame = save_->gameVersion();
    e.current = e.original;
    editScroll_ = 0;
    screen_ = Screen::Edit;
    return true;
}

bool App::editChanged() const {
    if (edit_.created) return true;  // the slot is still empty until applied
    uint8_t copy[pokemon::entity::kMaxPartySize];
    std::memcpy(copy, edit_.raw, sizeof(copy));
    pokemon::entity::apply(edit_.current, copy);
    return std::memcmp(copy, edit_.raw, edit_.party ? save_->partySize() : pokemon::entity::kStoredSize) != 0;
}

void App::applyEdit() {
    EditState& e = edit_;
    if (editChanged()) {
        pokemon::entity::apply(e.current, e.raw);
        if (e.party) {
            pokemon::entity::refreshParty(save_->format(), e.raw);
            save_->writePartySlot(e.slot, e.raw);
        } else {
            save_->writeBoxSlot(e.box, e.slot, e.raw);
        }
        unsaved_ = true;
    }
    screen_ = Screen::Box;
}

void App::updateEdit(const Input& in) {
    EditState& e = edit_;
    int zone = in.touched ? touchedZone(in) : -1;
    int delta = 0;
    if (zone >= kZoneItem && zone < kZoneItem + kFieldCount) {
        e.field = zone - kZoneItem;
    } else if (zone >= kZoneDecrease && zone < kZoneDecrease + kFieldCount) {
        e.field = zone - kZoneDecrease;
        delta = -1;
    } else if (zone >= kZoneIncrease && zone < kZoneIncrease + kFieldCount) {
        e.field = zone - kZoneIncrease;
        delta = 1;
    }
    if (!fieldVisible(e.current, e.field)) e.field = nextVisibleField(e.current, e.field, 1);
    if (in.repeated & kKeyUp) e.field = nextVisibleField(e.current, e.field, -1);
    if (in.repeated & kKeyDown) e.field = nextVisibleField(e.current, e.field, 1);
    if (in.repeated & kKeyRight) delta = 1;
    if (in.repeated & kKeyLeft) delta = -1;
    // L/R: +-10 on numbers, next/previous initial letter on name lists.
    const bool letterJump = isNameField(e.field) && (in.repeated & (kKeyL | kKeyR));
    if (in.repeated & kKeyR) delta = letterJump ? 1 : bigStep(e.field);
    if (in.repeated & kKeyL) delta = letterJump ? -1 : -bigStep(e.field);
    if ((in.pressed & kKeyA) && isToggleField(e.field)) delta = 1;
    if ((in.pressed & kKeyA) && e.field == kFieldShowdown) {
        e.status = exportShowdown(e.current);
        return;
    }
    if ((in.pressed & kKeyA) && e.field == kFieldNickname) {
        if (e.current.isEgg) {
            e.status = "Eggs keep their name until they hatch";
            return;
        }
        openKeyboard(KeyboardTarget::Nickname, e.current.nickname,
                     kbcjk::kindForLanguage(e.current.language) != kbcjk::Kind::Latin ? 5 : pk4::kNicknameMaxLength);
        return;
    }
    if ((in.pressed & kKeyA) && e.field == kFieldOtName) {
        openKeyboard(KeyboardTarget::OtName, e.current.trainer.name, editorigin::otNameMaxLengthFor(e.current));
        return;
    }
    if (delta != 0 || (in.pressed & (kKeyX | kKeyY))) {
        e.status.clear();
    }
    if (delta != 0) e.status = adjustField(e.current, e.field, delta, letterJump, saveTrainer().today);
    if (in.pressed & kKeyY) setExtreme(e.current, e.field, true, saveTrainer().today);
    if (in.pressed & kKeyX) pending_ = Pending::Legalize;  // runs next frame, after "Working..." is shown
    if (in.pressed & kKeyStart) applyEdit();
    if (in.pressed & kKeySelect) showLegality(e.current);
    if (in.pressed & kKeyB) {
        if (editChanged()) {
            dialog_ = Dialog::ConfirmDiscard;
        } else {
            screen_ = Screen::Box;
        }
    }
}

void App::openBackups(Screen returnTo) {
    backups_ = io::listBackups(savePath_, backupDir());
    backupCursor_ = 0;
    backupScroll_ = 0;
    backupsReturn_ = returnTo;
    screen_ = Screen::Backups;
}

void App::updateBackups(const Input& in) {
    int count = static_cast<int>(backups_.size());
    int zone = in.touched ? touchedZone(in) : -1;
    bool activate = in.pressed & kKeyA;
    if (zone >= kZoneItem && zone < kZoneItem + count) {
        int index = zone - kZoneItem;
        activate = index == backupCursor_;
        backupCursor_ = index;
    }
    if (in.repeated & kKeyUp) backupCursor_ = wrap(backupCursor_ - 1, count);
    if (in.repeated & kKeyDown) backupCursor_ = wrap(backupCursor_ + 1, count);
    if (in.pressed & kKeyB) screen_ = backupsReturn_;
    if (count > 0 && activate) dialog_ = Dialog::ConfirmRestore;
    if (in.pressed & kKeyX) backupNow();
}

// ----- Adding Pokémon to an empty box slot --------------------------------------

const char* const kAddMenuRows[] = {"New Pok\xC3\xA9mon", "Import Showdown set", "Event (Wonder Card)", "Egg"};
constexpr int kAddMenuCount = 4;


// ----- On-screen keyboard ----------------------------------------------------

namespace {

constexpr int kKbColumns = 10;
constexpr int kKbCharKeys = 40;  // 4 rows of 10
constexpr int kKbSpecialKeys = 5;
constexpr int kKbPages = 3;  // upper case, lower case, symbols
// One UTF-8 string of ten characters per row, except the symbols page whose
// rows are lists of single characters (the male and female signs have 3 bytes).
const char* const kKbPageRows[kKbPages][4] = {
    {"ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ.,'-", "0123456789"},
    {"abcdefghij", "klmnopqrst", "uvwxyz!?&:", "0123456789"},
    {"\xE2\x99\x82\xE2\x99\x80!?.,:;&-", "()'/+*#%=@", "\xC3\xA9\xC3\xA8\xC3\xA0\xC3\xA7\xC3\xB1\xC3\xBC\xC3\xB6\xC3\xA4\xC3\xAF\xC3\xAA", "0123456789"},
};
enum KbSpecial { kKbCase, kKbSpace, kKbDelete, kKbCancel, kKbOk };
const char* const kKbCaseLabels[kKbPages] = {"ABC", "abc", "#&+"};
const char* const kKbSpecialLabels[kKbSpecialKeys] = {"", "Space", "Del", "Cancel", "OK"};

// Character (UTF-8) of a character key (0-39) on a keyboard page.
std::string keyboardChar(kbcjk::Kind kind, int page, int key) {
    if (key < 0 || key >= kKbCharKeys) return " ";
    const int extra = kbcjk::pageCount(kind);
    const char* row = page < extra ? kbcjk::pageRow(kind, page, key / kKbColumns)
                                   : kKbPageRows[page - extra][key / kKbColumns];
    int index = key % kKbColumns;
    // Walk to the index-th UTF-8 character of the row.
    while (index > 0 && *row) {
        ++row;
        while ((static_cast<unsigned char>(*row) & 0xC0) == 0x80) ++row;
        --index;
    }
    const char* end = row;
    if (*end) {
        ++end;
        while ((static_cast<unsigned char>(*end) & 0xC0) == 0x80) ++end;
    }
    return std::string(row, end);
}

size_t utf8Length(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) n += (c & 0xC0) != 0x80 ? 1 : 0;
    return n;
}

void utf8PopBack(std::string& s) {
    while (!s.empty()) {
        unsigned char c = static_cast<unsigned char>(s.back());
        s.pop_back();
        if ((c & 0xC0) != 0x80) break;
    }
}

}  // namespace

void App::openKeyboard(KeyboardTarget target, const std::string& text, size_t maxLength) {
    kbTarget_ = target;
    kbReturn_ = screen_;
    kbText_ = text;
    kbMax_ = maxLength;
    kbKey_ = 0;
    kbPage_ = 0;
    // Japanese and Korean targets get their kana / jamo pages first.
    uint8_t language = 0;
    if (target == KeyboardTarget::Nickname || target == KeyboardTarget::OtName) language = edit_.current.language;
    else if (target == KeyboardTarget::BoxName || target == KeyboardTarget::TrainerName) language = save_ ? save_->language() : 0;
    kbKind_ = kbcjk::kindForLanguage(language);
    screen_ = Screen::Keyboard;
}

void App::keyboardPress(int key) {
    if (key < 0) return;
    if (key < kKbCharKeys) {
        std::string ch = keyboardChar(kbKind_, kbPage_, key);
        if (kbPage_ < kbcjk::pageCount(kbKind_)) {
            kbcjk::type(kbKind_, kbText_, ch, kbMax_);
            return;
        }
        if (utf8Length(kbText_) >= kbMax_) return;
        kbText_ += ch;
        return;
    }
    switch (key - kKbCharKeys) {
        case kKbCase:
            kbPage_ = (kbPage_ + 1) % (kKbPages + kbcjk::pageCount(kbKind_));
            break;
        case kKbSpace:
            if (utf8Length(kbText_) < kbMax_ && !kbText_.empty()) kbText_ += ' ';
            break;
        case kKbDelete:
            keyboardDelete();
            break;
        case kKbCancel:
            screen_ = kbReturn_;
            break;
        default:
            keyboardDone();
            break;
    }
}

void App::keyboardDelete() {
    if (kbKind_ == kbcjk::Kind::Korean) kbcjk::backspace(kbKind_, kbText_);
    else utf8PopBack(kbText_);
}

void App::keyboardDone() {
    std::string text = kbText_;
    while (!text.empty() && text.back() == ' ') text.pop_back();
    screen_ = kbReturn_;
    if (kbTarget_ == KeyboardTarget::TrainerName) {
        if (!text.empty()) trCurrent_.name = text;  // an empty name is never valid
        return;
    }
    if (kbTarget_ == KeyboardTarget::BankBoxName || kbTarget_ == KeyboardTarget::BankSearch) {
        bankKeyboardDone(text);
        return;
    }
    if (kbTarget_ == KeyboardTarget::BoxName) {
        if (text != save_->boxName(box_)) {
            if (save_->setBoxName(box_, text)) {
                unsaved_ = true;
            } else {
                showMessage("Box name", "The game cannot show some of\nthose characters.");
            }
        }
        return;
    }
    pokemon::Pokemon& p = edit_.current;
    if (kbTarget_ == KeyboardTarget::OtName) {
        if (!editorigin::setOtName(p, text) && !text.empty()) edit_.status = "The game cannot show that name";
        return;  // an empty name is never valid: keeps the old one
    }
    if (p.format <= 2 && !text.empty() && !text::canEncodeGb(p.format, text, p.language)) {
        edit_.status = "The game cannot show those characters";
        return;
    }
    if (kbKind_ != kbcjk::Kind::Latin && !text.empty() && !kbcjk::storable(p.format, p.language, text)) {
        edit_.status = "The game cannot show those characters";
        return;
    }
    std::string speciesName = pokemon::create4::defaultNickname(p.species, p.language, p.format);
    if (text.empty() || text == speciesName) {
        p.nickname = speciesName;
        p.isNicknamed = false;
    } else {
        p.nickname = text;
        p.isNicknamed = true;
    }
}

void App::updateKeyboard(const Input& in) {
    int zone = in.touched ? touchedZone(in) : -1;
    if (zone >= kZoneItem && zone < kZoneItem + kKbCharKeys + kKbSpecialKeys) {
        kbKey_ = zone - kZoneItem;
        keyboardPress(kbKey_);
        return;
    }
    // D-pad over a 10-column grid; the special row spans 2 columns per key.
    int row = kbKey_ < kKbCharKeys ? kbKey_ / kKbColumns : 4;
    int col = kbKey_ < kKbCharKeys ? kbKey_ % kKbColumns : (kbKey_ - kKbCharKeys) * 2;
    if (in.repeated & kKeyLeft) col = wrap(col - (row == 4 ? 2 : 1), kKbColumns);
    if (in.repeated & kKeyRight) col = wrap(col + (row == 4 ? 2 : 1), kKbColumns);
    if (in.repeated & kKeyUp) row = wrap(row - 1, 5);
    if (in.repeated & kKeyDown) row = wrap(row + 1, 5);
    kbKey_ = row == 4 ? kKbCharKeys + col / 2 : row * kKbColumns + col;
    if (in.pressed & kKeyA) keyboardPress(kbKey_);
    if (in.repeated & kKeyB) keyboardDelete();
    if (in.pressed & kKeySelect) kbPage_ = (kbPage_ + 1) % (kKbPages + kbcjk::pageCount(kbKind_));
    if (in.pressed & kKeyY && utf8Length(kbText_) < kbMax_ && !kbText_.empty()) kbText_ += ' ';
    if (in.pressed & kKeyStart) keyboardDone();
}

void App::drawKeyboard(Canvas& top, Canvas& bottom) {
    bool nickname = kbTarget_ == KeyboardTarget::Nickname;
    bool trainerName = kbTarget_ == KeyboardTarget::TrainerName;
    std::string kbTitle = nickname      ? "Nickname"
                          : trainerName ? "Trainer name"
                          : kbTarget_ == KeyboardTarget::OtName      ? "OT name"
                          : kbTarget_ == KeyboardTarget::BankBoxName ? "Bank box " + twoDigits(bankBox_ + 1) + " name"
                          : kbTarget_ == KeyboardTarget::BankSearch  ? "Search the Bank"
                                                                     : "Box " + twoDigits(box_ + 1) + " name";
    drawChrome(top, kbTitle,
               std::to_string(utf8Length(kbText_)) + "/" + std::to_string(kbMax_), kAccent);
    top.roundRect(4, kBodyTop + 3, 248, 150, kBorder, kPanel);
    if (nickname) {
        pokemon::Pokemon icon;
        icon.species = edit_.current.species;
        drawIcon(top, 12, 50, 40, 30, icon, false);
        top.text(kFontSmall, 58, 52, 180, Align::Left, data::speciesName(edit_.current.species), kMuted);
        top.text(kFontSmall, 58, 64, 180, Align::Left, "Empty = species name", kDim);
    } else {
        top.text(kFontSmall, 12, 52, 232, Align::Left, "Up to " + std::to_string(kbMax_) + " characters", kMuted);
    }
    top.roundRect(12, 90, 232, 30, kAccent, kPanelDark);
    int w = top.text(kFontLarge, 20, 95, 216, Align::Left, kbText_, kText);
    top.fillRect(21 + w, 97, 2, 18, kAccent);  // cursor
    top.text(kFontSmall, 12, 132, 232, Align::Left, kbKind_ == kbcjk::Kind::Latin ? "A: type   B: delete   Y: space   SEL: ABC/abc/#&"
                                                                : "A: type   B: delete   Y: space   SEL: page", kDim);
    top.text(kFontSmall, 12, 144, 232, Align::Left, "START: done", kDim);

    bottom.grid(0, 0, gfx::kScreenWidth, kHintTop, kBackground, kGridLine, 12);
    const int keyW = 23;
    const int keyH = 26;
    const int x0 = 8;
    const int y0 = 8;
    for (int key = 0; key < kKbCharKeys; ++key) {
        int x = x0 + (key % kKbColumns) * (keyW + 1);
        int y = y0 + (key / kKbColumns) * (keyH + 2);
        bool selected = key == kbKey_;
        bottom.roundRect(x, y, keyW, keyH, selected ? kAccent : kBorder, selected ? rgb(28, 58, 110) : kPanel);
        bottom.text(kFontLarge, x, y + 4, keyW, Align::Center, keyboardChar(kbKind_, kbPage_, key), kText);
        zones_.push_back({x, y, keyW, keyH, kZoneItem + key});
    }
    const int extra = kbcjk::pageCount(kbKind_);
    const char* caseLabel = kbPage_ < extra ? kbcjk::pageLabel(kbKind_, kbPage_) : kKbCaseLabels[kbPage_ - extra];
    for (int k = 0; k < kKbSpecialKeys; ++k) {
        int x = x0 + k * 2 * (keyW + 1);
        int y = y0 + 4 * (keyH + 2) + 4;
        int w2 = keyW * 2 + 1;
        int key = kKbCharKeys + k;
        bool selected = key == kbKey_;
        Color border = k == kKbOk ? kGreen : selected ? kAccent : kBorder;
        bottom.roundRect(x, y, w2, keyH, border, selected ? rgb(28, 58, 110) : kPanelDark);
        bottom.text(kFontSmall, x, y + 6, w2, Align::Center, k == kKbCase ? caseLabel : kKbSpecialLabels[k], k == kKbOk ? kGreen : kText);
        zones_.push_back({x, y, w2, keyH, kZoneItem + key});
    }
    drawHints(bottom, {{"A", "Type"}, {"B", "Del"}, {"ST", "Done"}}, {kKeyA, kKeyB, kKeyStart});
}

std::string App::showdownDir() const {
    return io::joinPath(config_.dataDir, "showdown");
}

std::string App::eventsDir() const {
    return io::joinPath(config_.dataDir, "events");
}

void App::openPicker(PickerKind kind) {
    pickerKind_ = kind;
    pickerRows_.clear();
    pickerSpecies_.clear();
    pickerInitials_.clear();
    eventNotes_.clear();
    pickerCursor_ = 0;
    pickerScroll_ = 0;
    previewFor_ = -1;
    switch (kind) {
        case PickerKind::AddMenu:
            addMenuIds_.clear();
            for (int id = 0; id < kAddMenuCount; ++id) {
                if (id == 2 && save_->format() <= 2) continue;  // Wonder Cards and events: Gen III-V
                if (id == 3 && save_->format() == 1) continue;  // Gen I has no Day-Care eggs
                addMenuIds_.push_back(id);
                pickerRows_.push_back(kAddMenuRows[id]);
            }
            pickerCursor_ = std::min(addMenuCursor_, static_cast<int>(addMenuIds_.size()) - 1);
            break;
        case PickerKind::Species:
            // Alphabetical, with the Dex number as a prefix.
            pickerSpecies_ = data::sortedIds(data::NameKind::Species, save_->format());
            for (uint16_t s : pickerSpecies_) {
                char number[16];
                std::snprintf(number, sizeof number, "%03d", s);
                pickerRows_.push_back(std::string(number) + "  " + data::speciesName(s));
                // L/R jump by initial letter only in alphabetical order (else a page).
                if (data::currentOrder() == data::Order::Alphabetical) {
                    pickerInitials_.push_back(data::initialOf(data::speciesName(s)));
                }
            }
            break;
        case PickerKind::Showdown: {
            showdownSets_.clear();
            showdownSources_.clear();
            for (const std::string& path : io::listFiles(showdownDir(), ".txt")) {
                std::vector<uint8_t> bytes;
                if (!io::readFile(path, bytes)) continue;
                for (showdown::Set& set : showdown::parse(std::string(bytes.begin(), bytes.end()))) {
                    if (set.species == 0) continue;
                    pickerRows_.push_back(set.title);
                    showdownSets_.push_back(std::move(set));
                    showdownSources_.push_back(io::fileName(path));
                }
            }
            break;
        }
        case PickerKind::Event: {
            // Gen V saves list the cards their game could receive.
            eventCards_.clear();
            // Cards are listed by species name (cards of one species keep table order).
            std::vector<std::pair<std::string, int>> cards;  // sort key, card index
            auto addCard = [&cards](int index, uint16_t species) {
                cards.emplace_back(data::foldName(data::speciesName(species)), index);
            };
            const uint8_t format = save_->format();
            if (format >= 5) {
                for (int i = 0; i < data::events5Count(); ++i) {
                    if (data::canReceiveEvent5(data::event5(i), save_->gameVersion())) addCard(i, data::event5(i).species);
                }
            } else if (format == 3) {
                for (int i = 0; i < data::events3Count(); ++i) addCard(i, data::event3(i).species);  // Gen III distributions
                for (int i : pokemon::cxd3::menuEntries()) addCard(kColoXdCard + i, data::gc3(i).species);  // Colosseum/XD
            } else {
                for (int i = 0; i < data::events4Count(); ++i) addCard(i, data::event4(i).species);
                addCard(kManaphyEggCard, 490);
            }
            std::sort(cards.begin(), cards.end());
            for (const auto& card : cards) {
                const bool v5 = format >= 5;
                const bool v3 = format == 3;
                const bool manaphy = card.second == kManaphyEggCard;
                const bool coloXd = v3 && card.second >= kColoXdCard;
                const uint16_t species = manaphy ? 490
                                         : coloXd ? data::gc3(card.second - kColoXdCard).species
                                         : v3    ? data::event3(card.second).species
                                         : v5    ? data::event5(card.second).species
                                                 : data::event4(card.second).species;
                const std::string coloXdTitle = coloXd ? pokemon::cxd3::title(data::gc3(card.second - kColoXdCard)) : "";
                const char* title = manaphy ? "Pokémon Ranger Manaphy egg"
                                    : coloXd ? coloXdTitle.c_str()
                                    : v3    ? data::event3(card.second).title
                                    : v5    ? data::event5(card.second).title
                                            : data::event4(card.second).title;
                char number[8];
                std::snprintf(number, sizeof number, "%03d", species);
                pickerRows_.push_back(std::string(number) + "  " + data::speciesName(species) + "  \xE2\x80\xA2  " + title);
                pickerInitials_.push_back(data::initialOf(data::speciesName(species)));
                eventCards_.push_back(card.second);
            }
            eventNotes_.assign(eventCards_.size(), std::string());
            if (format != 3) addCardFiles();
            break;
        }
    }
    screen_ = Screen::Picker;
}

// Appends the Wonder Card files of /PKTOOL/events/ to the Event picker. Cards that
// cannot be received (not a Pokémon, other generation, bad file) are listed too,
// greyed, with the reason in eventNotes_.
void App::addCardFiles() {
    fileCards_.clear();
    std::vector<std::string> paths;
    for (int e = 0; e < pokemon::cardfile::kExtensionCount; ++e) {
        for (std::string& path : io::listFiles(eventsDir(), pokemon::cardfile::kExtensions[e])) paths.push_back(std::move(path));
    }
    std::sort(paths.begin(), paths.end(), [](const std::string& a, const std::string& b) {
        return data::foldName(io::fileName(a).c_str()) < data::foldName(io::fileName(b).c_str());
    });
    if (static_cast<int>(paths.size()) > kMaxCardFiles) paths.resize(kMaxCardFiles);
    const uint8_t format = save_->format();
    for (const std::string& path : paths) {
        const std::string name = io::fileName(path);
        const size_t dot = name.find_last_of('.');
        std::vector<uint8_t> bytes;
        std::unique_ptr<pokemon::cardfile::Card> card;
        if (io::fileSize(path) <= 1024 && io::readFile(path, bytes)) {
            card = pokemon::cardfile::parse(bytes.data(), bytes.size(), name.substr(dot == std::string::npos ? 0 : dot), name);
        } else {
            card = pokemon::cardfile::parse(nullptr, 0, ".pgt", name);
            card->reason = "Not a card file (size)";
        }
        std::string note;
        if (card->status != pokemon::cardfile::Status::Ok) {
            note = card->reason;
        } else if (card->generation != (format >= 5 ? 5 : 4)) {
            note = card->generation == 5 ? "Gen V card: needs a Gen V save" : "Gen IV card: needs a Gen IV save";
        } else if (card->generation == 5 && !data::canReceiveEvent5(card->event5, save_->gameVersion())) {
            note = "This game cannot receive the card";
        }
        std::string row;
        if (note.empty()) {
            char number[8];
            std::snprintf(number, sizeof number, "%03d", card->species);
            row = std::string(number) + "  " + data::speciesName(card->species) + "  \xE2\x80\xA2  " + card->title + " (file)";
            pickerInitials_.push_back(data::initialOf(data::speciesName(card->species)));
        } else {
            row = "---  " + name + "  \xE2\x80\xA2  " + note;
            pickerInitials_.push_back('~');
        }
        eventCards_.push_back(kFileCardBase + static_cast<int>(fileCards_.size()));
        pickerRows_.push_back(row);
        eventNotes_.push_back(note);
        fileCards_.push_back(std::move(card));
    }
}

// Y in the species list: alphabetical <-> Dex order (the Settings option, saved
// to settings.ini), keeping the highlighted species.
void App::toggleSpeciesOrder() {
    const uint16_t current = pickerCursor_ < static_cast<int>(pickerSpecies_.size())
                                 ? pickerSpecies_[static_cast<size_t>(pickerCursor_)]
                                 : 0;
    settings_.sortById = !settings_.sortById;
    data::setOrder(settings_.sortById ? data::Order::ById : data::Order::Alphabetical);
    if (io::saveSettings(settingsPath(), settings_) == io::WriteResult::Ok) setSaved_ = settings_;
    openPicker(PickerKind::Species);
    for (size_t i = 0; i < pickerSpecies_.size(); ++i) {
        if (pickerSpecies_[i] == current) pickerCursor_ = static_cast<int>(i);
    }
}

void App::updatePicker(const Input& in) {
    int count = static_cast<int>(pickerRows_.size());
    int zone = in.touched ? touchedZone(in) : -1;
    bool activate = in.pressed & kKeyA;
    if (zone >= kZoneItem && zone < kZoneItem + count) {
        int index = zone - kZoneItem;
        activate = index == pickerCursor_;
        pickerCursor_ = index;
    }
    if (count > 0) {
        if (in.repeated & kKeyUp) pickerCursor_ = wrap(pickerCursor_ - 1, count);
        if (in.repeated & kKeyDown) pickerCursor_ = wrap(pickerCursor_ + 1, count);
        if (in.repeated & kKeyLeft) pickerCursor_ = clampInt(pickerCursor_ - 10, 0, count - 1);
        if (in.repeated & kKeyRight) pickerCursor_ = clampInt(pickerCursor_ + 10, 0, count - 1);
        // L/R: first entry of the previous/next initial letter in the name-sorted lists, else a page.
        const bool byLetter = static_cast<int>(pickerInitials_.size()) == count;
        const int direction = (in.repeated & kKeyR) ? 1 : (in.repeated & kKeyL) ? -1 : 0;
        if (direction != 0) {
            pickerCursor_ = byLetter ? data::jumpGroup(count, pickerCursor_, direction,
                                                       [this](int i) { return pickerInitials_[static_cast<size_t>(i)]; })
                                     : clampInt(pickerCursor_ + 10 * direction, 0, count - 1);
        }
    }
    if (pickerKind_ == PickerKind::Species && (in.pressed & kKeyY)) {
        toggleSpeciesOrder();
        return;
    }
    if (in.pressed & kKeyB) {
        if (pickerKind_ == PickerKind::AddMenu) {
            screen_ = Screen::Box;
        } else {
            openPicker(PickerKind::AddMenu);
        }
        return;
    }
    if (!activate || count == 0) return;
    if (pickerKind_ == PickerKind::AddMenu) {
        addMenuCursor_ = pickerCursor_;
        const PickerKind next[] = {PickerKind::Species, PickerKind::Showdown, PickerKind::Event, PickerKind::Species};
        const int id = addMenuIds_[static_cast<size_t>(pickerCursor_)];
        eggMode_ = id == 3;
        openPicker(next[id]);
        return;
    }
    if (pickerKind_ == PickerKind::Event && pickerCursor_ < static_cast<int>(eventNotes_.size()) &&
        !eventNotes_[static_cast<size_t>(pickerCursor_)].empty()) {
        showMessage("Cannot receive", eventNotes_[static_cast<size_t>(pickerCursor_)]);
        return;
    }
    pending_ = Pending::Create;  // runs next frame, after "Working..." is shown
}

pokemon::create4::Trainer App::saveTrainer() const {
    pokemon::create4::Trainer t;
    t.name = save_->trainerName();
    t.tid = save_->trainerId();
    t.sid = save_->secretId();
    t.gender = save_->trainerGender() ? pokemon::Gender::Female : pokemon::Gender::Male;
    t.language = save_->language();
    t.version = save_->gameVersion();
    std::time_t now = std::time(nullptr);
    if (const std::tm* local = std::localtime(&now)) {
        t.today.year = static_cast<uint8_t>(clampInt(local->tm_year - 100, 0, 99));
        t.today.month = static_cast<uint8_t>(local->tm_mon + 1);
        t.today.day = static_cast<uint8_t>(local->tm_mday);
    }
    if (t.today.month == 0) t.today = {9, 1, 1};  // no clock: 2009-01-01
    return t;
}

uint32_t App::nextSeed() {
    ++seedCounter_;
    return static_cast<uint32_t>(std::time(nullptr)) * 2654435761u + seedCounter_ * 0x9E3779B9u;
}

// Quick model of the selected picker row for the top screen (not legalized).
const pokemon::Pokemon& App::pickerPreview() {
    if (previewFor_ == pickerCursor_) return preview_;
    previewFor_ = pickerCursor_;
    pokemon::create4::Trainer trainer = saveTrainer();
    switch (pickerKind_) {
        case PickerKind::Species:
        {
            uint16_t species = pickerSpecies_[static_cast<size_t>(pickerCursor_)];
            uint8_t level = std::max(settings_.newLevel, minCreateLevel(save_->format(), species));
            preview_ = pokemon::create4::newPokemon(trainer, species, eggMode_ ? 1 : level, 1);
        }
            break;
        case PickerKind::Showdown: {
            const showdown::Set& set = showdownSets_[pickerCursor_];
            if (save_->format() <= 2) {  // DVs and Stat Exp: let the importer map the set
                preview_ = showdown::buildGen4(set, trainer, 1, 0).pokemon;
                break;
            }
            pokemon::Pokemon& p = preview_;
            p = pokemon::create4::newPokemon(trainer, set.species, set.level ? set.level : 1, 1);
            if (!set.nickname.empty()) {
                p.nickname = set.nickname;
                p.isNicknamed = true;
            }
            p.heldItem = set.item;
            if (set.ability) p.ability = set.ability;
            if (set.nature >= 0) p.nature = static_cast<uint8_t>(set.nature);
            if (set.gender >= 0) p.gender = static_cast<pokemon::Gender>(set.gender);
            p.shiny = set.shiny;
            for (int s = 0; s < pokemon::kStatCount; ++s) {
                p.ivs[s] = set.ivs[s];
                p.evs[s] = set.evs[s];
            }
            if (set.moves[0] || set.moves[1] || set.moves[2] || set.moves[3]) {
                for (int m = 0; m < 4; ++m) edit4::setMove(p, m, 0);
                for (int m = 0; m < 4; ++m) edit4::setMove(p, m, set.moves[m]);
            }
            break;
        }
        case PickerKind::Event: {
            // Cards that cannot be received have nothing to preview.
            preview_ = eventNotes_[static_cast<size_t>(pickerCursor_)].empty() ? receiveEvent(1) : pokemon::Pokemon();
            break;
        }
        case PickerKind::AddMenu:
            preview_ = pokemon::Pokemon();
            break;
    }
    return preview_;
}

void App::createPokemon() {
    pokemon::create4::Trainer trainer = saveTrainer();
    uint8_t raw[pokemon::entity::kStoredSize];
    switch (pickerKind_) {
        case PickerKind::Species: {
            uint16_t species = pickerSpecies_[static_cast<size_t>(pickerCursor_)];
            if (eggMode_) {
                pokemon::Pokemon egg = pokemon::create4::newEgg(trainer, species, nextSeed());
                legality::LegalizeResult result = legality::legalize(egg, kLegalizeEffort);
                std::vector<std::string> notes;
                if (egg.species != species) {
                    notes.push_back(std::string("Eggs hatch into ") + data::speciesName(egg.species));
                }
                pokemon::create4::encode(result.pokemon, raw);
                startCreatedEdit(result.pokemon, raw, "Egg", {}, notes);
                break;
            }
            // Level 50, or the lowest level the species can be met at (e.g. Mewtwo: 70).
            uint8_t level = std::max(settings_.newLevel, minCreateLevel(save_->format(), species));
            pokemon::Pokemon p = pokemon::create4::newPokemon(trainer, species, level, nextSeed());
            legality::LegalizeResult result = legality::legalize(p, kLegalizeEffort);
            // The PID and IVs were random anyway: only report the encounter.
            std::vector<std::string> changes;
            for (const std::string& c : result.changes) {
                if (c.compare(0, 7, "New PID") != 0 && c.compare(0, 4, "IVs ") != 0) changes.push_back(c);
            }
            std::vector<std::string> notes;
            bool hasCard = save_->format() <= 2   ? false
                           : save_->format() >= 5 ? data::findEvent5(species) >= 0
                           : save_->format() == 3 ? data::findEvent3(species) >= 0
                                                  : data::findEvent4(species) >= 0;
            if (!result.unresolved.empty() && hasCard) {
                notes.push_back("Try Add > Event: it has an event");
            }
            pokemon::create4::encode(result.pokemon, raw);
            startCreatedEdit(result.pokemon, raw, "New Pok\xC3\xA9mon", changes, notes);
            break;
        }
        case PickerKind::Showdown: {
            showdown::Imported result =
                showdown::buildGen4(showdownSets_[pickerCursor_], trainer, nextSeed(), kLegalizeEffort);
            if (result.pokemon.species == 0) {
                showMessage("Cannot import", result.warnings.empty() ? "Unsupported set." : result.warnings[0]);
                break;
            }
            pokemon::create4::encode(result.pokemon, raw);
            startCreatedEdit(result.pokemon, raw, "Showdown set", result.changes, result.warnings);
            break;
        }
        case PickerKind::Event: {
            pokemon::Pokemon p = receiveEvent(nextSeed(), raw);
            if (p.species == 0) {
                showMessage("Cannot create", eventError_);
                break;
            }
            if (save_->format() == 3 && eventCards_[pickerCursor_] >= kColoXdCard) {
                startCreatedEdit(p, raw, "Colosseum/XD", {"Generated with this save's trainer IDs"}, {});
            } else if (save_->format() == 3) {
                startCreatedEdit(p, raw, "Event", {"Received like the distribution gave it"}, {});
            } else {
                startCreatedEdit(p, raw, "Wonder Card", {"Received from the card"}, {});
            }
            break;
        }
        case PickerKind::AddMenu:
            break;
    }
}

pokemon::Pokemon App::receiveEvent(uint32_t seed, uint8_t* raw) {
    uint8_t scratch[pokemon::entity::kStoredSize];
    if (raw == nullptr) raw = scratch;
    int card = eventCards_[pickerCursor_];
    pokemon::create4::Trainer trainer = saveTrainer();
    if (card >= kFileCardBase) {
        const pokemon::cardfile::Card& file = *fileCards_[static_cast<size_t>(card - kFileCardBase)];
        if (file.generation == 5) return pokemon::gift5::receive(file.event5, trainer, seed, raw);
        return pokemon::gift4::receive(file.event4, trainer.today, seed, raw);
    }
    if (save_->format() >= 5) return pokemon::gift5::receive(data::event5(card), trainer, seed, raw);
    if (save_->format() == 3 && card >= kColoXdCard) {
        pokemon::Pokemon p = pokemon::cxd3::create(card - kColoXdCard, trainer, seed, pokemon::gift3::Criteria(), raw,
                                                   nullptr, &eventError_);
        return p;
    }
    if (save_->format() == 3) return pokemon::gift3::receive(data::event3(card), trainer, seed, pokemon::gift3::Criteria(), raw);
    if (card == kManaphyEggCard) return pokemon::gift4::receiveManaphyEgg(trainer, seed, raw);
    return pokemon::gift4::receive(data::event4(card), trainer.today, seed, raw);
}

const char* App::eventTitle() const {
    int card = eventCards_[pickerCursor_];
    if (card >= kFileCardBase) return fileCards_[static_cast<size_t>(card - kFileCardBase)]->fileName.c_str();
    if (save_->format() == 3 && card >= kColoXdCard) {
        static std::string coloXdTitle;
        coloXdTitle = pokemon::cxd3::title(data::gc3(card - kColoXdCard));
        return coloXdTitle.c_str();
    }
    if (save_->format() == 3) return data::event3(card).title;
    if (card == kManaphyEggCard) return "Pok\xC3\xA9mon Ranger Manaphy egg";
    return save_->format() >= 5 ? data::event5(card).title : data::event4(card).title;
}

void App::startCreatedEdit(const pokemon::Pokemon& p, const uint8_t* raw, const std::string& title,
                           const std::vector<std::string>& changes, const std::vector<std::string>& notes) {
    EditState& e = edit_;
    e = EditState();
    e.party = false;
    e.box = box_;
    e.slot = slotCursor_;
    e.created = true;
    std::memcpy(e.raw, raw, pokemon::entity::kStoredSize);
    e.original = p;
    e.current = p;
    editScroll_ = 0;
    screen_ = Screen::Edit;

    // Result dialog: problems first, then notes, the changes made and the checks passed.
    legality::Report report = legality::check(p);
    dialogReport_ = legality::Report();
    for (const auto& f : report.findings) {
        if (f.severity == legality::Severity::Invalid) dialogReport_.findings.push_back(f);
    }
    for (const std::string& n : notes) dialogReport_.findings.push_back({legality::Severity::Warning, n});
    for (const std::string& c : changes) dialogReport_.findings.push_back({legality::Severity::Valid, c});
    for (const auto& f : report.findings) {
        if (f.severity != legality::Severity::Invalid) dialogReport_.findings.push_back(f);
    }
    dialogTitle_ = title + (report.legal() ? " (legal)" : " (not legal)");
    dialog_ = Dialog::Legality;
}

// =============================================================================
// Drawing: shared pieces

void App::draw(Canvas& top, Canvas& bottom) {
    zones_.clear();
    top.resetClip();
    bottom.resetClip();
    switch (screen_) {
        case Screen::Home:
            drawHome(top, bottom);
            break;
        case Screen::Browser:
            drawBrowser(top, bottom);
            break;
        case Screen::SaveMenu:
            drawSaveMenu(top, bottom);
            break;
        case Screen::Box:
            drawBox(top, bottom);
            break;
        case Screen::Edit:
            drawEdit(top, bottom);
            break;
        case Screen::Backups:
            drawBackups(top, bottom);
            break;
        case Screen::Picker:
            drawPicker(top, bottom);
            break;
        case Screen::Bank:
            drawBank(top, bottom);
            break;
        case Screen::Keyboard:
            drawKeyboard(top, bottom);
            break;
        case Screen::Trainer:
            drawTrainer(top, bottom);
            break;
        case Screen::Bag:
            drawBag(top, bottom);
            break;
        case Screen::ItemPicker:
            drawItemPicker(top, bottom);
            break;
        case Screen::Ribbons:
            drawRibbons(top, bottom);
            break;
        case Screen::Settings:
            drawSettings(top, bottom);
            break;
    }
    if (pending_ != Pending::None) {
        bottom.blendRect(0, 0, gfx::kScreenWidth, gfx::kScreenHeight, kTitleBar, 20);
        bottom.roundRect(58, 78, 140, 34, kAccent, kPanel);
        const char* text = pending_ == Pending::Scan       ? "Scanning SD card..."
                           : pending_ == Pending::Legalize ? "Legalizing..."
                           : pending_ == Pending::Create   ? "Creating..."
                           : pending_ == Pending::GbaBackup ? "Reading GBA cartridge..."
                                                           : "Working...";
        bottom.text(kFontSmall, 58, 88, 140, Align::Center, text, kText);
        zones_.clear();
    } else if (dialog_ != Dialog::None) {
        drawDialog(bottom);
    }
    if (fadeAlpha() > 0 && !config_.hardwareFade) {
        const int alpha = fadeAlpha();
        top.blendRect(0, 0, gfx::kScreenWidth, gfx::kScreenHeight, kBackground, alpha);
        bottom.blendRect(0, 0, gfx::kScreenWidth, gfx::kScreenHeight, kBackground, alpha);
    }
    redraw_ = false;
}

void App::drawChrome(Canvas& c, const std::string& status, const std::string& badge, Color badgeColor,
                     Color statusColor) {
    c.grid(0, kBodyTop, gfx::kScreenWidth, gfx::kScreenHeight - kBodyTop, kBackground, kGridLine, 12);
    c.fillRect(0, 0, gfx::kScreenWidth, kTitleHeight, kTitleBar);
    c.text(kFontLarge, 0, 0, gfx::kScreenWidth, Align::Center, "PKTool", kText);
    c.hline(10, kTitleHeight - 2, gfx::kScreenWidth - 20, kAccent);
    c.fillRect(0, kStatusTop, gfx::kScreenWidth, kStatusHeight, kStatusBar);
    c.hline(0, kBodyTop - 1, gfx::kScreenWidth, kBorder);

    int badgeWidth = badge.empty() ? 0 : Canvas::textWidth(kFontSmall, badge) + 12;
    c.text(kFontSmall, 6, kStatusTop + 1, gfx::kScreenWidth - 16 - badgeWidth, Align::Left, status,
           statusColor == 0 ? kText : statusColor);
    if (!badge.empty()) {
        int x = gfx::kScreenWidth - badgeWidth - 4;
        c.fillCircle(x + 3, kStatusTop + 7, 2, badgeColor);
        c.text(kFontSmall, x + 8, kStatusTop + 1, badge, kMuted);
    } else {
        // Battery-style status pill, as in the reference design.
        c.roundRect(gfx::kScreenWidth - 22, kStatusTop + 4, 16, 8, kBorderLight, 0);
        c.fillRect(gfx::kScreenWidth - 20, kStatusTop + 6, 12, 4, kGreen);
    }
}

void App::drawHints(Canvas& c, const std::vector<std::pair<std::string, std::string>>& hints,
                    const std::vector<int>& ids) {
    c.fillRect(0, kHintTop, gfx::kScreenWidth, gfx::kScreenHeight - kHintTop, kTitleBar);
    c.hline(0, kHintTop, gfx::kScreenWidth, kBorder);
    std::vector<int> widths;
    int total = 0;
    for (const auto& hint : hints) {
        int badge = std::max(11, Canvas::textWidth(kFontSmall, hint.first) + 6);
        int w = badge + 3 + Canvas::textWidth(kFontSmall, hint.second);
        widths.push_back(w);
        total += w;
    }
    int gap = hints.size() > 1
                  ? std::max(4, std::min(12, (gfx::kScreenWidth - 8 - total) / static_cast<int>(hints.size() - 1)))
                  : 0;
    total += gap * static_cast<int>(hints.size() > 0 ? hints.size() - 1 : 0);
    int x = (gfx::kScreenWidth - total) / 2;
    for (size_t i = 0; i < hints.size(); ++i) {
        int badge = std::max(11, Canvas::textWidth(kFontSmall, hints[i].first) + 6);
        int y = kHintTop + 3;
        if (badge == 11) {
            c.fillCircle(x + 5, y + 5, 5, kAccent);
        } else {
            c.roundRect(x, y, badge, 11, kAccent, kAccent);
        }
        if (hints[i].first == "+") {
            // D-pad glyph.
            c.fillRect(x + 4, y + 2, 3, 7, kTitleBar);
            c.fillRect(x + 2, y + 4, 7, 3, kTitleBar);
        } else {
            c.text(kFontSmall, x, y - 1, badge, Align::Center, hints[i].first, kTitleBar);
        }
        c.text(kFontSmall, x + badge + 3, y - 1, hints[i].second, kText);
        if (i < ids.size() && ids[i] > 0) {
            zones_.push_back({x - 2, kHintTop, widths[i] + 4, gfx::kScreenHeight - kHintTop, ids[i]});
        }
        x += widths[i] + gap;
    }
}

void App::drawDialog(Canvas& c) {
    zones_.clear();
    c.blendRect(0, 0, gfx::kScreenWidth, gfx::kScreenHeight, kTitleBar, 18);
    std::vector<std::string> lines;
    std::vector<int> lineSeverity;  // per line: severity for the dot, -1 for continuation lines
    std::string title = dialogTitle_;
    switch (dialog_) {
        case Dialog::ConfirmWrite:
            title = "Write to SD card?";
            lines = {"The current file is backed up", "first, then replaced safely."};
            if (bankDirty_ || !bankWithdrawn_.empty()) lines.push_back("The Bank is updated too.");
            break;
        case Dialog::ConfirmClose:
            title = "Discard changes?";
            lines = {"Your edits have not been", "written to the SD card."};
            break;
        case Dialog::ConfirmDiscard:
            title = "Discard edits?";
            lines = {screen_ == Screen::Trainer ? "Changes to the trainer card"
                     : screen_ == Screen::Bag   ? "Changes to the bag"
                     : screen_ == Screen::Ribbons ? "Changes to the ribbons"
                                                : "Changes to this Pok\xC3\xA9mon",
                     "will be lost."};
            break;
        case Dialog::ConfirmRestore:
            title = "Restore this backup?";
            lines = {backupLabel(io::fileName(savePath_), backups_.empty() ? "" : backups_[backupCursor_]),
                     "The current save is backed up first."};
            break;
        case Dialog::ConfirmRelease: {
            pokemon::Pokemon p;
            const bool inBank = screen_ == Screen::Bank;
            if (inBank) {
                if (bank_.entity(bankBox_, bankCursor_)) {
                    p = pokemon::entity::decode(bank_.format(bankBox_, bankCursor_), bank_.entity(bankBox_, bankCursor_),
                                                pokemon::entity::kStoredSize);
                }
            } else {
                readSlot(box_, slotCursor_, p);
            }
            title = "Release this Pok\xC3\xA9mon?";
            lines = {p.isEgg ? std::string("Egg") : data::speciesName(p.species)};
            if (inBank) {
                lines.push_back("It is removed from the Bank.");
            } else {
                lines.push_back("It is removed from the save");
                lines.push_back("once you write it to the SD card.");
            }
            break;
        }
        case Dialog::Legality: {
            // Each finding may wrap onto two lines; lineSeverity keeps the dot colour.
            lineSeverity.clear();
            for (const auto& f : dialogReport_.findings) {
                std::vector<std::string> wrapped = wrapText(f.text, 200, 2);
                for (size_t k = 0; k < wrapped.size() && lines.size() < 10; ++k) {
                    lines.push_back(wrapped[k]);
                    lineSeverity.push_back(k == 0 ? static_cast<int>(f.severity) : -1);
                }
            }
            break;
        }
        default:
            lines = splitLines(dialogText_);
            break;
    }
    int lineHeight = 11;
    int h = 44 + static_cast<int>(lines.size()) * lineHeight;
    int y = (gfx::kScreenHeight - h) / 2;
    c.roundRect(16, y, 224, h, kAccent, kPanel, 1);
    c.fillRect(17, y + 1, 222, 16, kPanelDark);
    c.text(kFontSmall, 16, y + 2, 224, Align::Center, title, kText);
    c.hline(17, y + 17, 222, kBorder);
    if (dialog_ == Dialog::Legality) {
        Color titleColor = dialogReport_.overall() == legality::Severity::Invalid   ? kRed
                           : dialogReport_.overall() == legality::Severity::Warning ? kYellow
                                                                                     : kGreen;
        c.fillRect(17, y + 1, 222, 16, kPanelDark);
        c.text(kFontSmall, 16, y + 2, 224, Align::Center, title, titleColor);
        int current = 0;
        for (size_t i = 0; i < lines.size(); ++i) {
            int ly = y + 20 + static_cast<int>(i) * lineHeight;
            if (lineSeverity[i] >= 0) {
                current = lineSeverity[i];
                auto severity = static_cast<legality::Severity>(current);
                Color dot = severity == legality::Severity::Invalid   ? kRed
                            : severity == legality::Severity::Warning ? kYellow
                                                                       : kGreen;
                c.fillCircle(25, ly + 6, 2, dot);
            }
            c.text(kFontSmall, 31, ly, 203, Align::Left, lines[i],
                   static_cast<legality::Severity>(current) == legality::Severity::Valid ? kMuted : kText);
        }
    } else {
        for (size_t i = 0; i < lines.size(); ++i) {
            c.text(kFontSmall, 22, y + 20 + static_cast<int>(i) * lineHeight, 212, Align::Center, lines[i], kMuted);
        }
    }

    int buttonsY = y + h - 20;
    if (dialog_ == Dialog::Message || dialog_ == Dialog::Legality) {
        c.roundRect(88, buttonsY, 80, 15, kAccent, kPanelDark);
        c.fillCircle(100, buttonsY + 7, 5, kAccent);
        c.text(kFontSmall, 95, buttonsY + 1, 11, Align::Center, "A", kTitleBar);
        c.text(kFontSmall, 108, buttonsY + 1, "OK", kText);
        zones_.push_back({88, buttonsY, 80, 15, kKeyA});
    } else {
        c.roundRect(40, buttonsY, 80, 15, kGreen, kPanelDark);
        c.fillCircle(52, buttonsY + 7, 5, kGreen);
        c.text(kFontSmall, 47, buttonsY + 1, 11, Align::Center, "A", kTitleBar);
        c.text(kFontSmall, 60, buttonsY + 1, "Yes", kText);
        c.roundRect(136, buttonsY, 80, 15, kBorderLight, kPanelDark);
        c.fillCircle(148, buttonsY + 7, 5, kBorderLight);
        c.text(kFontSmall, 143, buttonsY + 1, 11, Align::Center, "B", kTitleBar);
        c.text(kFontSmall, 156, buttonsY + 1, "No", kText);
        zones_.push_back({40, buttonsY, 80, 15, kKeyA});
        zones_.push_back({136, buttonsY, 80, 15, kKeyB});
    }
}

void App::drawList(Canvas& c, const std::vector<std::string>& rows, int cursor, int& scroll, int x, int y, int w,
                   int rowHeight, int visibleRows, const std::vector<std::string>* dimmed) {
    const int oldScroll = scroll;
    if (cursor < scroll) scroll = cursor;
    if (cursor >= scroll + visibleRows) scroll = cursor - visibleRows + 1;
    scroll = clampInt(scroll, 0, std::max(0, static_cast<int>(rows.size()) - visibleRows));
    const int count = static_cast<int>(rows.size());
    const int shown = std::min(visibleRows, std::max(0, count - scroll));

    // Selection bar: glides to its row (a few frames) when animations are on.
    int barY = y + (cursor - scroll) * rowHeight;
    const bool selectedVisible = cursor >= scroll && cursor < scroll + shown;
    if (selectedVisible && settings_.animations) {
        const int target = barY * kEaseFrac;
        if (easeKey_ != &scroll || scroll != oldScroll || std::abs(easeY_ - target) > rowHeight * 3 * kEaseFrac) {
            easeY_ = target;  // another list, a scrolled window or a long jump: no glide
            easeKey_ = &scroll;
            easeFrame_ = frame_;
        }
        int steps = static_cast<int>(std::min<uint32_t>(frame_ - easeFrame_, 6));
        for (; steps > 0 && easeY_ != target; --steps) {
            int diff = target - easeY_;
            easeY_ += diff / 2 != 0 ? diff / 2 : diff;  // halve the distance, finish the last sub-pixel
        }
        easeFrame_ = frame_;
        easing_ = easeY_ != target;
        barY = (easeY_ + kEaseFrac / 2) / kEaseFrac;
    } else if (!selectedVisible || !settings_.animations) {
        easeKey_ = nullptr;
    }

    for (int i = 0; i < shown; ++i) {  // row bands
        const int ry = y + i * rowHeight;
        if (scroll + i != cursor && (scroll + i) % 2 == 0) c.fillRect(x + 1, ry, w - 2, rowHeight - 1, kPanelDark);
    }
    if (selectedVisible) c.roundRect(x, barY, w, rowHeight - 1, kAccent, rgb(28, 58, 110));
    for (int i = 0; i < shown; ++i) {  // texts and touch zones
        const int index = scroll + i;
        const int ry = y + i * rowHeight;
        const bool selected = index == cursor;
        c.text(kFontSmall, x + 6, ry + (rowHeight - 13) / 2, w - 12, Align::Left, rows[index],
               dimmed != nullptr && !(*dimmed)[static_cast<size_t>(index)].empty() ? kDim : selected ? kText : kMuted);
        zones_.push_back({x, ry, w, rowHeight, kZoneItem + index});
    }
    // Scrollbar.
    if (static_cast<int>(rows.size()) > visibleRows) {
        int trackH = visibleRows * rowHeight;
        int thumbH = std::max(8, trackH * visibleRows / static_cast<int>(rows.size()));
        int thumbY = y + (trackH - thumbH) * scroll / std::max(1, static_cast<int>(rows.size()) - visibleRows);
        c.fillRect(x + w + 2, y, 2, trackH, kPanelDark);
        c.fillRect(x + w + 2, thumbY, 2, thumbH, kAccent);
    }
}

void App::drawIcon(Canvas& c, int x, int y, int w, int h, const pokemon::Pokemon& p, bool showName) {
    gfx::Image icon = sprites_.get("icons", p.isEgg ? 0 : p.species);
    if (icon.pixels != nullptr) {
        c.image(x + (w - icon.width) / 2, y + (h - icon.height) / 2, icon);
        return;
    }
    // Placeholder: coloured emblem, optionally with the start of the name.
    int cx = x + w / 2;
    int cy = showName ? y + 9 : y + h / 2;
    if (p.isEgg) {
        c.fillCircle(cx, cy, 6, rgb(236, 230, 200));
        c.circle(cx, cy, 6, kTitleBar);
    } else {
        drawBallEmblem(c, cx, cy, 6, speciesColor(p.species), kText);
    }
    if (showName) {
        std::string name = p.isEgg ? "Egg" : data::speciesName(p.species);
        c.text(kFontSmall, x, y + h - 14, w, Align::Center, name, kText);
    }
}

legality::Severity App::badgeSeverity(const pokemon::Pokemon& p) {
    if (!badgeValid_ || !(badgeFor_ == p)) {
        badgeSeverity_ = legality::check(p).overall();
        badgeFor_ = p;
        badgeValid_ = true;
    }
    return badgeSeverity_;
}

// Summary of one Pokémon on the top screen (below the status bar).
void App::drawSummary(Canvas& c, const pokemon::Pokemon* p, bool badData, bool badge) {
    const int px = 4;
    const int py = kBodyTop + 3;
    const int pw = gfx::kScreenWidth - 8;
    const int ph = gfx::kScreenHeight - py - 3;
    c.roundRect(px, py, pw, ph, kBorder, kPanel);

    if (p == nullptr) {
        drawBallEmblem(c, 128, 96, 18, kDim, kPanelDark);
        c.text(kFontSmall, px, 124, pw, Align::Center, badData ? "Bad data (checksum mismatch)" : "Empty slot",
               badData ? kRed : kMuted);
        return;
    }

    // Sprite area.
    const int sx = px + 5;
    const int sy = py + 5;
    const int ss = 78;
    c.roundRect(sx, sy, ss, ss, kBorder, kPanelDark);
    c.setClip(sx + 1, sy + 1, ss - 2, ss - 2);  // large sprites (80x80) are clipped to the frame
    // Species 0 of every sprite kind is the egg.
    gfx::Image front = p->isEgg ? sprites_.get("front", 0) : sprites_.get(p->shiny ? "front-shiny" : "front", p->species);
    const int bob = bobOffset();  // the Pokémon bobs one pixel (Settings > Animations)
    if (p->isEgg && front.pixels == nullptr) {
        c.fillCircle(sx + ss / 2, sy + ss / 2 + bob, 18, rgb(236, 230, 200));
        c.circle(sx + ss / 2, sy + ss / 2 + bob, 18, kTitleBar);
    } else if (front.pixels != nullptr) {
        c.image(sx + (ss - front.width) / 2, sy + (ss - front.height) / 2 + bob, front);
    } else {
        gfx::Image icon = sprites_.get("icons", p->species);
        if (icon.pixels != nullptr && icon.width * 2 <= ss) {
            c.imageScaled(sx + (ss - icon.width * 2) / 2, sy + (ss - icon.height * 2) / 2, icon, 2);
        } else {
            drawBallEmblem(c, sx + ss / 2, sy + ss / 2 - 6, 20, speciesColor(p->species), kText);
            c.text(kFontSmall, sx, sy + ss - 18, ss, Align::Center, "#" + std::to_string(p->species), kMuted);
        }
    }
    c.resetClip();
    if (p->shiny) drawStar(c, sx + ss - 11, sy + 4, kYellow);

    // Legality badge at the bottom of the sprite frame.
    if (!p->isEgg && badge) {
        auto overall = badgeSeverity(*p);
        const char* label = overall == legality::Severity::Invalid   ? "ILLEGAL"
                            : overall == legality::Severity::Warning ? "CHECK"
                                                                      : "LEGAL";
        Color color = overall == legality::Severity::Invalid   ? kRed
                      : overall == legality::Severity::Warning ? kYellow
                                                                : kGreen;
        int bw = Canvas::textWidth(kFontSmall, label) + 10;
        int bx = sx + (ss - bw) / 2;
        int by = sy + ss - 14;
        c.roundRect(bx, by, bw, 12, color, kPanelDark);
        c.text(kFontSmall, bx, by - 1, bw, Align::Center, label, color);
    }

    // Name and basic info.
    const int ix = sx + ss + 7;
    const int iw = px + pw - ix - 5;
    std::string name = p->isEgg ? "Egg" : (p->isNicknamed ? p->nickname : data::speciesName(p->species));
    int nameWidth = c.text(kFontLarge, ix, py + 2, iw - 10, Align::Left, name, kText);
    drawGenderGlyph(c, ix + nameWidth + 3, py + 7, p->gender);
    std::string line = "Lv. " + std::to_string(p->level) + "   #" + std::to_string(p->species);
    if (p->isNicknamed && !p->isEgg) line += "  " + std::string(data::speciesName(p->species));
    c.text(kFontSmall, ix, py + 19, iw, Align::Left, line, kMuted);

    struct Row {
        const char* label;
        std::string value;
    };
    char id[24];
    std::snprintf(id, sizeof(id), "%05u", p->trainer.tid);
    const bool gb = p->format <= 2;
    std::string metText = "-";
    if (p->format == 2 && hasCaughtData(*p)) {
        metText = std::string(data::locationName2(p->met.metLocation)) + " Lv." + std::to_string(p->met.metLevel);
    }
    const Row rows[] = {
        gb ? Row{"Type", gbTypeText(*p)} : Row{"Nature", data::natureName(p->nature)},
        gb ? (p->format == 1 ? Row{"Catch", std::to_string(p->catchRate)} : Row{"Friend.", std::to_string(p->friendship)})
           : Row{"Ability", data::abilityName(p->ability)},
        gb ? (p->format == 1 ? Row{"EXP", std::to_string(p->exp)} : Row{"Item", data::itemName(p->heldItem)})
           : Row{"Item", data::itemName(p->heldItem)},
        {"OT", p->trainer.name + "  " + id},
        gb ? (p->format == 1 ? Row{"DVs", gbDvText(*p)} : Row{"Met", metText}) : Row{"Ball", data::ballName(p->met.ball)},
    };
    const int labelW = 44;
    for (int i = 0; i < 5; ++i) {
        int ry = py + 31 + i * 11;
        c.text(kFontSmall, ix, ry, rows[i].label, kMuted);
        c.text(kFontSmall, ix + labelW, ry, iw - labelW, Align::Left, rows[i].value, kText);
    }

    // Stats: 3x2 grid with value and an IV bar.
    const int statY = py + 88;
    uint16_t stats[pokemon::kStatCount] = {};
    if (gb) {
        pokemon::gb::calcStats(p->format, p->species, p->originGame == 41 || p->originGame == 38, p->ivs, p->statExp,
                               p->level, stats);
    } else if (const data::PersonalInfo* info = data::personalFor(p->format, p->species)) {
        pokemon::calcStats(p->species, info->baseStats, p->ivs, p->evs, p->level, p->nature, stats);
    }
    const int colW = (pw - 10) / 3;
    for (int s = 0; s < pokemon::kStatCount; ++s) {
        if (gb && p->format == 1 && s == pokemon::kSpD) break;  // one Special stat
        int cx = px + 5 + (s % 3) * colW;
        int cy = statY + (s / 3) * 14;
        int barW = colW - 8;
        const int maxIv = gb ? 15 : 31;
        Color labelColor = kMuted;
        if (!gb) {
            uint8_t mod = pokemon::natureModifier(p->nature, static_cast<pokemon::Stat>(s));
            labelColor = mod > 10 ? kRed : (mod < 10 ? kMale : kMuted);
        }
        const char* statName = gb && p->format == 1 && s == pokemon::kSpA ? "Spc" : kStatNames[s];
        c.text(kFontSmall, cx, cy, statName, labelColor);
        c.text(kFontSmall, cx, cy, barW, Align::Right, std::to_string(stats[s]), kText);
        c.fillRect(cx, cy + 11, barW, 2, kPanelDark);
        int fill = barW * std::min<int>(p->ivs[s], maxIv) / maxIv;
        c.fillRect(cx, cy + 11, fill, 2, p->ivs[s] == maxIv ? kGreen : kAccent);
    }

    // Moves: 2x2 pills.
    const int movesY = statY + 30;
    const int pillW = (pw - 13) / 2;
    for (int m = 0; m < 4; ++m) {
        int mx = px + 5 + (m % 2) * (pillW + 3);
        int my = movesY + (m / 2) * 15;
        bool empty = p->moves[m] == 0;
        c.roundRect(mx, my, pillW, 13, empty ? kDim : kBorder, kPanelDark);
        if (!empty) {
            c.text(kFontSmall, mx + 5, my, pillW - 30, Align::Left, data::moveName(p->moves[m]), kText);
            c.text(kFontSmall, mx, my, pillW - 5, Align::Right, std::to_string(p->pp[m]), kMuted);
        } else {
            c.text(kFontSmall, mx + 5, my, "-", kDim);
        }
    }
}

// =============================================================================
// Drawing: screens

void App::drawHome(Canvas& top, Canvas& bottom) {
    drawChrome(top, "PKTool  \xE2\x80\xA2  " + std::string(config_.storageReady ? "SD card ready" : "No SD card"), "", 0);
    // Logo.
    drawBallEmblem(top, 128, 78, 26, kRed, kText);
    top.text(kFontLarge, 0, 110, gfx::kScreenWidth, Align::Center, "PKTool", kText);
    top.text(kFontSmall, 0, 130, gfx::kScreenWidth, Align::Center, "Pok\xC3\xA9mon save editor for Nintendo DS", kMuted);
    top.text(kFontSmall, 0, 144, gfx::kScreenWidth, Align::Center, "Gen I \xC2\xB7 II \xC2\xB7 III \xC2\xB7 IV \xC2\xB7 V", kDim);
    top.text(kFontSmall, 0, 174, gfx::kScreenWidth - 6, Align::Right, "v" + config_.version, kDim);

    bottom.grid(0, 0, gfx::kScreenWidth, kHintTop, kBackground, kGridLine, 12);
    struct Tile {
        const char* label;
        const char* caption;
    };
    const Tile tiles[] = {
        {"Open save", "Browse your saves"},
        {"Backups", "Restore a save"},
        {"Bank", "Pok\xC3\xA9mon on the SD"},
        {"Settings", "Options and About"},
    };
    for (int i = 0; i < 4; ++i) {
        int x = 8 + (i % 2) * 124;
        int y = 8 + (i / 2) * 82;
        int w = 116;
        int h = 76;
        bool selected = i == homeCursor_;
        bool disabled = false;
        bottom.roundRect(x, y, w, h, selected ? kAccent : kBorder, kPanel, selected ? 2 : 1);
        int cx = x + w / 2;
        int cy = y + 28;
        Color ink = disabled ? kDim : kText;
        switch (i) {
            case 0:  // cartridge
                bottom.roundRect(cx - 14, cy - 16, 28, 30, ink, rgb(40, 60, 120));
                bottom.fillRect(cx - 9, cy - 11, 18, 14, rgb(120, 110, 220));
                drawBallEmblem(bottom, cx, cy - 4, 5, kRed, kText);
                break;
            case 1:  // clock
                bottom.circle(cx, cy - 2, 14, ink);
                bottom.circle(cx, cy - 2, 13, ink);
                bottom.vline(cx, cy - 11, 9, ink);
                bottom.hline(cx, cy - 2, 7, ink);
                break;
            case 2:  // storage box
                bottom.rect(cx - 13, cy - 14, 26, 26, ink, 2);
                bottom.rect(cx - 6, cy - 7, 12, 12, ink, 2);
                break;
            default:  // settings: three sliders
                for (int row = 0; row < 3; ++row) {
                    int ly = cy - 12 + row * 10;
                    bottom.hline(cx - 14, ly, 28, ink);
                    bottom.fillRect(cx - 9 + ((row * 9) % 17), ly - 2, 4, 5, kAccent);
                }
                break;
        }
        bottom.text(kFontLarge, x, y + 46, w, Align::Center, tiles[i].label, ink);
        bottom.text(kFontSmall, x, y + 61, w, Align::Center, tiles[i].caption, disabled ? kDim : kMuted);
        zones_.push_back({x, y, w, h, kZoneItem + i});
    }
    drawHints(bottom, {{"A", "Open"}, {"+", "Move"}, {"START", "Exit"}}, {kKeyA, 0, kKeyStart});
}

void App::drawBrowser(Canvas& top, Canvas& bottom) {
    drawChrome(top, "Pok\xC3\xA9mon saves on the SD card", std::to_string(saveFiles_.size()) + " found", kAccent);
    top.roundRect(4, kBodyTop + 3, 248, 150, kBorder, kPanel);
    if (saveFiles_.empty()) {
        top.text(kFontLarge, 4, 70, 248, Align::Center, "No Pok\xC3\xA9mon saves found", kText);
        top.text(kFontSmall, 4, 92, 248, Align::Center, "PKTool looks for Gen I to V", kMuted);
        top.text(kFontSmall, 4, 103, 248, Align::Center, ".sav files up to three", kMuted);
        top.text(kFontSmall, 4, 114, 248, Align::Center, "folders deep on the SD card.", kMuted);
    } else {
        const saves::SaveSummary& save = saveFiles_[browserCursor_];
        // "Cover": the game's box-art legendary.
        const int cx = 10;
        const int cy = kBodyTop + 8;
        const int cs = 84;
        top.roundRect(cx, cy, cs, cs, kBorder, kPanelDark);
        top.setClip(cx + 1, cy + 1, cs - 2, cs - 2);
        gfx::Image cover = sprites_.get("front", saves::coverSpecies(save.version));
        if (cover.pixels != nullptr) {
            top.image(cx + (cs - cover.width) / 2, cy + (cs - cover.height) / 2, cover);
        } else {
            drawBallEmblem(top, cx + cs / 2, cy + cs / 2, 20, kRed, kText);
        }
        top.resetClip();
        const int ix = cx + cs + 8;
        const int iw = 248 - ix;
        top.text(kFontSmall, ix, cy, iw, Align::Left, "Pok\xC3\xA9mon", kMuted);
        top.text(kFontLarge, ix, cy + 10, iw, Align::Left, saves::versionName(save.version), kText);
        char id[16];
        std::snprintf(id, sizeof id, "%05u", save.trainerId);
        top.text(kFontSmall, ix, cy + 32, iw, Align::Left, save.trainer + "   ID " + id, kText);
        top.text(kFontSmall, ix, cy + 45, iw, Align::Left, io::fileName(save.path), kMuted);
        std::string folder = save.path.substr(0, save.path.size() - io::fileName(save.path).size());
        top.text(kFontSmall, ix, cy + 57, iw, Align::Left, folder.empty() ? "/" : folder, kDim);

        // Party.
        top.hline(12, cy + cs + 6, 232, kBorder);
        for (size_t i = 0; i < 6; ++i) {
            int x = 10 + static_cast<int>(i) * 40;
            int y = cy + cs + 12;
            top.roundRect(x, y, 38, 32, i < save.party.size() ? kBorder : kDim, kPanelDark);
            if (i < save.party.size()) {
                pokemon::Pokemon p;
                p.species = save.party[i];
                p.isEgg = save.party[i] == 0;
                drawIcon(top, x - 1, y + 1, 40, 30, p, false);
            }
        }
    }

    bottom.grid(0, 0, gfx::kScreenWidth, kHintTop, kBackground, kGridLine, 12);
    bottom.roundRect(4, 4, 248, 168, kBorder, kPanel);
    bottom.text(kFontSmall, 10, 5, "SELECT SAVE", kMuted);
    if (saveFiles_.empty()) {
        bottom.text(kFontSmall, 4, 80, 248, Align::Center, "Nothing here yet", kDim);
    }
    // Rows with the game's icon, version, trainer and file name.
    const int rowH = 30;
    const int visible = 5;
    const int count = static_cast<int>(saveFiles_.size());
    if (browserCursor_ < browserScroll_) browserScroll_ = browserCursor_;
    if (browserCursor_ >= browserScroll_ + visible) browserScroll_ = browserCursor_ - visible + 1;
    browserScroll_ = clampInt(browserScroll_, 0, std::max(0, count - visible));
    for (int i = 0; i < visible && browserScroll_ + i < count; ++i) {
        int index = browserScroll_ + i;
        const saves::SaveSummary& save = saveFiles_[index];
        int y = 19 + i * (rowH + 1);
        bool selected = index == browserCursor_;
        if (selected) {
            bottom.roundRect(8, y, 236, rowH, kAccent, rgb(28, 58, 110));
        } else {
            bottom.fillRect(9, y, 234, rowH, kPanelDark);
        }
        pokemon::Pokemon icon;
        icon.species = saves::coverSpecies(save.version);
        drawIcon(bottom, 10, y, 40, rowH, icon, false);
        bottom.text(kFontSmall, 54, y + 2, 186, Align::Left, saves::versionName(save.version) + std::string("  \xE2\x80\xA2  ") + save.trainer,
                    selected ? kText : kMuted);
        bottom.text(kFontSmall, 54, y + 15, 186, Align::Left, io::fileName(save.path), kDim);
        zones_.push_back({8, y, 236, rowH, kZoneItem + index});
    }
    if (count > visible) {
        int trackH = visible * (rowH + 1);
        int thumbH = std::max(8, trackH * visible / count);
        int thumbY = 19 + (trackH - thumbH) * browserScroll_ / std::max(1, count - visible);
        bottom.fillRect(246, 19, 2, trackH, kPanelDark);
        bottom.fillRect(246, thumbY, 2, thumbH, kAccent);
    }
    if (config_.gbaSlot != nullptr) {
        // Five hints do not fit: B (back) works as on every screen.
        drawHints(bottom, {{"A", "Open"}, {"X", "Backups"}, {"Y", "Rescan"}, {"SEL", "GBA"}},
                  {kKeyA, kKeyX, kKeyY, kKeySelect});
    } else {
        drawHints(bottom, {{"A", "Open"}, {"B", "Back"}, {"X", "Backups"}, {"Y", "Rescan"}},
                  {kKeyA, kKeyB, kKeyX, kKeyY});
    }
}

namespace {

// Small pictogram of a save menu row, drawn in a 14x14 square at (x, y).
void drawSaveMenuIcon(Canvas& c, int item, int x, int y, Color ink) {
    c.roundRect(x, y, 14, 14, ink, kPanelDark);
    switch (item) {
        case kSmBoxes:  // box grid
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) c.fillRect(x + 3 + i * 3, y + 3 + j * 3, 2, 2, ink);
            }
            break;
        case kSmTrainer:  // head and shoulders
            c.fillCircle(x + 7, y + 5, 2, ink);
            c.fillRect(x + 3, y + 8, 8, 3, ink);
            break;
        case kSmBag:  // satchel with a handle
            c.fillRect(x + 3, y + 6, 8, 6, ink);
            c.rect(x + 5, y + 3, 4, 3, ink);
            break;
        case kSmBank:  // drawer
            c.rect(x + 3, y + 3, 8, 8, ink);
            c.hline(x + 3, y + 7, 8, ink);
            break;
        case kSmSave:  // arrow into a tray
            c.fillRect(x + 6, y + 2, 2, 6, ink);
            c.fillRect(x + 4, y + 6, 6, 1, ink);
            c.fillRect(x + 5, y + 7, 4, 1, ink);
            c.fillRect(x + 3, y + 10, 8, 2, ink);
            break;
        case kSmBackups:  // clock
            c.circle(x + 7, y + 7, 4, ink);
            c.vline(x + 7, y + 4, 4, ink);
            c.hline(x + 7, y + 7, 3, ink);
            break;
        default:  // cross
            for (int i = 0; i < 7; ++i) {
                c.pixel(x + 4 + i, y + 4 + i, ink);
                c.pixel(x + 4 + i, y + 10 - i, ink);
            }
            break;
    }
}

}  // namespace

void App::drawSaveMenu(Canvas& top, Canvas& bottom) {
    const saves::TrainerData t = save_->trainerData();
    drawChrome(top, saveTitle() + "  \xE2\x80\xA2  " + save_->trainerName(), unsaved_ ? "Unsaved" : "", kYellow);

    // Top: the trainer card summary, as in the save browser.
    top.roundRect(4, kBodyTop + 3, 248, 150, kBorder, kPanel);
    const int cx = 10;
    const int cy = kBodyTop + 8;
    const int cs = 84;
    top.roundRect(cx, cy, cs, cs, kBorder, kPanelDark);
    top.setClip(cx + 1, cy + 1, cs - 2, cs - 2);
    gfx::Image cover = sprites_.get("front", saves::coverSpecies(save_->gameVersion()));
    if (cover.pixels != nullptr) {
        top.image(cx + (cs - cover.width) / 2, cy + (cs - cover.height) / 2, cover);
    } else {
        drawBallEmblem(top, cx + cs / 2, cy + cs / 2, 20, kRed, kText);
    }
    top.resetClip();
    const int ix = cx + cs + 8;
    const int iw = 248 - ix;
    top.text(kFontSmall, ix, cy, iw, Align::Left, "Pok\xC3\xA9mon", kMuted);
    top.text(kFontLarge, ix, cy + 10, iw, Align::Left, saves::versionName(save_->gameVersion()), kText);
    char id[16];
    std::snprintf(id, sizeof id, "%05u", t.tid);
    int nameW = top.text(kFontSmall, ix, cy + 32, iw, Align::Left, t.name, kText);
    top.text(kFontSmall, ix + std::min(nameW, iw - 52) + 6, cy + 32, 52, Align::Left, std::string("ID ") + id, kMuted);
    top.text(kFontSmall, ix, cy + 45, iw, Align::Left, "\xC2\xA5" + groupDigits(t.money), kText);
    top.text(kFontSmall, ix, cy + 57, iw, Align::Left,
             std::to_string(t.hours) + ":" + twoDigits(t.minutes) + ":" + twoDigits(t.seconds), kMuted);
    top.text(kFontSmall, ix, cy + 69, iw, Align::Left, io::fileName(savePath_), kDim);

    // Party strip.
    top.hline(12, cy + cs + 6, 232, kBorder);
    for (int i = 0; i < saves::SaveFile::kPartyMax; ++i) {
        int x = 10 + i * 40;
        int y = cy + cs + 12;
        pokemon::Pokemon p;
        bool used = readSlot(-1, i, p) == saves::SlotState::Valid;
        top.roundRect(x, y, 38, 32, used ? kBorder : kDim, kPanelDark);
        if (used) drawIcon(top, x - 1, y + 1, 40, 30, p, false);
    }

    // Bottom: the menu.
    bottom.grid(0, 0, gfx::kScreenWidth, kHintTop, kBackground, kGridLine, 12);
    bottom.roundRect(4, 3, 248, 171, kBorder, kPanel);
    const int rowH = 21;
    const int rowGap = 1;
    const int y0 = 6;
    struct Row {
        const char* label;
        std::string caption;
        Color captionColor;
        Color ink;
    };
    const std::string boxes = std::to_string(save_->boxCount()) + " boxes";
    const Row rows[kSmCount] = {
        {"Boxes", "Party and " + boxes, kMuted, kAccent},
        {"Trainer", "Card, money, badges", kMuted, kCursor},
        {"Bag", "Items and quantities", kMuted, kYellow},
        {"Bank", "Pok\xC3\xA9mon on the SD", kMuted, kGreen},
        {"Save to SD", unsaved_ ? "Unsaved changes" : "No changes", unsaved_ ? kYellow : kDim,
         unsaved_ ? kYellow : kMuted},
        {"Backups", "Restore or back up", kMuted, kMuted},
        {"Close save", unsaved_ ? "Asks to discard" : "Back to the saves", kMuted, kRed},
    };
    for (int i = 0; i < kSmCount; ++i) {
        int y = y0 + i * (rowH + rowGap);
        bool selected = i == saveMenuCursor_;
        if (selected) {
            bottom.roundRect(8, y, 240, rowH, kAccent, kSelectedRow, 2);
        } else {
            bottom.fillRect(9, y, 238, rowH, kPanelDark);
        }
        drawSaveMenuIcon(bottom, i, 14, y + 3, rows[i].ink);
        bottom.text(kFontLarge, 34, y + 2, 100, Align::Left, rows[i].label, selected ? kText : kMuted);
        bottom.text(kFontSmall, 100, y + 5, 142, Align::Right, rows[i].caption, rows[i].captionColor);
        zones_.push_back({8, y, 240, rowH, kZoneItem + i});
    }
    drawHints(bottom, {{"+", "Choose"}, {"A", "Open"}, {"SEL", "Save"}, {"B", "Close"}},
              {0, kKeyA, kKeySelect, kKeyB});
}

void App::drawBox(Canvas& top, Canvas& bottom) {
    // Top: summary of the selected Pokémon.
    std::string status = saveTitle() + "  \xE2\x80\xA2  " + save_->trainerName();
    drawChrome(top, status, moving_ ? "Moving" : unsaved_ ? "Unsaved" : "", moving_ ? kAccent : kYellow);
    pokemon::Pokemon selected;
    auto state = readSlot(box_, slotCursor_, selected);
    int cursorX = 0, cursorY = 0, cursorW = 0;  // the menu is anchored here
    drawSummary(top, state == saves::SlotState::Valid ? &selected : nullptr,
                state == saves::SlotState::BadChecksum);

    // Bottom: box header, grid, hints.
    bottom.grid(0, 0, gfx::kScreenWidth, kHintTop, kBackground, kGridLine, 12);
    bottom.roundRect(4, 3, 248, 17, kBorder, kPanelDark);
    bottom.fillRect(6, 5, 10, 13, kAccent);
    bottom.fillRect(240, 5, 10, 13, kAccent);
    bottom.arrow(9, 8, 7, -1, kTitleBar);
    bottom.arrow(243, 8, 7, 1, kTitleBar);
    std::string title = isParty() ? std::string("PARTY") : "BOX " + twoDigits(box_ + 1);
    if (!isParty()) {
        std::string boxName = save_->boxName(box_);
        if (!boxName.empty()) title += "  \xE2\x80\xA2  " + boxName;
    }
    bottom.text(kFontSmall, 18, 4, 220, Align::Center, title, kText);
    zones_.push_back({4, 2, 30, 20, kZonePrevBox});
    zones_.push_back({222, 2, 30, 20, kZoneNextBox});
    if (!isParty()) zones_.push_back({36, 2, 184, 20, kZoneBoxTitle});

    bottom.roundRect(4, 23, 248, 151, kBorder, kBoxArea);
    if (isParty()) {
        for (int slot = 0; slot < saves::SaveFile::kPartyMax; ++slot) {
            int x = 9 + (slot % 2) * 121;
            int y = 27 + (slot / 2) * 48;
            int w = 117;
            int h = 44;
            pokemon::Pokemon p;
            auto s = readSlot(-1, slot, p);
            bool used = s == saves::SlotState::Valid;
            bottom.roundRect(x, y, w, h, used ? kBorder : kDim, used ? kPanel : kBoxArea);
            if (used) {
                drawIcon(bottom, x + 2, y + 7, 42, 30, p, false);
                std::string name = p.isEgg ? "Egg" : (p.isNicknamed ? p.nickname : data::speciesName(p.species));
                bottom.text(kFontSmall, x + 46, y + 5, w - 50, Align::Left, name, kText);
                bottom.text(kFontSmall, x + 46, y + 18, "Lv. " + std::to_string(p.level), kMuted);
                drawGenderGlyph(bottom, x + w - 12, y + 21, p.gender);
                if (p.shiny) drawStar(bottom, x + w - 11, y + 4, kYellow);
                bottom.text(kFontSmall, x + 46, y + 29, w - 50, Align::Left, data::itemName(p.heldItem),
                            p.heldItem ? kGreen : kDim);
            } else if (s == saves::SlotState::BadChecksum) {
                bottom.text(kFontSmall, x, y + 15, w, Align::Center, "Bad data", kRed);
            }
            if (moving_ && moveBox_ == box_ && slot == moveSlot_) bottom.blendRect(x, y, w, h, kAccent, 10);
            if (slot == slotCursor_) {
                bottom.brackets(x - 2, y - 2, w + 4, h + 4, kCursor, 6);
                cursorX = x, cursorY = y, cursorW = w;
            }
            zones_.push_back({x, y, w, h, kZoneItem + slot});
        }
    } else {
        for (int slot = 0; slot < save_->boxSlotCount(); ++slot) {
            int x = 8 + (slot % 6) * 40;
            int y = 25 + (slot / 6) * 29;
            pokemon::Pokemon p;
            auto s = readSlot(box_, slot, p);
            if (s == saves::SlotState::Valid) {
                drawIcon(bottom, x, y, 40, 29, p, true);
                if (p.shiny) drawStar(bottom, x + 32, y + 1, kYellow);
            } else if (s == saves::SlotState::BadChecksum) {
                bottom.text(kFontSmall, x, y + 8, 40, Align::Center, "!", kRed);
            } else {
                bottom.pixel(x + 20, y + 14, kBoxGrid);
                bottom.pixel(x + 19, y + 14, kBoxGrid);
            }
            if (moving_ && moveBox_ == box_ && slot == moveSlot_) bottom.blendRect(x + 1, y, 38, 29, kAccent, 10);
            if (slot == slotCursor_) {
                bottom.brackets(x + 1, y, 38, 29, kCursor, 5);
                cursorX = x, cursorY = y, cursorW = 40;
            }
            zones_.push_back({x, y, 40, 29, kZoneItem + slot});
        }
    }
    if (menuOpen_) {
        // Popup next to the selected slot: right of it if it fits, else left; never off the grid.
        zones_.clear();  // only the menu and its hints react while it is open
        constexpr int kRow = 13;
        int w = 0;
        for (MenuAction a : menuItems_) w = std::max(w, Canvas::textWidth(kFontSmall, menuLabel(static_cast<int>(a))));
        w += 20;
        int h = static_cast<int>(menuItems_.size()) * kRow + 6;
        int x = cursorX + cursorW + 2;
        if (x + w > gfx::kScreenWidth - 4) x = cursorX - w - 2;
        x = clampInt(x, 4, gfx::kScreenWidth - 4 - w);
        int y = clampInt(cursorY, 25, kHintTop - 3 - h);
        bottom.blendRect(0, 0, gfx::kScreenWidth, kHintTop, kTitleBar, 10);
        bottom.roundRect(x, y, w, h, kAccent, kPanel, 1);
        for (size_t i = 0; i < menuItems_.size(); ++i) {
            int ry = y + 3 + static_cast<int>(i) * kRow;
            bool selected = static_cast<int>(i) == menuCursor_;
            MenuAction a = menuItems_[i];
            if (selected) bottom.roundRect(x + 3, ry, w - 6, kRow - 1, kAccent, rgb(28, 58, 110));
            Color color = selected ? kText : a == MenuAction::Release ? kRed : kMuted;
            bottom.text(kFontSmall, x + 8, ry, w - 12, Align::Left, menuLabel(static_cast<int>(a)), color);
            zones_.push_back({x + 3, ry, w - 6, kRow, kZoneItem + static_cast<int>(i)});
        }
        drawHints(bottom, {{"+", "Choose"}, {"A", "OK"}, {"B", "Close"}}, {0, kKeyA, kKeyB});
        return;
    }
    if (moving_) {
        drawHints(bottom, {{"+", "Target"}, {"L/R", "Box"}, {"A", "Drop"}, {"B", "Cancel"}},
                  {0, 0, kKeyA, kKeyB});
        return;
    }
    bool addHere = !isParty() && state == saves::SlotState::Empty;
    drawHints(bottom,
              {{"A", addHere ? "Add" : "Menu"}, {"Y", "Bank"}, {"SEL", "Save"}, {"B", "Back"}},
              {kKeyA, kKeyY, kKeySelect, kKeyB});
}

void App::drawEdit(Canvas& top, Canvas& bottom) {
    const EditState& e = edit_;
    std::string status = (e.created ? "New  \xE2\x80\xA2  " : "Editing  \xE2\x80\xA2  ") +
                         std::string(e.party ? "Party" : "Box " + twoDigits(e.box + 1)) + " slot " +
                         std::to_string(e.slot + 1);
    bool pidChanged = e.current.pid != e.original.pid;
    // Editor messages (e.g. "EV total is limited to 510") replace the status text.
    drawChrome(top, e.status.empty() ? status : e.status, pidChanged ? "New PID" : "", kYellow,
               e.status.empty() ? 0 : kYellow);
    drawSummary(top, &e.current, false);

    bottom.grid(0, 0, gfx::kScreenWidth, kHintTop, kBackground, kGridLine, 12);
    bottom.roundRect(4, 3, 248, 170, kBorder, kPanel);
    const int rowH = 14;
    const int visible = 11;
    const int x = 8;
    const int y = 7;
    const int w = 236;
    // Only the fields the format has; editScroll_ indexes this list.
    int shown[kFieldCount];
    int shownCount = 0;
    int selectedIndex = 0;
    for (int f = 0; f < kFieldCount; ++f) {
        if (!fieldVisible(e.current, f)) continue;
        if (f == e.field) selectedIndex = shownCount;
        shown[shownCount++] = f;
    }
    if (selectedIndex < editScroll_) editScroll_ = selectedIndex;
    if (selectedIndex >= editScroll_ + visible) editScroll_ = selectedIndex - visible + 1;
    editScroll_ = clampInt(editScroll_, 0, std::max(0, shownCount - visible));
    for (int i = 0; i < visible; ++i) {
        if (editScroll_ + i >= shownCount) break;
        int field = shown[editScroll_ + i];
        int ry = y + i * (rowH + 1);
        bool selected = field == e.field;
        bool changed = fieldValue(e.current, field) != fieldValue(e.original, field);
        if (selected) {
            bottom.roundRect(x, ry, w, rowH, kAccent, rgb(28, 58, 110));
        } else {
            bottom.fillRect(x + 1, ry, w - 2, rowH, kPanelDark);
        }
        std::string label = fieldLabelFor(e.current, field);
        bottom.text(kFontSmall, x + 6, ry, 80, Align::Left, label, selected ? kText : kMuted);
        Color valueColor = changed ? kYellow : kText;
        const int vx = x + 92;
        const int vw = w - 96;
        if (selected) {
            bottom.arrow(vx + 2, ry + 3, 7, -1, kAccent);
            bottom.arrow(vx + vw - 6, ry + 3, 7, 1, kAccent);
            bottom.text(kFontSmall, vx + 12, ry, vw - 24, Align::Center, fieldValue(e.current, field), valueColor);
        } else {
            bottom.text(kFontSmall, vx + 12, ry, vw - 24, Align::Center, fieldValue(e.current, field), valueColor);
        }
        zones_.push_back({x, ry, 92, rowH, kZoneItem + field});
        zones_.push_back({vx, ry, vw / 2, rowH, kZoneDecrease + field});
        zones_.push_back({vx + vw / 2, ry, vw - vw / 2, rowH, kZoneIncrease + field});
    }
    // Scrollbar.
    int trackH = visible * (rowH + 1);
    int thumbH = trackH * visible / std::max(shownCount, visible);
    int thumbY = y + (trackH - thumbH) * editScroll_ / std::max(1, shownCount - visible);
    bottom.fillRect(x + w + 2, y, 2, trackH, kPanelDark);
    bottom.fillRect(x + w + 2, thumbY, 2, thumbH, kAccent);

    drawHints(bottom, {{"+", "Change"}, {"X", "Legalize"}, {"SEL", "Check"}, {"ST", "Apply"}},
              {0, kKeyX, kKeySelect, kKeyStart});
}

void App::drawBackups(Canvas& top, Canvas& bottom) {
    drawChrome(top, "Backups  \xE2\x80\xA2  " + io::fileName(savePath_), std::to_string(backups_.size()), kAccent);
    top.roundRect(4, kBodyTop + 3, 248, 150, kBorder, kPanel);
    top.text(kFontLarge, 12, 44, 232, Align::Left, "Restore a backup", kText);
    top.text(kFontSmall, 12, 64, 232, Align::Left, "Backups are stored in " + backupDir() + ".", kMuted);
    top.text(kFontSmall, 12, 76, 232, Align::Left, "Every write and restore creates one first,", kMuted);
    top.text(kFontSmall, 12, 88, 232, Align::Left, "so restoring can always be undone.", kMuted);
    if (!backups_.empty()) {
        top.hline(12, 110, 232, kBorder);
        top.text(kFontSmall, 12, 116, "Selected", kMuted);
        top.text(kFontSmall, 60, 116, 184, Align::Left,
                 backupLabel(io::fileName(savePath_), backups_[backupCursor_]), kText);
    }

    bottom.grid(0, 0, gfx::kScreenWidth, kHintTop, kBackground, kGridLine, 12);
    bottom.roundRect(4, 4, 248, 168, kBorder, kPanel);
    bottom.text(kFontSmall, 10, 5, "BACKUPS (NEWEST FIRST)", kMuted);
    std::vector<std::string> rows;
    for (const std::string& path : backups_) rows.push_back(backupLabel(io::fileName(savePath_), path));
    if (rows.empty()) {
        bottom.text(kFontSmall, 4, 80, 248, Align::Center, "No backups for this save yet", kDim);
    }
    drawList(bottom, rows, backupCursor_, backupScroll_, 8, 19, 236, 15, 10);
    drawHints(bottom, {{"A", "Restore"}, {"X", "Back up now"}, {"B", "Back"}}, {kKeyA, kKeyX, kKeyB});
}

void App::drawPicker(Canvas& top, Canvas& bottom) {
    std::string target = "Box " + twoDigits(box_ + 1) + " slot " + std::to_string(slotCursor_ + 1);
    const char* heading = "ADD A POK\xC3\x89MON";
    const char* emptyText = "";
    switch (pickerKind_) {
        case PickerKind::AddMenu:
            break;
        case PickerKind::Species:
            heading = eggMode_ ? "EGG OF..." : "SPECIES";
            break;
        case PickerKind::Showdown:
            heading = "SHOWDOWN SETS";
            emptyText = "No sets found";
            break;
        case PickerKind::Event:
            heading = save_->format() == 3 ? "EVENTS" : "WONDER CARDS";
            break;
    }
    // Status bar: where the selection comes from, or the target slot.
    std::string status = "Add  \xE2\x80\xA2  " + target;
    if (pickerKind_ == PickerKind::Showdown && !pickerRows_.empty()) status = showdownSources_[pickerCursor_];
    if (pickerKind_ == PickerKind::Event && !pickerRows_.empty()) status = eventTitle();
    drawChrome(top, status, std::to_string(pickerRows_.size()), kAccent);

    if (pickerKind_ == PickerKind::AddMenu) {
        top.roundRect(4, kBodyTop + 3, 248, 150, kBorder, kPanel);
        const int menuId = addMenuIds_[static_cast<size_t>(pickerCursor_)];
        top.text(kFontLarge, 12, 44, 232, Align::Left, kAddMenuRows[menuId], kText);
        static const char* const kHelp[][4] = {
            {"Pick a species. PKTool gives it a real", "encounter from your game, its moves",
             "and a PID/IVs the game could make.", "Then edit level, nature, moves..."},
            {"Sets from .txt files in", "", "(Showdown \"Export\" format, several sets",
             "per file). They are legalized on import."},
            {"Official Mystery Gift Pok\xC3\xA9mon (Gen IV/V) and", "the old Gen III events (Jirachi, Mew...),",
             "received like the game does: the trainer,", "moves, a new PID and IVs when left random."},
            {"An egg from the Day-Care: pick what", "it should hatch into (its first stage",
             "is used). It hatches in the game after", "its egg cycles (the Friendship value)."},
        };
        for (int i = 0; i < 4; ++i) {
            std::string line = kHelp[menuId][i];
            if (menuId == 1 && i == 1) line = showdownDir();
            top.text(kFontSmall, 12, 66 + i * 12, 232, Align::Left, line, menuId == 1 && i == 1 ? kAccent : kMuted);
        }
        top.hline(12, 124, 232, kBorder);
        top.text(kFontSmall, 12, 130, 232, Align::Left, "The new Pok\xC3\xA9mon opens in the editor;", kMuted);
        top.text(kFontSmall, 12, 142, 232, Align::Left, "START there puts it in " + target + ".", kMuted);
    } else if (pickerRows_.empty()) {
        top.roundRect(4, kBodyTop + 3, 248, 150, kBorder, kPanel);
        top.text(kFontLarge, 4, 70, 248, Align::Center, emptyText, kText);
        top.text(kFontSmall, 4, 92, 248, Align::Center, "Put Showdown sets (.txt) in", kMuted);
        top.text(kFontSmall, 4, 104, 248, Align::Center, showdownDir(), kAccent);
    } else if (pickerKind_ == PickerKind::Event && !eventNotes_[static_cast<size_t>(pickerCursor_)].empty()) {
        top.roundRect(4, kBodyTop + 3, 248, 150, kBorder, kPanel);
        top.text(kFontLarge, 4, 66, 248, Align::Center, "Cannot be received", kText);
        top.text(kFontSmall, 8, 92, 240, Align::Center, eventNotes_[static_cast<size_t>(pickerCursor_)], kMuted);
        top.text(kFontSmall, 8, 108, 240, Align::Center, eventTitle(), kDim);
    } else {
        // Only received events are final; the others are legalized on creation.
        drawSummary(top, &pickerPreview(), false, pickerKind_ == PickerKind::Event);
    }

    bottom.grid(0, 0, gfx::kScreenWidth, kHintTop, kBackground, kGridLine, 12);
    bottom.roundRect(4, 4, 248, 168, kBorder, kPanel);
    bottom.text(kFontSmall, 10, 5, heading, kMuted);
    if (pickerRows_.empty() && pickerKind_ == PickerKind::Showdown) {
        bottom.text(kFontSmall, 4, 80, 248, Align::Center, "Nothing here yet", kDim);
    }
    drawList(bottom, pickerRows_, pickerCursor_, pickerScroll_, 8, 19, 236, 15, 10,
             pickerKind_ == PickerKind::Event ? &eventNotes_ : nullptr);
    if (pickerKind_ == PickerKind::AddMenu) {
        drawHints(bottom, {{"A", "Select"}, {"B", "Back"}}, {kKeyA, kKeyB});
    } else {
        if (pickerKind_ == PickerKind::Species) {
            // Y switches the order; L/R jump to the next letter (A-Z) or a page (Dex order).
            drawHints(bottom,
                      {{"A", "Create"},
                       {"Y", settings_.sortById ? "Sort A-Z" : "Sort No."},
                       {"LR", pickerInitials_.empty() ? "Page" : "Letter"},
                       {"B", "Back"}},
                      {kKeyA, kKeyY, 0, kKeyB});
        } else {
            drawHints(bottom, {{"A", "Create"}, {"LR", pickerInitials_.empty() ? "Page" : "Letter"}, {"B", "Back"}},
                      {kKeyA, 0, kKeyB});
        }
    }
}

}  // namespace pktool::ui
