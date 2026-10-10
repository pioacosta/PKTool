#include "core/pokemon/gift5.h"

#include "core/data/learn5.h"
#include "core/data/personal.h"
#include "core/pokemon/edit4.h"
#include "core/pokemon/gift4.h"
#include "core/pokemon/pk4.h"
#include "core/text/gen4_text.h"
#include "core/util/bytes.h"
#include "core/util/lcrng.h"

namespace pktool::pokemon::gift5 {
namespace {

namespace g = data::pgf;

constexpr size_t kPk5Contest = 0x1E;  // six contest stats, after the EVs

// Where each card ribbon (bit n of the card's two ribbon bytes) goes in a PK5.
struct RibbonBit {
    uint8_t offset;
    uint8_t bit;
};
constexpr RibbonBit kRibbonMap[16] = {
    {0x3F, 4},  // Country
    {0x3F, 5},  // National
    {0x3F, 6},  // Earth
    {0x3F, 7},  // World
    {0x27, 2},  // Classic
    {0x27, 3},  // Premier
    {0x26, 3},  // Event
    {0x26, 6},  // Birthday
    {0x26, 7},  // Special
    {0x27, 0},  // Souvenir
    {0x27, 1},  // Wishing
    {0x3F, 1},  // Battle Champion
    {0x3F, 2},  // Regional Champion
    {0x3F, 3},  // National Champion
    {0x26, 5},  // World Champion
    {0, 0},     // unused
};

bool isEggCard(const data::Event5& card) {
    return card.pgf[g::kIsEgg] == 1;
}

// Gen V shiny gift PID: keeps the low byte (gender) and makes the upper half
// give a shiny xor of 0 (MonochromeRNG.GetShinyPID in PKHeX).
uint32_t shinyPid(uint8_t genderByte, uint8_t abilityBit, uint16_t tid, uint16_t sid) {
    uint32_t pid = (static_cast<uint32_t>(genderByte ^ tid ^ sid) << 16) | genderByte;
    if (((pid >> 16) & 1) != (abilityBit & 1u)) pid ^= 0x10000u;
    return pid;
}

}  // namespace

uint8_t receivingGame(const data::Event5& card, uint8_t trainerVersion) {
    if (card.pgf[g::kOriginGame] != 0) return card.pgf[g::kOriginGame];
    if (data::canReceiveEvent5(card, trainerVersion)) return trainerVersion;
    for (uint8_t v = 20; v <= 23; ++v) {
        if (data::canReceiveEvent5(card, v)) return v;
    }
    return 20;
}

Pokemon receive(const data::Event5& card, const create4::Trainer& trainer, uint32_t seed,
                uint8_t out[pk5::kStoredSize]) {
    const uint8_t* c = card.pgf;
    uint32_t rng = seed ^ 0x6B1F7C3Du;
    auto next16 = [&rng]() {
        rng = util::lcrngNext(rng);
        return rng >> 16;
    };
    const bool egg = isEggCard(card);
    const uint16_t species = util::readU16(c + g::kSpecies);
    const uint8_t level = c[g::kLevel] ? c[g::kLevel] : static_cast<uint8_t>(1 + next16() % 100);

    create4::Trainer t = trainer;
    t.version = receivingGame(card, trainer.version);
    t.language = c[g::kLanguage] ? c[g::kLanguage] : card.restrictLanguage ? card.restrictLanguage : trainer.language;
    if (!egg) {
        t.tid = util::readU16(c + g::kTid);
        t.sid = util::readU16(c + g::kSid);
        t.name = text::decodeGen5(c + g::kOtName, 8);
        if (c[g::kOtGender] != 3) t.gender = (c[g::kOtGender] & 1) ? Gender::Female : Gender::Male;
    }
    // Level-up moves up to the card's level, random IVs: the card's own
    // values replace them below.
    Pokemon p = create4::newPokemon(t, species, level, seed);
    p.form = c[g::kForm];
    p.heldItem = util::readU16(c + g::kHeldItem);
    p.met.ball = c[g::kBall];
    p.met.fatefulEncounter = true;
    p.met.metLocation = util::readU16(c + g::kMetLocation);
    p.met.eggLocation = util::readU16(c + g::kEggLocation);
    p.met.metLevel = level;
    Date cardDate{card.date[0], card.date[1], card.date[2]};
    p.met.metDate = gift4::eventDate(data::eventPeriods(card), seed, cardDate.month != 0 ? cardDate : trainer.today);

    bool fixedMoves = false;
    for (int m = 0; m < 4; ++m) fixedMoves = fixedMoves || util::readU16(c + g::kMoves + m * 2) != 0;
    if (fixedMoves) {
        for (int m = 0; m < 4; ++m) edit4::setMove(p, m, 0);
        for (int m = 0; m < 4; ++m) edit4::setMove(p, m, util::readU16(c + g::kMoves + m * 2));
    }

    std::string nickname = text::decodeGen5(c + g::kNickname, pk4::kNicknameChars);
    if (!nickname.empty()) {
        p.nickname = nickname;
        p.isNicknamed = true;
    }

    // Nature, ability, gender and PID.
    p.nature = c[g::kNature] < 25 ? c[g::kNature] : static_cast<uint8_t>(next16() % 25);
    uint8_t abilityType = c[g::kAbilityType];
    uint8_t slot = abilityType <= 2 ? abilityType : abilityType == 3 ? next16() & 1 : next16() % 3;
    uint32_t pid = util::readU32(c + g::kPid);
    if (pid == 0) {
        pid = (next16() << 16) | next16();
        Gender wanted = c[g::kGender] <= 1 ? static_cast<Gender>(c[g::kGender]) : edit4::genderFromPid(species, pid);
        for (int attempt = 0; attempt < 1024 && edit4::genderFromPid(species, pid) != wanted; ++attempt) {
            pid = (pid & 0xFFFFFF00u) | (next16() & 0xFF);
        }
        if (c[g::kPidType] == 2) {
            pid = shinyPid(static_cast<uint8_t>(pid), slot == 1, p.trainer.tid, p.trainer.sid);
        } else if (c[g::kPidType] == 0 && isShiny(pid, p.trainer.tid, p.trainer.sid)) {
            pid ^= 0x10000000u;
        }
        pid = slot == 1 ? pid | 0x10000u : pid & ~0x10000u;
    }
    p.pid = pid;
    edit4::refreshDerived(p);
    const data::PersonalInfo* info = data::personal5(species);
    p.hiddenAbility = slot == 2 && info && info->hiddenAbility != 0;
    p.ability = p.hiddenAbility ? info->hiddenAbility : edit4::abilityForSlot(species, slot == 1 ? 1 : 0);

    // IVs: 0-31 fixed, 0xFC-0xFE = that many random ones are 31, else random.
    int flawless = 0;
    for (int s = 0; s < kStatCount; ++s) {
        uint8_t v = c[g::kIvs + s];
        if (v >= 0xFC && v <= 0xFE) flawless = v - 0xFB;
        p.ivs[s] = v <= 31 ? v : static_cast<uint8_t>(next16() & 31);
    }
    for (int placed = 0, guard = 0; placed < flawless && guard < 64; ++guard) {
        int s = static_cast<int>(next16() % kStatCount);
        if (c[g::kIvs + s] <= 31 || p.ivs[s] == 31) continue;
        p.ivs[s] = 31;
        ++placed;
    }

    if (egg) {
        p.isEgg = true;
        p.nickname = create4::eggName(p.language, 5);
        p.isNicknamed = true;
        p.met.eggDate = p.met.metDate;
        p.met.metDate = Date{};
        p.met.metLevel = 0;
        p.friendship = info ? info->hatchCycles : 20;
    }

    create4::encode(p, out);
    for (int i = 0; i < 6; ++i) out[kPk5Contest + i] = c[g::kContest + i];
    uint16_t ribbons = util::readU16(c + g::kRibbons);
    for (int bit = 0; bit < 15; ++bit) {
        if (ribbons & (1u << bit)) out[kRibbonMap[bit].offset] |= static_cast<uint8_t>(1u << kRibbonMap[bit].bit);
    }
    pk4::refreshChecksum(out);
    return pk5::toPokemon(out, pk5::kStoredSize);
}

bool cardHasMove(const data::Event5& card, uint16_t move) {
    for (int m = 0; m < 4; ++m) {
        if (util::readU16(card.pgf + g::kMoves + m * 2) == move) return true;
    }
    return false;
}

const data::Event5* matchingEvent(const Pokemon& p) {
    constexpr uint16_t kLocationLinkTrade = 30003;
    const bool hatched = p.isEgg || p.met.eggLocation != 0;
    // The same card was often distributed in several languages: prefer the
    // one of the Pokémon's language and game.
    const data::Event5* fallback = nullptr;
    auto consider = [&](const data::Event5& card) {
        uint8_t language = card.pgf[g::kLanguage] ? card.pgf[g::kLanguage] : card.restrictLanguage;
        bool exact = (language == 0 || language == p.language) && data::canReceiveEvent5(card, p.originGame);
        if (!fallback) fallback = &card;
        return exact;
    };
    for (int i = 0; i < data::events5Count(); ++i) {
        const data::Event5& card = data::event5(i);
        const uint8_t* c = card.pgf;
        bool lineMatch = false;
        for (uint16_t s = p.species; s != 0 && !lineMatch; s = data::preEvolution5(s)) {
            lineMatch = s == card.species;
        }
        if (!lineMatch) continue;
        if (isEggCard(card)) {
            uint16_t eggLocation = util::readU16(c + g::kEggLocation);
            if (hatched && (p.met.eggLocation == eggLocation || (!p.isEgg && p.met.eggLocation == kLocationLinkTrade)) &&
                consider(card)) {
                return &card;
            }
            continue;
        }
        if (hatched) continue;
        if (util::readU16(c + g::kTid) != p.trainer.tid || util::readU16(c + g::kSid) != p.trainer.sid) continue;
        if (text::decodeGen5(c + g::kOtName, 8) != p.trainer.name) continue;
        if (util::readU16(c + g::kMetLocation) != p.met.metLocation) continue;
        if (c[g::kLevel] != 0 && c[g::kLevel] != p.met.metLevel) continue;
        uint32_t pid = util::readU32(c + g::kPid);
        if (pid != 0 && pid != p.pid) continue;
        if (consider(card)) return &card;
    }
    return fallback;
}

}  // namespace pktool::pokemon::gift5
