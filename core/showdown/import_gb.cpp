#include <algorithm>
#include <string>

#include "core/data/gen12.h"
#include "core/data/names.h"
#include "core/legality/legalize.h"
#include "core/pokemon/edit_gb.h"
#include "core/pokemon/gb_common.h"
#include "core/showdown/import4.h"

namespace pktool::showdown {

using pokemon::Pokemon;
namespace editgb = pokemon::editgb;

Imported buildGb(const Set& set, const pokemon::create4::Trainer& trainer, uint32_t seed, int effort) {
    Imported out;
    out.warnings = set.warnings;
    const uint8_t format = pokemon::create4::formatFor(trainer.version);
    const uint16_t maxSpecies = format == 1 ? data::kMaxSpeciesGen1 : data::kMaxSpeciesGen2;
    const char* genName = format == 1 ? "Gen I" : "Gen II";
    if (set.species == 0 || set.species > maxSpecies) {
        out.warnings.push_back(std::string(data::speciesNameEn(set.species)) + " does not exist in " + genName);
        out.pokemon.species = 0;
        out.report.findings.push_back({legality::Severity::Invalid, std::string("Species does not exist in ") + genName});
        return out;
    }
    Pokemon p = pokemon::create4::newPokemon(trainer, set.species, set.level == 0 ? 1 : set.level, seed);
    if (!set.nickname.empty()) {
        const size_t limit = p.language == 1 ? 5 : 10;
        std::string nick = set.nickname.substr(0, limit);
        if (nick != p.nickname) {
            p.nickname = nick;
            p.isNicknamed = true;
        }
    }
    if (format == 2) {
        if (set.item != 0) {
            const uint8_t item = data::itemToGen2(set.item);
            if (item != 0 && data::isHeldItem2(item)) {
                p.heldItem = set.item;
            } else {
                out.warnings.push_back(std::string(data::itemNameEn(set.item)) + " cannot be held in Gen II");
            }
        }
        if (set.friendship != 255) p.friendship = set.friendship;
    } else if (set.item != 0) {
        out.warnings.push_back("Pok\xC3\xA9mon hold no items in Gen I");
    }
    bool anyMove = false;
    for (uint16_t move : set.moves) anyMove = anyMove || move != 0;
    if (anyMove) {
        for (int i = 0; i < 4; ++i) editgb::setMove(p, i, 0);
        for (int i = 0; i < 4; ++i) {
            if (editgb::setMove(p, i, set.moves[i]) != editgb::Result::Ok) {
                out.warnings.push_back(std::string("Skipped ") + data::moveNameEn(set.moves[i]));
            }
        }
    }
    // IVs 0-31 become DVs 0-15; Attack, Defense, Speed and Special (SpA) are used.
    editgb::setDv(p, pokemon::kAtk, set.ivs[pokemon::kAtk] / 2);
    editgb::setDv(p, pokemon::kDef, set.ivs[pokemon::kDef] / 2);
    editgb::setDv(p, pokemon::kSpe, set.ivs[pokemon::kSpe] / 2);
    editgb::setDv(p, pokemon::kSpA, set.ivs[pokemon::kSpA] / 2);
    // EVs become Stat Exp: the square the games would count from.
    for (int s : {pokemon::kHp, pokemon::kAtk, pokemon::kDef, pokemon::kSpe, pokemon::kSpA}) {
        const unsigned ev = set.evs[s];
        editgb::setStatExp(p, static_cast<pokemon::Stat>(s), static_cast<uint16_t>(std::min(65535u, ev * ev)));
    }
    if (format == 2) {
        if (set.gender >= 0 && static_cast<pokemon::Gender>(set.gender) != p.gender) {
            if (editgb::setGender(p, static_cast<pokemon::Gender>(set.gender)) != editgb::Result::Ok) {
                out.warnings.push_back("No DVs give that gender");
            }
        }
        if (set.shiny != p.shiny && editgb::setShiny(p, set.shiny) != editgb::Result::Ok) {
            out.warnings.push_back("No DVs give that shininess and gender");
        }
    } else if (set.shiny) {
        out.warnings.push_back("Gen I has no shiny Pok\xC3\xA9mon");
    }
    legality::LegalizeResult legal = legality::legalize(p, effort);
    out.pokemon = legal.pokemon;
    out.changes = legal.changes;
    out.report = legal.report;
    return out;
}

}  // namespace pktool::showdown
