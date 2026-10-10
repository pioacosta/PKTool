#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "core/bank/bank.h"
#include "core/data/gen3.h"
#include "core/data/names.h"
#include "core/data/sorted_names.h"
#include "core/io/file_io.h"
#include "core/legality/legalize.h"
#include "core/legality/legality.h"
#include "core/legality/pid_iv.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/edit4.h"
#include "core/pokemon/pk3.h"
#include "core/pokemon/pk4.h"
#include "core/pokemon/transfer34.h"
#include "core/saves/gen3_save.h"
#include "core/saves/save_probe.h"
#include "core/text/gen4_text.h"
#include "core/util/bytes.h"
#include "ds/app/app.h"
#include "tests/test.h"

using namespace pktool;
namespace pk3 = pokemon::pk3;
namespace edit4 = pokemon::edit4;
using saves::Gen3Save;

namespace {

constexpr size_t kSector = 0x1000;

uint16_t sectorChecksum(const uint8_t* sector) {
    uint32_t sum = 0;
    for (size_t i = 0; i < 0xF80; i += 4) sum += util::readU32(sector + i);
    return static_cast<uint16_t>(sum + (sum >> 16));
}

// A FireRed save whose newer slot (1) has its sectors rotated by 5, like
// after several saves in the game. Slot 0 is an older copy.
std::vector<uint8_t> blankFrlg() {
    std::vector<uint8_t> data(Gen3Save::kFileSize, 0);
    for (int slot = 0; slot < 2; ++slot) {
        for (int i = 0; i < 14; ++i) {
            uint8_t* sector = data.data() + (slot * 14 + i) * kSector;
            uint16_t id = static_cast<uint16_t>((i + (slot == 1 ? 5 : 0)) % 14);
            if (id == 0) {
                util::writeU32(sector + 0xAC, 1);  // FR/LG marker
                std::memset(sector, 0xFF, 8);  // not a Japanese save: all 8 name bytes used
                text::encodeGen3(slot == 1 ? "PATE" : "KATE", sector, 8, 7, 2);  // the newer slot has the new name
                sector[8] = 1;                    // female
                util::writeU16(sector + 0x0A, 12345);
                util::writeU16(sector + 0x0C, 54321);
            }
            util::writeU16(sector + 0xFF4, id);
            util::writeU32(sector + 0xFF8, 0x08012025);
            util::writeU32(sector + 0xFFC, slot == 1 ? 7 : 6);
            util::writeU16(sector + 0xFF6, sectorChecksum(sector));
        }
    }
    return data;
}

// A decrypted PK3 of `species` owned by the save's trainer.
void makePk3(uint16_t species, uint8_t out[pk3::kPartySize]) {
    std::memset(out, 0, pk3::kPartySize);
    std::memset(out + pk3::offset::kNickname, 0xFF, pk3::kNicknameChars);
    std::memset(out + pk3::offset::kOtName, 0xFF, pk3::kOtNameChars);
    pokemon::Pokemon p;
    p.format = 3;
    p.species = species;
    p.pid = 0x12345678;
    p.trainer.tid = 12345;
    p.trainer.sid = 54321;
    p.trainer.name = "PATE";
    p.trainer.gender = pokemon::Gender::Female;
    p.nickname = "SPARKY";
    p.language = 2;
    p.originGame = 4;  // FireRed
    p.met.ball = 4;
    p.met.metLevel = 5;
    p.met.metLocation = 101;
    p.exp = 1000;
    p.friendship = 70;
    p.heldItem = 234;  // Leftovers (Gen IV ID)
    p.moves[0] = 85;   // Thunderbolt
    p.pp[0] = 15;
    p.ivs[pokemon::kSpe] = 31;
    pk3::applyPokemon(p, out);
    pk3::refreshPartyStats(out);
}

}  // namespace

