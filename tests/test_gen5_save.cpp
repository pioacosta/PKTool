#include <cstdlib>
#include <cstring>
#include <vector>

#include "core/bank/bank.h"
#include "core/data/events5.h"
#include "core/data/names.h"
#include "core/data/sorted_names.h"
#include "core/io/file_io.h"
#include "core/legality/legality.h"
#include "core/pokemon/edit4.h"
#include "core/pokemon/pk4.h"
#include "core/pokemon/pk5.h"
#include "core/pokemon/transfer45.h"
#include "tests/fixtures.h"
#include "ds/app/app.h"
#include "core/saves/gen5_save.h"
#include "core/saves/save_commit.h"
#include "core/text/gen4_text.h"
#include "core/util/bytes.h"
#include "core/util/crc16.h"
#include "tests/test.h"

using namespace pktool;
using saves::Gen5Save;
namespace pk4 = pokemon::pk4;
namespace pk5 = pokemon::pk5;

namespace {

// A blank Black 2/White 2 save with trainer "PIO" 12345/54321 and valid
// checksums (the checksum table sits at 0x25F00).
std::vector<uint8_t> blankB2W2() {
    std::vector<uint8_t> data(Gen5Save::kFileSize, 0);
    text::encodeGen5("PIO", &data[0x19400 + 4], 8, 7);
    util::writeU16(&data[0x19400 + 0x14], 12345);
    util::writeU16(&data[0x19400 + 0x16], 54321);
    data[0x19400 + 0x1E] = 2;   // English
    data[0x19400 + 0x1F] = 23;  // Black 2
    text::encodeGen5("BOX 1", &data[4], 10, 9);
    Gen5Save save;
    // Detection needs the footer checksum: write it by hand first.
    util::writeU16(&data[0x25FA2], util::crc16Ccitt(&data[0x25F00], 0x94));
    CHECK(save.load(data) == saves::LoadResult::Ok);
    save.refreshChecksums();
    return save.data();
}

// A Gen V Snivy (Black 2) with a nature independent of its PID.
std::vector<uint8_t> snivy() {
    std::vector<uint8_t> raw(pk5::kStoredSize, 0);
    pokemon::Pokemon p;
    p.format = 5;
    p.species = 495;
    p.pid = 0x12345678;
    p.nature = 3;  // Adamant, while PID % 25 is 1
    p.nickname = "SNIVY";
    p.trainer.name = "PIO";
    p.trainer.tid = 12345;
    p.trainer.sid = 54321;
    p.originGame = 23;
    p.language = 2;
    p.met.ball = 4;
    p.met.metLocation = 117;  // starter gift in Aspertia City
    p.met.metLevel = 5;
    p.gender = pokemon::edit4::genderFromPid(495, p.pid);
    p.ability = pokemon::edit4::abilityForSlot(495, (p.pid >> 16) & 1);
    pokemon::edit4::setLevel(p, 5);
    pokemon::edit4::setMove(p, 0, 33);  // Tackle
    for (auto& iv : p.ivs) iv = 20;
    pk5::applyPokemon(p, raw.data());
    return raw;
}

}  // namespace

TEST(gen5_detects_and_reads_trainer) {
    std::vector<uint8_t> data = blankB2W2();
    CHECK(Gen5Save::detect(data) == saves::Game::Black2White2);
    Gen5Save save;
    CHECK(save.load(data) == saves::LoadResult::Ok);
    CHECK(save.checksumsValid());
    CHECK(save.trainerName() == "PIO");
    CHECK_EQ(save.trainerId(), 12345);
    CHECK_EQ(save.gameVersion(), 23);
    CHECK(save.boxName(0) == "BOX 1");
    CHECK_EQ(save.boxCount(), 24);
    CHECK_EQ(save.partyCount(), 0);
    std::unique_ptr<saves::SaveFile> any = saves::loadSaveFile(data);
    CHECK(any != nullptr && any->format() == 5);
}

