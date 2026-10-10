// Colosseum/XD shadow locks, Poke Spots and generated Colosseum/XD Pokemon.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/data/names.h"
#include "core/io/file_io.h"
#include "core/data/encounters3.h"
#include "core/data/learn4.h"
#include "core/data/personal.h"
#include "core/data/locks3.h"
#include "core/legality/legalize.h"
#include "core/legality/legality.h"
#include "core/legality/rng3.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/cxd3.h"
#include "core/pokemon/pk3.h"
#include "core/saves/gen3_save.h"
#include "tests/test.h"

using namespace pktool;
namespace pk3 = pokemon::pk3;

namespace {

std::vector<std::string> fixtureFiles() {
    const char* dir = std::getenv("PKTOOL_FIXTURES");
    if (dir == nullptr || *dir == '\0') return {};
    return io::findSaveFiles(dir, 2);
}

}  // namespace

TEST(real_saves_colosseum_xd_pokemon_verdicts) {
    for (const std::string& path : fixtureFiles()) {
        std::vector<uint8_t> bytes;
        CHECK(io::readFile(path, bytes));
        if (saves::Gen3Save::detect(bytes) == saves::Game::Unknown) continue;
        saves::Gen3Save save;
        CHECK(save.load(bytes) == saves::LoadResult::Ok);
        int total = 0;
        int valid = 0;
        auto visit = [&](const pokemon::Pokemon& p, const char* where) {
            if (p.isEgg || p.originGame != legality::version::kColosseumXD) return;
            legality::Report r = legality::checkGen3(p);
            ++total;
            if (r.legal()) ++valid;
            std::printf("  %s %s Lv.%d loc %d fateful %d TID %u SID %u: %s\n", where, data::speciesName(p.species), p.level,
                        p.met.metLocation, p.met.fatefulEncounter, p.trainer.tid, p.trainer.sid, r.legal() ? "legal" : "ILLEGAL");
            for (const auto& f : r.findings) {
                if (f.severity != legality::Severity::Valid) std::printf("      [%d] %s\n", static_cast<int>(f.severity), f.text.c_str());
            }
        };
        for (int slot = 0; slot < save.partyCount(); ++slot) {
            uint8_t buffer[pk3::kPartySize];
            if (save.readPartySlot(slot, buffer) == saves::SlotState::Valid) visit(pk3::toPokemon(buffer, sizeof buffer), "party");
        }
        for (int box = 0; box < save.boxCount(); ++box) {
            for (int slot = 0; slot < saves::SaveFile::kSlotsPerBox; ++slot) {
                uint8_t buffer[pk3::kStoredSize];
                if (save.readBoxSlot(box, slot, buffer) == saves::SlotState::Valid) visit(pk3::toPokemon(buffer, sizeof buffer), "box");
            }
        }
        std::printf("  %s: %d Colosseum/XD Pokemon, %d valid\n", io::fileName(path).c_str(), total, valid);
    }
}

// ------------------------------------------------------------ generated Pokemon