TEST(gen3_text_round_trip) {
    uint8_t buffer[10];
    std::memset(buffer, 0xAA, sizeof buffer);
    CHECK_EQ(text::encodeGen3("Pikachu!", buffer, 10, 10, 2), 8u);
    CHECK_EQ(buffer[8], text::kGen3Terminator);
    CHECK_EQ(buffer[9], 0xAA);  // bytes after the terminator are kept
    CHECK(text::decodeGen3(buffer, 10, 2) == "Pikachu!");
    CHECK(text::decodeGen3(buffer, 3, 2) == "Pik");
}

TEST(gen3_species_and_item_ids) {
    CHECK_EQ(data::speciesFromGen3(25), 25);
    CHECK_EQ(data::speciesFromGen3(277), 252);  // Treecko
    CHECK_EQ(data::speciesFromGen3(410), 386);  // Deoxys
    CHECK_EQ(data::speciesToGen3(386), 410);
    CHECK_EQ(data::speciesToGen3(252), 277);
    for (uint16_t s = 1; s <= 386; ++s) CHECK_EQ(data::speciesFromGen3(data::speciesToGen3(s)), s);
    CHECK_EQ(data::itemFromGen3(200), 234);  // Leftovers
    CHECK_EQ(data::itemToGen3(234), 200);
    CHECK_EQ(data::ability3(127, 1), data::ability3(127, 0));  // Pinsir: Mold Breaker came in Gen IV
    CHECK(data::ability3(214, 1) != data::ability3(214, 0));   // Heracross: Swarm / Guts
}

TEST(pk3_encrypt_round_trip) {
    uint8_t d[pk3::kPartySize];
    makePk3(25, d);
    CHECK_EQ(pk3::calcChecksum(d), pk3::storedChecksum(d));
    uint8_t e[pk3::kPartySize];
    std::memcpy(e, d, sizeof e);
    pk3::encrypt(e);
    CHECK(std::memcmp(e, d, pk3::kStoredSize) != 0);
    pk3::decrypt(e);
    CHECK(std::memcmp(e, d, sizeof e) == 0);

    pokemon::Pokemon p = pk3::toPokemon(d, sizeof d);
    CHECK_EQ(p.format, 3);
    CHECK_EQ(p.species, 25);
    CHECK(p.nickname == "SPARKY");
    CHECK(p.isNicknamed);
    CHECK(p.trainer.name == "PATE");
    CHECK_EQ(p.heldItem, 234);
    CHECK_EQ(p.moves[0], 85);
    CHECK_EQ(p.ivs[pokemon::kSpe], 31);
    CHECK_EQ(p.originGame, 4);
    CHECK_EQ(p.met.metLocation, 101);
    CHECK(p.trainer.gender == pokemon::Gender::Female);
    CHECK_EQ(d[pk3::offset::kPartyLevel], p.level);
}

TEST(gen3_save_reads_and_writes_the_newer_slot) {
    std::vector<uint8_t> bytes = blankFrlg();
    CHECK(Gen3Save::detect(bytes) == saves::Game::FireRedLeafGreen);
    Gen3Save save;
    CHECK(save.load(bytes) == saves::LoadResult::Ok);
    CHECK_EQ(save.activeSlot(), 1);
    CHECK(save.trainerName() == "PATE");
    CHECK_EQ(save.trainerId(), 12345);
    CHECK_EQ(save.secretId(), 54321);
    CHECK_EQ(save.trainerGender(), 1);
    CHECK_EQ(save.partyCount(), 0);
    CHECK_EQ(save.boxCount(), 14);

    uint8_t mon[pk3::kPartySize];
    makePk3(6, mon);
    CHECK(save.appendParty(mon));
    // Box 3, slot 24 crosses the boundary between two storage sectors.
    CHECK(save.writeBoxSlot(2, 23, mon));
    CHECK(save.setBoxName(0, "FAVES"));
    save.refreshChecksums();
    CHECK(save.checksumsValid());

    std::unique_ptr<saves::SaveFile> reloaded = saves::loadSaveFile(save.data());
    CHECK(reloaded != nullptr);
    CHECK_EQ(reloaded->format(), 3);
    CHECK_EQ(reloaded->gameVersion(), 4);  // FireRed, from the trainer's Pokémon
    CHECK_EQ(reloaded->partyCount(), 1);
    uint8_t back[pk3::kPartySize];
    CHECK(reloaded->readPartySlot(0, back) == saves::SlotState::Valid);
    CHECK(std::memcmp(back, mon, sizeof back) == 0);
    CHECK(reloaded->readBoxSlot(2, 23, back) == saves::SlotState::Valid);
    CHECK(std::memcmp(back, mon, pk3::kStoredSize) == 0);
    CHECK(reloaded->readBoxSlot(2, 22, back) == saves::SlotState::Empty);
    CHECK(reloaded->boxName(0) == "FAVES");

    // The older slot is untouched.
    CHECK(std::equal(bytes.begin(), bytes.begin() + 14 * kSector, reloaded->data().begin()));
}