TEST(pk5_keeps_nature_and_hidden_ability) {
    std::vector<uint8_t> raw = snivy();
    pokemon::Pokemon p = pk5::toPokemon(raw.data(), raw.size());
    CHECK_EQ(p.format, 5);
    CHECK_EQ(p.nature, 3);
    CHECK(p.nickname == "SNIVY");
    CHECK(legality::check(p).legal());

    // Nature edits do not touch the PID in Gen V.
    uint32_t pid = p.pid;
    CHECK(pokemon::edit4::setNature(p, 10) == pokemon::edit4::Result::Ok);
    CHECK_EQ(p.pid, pid);
    // Hidden ability (Contrary for Snivy) and back to a regular slot.
    CHECK(pokemon::edit4::setAbilitySlot(p, 2) == pokemon::edit4::Result::Ok);
    CHECK(p.hiddenAbility);
    CHECK_EQ(p.ability, 126);
    CHECK(pokemon::edit4::setShiny(p, true) == pokemon::edit4::Result::Ok);
    CHECK(p.shiny);
    CHECK(p.hiddenAbility);
    pk5::applyPokemon(p, raw.data());
    pokemon::Pokemon back = pk5::toPokemon(raw.data(), raw.size());
    CHECK_EQ(back.nature, 10);
    CHECK(back.hiddenAbility);
    CHECK(back.shiny);
    // A starter never has its hidden ability: the checker says so.
    legality::Report report = legality::check(back);
    CHECK(!report.legal());
    bool hiddenReported = false;
    for (const auto& f : report.findings) hiddenReported = hiddenReported || f.text.find("Hidden ability") == 0;
    CHECK(hiddenReported);
    CHECK(pokemon::edit4::setAbilitySlot(back, 1) == pokemon::edit4::Result::Ok);
    CHECK(!back.hiddenAbility);
    CHECK_EQ((back.pid >> 16) & 1, 1u);
}

TEST(gen5_write_slot_and_commit) {
    char pattern[] = "/tmp/pktool-gen5-XXXXXX";
    std::string dir = mkdtemp(pattern);
    std::string path = io::joinPath(dir, "Pokemon Black 2.sav");
    const std::vector<uint8_t> original = blankB2W2();
    CHECK(io::safeWriteFile(path, original) == io::WriteResult::Ok);

    Gen5Save save;
    CHECK(save.load(original) == saves::LoadResult::Ok);
    std::vector<uint8_t> raw = snivy();
    CHECK(save.writeBoxSlot(23, 29, raw.data()));
    saves::CommitResult result = saves::commitSave(save, path, io::joinPath(dir, "backups"));
    CHECK(result.status == saves::CommitResult::Status::Ok);

    std::vector<uint8_t> written;
    CHECK(io::readFile(path, written));
    Gen5Save reloaded;
    CHECK(reloaded.load(written) == saves::LoadResult::Ok);
    CHECK(reloaded.checksumsValid());
    uint8_t slot[pk5::kStoredSize];
    CHECK(reloaded.readBoxSlot(23, 29, slot) == saves::SlotState::Valid);
    CHECK(std::memcmp(slot, raw.data(), sizeof slot) == 0);
    CHECK(reloaded.readBoxSlot(0, 0, slot) == saves::SlotState::Empty);
}

TEST(bank_file_round_trip_and_corruption) {
    char pattern[] = "/tmp/pktool-bank-XXXXXX";
    std::string dir = mkdtemp(pattern);
    std::string path = io::joinPath(dir, "bank/bank.pkb");
    bank::Bank bank;
    CHECK(bank.load(path) == bank::Bank::LoadResult::Created);
    std::vector<uint8_t> raw = snivy();
    CHECK(bank.set(19, 29, 5, raw.data()));
    CHECK(bank.save() == io::WriteResult::Ok);

    bank::Bank again;
    CHECK(again.load(path) == bank::Bank::LoadResult::Ok);
    CHECK_EQ(again.count(), 1);
    CHECK_EQ(again.format(19, 29), 5);
    CHECK(std::memcmp(again.entity(19, 29), raw.data(), raw.size()) == 0);

    // A damaged file is reported and never overwritten.
    std::vector<uint8_t> bytes;
    CHECK(io::readFile(path, bytes));
    bytes[100] ^= 0xFF;
    CHECK(io::safeWriteFile(path, bytes) == io::WriteResult::Ok);
    bank::Bank damaged;
    CHECK(damaged.load(path) == bank::Bank::LoadResult::Corrupt);
    CHECK(damaged.readOnly());
    CHECK(!damaged.set(0, 0, 5, raw.data()));
    CHECK(damaged.save() != io::WriteResult::Ok);
}

