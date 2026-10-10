#include "core/pokemon/gift4.h"

#include <cstring>

#include "core/data/learn4.h"
#include "core/pokemon/edit4.h"
#include "core/util/bytes.h"
#include "core/util/lcrng.h"

namespace pktool::pokemon::gift4 {
namespace {

// Advance RNG used by the games to re-roll a shiny gift PID.
uint32_t arngNext(uint32_t seed) {
    return seed * 0x6C078965u + 1u;
}

uint32_t shinyValue(uint32_t pid) {
    return ((pid >> 16) ^ (pid & 0xFFFF)) >> 3;
}

}  // namespace

Date eventDate(data::EventPeriods periods, uint32_t seed, const Date& fallback) {
    uint8_t day[3];
    if (!data::pickEventDay(periods, util::lcrngNext(seed ^ 0x0DA7E5EDu), day)) return fallback;
    return Date{day[0], day[1], day[2]};
}

Pokemon receive(const data::Event4& card, const Date& today, uint32_t seed, uint8_t out[pk4::kStoredSize]) {
    std::memcpy(out, card.templatePk4, pk4::kStoredSize);
    Pokemon p = pk4::toPokemon(out, pk4::kStoredSize);
    Gender gender = p.gender;
    uint32_t rng = seed;

    if (p.pid <= 1) {  // 0/1 = random, never shiny
        uint32_t trainerValue = (p.trainer.tid ^ p.trainer.sid) >> 3;
        for (int attempt = 0; attempt < 100000; ++attempt) {
            rng = util::lcrngNext(rng);
            uint32_t pid = rng;
            while (shinyValue(pid) == trainerValue) pid = arngNext(pid);
            if (edit4::genderFromPid(p.species, pid) == gender) {
                p.pid = pid;
                break;
            }
        }
    }
    if ((util::readU32(out + pk4::offset::kIv32) & 0x3FFFFFFFu) == 0) {
        uint32_t ivSeed = util::lcrngNext(rng ^ 0x5EED5EEDu);
        uint32_t words[2];
        for (uint32_t& w : words) {
            ivSeed = util::lcrngNext(ivSeed);
            w = (ivSeed >> 16) & 0x7FFF;
        }
        uint32_t iv32 = (words[1] << 15) | words[0];
        for (int s = 0; s < kStatCount; ++s) {
            p.ivs[s] = static_cast<uint8_t>((iv32 >> (5 * s)) & 31);  // IV32 uses the model's stat order
        }
    }
    edit4::refreshDerived(p);
    p.gender = gender;

    p.met.metLocation = static_cast<uint16_t>(p.met.eggLocation + 3000);
    p.met.eggLocation = 0;
    p.met.metDate = eventDate(data::eventPeriods(card), seed, today);
    p.isEgg = false;
    if (p.friendship == 0) p.friendship = 70;

    pk4::applyPokemon(p, out);
    return pk4::toPokemon(out, pk4::kStoredSize);
}

Pokemon receiveManaphyEgg(const create4::Trainer& trainer, uint32_t seed, uint8_t out[pk4::kStoredSize]) {
    constexpr uint16_t kManaphy = 490;
    constexpr uint16_t kLocationRanger = 3001;
    create4::Trainer t = trainer;
    const bool gen4 = t.version == 7 || t.version == 8 || (t.version >= 10 && t.version <= 12);
    if (!gen4) t.version = 10;  // Diamond
    if (t.language == 0 || t.language == 6 || t.language >= 8) t.language = 2;  // English

    Pokemon p = create4::newEgg(t, kManaphy, seed);
    p.nickname = create4::eggName(t.language, 4);
    p.isNicknamed = false;
    p.met.fatefulEncounter = true;
    p.met.eggLocation = kLocationRanger;
    p.met.eggDate = t.today;
    p.met.metDate = t.today;
    for (int i = 0; i < 4; ++i) edit4::setMove(p, i, 0);
    const uint16_t moves[3] = {294, 145, 346};  // Tail Glow, Bubble, Water Sport
    for (int i = 0; i < 3; ++i) edit4::setMove(p, i, moves[i]);

    // One Method 1 frame: PID low, PID high, then the two IV words.
    uint32_t rng = seed ^ 0x3A7A9A11u;
    const uint32_t trainerValue = (p.trainer.tid ^ p.trainer.sid) >> 3;
    for (int attempt = 0; attempt < 100000; ++attempt) {
        uint32_t a = (rng = util::lcrngNext(rng)) >> 16;
        uint32_t b = (rng = util::lcrngNext(rng)) >> 16;
        uint32_t c = ((rng = util::lcrngNext(rng)) >> 16) & 0x7FFF;
        uint32_t d = ((rng = util::lcrngNext(rng)) >> 16) & 0x7FFF;
        if (((a ^ b) >> 3) == trainerValue) continue;  // an egg is never shiny for its trainer
        p.pid = (b << 16) | a;
        const uint32_t iv32 = (d << 15) | c;
        for (int s = 0; s < kStatCount; ++s) p.ivs[s] = static_cast<uint8_t>((iv32 >> (5 * s)) & 31);
        break;
    }
    edit4::refreshDerived(p);
    p.ability = edit4::abilityForSlot(p, static_cast<uint8_t>(p.pid & 1));

    create4::encode(p, out);
    return pk4::toPokemon(out, pk4::kStoredSize);
}

const data::Event4* matchingEvent(const Pokemon& p) {
    for (int i = 0; i < data::events4Count(); ++i) {
        const data::Event4& card = data::event4(i);
        bool lineMatch = false;
        for (uint16_t s = p.species; s != 0 && !lineMatch; s = data::preEvolution4(s)) lineMatch = s == card.species;
        if (!lineMatch) continue;
        uint16_t tid = util::readU16(card.templatePk4 + pk4::offset::kTid);
        uint16_t sid = util::readU16(card.templatePk4 + pk4::offset::kSid);
        if (tid != p.trainer.tid || sid != p.trainer.sid) continue;
        Pokemon t = pk4::toPokemon(card.templatePk4, pk4::kStoredSize);
        if (t.trainer.name != p.trainer.name) continue;
        uint32_t pid = util::readU32(card.templatePk4 + pk4::offset::kPid);
        if (pid > 1 && pid != p.pid) continue;
        return &card;
    }
    return nullptr;
}

}  // namespace pktool::pokemon::gift4