TEST(gen3_save_rejects_other_files) {
    std::vector<uint8_t> bytes = blankFrlg();
    bytes[0x10] ^= 1;  // older slot: bad checksum is fine, the newer one is used
    Gen3Save save;
    CHECK(save.load(bytes) == saves::LoadResult::Ok);
    bytes[14 * kSector + 0x20] ^= 1;  // newer slot corrupted
    CHECK(save.load(bytes) == saves::LoadResult::BadChecksum);
    std::vector<uint8_t> random(Gen3Save::kFileSize, 0x5A);
    CHECK(Gen3Save::detect(random) == saves::Game::Unknown);
    CHECK_EQ(std::string(saves::versionName(3)), std::string("Emerald"));
    CHECK_EQ(saves::coverSpecies(4), 6);
}

namespace {

pokemon::create4::Trainer frlgTrainer() {
    pokemon::create4::Trainer t;
    t.name = "PATE";
    t.tid = 12345;
    t.sid = 54321;
    t.gender = pokemon::Gender::Female;
    t.version = 4;  // FireRed
    t.today = {26, 9, 29};
    return t;
}

// A legal FireRed Charizard, encoded as decrypted party PK3.
pokemon::Pokemon legalCharizard(uint8_t level, uint32_t seed) {
    pokemon::Pokemon p = pokemon::create4::newPokemon(frlgTrainer(), 6, level, seed);
    return legality::legalizeGen3(p, 4).pokemon;
}

void pressKey3(ui::App& app, uint32_t keys) {
    ui::Input in;
    in.pressed = keys;
    in.repeated = keys;
    app.update(in);
    app.update(ui::Input());
}

}  // namespace

TEST(gen3_every_species_is_created_legal) {
    int illegal = 0;
    int eventOnly = 0;
    for (uint16_t sp = 1; sp <= 386; ++sp) {
        uint8_t level = legality::minCreateLevel3(sp);
        if (level == 0) {  // only from events (Mew, Jirachi, Deoxys...)
            ++eventOnly;
            pokemon::Pokemon p = pokemon::create4::newPokemon(frlgTrainer(), sp, 70, sp);
            CHECK(!legality::legalizeGen3(p, 4).unresolved.empty());
            continue;
        }
        pokemon::Pokemon p = pokemon::create4::newPokemon(frlgTrainer(), sp, std::max<uint8_t>(level, 50), sp * 7919u);
        CHECK_EQ(p.format, 3);
        legality::LegalizeResult r = legality::legalizeGen3(p, 4);
        uint8_t raw[pk3::kPartySize] = {};
        pokemon::create4::encode(r.pokemon, raw);
        pokemon::Pokemon back = pk3::toPokemon(raw, pk3::kStoredSize);
        legality::Report report = legality::checkGen3(back);
        if (!report.legal()) {
            ++illegal;
            std::printf("  %s:", data::speciesName(sp));
            for (const auto& f : report.findings) {
                if (f.severity == legality::Severity::Invalid) std::printf(" [%s]", f.text.c_str());
            }
            std::printf("\n");
        }
        CHECK_EQ(back.species, sp);
        if (back.met.metLocation == legality::location3::kFateful) {  // only from events: the event's trainer
            ++eventOnly;
        } else {
            CHECK(back.trainer.name == "PATE");
        }
    }
    std::printf("  %d event-only species\n", eventOnly);
    CHECK(eventOnly < 12);
    CHECK_EQ(illegal, 0);
}