TEST(transfer45_converts_like_poke_transfer) {
    uint8_t pk4Data[pk4::kStoredSize];
    std::memcpy(pk4Data, test::kRoseradePk4, sizeof pk4Data);
    pokemon::Pokemon before = pk4::toPokemon(pk4Data, sizeof pk4Data);
    // Teach it Cut (an HM) in slot 2 to see it removed.
    pokemon::Pokemon withHm = before;
    pokemon::edit4::setMove(withHm, 1, 15);
    pk4::applyPokemon(withHm, pk4Data);

    uint8_t out[pk4::kStoredSize];
    std::vector<std::string> notes;
    pokemon::transfer45::convert(pk4Data, out, &notes);
    CHECK_EQ(pk4::calcChecksum(out), pk4::storedChecksum(out));
    pokemon::Pokemon after = pk5::toPokemon(out, sizeof out);
    CHECK_EQ(after.species, before.species);
    CHECK_EQ(after.pid, before.pid);
    CHECK_EQ(after.nature, before.nature);
    CHECK(after.nickname == before.nickname);
    CHECK(after.trainer.name == before.trainer.name);
    CHECK_EQ(after.met.metLocation, 30001);
    CHECK_EQ(after.met.metLevel, before.level);
    CHECK_EQ(after.friendship, 70);
    for (uint16_t m : after.moves) CHECK(m != 15);
    CHECK(after.moves[0] != 0);
    bool noted = false;
    for (const std::string& n : notes) noted = noted || n.find("Cut") != std::string::npos;
    CHECK(noted);
    CHECK(legality::check(after).legal());
}

namespace {

void pressKey5(ui::App& app, uint32_t keys) {
    ui::Input in;
    in.pressed = keys;
    in.repeated = keys;
    app.update(in);
    app.update(ui::Input());
}

}  // namespace

TEST(ui_bank_moves_gen4_pokemon_into_gen5_save) {
    char pattern[] = "/tmp/pktool-bank45-XXXXXX";
    std::string dir = mkdtemp(pattern);
    std::string path = io::joinPath(dir, "Pokemon Black 2.sav");
    CHECK(io::safeWriteFile(path, blankB2W2()) == io::WriteResult::Ok);
    ui::Config config;
    config.scanRoot = dir;
    config.dataDir = io::joinPath(dir, "PKTOOL");
    std::string bankFile = io::joinPath(config.dataDir, "bank/bank.pkb");
    {
        bank::Bank bank;
        bank.load(bankFile);
        CHECK(bank.set(0, 0, 4, test::kRoseradePk4));
        CHECK(bank.save() == io::WriteResult::Ok);
    }
    {
        ui::App app(config);
        CHECK(app.openSave(path));
        pressKey5(app, ui::kKeyA);  // save menu: Boxes
        pressKey5(app, ui::kKeyR);  // box 1, slot 1 (empty)
        pressKey5(app, ui::kKeyY);
        pressKey5(app, ui::kKeyA);  // withdraw: converted to PK5
        pressKey5(app, ui::kKeyA);
        pressKey5(app, ui::kKeySelect);
        pressKey5(app, ui::kKeyA);
    }
    std::vector<uint8_t> written;
    CHECK(io::readFile(path, written));
    Gen5Save save;
    CHECK(save.load(written) == saves::LoadResult::Ok);
    uint8_t slot[pk5::kStoredSize];
    CHECK(save.readBoxSlot(0, 0, slot) == saves::SlotState::Valid);
    pokemon::Pokemon p = pk5::toPokemon(slot, sizeof slot);
    CHECK_EQ(p.species, 407);
    CHECK_EQ(p.met.metLocation, 30001);
    CHECK(legality::check(p).legal());
    bank::Bank bank;
    CHECK(bank.load(bankFile) == bank::Bank::LoadResult::Ok);
    CHECK_EQ(bank.count(), 0);
}

TEST(ui_creates_legal_pokemon_in_gen5_save) {
    char pattern[] = "/tmp/pktool-create5-XXXXXX";
    std::string dir = mkdtemp(pattern);
    std::string path = io::joinPath(dir, "Pokemon Black 2.sav");
    CHECK(io::safeWriteFile(path, blankB2W2()) == io::WriteResult::Ok);
    ui::Config config;
    config.scanRoot = dir;
    config.dataDir = io::joinPath(dir, "PKTOOL");
    {
        ui::App app(config);
        CHECK(app.openSave(path));
        pressKey5(app, ui::kKeyA);  // save menu: Boxes
        pressKey5(app, ui::kKeyR);  // box 1, slot 1
        pressKey5(app, ui::kKeyA);  // Add menu
        pressKey5(app, ui::kKeyA);  // New Pokémon
        for (int i = 0; i < 70; ++i) pressKey5(app, ui::kKeyR);  // jump to the end of the list
        pressKey5(app, ui::kKeyUp);  // one before the last species
        pressKey5(app, ui::kKeyA);   // create
        pressKey5(app, ui::kKeyA);   // close the report
        pressKey5(app, ui::kKeyStart);
        pressKey5(app, ui::kKeySelect);
        pressKey5(app, ui::kKeyA);
    }
    std::vector<uint8_t> written;
    CHECK(io::readFile(path, written));
    Gen5Save save;
    CHECK(save.load(written) == saves::LoadResult::Ok);
    uint8_t slot[pk5::kStoredSize];
    CHECK(save.readBoxSlot(0, 0, slot) == saves::SlotState::Valid);
    pokemon::Pokemon p = pk5::toPokemon(slot, sizeof slot);
    CHECK_EQ(p.format, 5);
    CHECK(p.trainer.name == "PIO");
    std::printf("  created %s Lv.%d\n", data::speciesName(p.species), p.level);
    CHECK(p.species > 600);
}