namespace {

pokemon::create4::Trainer trainerWith(uint16_t tid, uint16_t sid) {
    pokemon::create4::Trainer t;
    t.name = "PATE";
    t.tid = tid;
    t.sid = sid;
    t.gender = pokemon::Gender::Female;
    t.version = 4;  // FireRed
    t.language = 2;
    t.today = {26, 9, 29};
    return t;
}

// IDs the XD RNG draws (TID, SID are consecutive outputs).
void drawnIds(uint32_t origin, uint16_t* tid, uint16_t* sid) {
    uint32_t s = legality::rng3::xdNext(origin);
    *tid = static_cast<uint16_t>(s >> 16);
    s = legality::rng3::xdNext(s);
    *sid = static_cast<uint16_t>(s >> 16);
}

int gcIndex(uint16_t species, uint8_t kind, int from = 0) {
    for (int i = from; i < data::gc3Count(); ++i) {
        if (data::gc3(i).species == species && data::gc3(i).kind == kind) return i;
    }
    return -1;
}

bool hasText(const legality::Report& r, legality::Severity severity, const char* text) {
    for (const auto& f : r.findings) {
        if (f.severity == severity && f.text.find(text) != std::string::npos) return true;
    }
    return false;
}

// `p` with the plain XD frame of `origin` (IV, IV, ability, PID pair).
pokemon::Pokemon withFrame(pokemon::Pokemon p, uint32_t origin) {
    namespace rng3 = legality::rng3;
    uint32_t s = rng3::xdNext(origin);
    uint32_t iv1 = (s >> 16) & 0x7FFF;
    s = rng3::xdNext(s);
    uint32_t iv2 = (s >> 16) & 0x7FFF;
    s = rng3::xdNext(rng3::xdNext(s));
    uint32_t hi = s >> 16;
    s = rng3::xdNext(s);
    p.pid = (hi << 16) | (s >> 16);
    uint32_t iv32 = (iv2 << 15) | iv1;
    for (int k = 0; k < 6; ++k) p.ivs[k] = static_cast<uint8_t>((iv32 >> (5 * k)) & 31);
    uint8_t raw[pk3::kStoredSize];
    pokemon::create4::encode(p, raw);
    return pk3::toPokemon(raw, pk3::kStoredSize);
}

}  // namespace

TEST(cxd_every_creatable_entry_is_legal) {
    uint16_t tid, sid;
    drawnIds(0x13579BDF, &tid, &sid);
    pokemon::create4::Trainer trainer = trainerWith(tid, sid);
    std::vector<int> entries = pokemon::cxd3::menuEntries();
    CHECK(entries.size() > 100);
    int made = 0;
    for (int index : entries) {
        const data::Gc3& e = data::gc3(index);
        uint8_t raw[pk3::kStoredSize];
        std::string error;
        pokemon::Pokemon p = pokemon::cxd3::create(index, trainer, 1000u + index, pokemon::gift3::Criteria(), raw, nullptr, &error);
        if (p.species == 0) {
            std::printf("  not created: %s (%s)\n", data::speciesName(e.species), error.c_str());
            CHECK(false);
            continue;
        }
        ++made;
        legality::Report r = legality::checkGen3(p);
        if (!r.legal()) {
            std::printf("  %s %s:", data::speciesName(e.species), pokemon::cxd3::title(e).c_str());
            for (const auto& f : r.findings) {
                if (f.severity == legality::Severity::Invalid) std::printf(" [%s]", f.text.c_str());
            }
            std::printf("\n");
        }
        CHECK(r.legal());
        CHECK_EQ(p.originGame, legality::version::kColosseumXD);
        CHECK_EQ(p.met.metLocation, e.location);
        CHECK(!hasText(r, legality::Severity::Warning, "not verified"));
    }
    CHECK_EQ(made, static_cast<int>(entries.size()));
}

TEST(cxd_starters_need_ids_the_games_can_draw) {
    // Some IDs have no RNG state that draws them: the starter cannot exist.
    uint16_t tid = 0, sid = 0;
    uint32_t origins[2];
    for (uint32_t k = 1;; ++k) {
        tid = static_cast<uint16_t>(k * 40503u);
        sid = static_cast<uint16_t>(k * 12347u + 5);
        if (legality::rng3::pidOrigins((static_cast<uint32_t>(tid) << 16) | sid, origins, 2) == 0) break;
    }
    pokemon::create4::Trainer trainer = trainerWith(tid, sid);
    uint8_t raw[pk3::kStoredSize];
    std::string error;
    int umbreon = gcIndex(197, data::kGcColoStarter);
    CHECK(umbreon >= 0);
    pokemon::Pokemon p = pokemon::cxd3::create(umbreon, trainer, 1, pokemon::gift3::Criteria(), raw, nullptr, &error);
    CHECK_EQ(p.species, 0);
    CHECK(!error.empty());
    // A shadow still has a frame: it only needs the IDs for the shiny rule.
    p = pokemon::cxd3::create(gcIndex(216, data::kGcXdShadow), trainer, 1, pokemon::gift3::Criteria(), raw);
    CHECK_EQ(p.species, 216);
    CHECK(legality::checkGen3(p).legal());
}