TEST(gen3_legalize_keeps_requested_traits) {
    pokemon::Pokemon p = pokemon::create4::newPokemon(frlgTrainer(), 25, 30, 77);  // Pikachu
    p.nature = 10;  // Timid (PID fixed by LEGALIZE)
    p.shiny = true;
    p.met.ball = 20;  // not a Gen III ball
    legality::LegalizeResult r = legality::legalizeGen3(p, 4);
    CHECK(r.report.legal());
    CHECK_EQ(r.pokemon.nature, 10);
    CHECK(r.pokemon.shiny);
    CHECK(r.pokemon.met.ball <= 12);
    CHECK(legality::findPidIvMethod(r.pokemon.pid, r.pokemon.ivs) != legality::PidIvMethod::None);
}

TEST(gen3_egg_is_created_legal) {
    pokemon::Pokemon egg = pokemon::create4::newEgg(frlgTrainer(), 26, 5);  // Raichu -> Pichu egg
    legality::LegalizeResult r = legality::legalizeGen3(egg, 4);
    for (const std::string& u : r.unresolved) std::printf("  unresolved: %s\n", u.c_str());
    CHECK(r.report.legal());
    CHECK(r.pokemon.isEgg);
    CHECK_EQ(r.pokemon.species, 172);
    CHECK_EQ(r.pokemon.level, 5);
    CHECK_EQ(r.pokemon.met.metLevel, 0);
}

TEST(gen3_checker_catches_problems) {
    pokemon::Pokemon good = legalCharizard(50, 3);
    CHECK(legality::checkGen3(good).legal());
    pokemon::Pokemon bad = good;
    bad.moves[0] = 400;  // a Gen IV move
    CHECK(!legality::checkGen3(bad).legal());
    bad = good;
    bad.ability = data::ability3(6, 1) == good.ability ? 0 : data::ability3(6, 1);
    CHECK(!legality::checkGen3(bad).legal());
    bad = good;
    bad.met.ball = 15;  // Heal Ball did not exist
    CHECK(!legality::checkGen3(bad).legal());
    bad = good;
    bad.moves[3] = 307;  // Blast Burn: FR/LG tutor for Charizard
    bad.pp[3] = 5;
    CHECK(legality::checkGen3(bad).legal());
}

TEST(pal_park_conversion) {
    pokemon::Pokemon mon = legalCharizard(60, 9);
    edit4::setMove(mon, 0, 19);   // Fly (HM)
    edit4::setMove(mon, 1, 307);  // Blast Burn (FR/LG tutor)
    uint8_t d3[pk3::kPartySize] = {};
    pokemon::create4::encode(mon, d3);
    util::writeU32(d3 + pk3::offset::kRibbons, (2u << 0) | (1u << 19));  // Cool Super rank, Effort Ribbon
    d3[pk3::offset::kContest] = 40;
    pk3::refreshChecksum(d3);

    uint8_t d4[pokemon::pk4::kStoredSize];
    std::vector<std::string> notes;
    pokemon::transfer34::convert(d3, d4, {26, 9, 30}, &notes);
    CHECK_EQ(pokemon::pk4::calcChecksum(d4), pokemon::pk4::storedChecksum(d4));
    pokemon::Pokemon q = pokemon::pk4::toPokemon(d4, pokemon::pk4::kStoredSize);
    CHECK_EQ(q.format, 4);
    CHECK_EQ(q.species, 6);
    CHECK_EQ(q.pid, mon.pid);
    CHECK_EQ(q.met.metLocation, 55);
    CHECK_EQ(q.met.metLevel, 60);
    CHECK_EQ(q.originGame, 4);
    CHECK(q.nickname == "CHARIZARD");
    CHECK(q.trainer.name == "PATE");
    CHECK_EQ(q.moves[0], 307);  // Fly forgotten, the rest moved up
    CHECK(!notes.empty());
    CHECK_EQ(d4[0x3C] & 3, 3);          // Cool + Cool Super
    CHECK((d4[0x3F] & (1 << 0)) != 0);  // Effort
    CHECK_EQ(d4[0x1E], 40);
    legality::Report report = legality::checkGen4(q);
    for (const auto& f : report.findings) {
        if (f.severity == legality::Severity::Invalid) std::printf("  invalid: %s\n", f.text.c_str());
    }
    CHECK(report.legal());
}

