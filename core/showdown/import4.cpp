#include "core/showdown/import4.h"

#include <cstdio>

#include "core/data/names.h"
#include "core/data/personal.h"
#include "core/data/gen3.h"
#include "core/legality/legalize.h"
#include "core/pokemon/edit4.h"
#include "core/pokemon/pk4.h"

namespace pktool::showdown {

using pokemon::Pokemon;
namespace edit4 = pokemon::edit4;

Imported buildGen4(const Set& set, const pokemon::create4::Trainer& trainer, uint32_t seed, int effort) {
    if (pokemon::create4::formatFor(trainer.version) <= 2) return buildGb(set, trainer, seed, effort);
    Imported out;
    out.warnings = set.warnings;
    const bool gen5 = trainer.version >= 20;
    const bool gen3 = pokemon::create4::formatFor(trainer.version) == 3;
    const uint16_t maxSpecies = gen3 ? data::kMaxSpeciesGen3 : data::kMaxSpeciesGen4;
    const char* genName = gen3 ? "Gen III" : "Gen IV";
    if (!gen5 && set.species > maxSpecies) {
        out.warnings.push_back(std::string(data::speciesNameEn(set.species)) + " does not exist in " + genName);
        out.pokemon.species = 0;
        out.report.findings.push_back({legality::Severity::Invalid, std::string("Species does not exist in ") + genName});
        return out;
    }
    Pokemon p = pokemon::create4::newPokemon(trainer, set.species, set.level == 0 ? 1 : set.level, seed);

    if (!set.nickname.empty()) {
        std::string nick = set.nickname.substr(0, pokemon::pk4::kNicknameMaxLength);
        if (nick != p.nickname) {
            p.nickname = nick;
            p.isNicknamed = true;
        }
    }
    p.heldItem = set.item;
    if ((!gen5 && p.heldItem > data::kMaxItemGen4) || (gen3 && p.heldItem != 0 && data::itemToGen3(p.heldItem) == 0)) {
        out.warnings.push_back(std::string(data::itemNameEn(p.heldItem)) + " does not exist in " + genName);
        p.heldItem = 0;
    }
    p.friendship = set.friendship;
    bool anyMove = false;
    for (uint16_t move : set.moves) anyMove = anyMove || move != 0;
    if (anyMove) {
        for (int i = 0; i < 4; ++i) edit4::setMove(p, i, 0);
        for (int i = 0; i < 4; ++i) {
            if (edit4::setMove(p, i, set.moves[i]) != edit4::Result::Ok) {
                out.warnings.push_back(std::string("Skipped ") + data::moveNameEn(set.moves[i]));
            }
        }
    }
    for (int s = 0; s < pokemon::kStatCount; ++s) {
        edit4::setIv(p, static_cast<pokemon::Stat>(s), set.ivs[s]);
        edit4::setEv(p, static_cast<pokemon::Stat>(s), set.evs[s]);
    }

    // PID-derived traits.
    uint8_t slot = edit4::abilitySlot(p);
    if (set.ability != 0) {
        const data::PersonalInfo* info = data::personal5(p.species);
        if (edit4::abilityForSlot(p, 0) == set.ability) {
            slot = 0;
        } else if (edit4::abilityForSlot(p, 1) == set.ability) {
            slot = 1;
        } else if (p.format >= 5 && info && info->hiddenAbility == set.ability) {
            slot = 2;  // hidden ability (Gen V)
        } else {
            out.warnings.push_back(std::string(data::abilityNameEn(set.ability)) + " is not available in this game");
        }
    }
    uint8_t nature = set.nature >= 0 ? static_cast<uint8_t>(set.nature) : p.nature;
    pokemon::Gender gender = p.gender;
    if (set.gender >= 0 && edit4::canHaveGender(p.species, static_cast<pokemon::Gender>(set.gender))) {
        gender = static_cast<pokemon::Gender>(set.gender);
    }
    if (p.format >= 5) {
        // Gen V: the nature is its own field; the PID only carries gender,
        // ability slot and shininess (LEGALIZE adds the encounter's rules).
        edit4::setNature(p, nature);
        p.shiny = set.shiny;
        p.gender = gender;
        if (edit4::setAbilitySlot(p, slot) != edit4::Result::Ok ||
            (p.shiny != set.shiny && edit4::setShiny(p, set.shiny) != edit4::Result::Ok) ||
            (p.gender != gender && edit4::setGender(p, gender) != edit4::Result::Ok)) {
            out.warnings.push_back("No valid PID for this shiny/gender/ability combination");
        }
    } else if (!legality::rerollPidGen4(p, nature, set.shiny, gender, slot, effort)) {
        out.warnings.push_back("No valid PID for this nature/shiny/gender/ability combination");
    }

    legality::LegalizeResult legal = legality::legalize(p, effort);
    out.pokemon = legal.pokemon;
    out.changes = legal.changes;
    for (int s = 0; s < pokemon::kStatCount; ++s) {
        if (out.pokemon.ivs[s] != set.ivs[s]) {
            char text[64];
            const uint8_t* v = out.pokemon.ivs;
            std::snprintf(text, sizeof text, "IVs set to %d/%d/%d/%d/%d/%d (closest legal)", v[0], v[1], v[2], v[3],
                          v[4], v[5]);
            out.warnings.push_back(text);
            break;
        }
    }
    out.report = legal.report;
    return out;
}

}  // namespace pktool::showdown