TEST(cxd_shadow_locks_reject_frames_the_party_cannot_lead_to) {
    uint16_t tid, sid;
    drawnIds(0x2468ACE1, &tid, &sid);
    pokemon::create4::Trainer trainer = trainerWith(tid, sid);
    struct Case { uint16_t species; uint8_t kind; };
    for (Case c : {Case{280, data::kGcXdShadow}, Case{363, data::kGcXdShadow}, Case{296, data::kGcColoShadow},
                   Case{207, data::kGcColoShadow}}) {
        int index = gcIndex(c.species, c.kind);
        CHECK(index >= 0);
        uint8_t raw[pk3::kStoredSize];
        pokemon::Pokemon p = pokemon::cxd3::create(index, trainer, 7, pokemon::gift3::Criteria(), raw);
        CHECK_EQ(p.species, c.species);
        legality::Report good = legality::checkGen3(p);
        CHECK(good.legal());
        // Random plain frames: only a few percent can follow the locks.
        int legal = 0, tested = 0;
        for (uint32_t o = 1; tested < 300; ++o) {
            pokemon::Pokemon q = withFrame(p, o * 2654435761u);
            if (c.kind == data::kGcXdShadow && q.shiny) continue;  // reported by another rule
            ++tested;
            legality::Report r = legality::checkGen3(q);
            if (r.legal()) {
                ++legal;
            } else {
                CHECK(hasText(r, legality::Severity::Invalid, "locks of the trainer's team"));
            }
        }
        std::printf("  %s: %d of %d random frames satisfy the locks\n", data::speciesName(c.species), legal, tested);
        CHECK(legal > 0);
        CHECK(legal < 60);
    }
}

TEST(cxd_shadow_search_honours_the_wanted_traits) {
    uint16_t tid, sid;
    drawnIds(0x55AA55AA, &tid, &sid);
    pokemon::create4::Trainer trainer = trainerWith(tid, sid);
    int index = gcIndex(355, data::kGcXdShadow);  // Duskull: three locks
    CHECK(index >= 0);
    pokemon::gift3::Criteria want;
    want.nature = 11;
    bool satisfied = false;
    uint8_t raw[pk3::kStoredSize];
    pokemon::Pokemon p = pokemon::cxd3::create(index, trainer, 3, want, raw, &satisfied);
    CHECK(satisfied);
    CHECK_EQ(p.nature, 11);
    CHECK(legality::checkGen3(p).legal());
}

TEST(cxd_poke_spot_pids_are_verified) {
    uint16_t tid, sid;
    drawnIds(0x0F0F0F0F, &tid, &sid);
    pokemon::create4::Trainer trainer = trainerWith(tid, sid);
    int index = gcIndex(27, data::kGcXdPokeSpot);  // Sandshrew (Rock)
    CHECK(index >= 0);
    uint8_t raw[pk3::kStoredSize];
    pokemon::Pokemon p = pokemon::cxd3::create(index, trainer, 11, pokemon::gift3::Criteria(), raw);
    CHECK_EQ(p.species, 27);
    legality::Report r = legality::checkGen3(p);
    CHECK(r.legal());
    CHECK(hasText(r, legality::Severity::Valid, "XD Poke Spot"));
    int legal = 0;
    for (uint32_t o = 1; o <= 200; ++o) {
        pokemon::Pokemon q = withFrame(p, o * 40503u);
        legality::Report rq = legality::checkGen3(q);
        if (rq.legal()) {
            ++legal;
        } else {
            CHECK(hasText(rq, legality::Severity::Invalid, "Poke Spot"));
        }
    }
    CHECK(legal < 60);
}