TEST(ui_edits_and_writes_gen3_save_and_deposits) {
    char pattern[] = "/tmp/pktool-gen3-XXXXXX";
    std::string dir = mkdtemp(pattern);
    std::string path = io::joinPath(dir, "Pokemon FireRed.sav");
    CHECK(io::safeWriteFile(path, blankFrlg()) == io::WriteResult::Ok);
    ui::Config config;
    config.scanRoot = dir;
    config.dataDir = io::joinPath(dir, "PKTOOL");
    {
        ui::App app(config);
        CHECK(app.openSave(path));
        pressKey3(app, ui::kKeyA);  // save menu: Boxes
        pressKey3(app, ui::kKeyR);      // box 1, slot 1
        pressKey3(app, ui::kKeyA);      // Add menu
        pressKey3(app, ui::kKeyA);      // New Pokémon
        for (int i = 0; i < data::sortedIndex(data::NameKind::Species, 3, 4); ++i) pressKey3(app, ui::kKeyDown);  // #004 Charmander (alphabetical list)
        pressKey3(app, ui::kKeyA);      // create
        pressKey3(app, ui::kKeyA);      // close the report
        pressKey3(app, ui::kKeyStart);  // put it in the slot
        pressKey3(app, ui::kKeyRight);  // slot 2
        pressKey3(app, ui::kKeyA);      // Add menu (remembers New Pokémon)
        pressKey3(app, ui::kKeyA);
        for (int i = 0; i < data::sortedIndex(data::NameKind::Species, 3, 1); ++i) pressKey3(app, ui::kKeyDown);
        pressKey3(app, ui::kKeyA);      // #001 Bulbasaur
        pressKey3(app, ui::kKeyA);
        pressKey3(app, ui::kKeyStart);
        pressKey3(app, ui::kKeyY);      // Bank: deposit Bulbasaur
        pressKey3(app, ui::kKeyA);
        pressKey3(app, ui::kKeyA);
        pressKey3(app, ui::kKeySelect);
        pressKey3(app, ui::kKeyA);      // write the save and the Bank
    }
    std::vector<uint8_t> written;
    CHECK(io::readFile(path, written));
    Gen3Save save;
    CHECK(save.load(written) == saves::LoadResult::Ok);
    uint8_t slot[pk3::kPartySize] = {};
    CHECK(save.readBoxSlot(0, 0, slot) == saves::SlotState::Valid);
    pokemon::Pokemon p = pk3::toPokemon(slot, pk3::kStoredSize);
    CHECK_EQ(p.species, 4);
    CHECK(p.trainer.name == "PATE");
    CHECK(legality::checkGen3(p).legal());
    CHECK(save.readBoxSlot(0, 1, slot) == saves::SlotState::Empty);  // deposited
    // The other slot of the file still holds the older save.
    std::vector<uint8_t> original = blankFrlg();
    CHECK(std::equal(written.begin(), written.begin() + 14 * kSector, original.begin()));

    bank::Bank bank;
    CHECK(bank.load(io::joinPath(config.dataDir, "bank/bank.pkb")) == bank::Bank::LoadResult::Ok);
    CHECK_EQ(bank.count(), 1);
    int found = 0;
    for (int b = 0; b < bank::Bank::kBoxCount; ++b) {
        for (int s = 0; s < bank::Bank::kSlotsPerBox; ++s) {
            if (bank.format(b, s) != 3) continue;
            ++found;
            CHECK_EQ(pk3::toPokemon(bank.entity(b, s), pk3::kStoredSize).species, 1);
        }
    }
    CHECK_EQ(found, 1);
}