TEST(ui_receives_wonder_card_in_gen5_save) {
    char pattern[] = "/tmp/pktool-event5-XXXXXX";
    std::string dir = mkdtemp(pattern);
    std::string path = io::joinPath(dir, "Pokemon Black 2.sav");
    CHECK(io::safeWriteFile(path, blankB2W2()) == io::WriteResult::Ok);
    ui::Config config;
    config.scanRoot = dir;
    config.dataDir = io::joinPath(dir, "PKTOOL");
    {
        ui::App app(config);
        CHECK(app.openSave(path));
        pressKey5(app, ui::kKeyA);  // save menu: Boxes
        pressKey5(app, ui::kKeyR);     // box 1, slot 1
        pressKey5(app, ui::kKeyA);     // Add menu
        pressKey5(app, ui::kKeyDown);
        pressKey5(app, ui::kKeyDown);  // Event
        pressKey5(app, ui::kKeyA);     // card list
        pressKey5(app, ui::kKeyA);     // receive the first card
        pressKey5(app, ui::kKeyA);     // close the report
        pressKey5(app, ui::kKeyStart);
        pressKey5(app, ui::kKeySelect);
        pressKey5(app, ui::kKeyA);
    }
    std::vector<uint8_t> written;
    CHECK(io::readFile(path, written));
    Gen5Save save;
    CHECK(save.load(written) == saves::LoadResult::Ok);
    uint8_t version = save.gameVersion();
    // The card list is alphabetical by species: the first row is the first species by name.
    int first = -1;
    for (int i = 0; i < data::events5Count(); ++i) {
        if (!data::canReceiveEvent5(data::event5(i), version)) continue;
        if (first < 0 || data::foldName(data::speciesName(data::event5(i).species)) <
                             data::foldName(data::speciesName(data::event5(first).species))) {
            first = i;
        }
    }
    CHECK(first >= 0);
    uint8_t slot[pk5::kStoredSize];
    CHECK(save.readBoxSlot(0, 0, slot) == saves::SlotState::Valid);
    pokemon::Pokemon p = pk5::toPokemon(slot, sizeof slot);
    CHECK_EQ(p.species, data::event5(first).species);
    CHECK(p.met.fatefulEncounter);
    CHECK(legality::check(p).legal());
}

TEST(gen5_box_rename) {
    Gen5Save save;
    CHECK(save.load(blankB2W2()) == saves::LoadResult::Ok);
    CHECK(save.setBoxName(23, "Legends"));
    save.refreshChecksums();
    CHECK(save.checksumsValid());
    Gen5Save reloaded;
    CHECK(reloaded.load(save.data()) == saves::LoadResult::Ok);
    CHECK(reloaded.boxName(23) == "Legends");
    CHECK(reloaded.boxName(0) == "BOX 1");
}

TEST(ui_bank_move_inside_bank) {
    char pattern[] = "/tmp/pktool-bankmove-XXXXXX";
    std::string dir = mkdtemp(pattern);
    ui::Config config;
    config.scanRoot = dir;
    config.dataDir = io::joinPath(dir, "PKTOOL");
    std::string bankFile = io::joinPath(config.dataDir, "bank/bank.pkb");
    std::vector<uint8_t> raw = snivy();
    {
        bank::Bank bank;
        bank.load(bankFile);
        CHECK(bank.set(0, 0, 5, raw.data()));
        CHECK(bank.save() == io::WriteResult::Ok);
    }
    ui::App app(config);
    pressKey5(app, ui::kKeyDown);   // Bank tile
    pressKey5(app, ui::kKeyA);
    pressKey5(app, ui::kKeyX);      // pick up
    pressKey5(app, ui::kKeyR);      // Bank box 2
    pressKey5(app, ui::kKeyDown);   // slot 7
    pressKey5(app, ui::kKeyX);      // drop
    bank::Bank bank;
    CHECK(bank.load(bankFile) == bank::Bank::LoadResult::Ok);
    CHECK_EQ(bank.format(0, 0), 0);
    CHECK_EQ(bank.format(1, 6), 5);
    CHECK(std::memcmp(bank.entity(1, 6), raw.data(), raw.size()) == 0);
}