TEST(cxd_legalize_makes_colosseum_xd_only_species) {
    // Species of the Colosseum/XD tables that no Gen III game offers (no wild
    // encounter, no event): LEGALIZE falls back to a Colosseum/XD origin.
    int tested = 0;
    for (int i = 0; i < data::gc3Count() && tested < 25; ++i) {
        const data::Gc3& e = data::gc3(i);
        if (e.kind == data::kGcColoEReader) continue;
        const data::Encounter4* begin;
        const data::Encounter4* end;
        data::encounters3(e.species, &begin, &end);
        if (begin != end || data::findEvent3(e.species) >= 0 || data::preEvolution4(e.species) != 0) continue;
        const data::PersonalInfo* info = data::personal4(e.species);
        if (info && (info->eggGroups[0] != data::kEggGroupUndiscovered || info->eggGroups[1] != data::kEggGroupUndiscovered)) {
            continue;  // breedable species hatch from a Day-Care egg instead
        }
        bool again = false;
        for (int j = 0; j < i; ++j) again = again || data::gc3(j).species == e.species;
        if (again) continue;
        ++tested;
        CHECK(legality::minCreateLevel3(e.species) > 0);
        pokemon::Pokemon p = pokemon::create4::newPokemon(trainerWith(12345, 54321), e.species, 50, e.species * 7u);
        legality::LegalizeResult r = legality::legalizeGen3(p, 4);
        for (const std::string& u : r.unresolved) std::printf("  %s unresolved: %s\n", data::speciesName(e.species), u.c_str());
        CHECK(r.unresolved.empty());
        CHECK(r.report.legal());
        CHECK_EQ(r.pokemon.species, e.species);
        // (Elekid-like babies hatch from their evolutions' eggs instead.)
        CHECK(r.pokemon.originGame == legality::version::kColosseumXD || r.pokemon.met.metLevel == 0);
        CHECK(r.pokemon.level >= 50);
    }
    std::printf("  %d species reach LEGALIZE's Colosseum/XD fallback\n", tested);

    // A Colosseum/XD Pokemon whose frame was edited is regenerated from its own encounter.
    uint16_t tid, sid;
    drawnIds(0x13579BDF, &tid, &sid);
    pokemon::create4::Trainer trainer = trainerWith(tid, sid);
    for (uint16_t species : {197, 355, 133}) {
        int index = gcIndex(species, species == 197 ? data::kGcColoStarter : species == 355 ? data::kGcXdShadow : data::kGcXdGift);
        uint8_t raw[pk3::kStoredSize];
        pokemon::Pokemon good = pokemon::cxd3::create(index, trainer, 5, pokemon::gift3::Criteria(), raw);
        CHECK_EQ(good.species, species);
        pokemon::Pokemon bad = good;
        bad.pid ^= 0x00030000u;
        pokemon::create4::encode(bad, raw);
        bad = pk3::toPokemon(raw, pk3::kStoredSize);
        CHECK(!legality::checkGen3(bad).legal());
        legality::LegalizeResult r = legality::legalizeGen3(bad, 4);  // (a starter's nature is fixed by the IDs)
        CHECK(r.report.legal());
        CHECK_EQ(r.pokemon.species, species);
        CHECK_EQ(r.pokemon.originGame, legality::version::kColosseumXD);
        CHECK_EQ(r.pokemon.trainer.tid, tid);
    }
    // A species with wild encounters never turns into a Colosseum/XD one.
    pokemon::Pokemon pikachu = pokemon::create4::newPokemon(trainerWith(12345, 54321), 25, 20, 3);
    CHECK(legality::legalizeGen3(pikachu, 4).pokemon.originGame != legality::version::kColosseumXD);
}
