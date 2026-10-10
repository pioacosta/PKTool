#include "core/pokemon/create4.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "core/data/gen3.h"
#include "core/data/learn3.h"
#include "core/data/learn4.h"
#include "core/data/language.h"
#include "core/data/learn5.h"
#include "core/data/moves.h"
#include "core/data/names.h"
#include "core/data/personal.h"
#include "core/pokemon/create_gb.h"
#include "core/pokemon/edit4.h"
#include "core/pokemon/entity.h"
#include "core/util/lcrng.h"

namespace pktool::pokemon::create4 {

std::string defaultNickname(uint16_t species, uint8_t language, uint8_t format) {
    return data::defaultNickname(species, language, format);
}

uint8_t formatFor(uint8_t gameVersion) {
    if (gameVersion >= 35 && gameVersion <= 38) return 1;  // Red/Blue/Yellow saves hold PK1
    if (gameVersion >= 39 && gameVersion <= 41) return 2;  // Gold/Silver/Crystal saves hold PK2
    if (gameVersion >= 20) return 5;                     // B/W/B2/W2 saves hold PK5
    if (gameVersion >= 1 && gameVersion <= 5) return 3;  // R/S/E/FR/LG saves hold PK3
    return 4;
}

Pokemon newPokemon(const Trainer& trainer, uint16_t species, uint8_t level, uint32_t seed) {
    if (formatFor(trainer.version) <= 2) return creategb::newPokemon(trainer, species, level, seed);
    Pokemon p;
    p.format = formatFor(trainer.version);
    p.species = species;
    p.language = trainer.language;
    p.nickname = defaultNickname(species, p.language, p.format);
    p.originGame = trainer.version;
    p.trainer.name = trainer.name;
    p.trainer.tid = trainer.tid;
    p.trainer.sid = trainer.sid;
    p.trainer.gender = trainer.gender;
    p.met.ball = data::kBallPoke;
    p.met.metDate = trainer.today;
    p.met.metLevel = level;
    edit4::setLevel(p, level);
    p.friendship = 70;  // base friendship of almost every species in Gen IV

    // Random IVs and PID (legalization replaces them with a valid frame).
    uint32_t rng = seed ^ 0xA5A5A5A5u;
    rng = util::lcrngNext(rng);
    uint32_t hi = rng >> 16;
    rng = util::lcrngNext(rng);
    p.pid = (hi << 16) | (rng >> 16);
    for (int s = 0; s < kStatCount; ++s) {
        rng = util::lcrngNext(rng);
        p.ivs[s] = static_cast<uint8_t>((rng >> 16) & 31);
    }
    if (p.format >= 5) {
        rng = util::lcrngNext(rng);
        p.nature = static_cast<uint8_t>((rng >> 16) % 25);
    }
    edit4::refreshDerived(p);
    uint8_t slot = static_cast<uint8_t>(p.format >= 5 ? (p.pid >> 16) & 1 : p.pid & 1);
    p.ability = edit4::abilityForSlot(species, slot);

    // Default moves: the four highest level-up moves learned up to `level`.
    std::vector<std::pair<uint8_t, uint16_t>> levelUp;
    for (uint16_t move = 1; move <= data::maxMove(p.format); ++move) {
        const data::LearnHit e = p.format >= 5    ? data::findLearn5(species, move)
                                    : p.format == 3 ? data::findLearn3(species, move)
                                                    : data::findLearn4(species, move);
        if (e && (e->sources & data::kLearnLevelUp) && e->minLevel <= level) levelUp.push_back({e->minLevel, move});
    }
    std::stable_sort(levelUp.begin(), levelUp.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    for (size_t i = 0; i < 4 && i < levelUp.size(); ++i) {
        edit4::setMove(p, static_cast<int>(i), levelUp[i].second);
    }
    return p;
}

std::string eggName(uint8_t language, uint8_t format) {
    switch (language) {
        case 1: return "\xE3\x82\xBF\xE3\x83\x9E\xE3\x82\xB4";  // タマゴ
        case 3: return format >= 5 ? "\xC5\x92uf" : "Oeuf";
        case 4: return "Uovo";
        case 5: return "Ei";
        case 7: return "Huevo";
        case 8: return "\xEC\x95\x8C";  // 알
        default: return "Egg";
    }
}

Pokemon newEgg(const Trainer& trainer, uint16_t species, uint32_t seed) {
    uint8_t format = formatFor(trainer.version);
    if (format <= 2) return creategb::newEgg(trainer, species, seed);
    uint16_t base = format >= 5 ? data::baseSpecies5(species) : data::baseSpecies4(species);
    if (format == 3) {
        // Gen IV babies (Chingling, Budew...) do not exist in Gen III saves: the egg
        // is the first stage of the line that does.
        base = species;
        for (uint16_t pre = data::preEvolution4(base); pre != 0 && pre <= data::kMaxSpeciesGen3;
             pre = data::preEvolution4(base)) {
            base = pre;
        }
    }
    Pokemon p = newPokemon(trainer, base, 1, seed);
    const data::PersonalInfo* info = data::personalFor(format, base);
    p.isEgg = true;
    p.nickname = eggName(trainer.language, format);
    p.isNicknamed = format >= 5;  // Gen V eggs have the nickname flag set, Gen IV ones do not
    p.met.eggLocation = format >= 5 ? 60002 : 2000;  // Day-Care
    p.met.eggDate = trainer.today;
    p.met.metDate = Date{};
    p.met.metLocation = 0;
    p.met.metLevel = 0;
    p.met.ball = data::kBallPoke;
    p.friendship = info ? info->hatchCycles : 20;
    return p;
}

void encode(const Pokemon& p, uint8_t out[pk4::kStoredSize]) {
    if (p.format <= 2) {
        std::memset(out, 0, pk4::kStoredSize);
        creategb::encode(p, out);
        return;
    }
    std::memset(out, 0, p.format == 3 ? 80 : pk4::kStoredSize);  // PK3 is 80 bytes
    entity::apply(p, out);
}

}  // namespace pktool::pokemon::create4
