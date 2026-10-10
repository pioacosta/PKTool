#include "core/pokemon/cxd3.h"

#include <cstring>

#include "core/data/gen3.h"
#include "core/data/locks3.h"
#include "core/data/names.h"
#include "core/legality/legality.h"
#include "core/legality/rng3.h"
#include "core/legality/shadow_locks.h"
#include "core/pokemon/edit4.h"
#include "core/pokemon/entity.h"

namespace pktool::pokemon::cxd3 {
namespace {

namespace rng3 = legality::rng3;
namespace shadow = legality::shadow;

constexpr uint8_t kBallPoke = 4;
constexpr uint32_t kAttempts = 150000;

struct Frame {
    uint32_t pid = 0;
    uint8_t ivs[kStatCount] = {};
    uint8_t abilityBit = 0;
};

uint32_t out16(uint32_t s) { return s >> 16; }

uint32_t mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

bool wantedOk(const gift3::Criteria& c, uint16_t species, uint32_t pid, uint16_t tid, uint16_t sid) {
    if (c.nature >= 0 && pid % 25 != static_cast<uint32_t>(c.nature)) return false;
    if (c.shiny >= 0 && isShiny(pid, tid, sid) != (c.shiny != 0)) return false;
    if (c.gender >= 0 && edit4::genderFromPid(species, pid) != static_cast<Gender>(c.gender)) return false;
    return true;
}

// IV, IV, ability, PID high, PID low after `origin`.
Frame plainFrame(uint32_t origin) {
    Frame f;
    uint32_t s = rng3::xdNext(origin);
    uint32_t iv1 = (s >> 16) & 0x7FFF;
    s = rng3::xdNext(s);
    uint32_t iv2 = (s >> 16) & 0x7FFF;
    s = rng3::xdNext(s);
    f.abilityBit = static_cast<uint8_t>(out16(s) & 1);
    s = rng3::xdNext(s);
    uint32_t hi = out16(s);
    s = rng3::xdNext(s);
    f.pid = (hi << 16) | out16(s);
    uint32_t iv32 = (iv2 << 15) | iv1;
    for (int k = 0; k < kStatCount; ++k) f.ivs[k] = static_cast<uint8_t>((iv32 >> (5 * k)) & 31);
    return f;
}

bool isShadowKind(uint8_t kind) { return kind == data::kGcColoShadow || kind == data::kGcXdShadow; }

// Locks of the row(s) of this encounter hold for `origin`.
bool locksHold(const data::Gc3& e, uint32_t origin, uint32_t tsv) {
    const uint8_t game = e.kind == data::kGcXdShadow ? data::kShadowXd : data::kShadowColo;
    bool known = false;
    for (int i = 0; i < data::shadowEnc3Count(); ++i) {
        const data::ShadowEnc3& row = data::shadowEnc3(i);
        if (row.species != e.species || row.level != e.level || row.game != game || row.location != e.location) continue;
        known = true;
        if (shadow::locksSatisfied(shadow::setOf(row), origin, tsv)) return true;
    }
    return !known;
}

bool findPlain(const data::Gc3& e, uint16_t tid, uint16_t sid, uint32_t seed, const gift3::Criteria& want, Frame* out,
               bool* satisfied) {
    const bool neverShiny = (e.flags & data::kGcNeverShiny) != 0;
    const bool xd = e.kind == data::kGcXdShadow;
    const bool shadowKind = isShadowKind(e.kind);
    const uint32_t tsv = xd ? static_cast<uint32_t>(tid ^ sid) >> 3 : shadow::kNoTsv;
    const uint32_t start = mix(seed ^ 0x3C6EF372u);
    bool haveFallback = false;
    for (uint32_t i = 0; i < kAttempts; ++i) {
        uint32_t origin = mix(start + i * 0x9E3779B9u);
        Frame f = plainFrame(origin);
        if (neverShiny && isShiny(f.pid, tid, sid)) continue;
        const bool traits = wantedOk(want, e.species, f.pid, tid, sid);
        if (haveFallback && !traits) continue;
        if (shadowKind && !locksHold(e, origin, tsv)) continue;
        if (!haveFallback) {
            *out = f;
            haveFallback = true;
        }
        if (traits) {
            *out = f;
            *satisfied = true;
            return true;
        }
    }
    *satisfied = false;
    return haveFallback;
}

bool findPokeSpot(const data::Gc3& e, uint16_t tid, uint16_t sid, uint32_t seed, const gift3::Criteria& want, Frame* out,
                  bool* satisfied) {
    *satisfied = false;
    int slot = -1;
    for (int i = 0; i < data::pokeSpotArea3Count() && slot < 0; ++i) {
        const data::PokeSpotArea3& area = data::pokeSpotArea3(i);
        if (area.location != e.location) continue;
        for (int s = 0; s < 3; ++s) {
            if (area.species[s] == e.species && area.level[s] == e.level) slot = s;
        }
    }
    if (slot < 0) return false;
    const uint32_t start = mix(seed ^ 0xA54FF53Au);
    bool havePid = false;
    uint32_t firstPid = 0;
    for (uint32_t i = 0; i < kAttempts; ++i) {
        uint32_t state = mix(start + i * 0x9E3779B9u);
        if (!rng3::pokeSpotActivation(static_cast<uint8_t>(slot), state)) continue;
        uint32_t a = rng3::xdNext(state);
        uint32_t b = rng3::xdNext(a);
        uint32_t pid = (out16(a) << 16) | out16(b);
        if (!havePid) {
            firstPid = pid;
            havePid = true;
        }
        if (wantedOk(want, e.species, pid, tid, sid)) {
            out->pid = pid;
            *satisfied = true;
            break;
        }
    }
    if (!havePid) return false;
    if (!*satisfied) out->pid = firstPid;
    for (uint32_t i = 0; i < kAttempts; ++i) {
        uint32_t preIv = mix(start ^ (i * 0x85EBCA6Bu) ^ 0x1234567u);
        if (rng3::pokeSpotIvFrame(preIv, out->ivs, &out->abilityBit)) return true;
    }
    return false;
}

std::string fixedNickname(const data::Gc3& e, uint8_t language) {
    if (e.nickname == 1 && language == 2) return "ZAPRONG";
    return create4::defaultNickname(e.species);
}

}  // namespace

std::vector<int> menuEntries() {
    std::vector<int> list;
    for (int i = 0; i < data::gc3Count(); ++i) {
        const data::Gc3& e = data::gc3(i);
        if (e.kind == data::kGcColoEReader) continue;
        bool seen = false;
        for (int j : list) {
            const data::Gc3& o = data::gc3(j);
            if (o.species == e.species && o.kind == e.kind && o.level == e.level && o.tid == e.tid) seen = true;
        }
        if (!seen) list.push_back(i);
    }
    return list;
}

std::string title(const data::Gc3& e) {
    const char* kind = "";
    switch (e.kind) {
        case data::kGcColoStarter: kind = "Colosseum starter"; break;
        case data::kGcColoGift: kind = "Colosseum gift"; break;
        case data::kGcColoBonus: kind = "Colosseum bonus disc"; break;
        case data::kGcColoShadow: kind = "Colosseum Shadow"; break;
        case data::kGcXdGift: kind = "XD gift"; break;
        case data::kGcXdTrade: kind = "XD trade"; break;
        case data::kGcXdShadow: kind = "XD Shadow"; break;
        case data::kGcXdPokeSpot: kind = "XD Poke Spot"; break;
        default: kind = "Colosseum e-Reader"; break;
    }
    return std::string(kind) + " Lv." + std::to_string(e.level);
}

Pokemon create(int index, const create4::Trainer& recipient, uint32_t seed, const gift3::Criteria& wanted,
               uint8_t out[pk3::kStoredSize], bool* satisfied, std::string* error) {
    const data::Gc3& e = data::gc3(index);
    Pokemon failed;
    auto fail = [&](const char* text) {
        if (error) *error = text;
        if (satisfied) *satisfied = false;
        return failed;
    };
    if (e.kind == data::kGcColoEReader) return fail("Japanese e-Reader Shadow Pokemon cannot be created");

    create4::Trainer t = recipient;
    // Every Colosseum/XD trainer is male, except the bonus-disc Celebi's.
    t.gender = (e.flags & data::kGcFemaleOt) != 0 ? Gender::Female : Gender::Male;
    if (t.language == 0 || t.language == 1 || t.language == 6 || t.language > 7) t.language = 2;
    if ((e.flags & data::kGcJapanOnly) != 0) t.language = 1;
    if (e.kind == data::kGcColoGift || e.kind == data::kGcColoBonus || e.kind == data::kGcXdTrade) {
        const char* name = data::gcTrainerName(e.trainer, t.language);
        if (name[0] == '\0') name = data::gcTrainerName(e.trainer, 2);
        t.name = name;
        t.tid = e.tid;
        t.sid = e.kind == data::kGcXdTrade ? recipient.sid : 0;  // Duking and bonus-disc gifts: ID32 = TID16
    }

    Frame f;
    bool ok = false;
    bool satisfiedNow = true;
    if (e.kind == data::kGcColoStarter) {
        if (!rng3::coloStarterFrame(t.tid, t.sid, e.species, &f.pid, f.ivs, &f.abilityBit)) {
            return fail("The games cannot draw these trainer IDs, so this starter has no frame for this save");
        }
        ok = true;
        satisfiedNow = wantedOk(wanted, e.species, f.pid, t.tid, t.sid);
    } else if (e.kind == data::kGcXdGift && e.species == 133) {
        if (!rng3::xdStarterFrame(t.tid, t.sid, &f.pid, f.ivs, &f.abilityBit)) {
            return fail("The games cannot draw these trainer IDs, so this starter has no frame for this save");
        }
        ok = true;
        satisfiedNow = wantedOk(wanted, e.species, f.pid, t.tid, t.sid);
    } else if (e.kind == data::kGcXdPokeSpot) {
        ok = findPokeSpot(e, t.tid, t.sid, seed, wanted, &f, &satisfiedNow);
    } else {
        ok = findPlain(e, t.tid, t.sid, seed, wanted, &f, &satisfiedNow);
    }
    if (!ok) return fail("No frame found for this Pokemon with these trainer IDs");
    if (satisfied) *satisfied = satisfiedNow;

    Pokemon p = create4::newPokemon(t, e.species, e.level, seed);
    p.format = 3;
    p.pid = f.pid;
    std::memcpy(p.ivs, f.ivs, sizeof p.ivs);
    p.originGame = legality::version::kColosseumXD;
    p.language = t.language;
    p.trainer.name = t.name;
    p.trainer.tid = t.tid;
    p.trainer.sid = t.sid;
    p.trainer.gender = t.gender;
    p.nickname = fixedNickname(e, t.language);
    p.isNicknamed = e.nickname == 1;
    p.met.ball = kBallPoke;
    p.met.metLocation = e.location;
    p.met.metLevel = e.level;
    p.met.fatefulEncounter = (e.flags & data::kGcFateful) != 0;
    p.heldItem = 0;
    if (e.moves[0] != 0) {
        for (int m = 0; m < 4; ++m) p.moves[m] = p.pp[m] = p.ppUps[m] = 0;
        for (int m = 0; m < 4; ++m) {
            if (e.moves[m] != 0) edit4::setMove(p, m, e.moves[m]);
        }
    }
    edit4::refreshDerived(p);
    p.ability = data::ability3(p.species, f.abilityBit);

    uint8_t buffer[entity::kStoredSize];
    create4::encode(p, buffer);
    std::memcpy(out, buffer, pk3::kStoredSize);
    return pk3::toPokemon(out, pk3::kStoredSize);
}

}  // namespace pktool::pokemon::cxd3
