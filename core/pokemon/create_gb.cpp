#include "core/pokemon/create_gb.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "core/data/gen12.h"
#include "core/data/language.h"
#include "core/legality/encounters_gb.h"
#include "core/pokemon/edit_gb.h"
#include "core/pokemon/gb_common.h"
#include "core/pokemon/pk1.h"
#include "core/pokemon/pk2.h"
#include "core/util/lcrng.h"

namespace pktool::pokemon::creategb {
namespace {

namespace gbn = pktool::legality::gb;

bool isYellowOrCrystal(uint8_t version) {
    return version == 38 || version == 41;
}

// Crystal's caught data of a Pokemon created from `start`: where and at what
// level it was met, and a time of day the encounter can happen at.
void caughtDataFor(Pokemon& p, const gbn::Start& start) {
    const gbn::Candidate& c = start.candidate;
    p.met.encounterType = 2;  // day
    if (c.source == gbn::Source::Egg) {  // hatched
        p.met.metLevel = 1;
        p.met.metLocation = gbn::kHatchLocationCrystal;
        return;
    }
    p.met.metLevel = start.level;
    p.met.metLocation = c.location != 0 ? c.location : 1;  // New Bark Town if the table has none
    for (int t = 1; t <= 3; ++t) {
        if (c.time == 0 || (c.time & (1 << t))) {
            p.met.encounterType = static_cast<uint8_t>(t);
            break;
        }
    }
}

}  // namespace

Pokemon newPokemon(const create4::Trainer& trainer, uint16_t species, uint8_t level, uint32_t seed) {
    Pokemon p;
    p.format = create4::formatFor(trainer.version);
    p.species = species;
    p.language = trainer.language == 1 ? 1 : 2;
    p.originGame = trainer.version;
    p.trainer.name = trainer.name;
    p.trainer.tid = trainer.tid;
    p.trainer.gender = p.format == 2 && trainer.version == 41 ? trainer.gender : Gender::Male;
    p.nickname = data::defaultNickname(species, p.language, p.format);
    editgb::setLevel(p, level);
    if (p.format == 1) {
        if (const data::PersonalGb* info = data::personalGb1(species, trainer.version == 38)) p.catchRate = info->catchRate;
        p.types[0] = p.types[1] = 0;
        if (const data::PersonalGb* info = data::personalGb1(species, trainer.version == 38)) {
            p.types[0] = info->types[0];
            p.types[1] = info->types[1];
        }
    } else {
        p.friendship = 70;
    }
    uint32_t rng = util::lcrngNext(seed ^ 0xA5A5A5A5u);
    const uint16_t dvs = static_cast<uint16_t>(rng >> 16);
    gb::unpackDvs(dvs, p.ivs);
    uint16_t moves[4];
    const bool yellowOrCrystal = isYellowOrCrystal(trainer.version);
    const gbn::Line line = gbn::lineOf(p.format, species);
    gbn::Start start;
    const bool haveStart = gbn::startOf(p.format, line, gbn::versionMask(p.format, trainer.version, p.language == 1), level, &start);
    if (haveStart) {
        gbn::movesFrom(p.format, line, start, level, yellowOrCrystal, moves);
    } else {
        gbn::defaultMoves(p.format, species, level, yellowOrCrystal, moves);
    }
    for (int i = 0; i < 4; ++i) editgb::setMove(p, i, moves[i]);
    editgb::refreshDerived(p);
    if (p.format == 2 && trainer.version == 41 && haveStart) caughtDataFor(p, start);
    return p;
}

Pokemon newEgg(const create4::Trainer& trainer, uint16_t species, uint32_t seed) {
    const uint8_t format = create4::formatFor(trainer.version);
    const gbn::Line line = gbn::lineOf(format, species);
    const uint16_t base = line.count > 0 ? line.species[0] : species;
    Pokemon p = creategb::newPokemon(trainer, base, 5, seed);
    if (format != 2) return p;
    const data::PersonalGb* info = data::personalGb2(base, true);
    p.isEgg = true;
    p.nickname = "EGG";
    p.isNicknamed = false;
    p.friendship = info ? info->hatchCycles : 20;
    p.met = MetInfo();  // the caught data is written when the egg hatches
    return p;
}

void encode(const Pokemon& p, uint8_t* out) {
    if (p.format == 1) {
        pk1::clear(out, p.language == 1);
    } else {
        pk2::clear(out, p.language == 1);
    }
    out[p.format == 1 ? pk1::kLanguageOffset : pk2::kLanguageOffset] = p.language == 0 ? 2 : p.language;
    if (p.format == 1) {
        pk1::applyPokemon(p, out);
        pk1::fillPartyArea(out);
    } else {
        pk2::applyPokemon(p, out);
        pk2::fillPartyArea(out);
    }
}

}  // namespace pktool::pokemon::creategb
