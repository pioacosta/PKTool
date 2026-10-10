#include <cstdlib>
#include <cstring>
#include <vector>

#include "core/bank/bank.h"
#include "core/data/events4.h"
#include "core/data/names.h"
#include "core/data/sorted_names.h"
#include "core/legality/legality.h"
#include "core/pokemon/pk4.h"
#include "core/io/file_io.h"
#include "core/pokemon/edit4.h"
#include "core/saves/gen4_save.h"
#include "core/saves/save_commit.h"
#include "core/saves/save_probe.h"
#include "core/text/gen4_text.h"
#include "core/util/bytes.h"
#include "core/util/crc16.h"
#include "ds/app/app.h"
#include "tests/fixtures.h"
#include "tests/test.h"

using namespace pktool;
using saves::Game;
using saves::Gen4Save;
namespace pk4 = pokemon::pk4;

namespace {

struct TestLayout {
    Game game;
    uint32_t generalSize, storageOffset, storageSize, footerSize, trainer, party;
    size_t boxSlot(int box, int slot) const {
        return (game == Game::HeartGoldSoulSilver ? box * 0x1000 : 4 + box * 0xFF0) + slot * 136;
    }
};

const TestLayout kPt{Game::Platinum, 0xCF2C, 0xCF2C, 0x121E4, 0x14, 0x68, 0xA0};
const TestLayout kDp{Game::DiamondPearl, 0xC100, 0xC100, 0x121E0, 0x14, 0x64, 0x98};
const TestLayout kHgss{Game::HeartGoldSoulSilver, 0xF628, 0xF700, 0x12310, 0x10, 0x64, 0x98};

void writeFooter(uint8_t* block, uint32_t size, uint32_t footerSize, uint32_t counter) {
    uint8_t* end = block + size;
    util::writeU32(end - 0x14, counter);  // major counter (read by the block chooser)
    util::writeU32(end - 0x10, counter);
    util::writeU32(end - 0xC, size);
    util::writeU32(end - 0x8, 0x20060623);
    util::writeU16(end - 2, util::crc16Ccitt(block, size - footerSize));
}

// Builds a blank save where partition `newest` holds the most recent blocks,
// with trainer "ASH" 12345/54321, the Roserade fixture in party slot 0 and
// box 2 slot 5, marked with `tag` in its held item.
std::vector<uint8_t> buildSave(const TestLayout& l, int newest = 1) {
    std::vector<uint8_t> data(Gen4Save::kFileSize, 0);
    for (int partition = 0; partition < 2; ++partition) {
        uint8_t* general = &data[partition * Gen4Save::kPartitionSize];
        uint8_t* storage = general + l.storageOffset;

        text::encodeGen4("ASH", general + l.trainer, 8, 7);
        util::writeU16(general + l.trainer + 0x10, 12345);
        util::writeU16(general + l.trainer + 0x12, 54321);
        general[l.trainer + 0x19] = 2;  // English

        uint8_t pkm[pk4::kPartySize];
        std::memcpy(pkm, test::kRoseradePk4, sizeof(pkm));
        util::writeU16(pkm + pk4::offset::kHeldItem, static_cast<uint16_t>(100 + partition));
        pk4::refreshChecksum(pkm);

        general[l.party - 4] = 1;
        uint8_t party[pk4::kPartySize];
        std::memcpy(party, pkm, sizeof(party));
        pk4::encrypt(party, sizeof(party));
        std::memcpy(general + l.party, party, sizeof(party));

        uint8_t stored[pk4::kStoredSize];
        std::memcpy(stored, pkm, sizeof(stored));
        pk4::encrypt(stored, sizeof(stored));
        std::memcpy(storage + l.boxSlot(2, 5), stored, sizeof(stored));

        uint32_t counter = partition == newest ? 10 : 9;
        writeFooter(general, l.generalSize, l.footerSize, counter);
        writeFooter(storage, l.storageSize, l.footerSize, counter);
    }
    return data;
}

}  // namespace

TEST(gen4_detects_each_game) {
    for (const TestLayout* l : {&kDp, &kPt, &kHgss}) {
        Gen4Save save;
        CHECK(save.load(buildSave(*l)) == Gen4Save::LoadResult::Ok);
        CHECK(save.game() == l->game);
        CHECK(save.checksumsValid());
    }
}

TEST(gen4_rejects_bad_input) {
    Gen4Save save;
    CHECK(save.load(std::vector<uint8_t>(1000, 0)) == Gen4Save::LoadResult::WrongSize);
    CHECK(save.load(std::vector<uint8_t>(Gen4Save::kFileSize, 0xFF)) == Gen4Save::LoadResult::UnknownFormat);
    CHECK(!save.isLoaded());
}

TEST(gen4_reads_trainer_party_and_boxes) {
    Gen4Save save;
    CHECK(save.load(buildSave(kPt)) == Gen4Save::LoadResult::Ok);
    CHECK(save.trainerName() == "ASH");
    CHECK_EQ(save.trainerId(), 12345);
    CHECK_EQ(save.secretId(), 54321);
    CHECK_EQ(save.partyCount(), 1);

    uint8_t party[pk4::kPartySize];
    CHECK(save.readPartySlot(0, party) == Gen4Save::SlotState::Valid);
    CHECK_EQ(util::readU16(party + pk4::offset::kSpecies), 407);
    CHECK(save.readPartySlot(1, party) == Gen4Save::SlotState::Empty);

    uint8_t stored[pk4::kStoredSize];
    CHECK(save.readBoxSlot(2, 5, stored) == Gen4Save::SlotState::Valid);
    CHECK(save.readBoxSlot(2, 4, stored) == Gen4Save::SlotState::Empty);
    CHECK(save.readBoxSlot(18, 0, stored) == Gen4Save::SlotState::Empty);
}

TEST(gen4_uses_newest_partition) {
    for (int newest : {0, 1}) {
        Gen4Save save;
        CHECK(save.load(buildSave(kPt, newest)) == Gen4Save::LoadResult::Ok);
        CHECK_EQ(save.activeGeneralPartition(), newest);
        CHECK_EQ(save.activeStoragePartition(), newest);
        uint8_t stored[pk4::kStoredSize];
        CHECK(save.readBoxSlot(2, 5, stored) == Gen4Save::SlotState::Valid);
        CHECK_EQ(util::readU16(stored + pk4::offset::kHeldItem), 100 + newest);
    }
}

TEST(gen4_falls_back_when_newest_block_is_corrupt) {
    std::vector<uint8_t> data = buildSave(kPt, 1);
    data[Gen4Save::kPartitionSize + kPt.storageOffset + 100] ^= 0xFF;  // corrupt newest storage
    Gen4Save save;
    CHECK(save.load(data) == Gen4Save::LoadResult::Ok);
    CHECK_EQ(save.activeGeneralPartition(), 1);
    CHECK_EQ(save.activeStoragePartition(), 0);
}

TEST(gen4_reports_when_both_copies_are_corrupt) {
    std::vector<uint8_t> data = buildSave(kPt, 1);
    data[100] ^= 0xFF;
    data[Gen4Save::kPartitionSize + 100] ^= 0xFF;
    Gen4Save save;
    CHECK(save.load(data) == Gen4Save::LoadResult::BadChecksum);
}

TEST(gen4_first_save_with_blank_second_partition) {
    std::vector<uint8_t> data = buildSave(kPt, 0);
    std::memset(&data[Gen4Save::kPartitionSize], 0xFF, Gen4Save::kPartitionSize);
    Gen4Save save;
    CHECK(save.load(data) == Gen4Save::LoadResult::Ok);
    CHECK_EQ(save.activeGeneralPartition(), 0);
}

TEST(gen4_write_box_slot_round_trip) {
    for (const TestLayout* l : {&kDp, &kPt, &kHgss}) {
        Gen4Save save;
        CHECK(save.load(buildSave(*l)) == Gen4Save::LoadResult::Ok);
        uint8_t stored[pk4::kStoredSize];
        CHECK(save.readBoxSlot(2, 5, stored) == Gen4Save::SlotState::Valid);

        util::writeU16(stored + pk4::offset::kHeldItem, 234);
        CHECK(save.writeBoxSlot(17, 29, stored));
        CHECK(save.clearBoxSlot(2, 5));
        CHECK(!save.checksumsValid());
        save.refreshChecksums();
        CHECK(save.checksumsValid());

        Gen4Save reloaded;
        CHECK(reloaded.load(save.data()) == Gen4Save::LoadResult::Ok);
        uint8_t check[pk4::kStoredSize];
        CHECK(reloaded.readBoxSlot(17, 29, check) == Gen4Save::SlotState::Valid);
        CHECK_EQ(util::readU16(check + pk4::offset::kHeldItem), 234);
        CHECK(reloaded.readBoxSlot(2, 5, check) == Gen4Save::SlotState::Empty);
        // Untouched partition is left as it was.
        CHECK(std::memcmp(reloaded.data().data(), buildSave(*l).data(), Gen4Save::kPartitionSize) == 0);
    }
}

TEST(gen4_hgss_marks_changed_boxes) {
    Gen4Save save;
    CHECK(save.load(buildSave(kHgss)) == Gen4Save::LoadResult::Ok);
    uint8_t stored[pk4::kStoredSize];
    CHECK(save.readBoxSlot(2, 5, stored) == Gen4Save::SlotState::Valid);
    CHECK(save.writeBoxSlot(3, 0, stored));
    const uint8_t* storage = save.data().data() + Gen4Save::kPartitionSize + kHgss.storageOffset;
    CHECK_EQ(util::readU32(storage + 18 * 0x1000 + 4), 1u << 3);
}

TEST(gen4_bad_slot_checksum_is_reported) {
    std::vector<uint8_t> data = buildSave(kPt, 1);
    // Flip a byte inside the encrypted Pokémon, then fix the block CRC so only
    // the entity itself is inconsistent.
    uint8_t* storage = &data[Gen4Save::kPartitionSize + kPt.storageOffset];
    storage[kPt.boxSlot(2, 5) + 40] ^= 0x01;
    writeFooter(storage, kPt.storageSize, kPt.footerSize, 10);
    Gen4Save save;
    CHECK(save.load(data) == Gen4Save::LoadResult::Ok);
    uint8_t stored[pk4::kStoredSize];
    CHECK(save.readBoxSlot(2, 5, stored) == Gen4Save::SlotState::BadChecksum);
}

namespace {

std::string makeTempDir() {
    char pattern[] = "/tmp/pktool-commit-XXXXXX";
    const char* dir = mkdtemp(pattern);
    return dir ? dir : "";
}

}  // namespace

TEST(gen4_commit_edits_backs_up_and_writes) {
    std::string dir = makeTempDir();
    std::string path = io::joinPath(dir, "Pokemon Platinum.sav");
    std::string backups = io::joinPath(dir, "PKTOOL/backups");
    const std::vector<uint8_t> original = buildSave(kPt);
    CHECK(io::safeWriteFile(path, original) == io::WriteResult::Ok);

    Gen4Save save;
    CHECK(save.load(original) == Gen4Save::LoadResult::Ok);
    uint8_t party[pk4::kPartySize];
    CHECK(save.readPartySlot(0, party) == Gen4Save::SlotState::Valid);
    pokemon::Pokemon p = pk4::toPokemon(party, sizeof(party));
    pokemon::edit4::setLevel(p, 60);
    CHECK(pokemon::edit4::setNature(p, 13) == pokemon::edit4::Result::Ok);
    pk4::applyPokemon(p, party);
    pk4::refreshPartyStats(party);
    CHECK(save.writePartySlot(0, party));

    saves::CommitResult result = saves::commitGen4Save(save, path, backups);
    CHECK(result.status == saves::CommitResult::Status::Ok);
    CHECK(!result.backupPath.empty());

    std::vector<uint8_t> written;
    CHECK(io::readFile(path, written));
    Gen4Save reloaded;
    CHECK(reloaded.load(written) == Gen4Save::LoadResult::Ok);
    CHECK(reloaded.checksumsValid());
    CHECK(reloaded.readPartySlot(0, party) == Gen4Save::SlotState::Valid);
    pokemon::Pokemon q = pk4::toPokemon(party, sizeof(party));
    CHECK_EQ(q.level, 60);
    CHECK_EQ(q.nature, 13);

    std::vector<uint8_t> backup;
    CHECK(io::readFile(result.backupPath, backup));
    CHECK(backup == original);
}

TEST(gen4_commit_refuses_invalid_save) {
    std::string dir = makeTempDir();
    std::string path = io::joinPath(dir, "game.sav");
    const std::vector<uint8_t> original = buildSave(kPt);
    CHECK(io::safeWriteFile(path, original) == io::WriteResult::Ok);

    // A save whose block CRCs are fine but that contains a corrupt Pokémon.
    std::vector<uint8_t> data = original;
    uint8_t* storage = &data[Gen4Save::kPartitionSize + kPt.storageOffset];
    storage[kPt.boxSlot(2, 5) + 40] ^= 0x01;
    writeFooter(storage, kPt.storageSize, kPt.footerSize, 10);
    Gen4Save save;
    CHECK(save.load(data) == Gen4Save::LoadResult::Ok);
    CHECK(!saves::validateGen4Save(save));

    CHECK(saves::commitGen4Save(save, path, io::joinPath(dir, "b")).status ==
          saves::CommitResult::Status::ValidationFailed);
    std::vector<uint8_t> onDisk;
    CHECK(io::readFile(path, onDisk));
    CHECK(onDisk == original);
    CHECK(!io::fileExists(path + ".tmp"));
}

namespace {

void pressKey(ui::App& app, uint32_t keys) {
    ui::Input in;
    in.pressed = keys;
    in.repeated = keys;
    app.update(in);
    app.update(ui::Input());  // lets pending actions run
}

}  // namespace

TEST(ui_adds_event_and_new_pokemon_to_empty_slots) {
    std::string dir = makeTempDir();
    std::string path = io::joinPath(dir, "Pokemon Platinum.sav");
    CHECK(io::safeWriteFile(path, buildSave(kPt)) == io::WriteResult::Ok);
    ui::Config config;
    config.scanRoot = dir;
    config.dataDir = io::joinPath(dir, "PKTOOL");
    ui::App app(config);
    CHECK(app.openSave(path));
    pressKey(app, ui::kKeyA);  // save menu: Boxes

    pressKey(app, ui::kKeyR);     // box 1, slot 1 (empty)
    pressKey(app, ui::kKeyA);     // Add menu
    pressKey(app, ui::kKeyDown);
    pressKey(app, ui::kKeyDown);  // Event
    pressKey(app, ui::kKeyA);     // card list
    pressKey(app, ui::kKeyA);     // receive the first card
    pressKey(app, ui::kKeyA);     // close the result dialog
    pressKey(app, ui::kKeyStart); // put it in the slot

    pressKey(app, ui::kKeyRight); // slot 2
    pressKey(app, ui::kKeyA);     // Add menu (remembers Event)
    pressKey(app, ui::kKeyUp);
    pressKey(app, ui::kKeyUp);    // New Pokémon
    pressKey(app, ui::kKeyA);     // species list
    for (int i = 0; i < data::sortedIndex(data::NameKind::Species, 4, 4); ++i) pressKey(app, ui::kKeyDown);  // #004 Charmander (alphabetical list)
    pressKey(app, ui::kKeyA);
    pressKey(app, ui::kKeyA);
    pressKey(app, ui::kKeyStart);

    pressKey(app, ui::kKeySelect);  // write
    pressKey(app, ui::kKeyA);       // confirm

    std::vector<uint8_t> written;
    CHECK(io::readFile(path, written));
    Gen4Save reloaded;
    CHECK(reloaded.load(written) == Gen4Save::LoadResult::Ok);
    uint8_t stored[pk4::kStoredSize];
    CHECK(reloaded.readBoxSlot(0, 0, stored) == Gen4Save::SlotState::Valid);
    pokemon::Pokemon event = pk4::toPokemon(stored, sizeof(stored));
    // The card list is alphabetical by species: the first row is the first species by name.
    uint16_t firstCard = data::event4(0).species;
    for (int i = 1; i < data::events4Count(); ++i) {
        if (data::foldName(data::speciesName(data::event4(i).species)) < data::foldName(data::speciesName(firstCard))) {
            firstCard = data::event4(i).species;
        }
    }
    CHECK_EQ(event.species, firstCard);
    CHECK(event.met.fatefulEncounter);
    CHECK(legality::checkGen4(event).legal());

    CHECK(reloaded.readBoxSlot(0, 1, stored) == Gen4Save::SlotState::Valid);
    pokemon::Pokemon created = pk4::toPokemon(stored, sizeof(stored));
    CHECK_EQ(created.species, 4);
    CHECK(created.trainer.name == "ASH");
    CHECK_EQ(created.trainer.tid, 12345);
    CHECK(legality::checkGen4(created).legal());
}

TEST(ui_bank_deposit_and_withdraw) {
    std::string dir = makeTempDir();
    std::string path = io::joinPath(dir, "Pokemon Platinum.sav");
    CHECK(io::safeWriteFile(path, buildSave(kPt)) == io::WriteResult::Ok);
    ui::Config config;
    config.scanRoot = dir;
    config.dataDir = io::joinPath(dir, "PKTOOL");
    std::string bankFile = io::joinPath(config.dataDir, "bank/bank.pkb");
    {
        ui::App app(config);
        CHECK(app.openSave(path));
        pressKey(app, ui::kKeyA);  // save menu: Boxes
        pressKey(app, ui::kKeyR);
        pressKey(app, ui::kKeyR);
        pressKey(app, ui::kKeyR);  // box 3: the Roserade is in slot 6
        for (int i = 0; i < 5; ++i) pressKey(app, ui::kKeyRight);
        pressKey(app, ui::kKeyY);  // Bank
        pressKey(app, ui::kKeyA);  // deposit into Bank box 1 slot 1
        pressKey(app, ui::kKeyA);  // close the message
        pressKey(app, ui::kKeySelect);
        pressKey(app, ui::kKeyA);  // write save + bank
    }
    std::vector<uint8_t> written;
    CHECK(io::readFile(path, written));
    Gen4Save save;
    CHECK(save.load(written) == Gen4Save::LoadResult::Ok);
    uint8_t stored[pk4::kStoredSize];
    CHECK(save.readBoxSlot(2, 5, stored) == Gen4Save::SlotState::Empty);
    bank::Bank bank;
    CHECK(bank.load(bankFile) == bank::Bank::LoadResult::Ok);
    CHECK_EQ(bank.format(0, 0), 4);
    CHECK_EQ(util::readU16(bank.entity(0, 0) + pk4::offset::kSpecies), 407);

    // Withdraw it again into box 1 slot 1, write, and the Bank is empty.
    {
        ui::App app(config);
        CHECK(app.openSave(path));
        pressKey(app, ui::kKeyA);  // save menu: Boxes
        pressKey(app, ui::kKeyR);  // box 1
        pressKey(app, ui::kKeyY);
        pressKey(app, ui::kKeyA);  // withdraw
        pressKey(app, ui::kKeyA);
        pressKey(app, ui::kKeySelect);
        pressKey(app, ui::kKeyA);
    }
    CHECK(io::readFile(path, written));
    CHECK(save.load(written) == Gen4Save::LoadResult::Ok);
    CHECK(save.readBoxSlot(0, 0, stored) == Gen4Save::SlotState::Valid);
    CHECK_EQ(util::readU16(stored + pk4::offset::kSpecies), 407);
    CHECK(bank.load(bankFile) == bank::Bank::LoadResult::Ok);
    CHECK_EQ(bank.count(), 0);
}

TEST(ui_bank_discarded_changes_keep_everything) {
    std::string dir = makeTempDir();
    std::string path = io::joinPath(dir, "Pokemon Platinum.sav");
    const std::vector<uint8_t> original = buildSave(kPt);
    CHECK(io::safeWriteFile(path, original) == io::WriteResult::Ok);
    ui::Config config;
    config.scanRoot = dir;
    config.dataDir = io::joinPath(dir, "PKTOOL");
    ui::App app(config);
    CHECK(app.openSave(path));
    pressKey(app, ui::kKeyA);  // save menu: Boxes
    for (int i = 0; i < 3; ++i) pressKey(app, ui::kKeyR);
    for (int i = 0; i < 5; ++i) pressKey(app, ui::kKeyRight);
    pressKey(app, ui::kKeyY);
    pressKey(app, ui::kKeyA);  // deposit (in memory)
    pressKey(app, ui::kKeyA);
    pressKey(app, ui::kKeyB);  // close the save
    pressKey(app, ui::kKeyA);  // discard
    std::vector<uint8_t> after;
    CHECK(io::readFile(path, after));
    CHECK(after == original);
    CHECK(!io::fileExists(io::joinPath(config.dataDir, "bank/bank.pkb")));
}

TEST(browser_lists_only_pokemon_saves) {
    std::string dir = makeTempDir();
    CHECK(io::makeDirectories(io::joinPath(dir, "other")));
    CHECK(io::safeWriteFile(io::joinPath(dir, "Pokemon Platinum.sav"), buildSave(kPt)) == io::WriteResult::Ok);
    CHECK(io::safeWriteFile(io::joinPath(dir, "Pokemon HeartGold.sav"), buildSave(kHgss)) == io::WriteResult::Ok);
    // Another game's 512 KiB save and a small one: both ignored.
    std::vector<uint8_t> noise(Gen4Save::kFileSize);
    for (size_t i = 0; i < noise.size(); ++i) noise[i] = static_cast<uint8_t>(i * 7 + 3);
    CHECK(io::safeWriteFile(io::joinPath(dir, "other/Mario.sav"), noise) == io::WriteResult::Ok);
    CHECK(io::safeWriteFile(io::joinPath(dir, "Zelda.sav"), std::vector<uint8_t>(8192, 1)) == io::WriteResult::Ok);

    std::vector<saves::SaveSummary> found = saves::findPokemonSaves(dir, 2);
    CHECK_EQ(found.size(), 2u);
    for (const saves::SaveSummary& s : found) {
        CHECK(s.trainer == "ASH");
        CHECK_EQ(s.party.size(), 1u);
        CHECK_EQ(s.party[0], 407);
        CHECK(s.version == 12 || s.version == 7);
        CHECK(saves::coverSpecies(s.version) != 0);
    }
}

TEST(ui_keyboard_nickname_and_box_name) {
    std::string dir = makeTempDir();
    std::string path = io::joinPath(dir, "Pokemon Platinum.sav");
    CHECK(io::safeWriteFile(path, buildSave(kPt)) == io::WriteResult::Ok);
    ui::Config config;
    config.scanRoot = dir;
    config.dataDir = io::joinPath(dir, "PKTOOL");
    {
        ui::App app(config);
        CHECK(app.openSave(path));
        pressKey(app, ui::kKeyA);  // save menu: Boxes
        pressKey(app, ui::kKeyA);       // menu
        pressKey(app, ui::kKeyA);       // Edit the party Roserade (field 1: Nickname)
        pressKey(app, ui::kKeyA);       // keyboard
        for (int i = 0; i < 10; ++i) pressKey(app, ui::kKeyB);  // clear
        pressKey(app, ui::kKeyA);       // "A"
        pressKey(app, ui::kKeyRight);
        pressKey(app, ui::kKeySelect);  // lower case
        pressKey(app, ui::kKeyA);       // "b"
        pressKey(app, ui::kKeyStart);   // done
        pressKey(app, ui::kKeyStart);   // apply

        pressKey(app, ui::kKeyR);       // box 1
        std::vector<uint16_t> topPixels(gfx::kScreenWidth * gfx::kScreenHeight);
        std::vector<uint16_t> bottomPixels(gfx::kScreenWidth * gfx::kScreenHeight);
        gfx::Canvas top(topPixels.data());
        gfx::Canvas bottom(bottomPixels.data());
        app.draw(top, bottom);          // touch zones come from the last draw
        ui::Input tap;                  // tap the box name to rename it
        tap.touched = true;
        tap.touchX = 128;
        tap.touchY = 10;
        app.update(tap);
        app.update(ui::Input());
        for (int i = 0; i < 12; ++i) pressKey(app, ui::kKeyB);
        pressKey(app, ui::kKeyDown);
        pressKey(app, ui::kKeyDown);
        pressKey(app, ui::kKeyDown);    // digits row
        pressKey(app, ui::kKeyA);       // "0"
        pressKey(app, ui::kKeyRight);
        pressKey(app, ui::kKeyA);       // "1"
        pressKey(app, ui::kKeyStart);
        pressKey(app, ui::kKeySelect);  // write
        pressKey(app, ui::kKeyA);
    }
    std::vector<uint8_t> written;
    CHECK(io::readFile(path, written));
    Gen4Save save;
    CHECK(save.load(written) == Gen4Save::LoadResult::Ok);
    uint8_t party[pk4::kPartySize];
    CHECK(save.readPartySlot(0, party) == Gen4Save::SlotState::Valid);
    pokemon::Pokemon p = pk4::toPokemon(party, sizeof party);
    CHECK(p.nickname == "Ab");
    CHECK(p.isNicknamed);
    CHECK(save.boxName(0) == "01");
}

TEST(ui_bank_with_party) {
    std::string dir = makeTempDir();
    std::string path = io::joinPath(dir, "Pokemon Platinum.sav");
    CHECK(io::safeWriteFile(path, buildSave(kPt)) == io::WriteResult::Ok);
    ui::Config config;
    config.scanRoot = dir;
    config.dataDir = io::joinPath(dir, "PKTOOL");
    std::string bankFile = io::joinPath(config.dataDir, "bank/bank.pkb");
    {
        bank::Bank bank;
        bank.load(bankFile);
        uint8_t stored[pk4::kStoredSize];
        std::memcpy(stored, test::kRoseradePk4, sizeof stored);
        CHECK(bank.set(0, 0, 4, stored));
        CHECK(bank.save() == io::WriteResult::Ok);
    }
    {
        ui::App app(config);
        CHECK(app.openSave(path));
        pressKey(app, ui::kKeyA);  // save menu: Boxes
        pressKey(app, ui::kKeyRight);   // party slot 2 (empty)
        pressKey(app, ui::kKeyY);
        pressKey(app, ui::kKeyA);       // withdraw into the party
        pressKey(app, ui::kKeyA);
        // The only original member cannot leave, the new one can go back.
        pressKey(app, ui::kKeyY);       // cursor is on the new member (slot 2)
        pressKey(app, ui::kKeyRight);   // Bank slot 2
        pressKey(app, ui::kKeyA);       // deposit
        pressKey(app, ui::kKeyA);
        pressKey(app, ui::kKeyY);       // cursor on slot 1, the last member
        pressKey(app, ui::kKeyRight);
        pressKey(app, ui::kKeyRight);   // Bank slot 3
        pressKey(app, ui::kKeyA);       // refused: the party needs one Pokémon
        pressKey(app, ui::kKeyA);
        pressKey(app, ui::kKeyB);
        pressKey(app, ui::kKeySelect);
        pressKey(app, ui::kKeyA);
    }
    std::vector<uint8_t> written;
    CHECK(io::readFile(path, written));
    Gen4Save save;
    CHECK(save.load(written) == Gen4Save::LoadResult::Ok);
    CHECK_EQ(save.partyCount(), 1);
    bank::Bank bank;
    CHECK(bank.load(bankFile) == bank::Bank::LoadResult::Ok);
    CHECK_EQ(bank.count(), 1);
    CHECK_EQ(bank.format(0, 1), 4);  // the withdrawn one went back to slot 2
}

TEST(save_party_append_and_remove) {
    Gen4Save save;
    CHECK(save.load(buildSave(kPt)) == Gen4Save::LoadResult::Ok);
    uint8_t party[pk4::kPartySize];
    CHECK(save.readPartySlot(0, party) == Gen4Save::SlotState::Valid);
    CHECK(save.appendParty(party));
    util::writeU16(party + pk4::offset::kHeldItem, 55);
    pk4::refreshChecksum(party);
    CHECK(save.appendParty(party));
    CHECK_EQ(save.partyCount(), 3);
    CHECK(save.removePartySlot(0));
    CHECK_EQ(save.partyCount(), 2);
    CHECK(save.readPartySlot(1, party) == Gen4Save::SlotState::Valid);
    CHECK_EQ(util::readU16(party + pk4::offset::kHeldItem), 55);
    CHECK(save.readPartySlot(2, party) == Gen4Save::SlotState::Empty);
    save.refreshChecksums();
    CHECK(saves::validateSave(save));
    CHECK(save.removePartySlot(0));
    CHECK(!save.removePartySlot(0));  // the last one stays
}

// ----- Slot menu (A on a Pokemon) ---------------------------------------------

namespace {

constexpr int kMarkedItem = 77;

struct MenuFixture {
    std::string dir;
    std::string path;
    ui::Config config;
};

// buildSave() plus a second Roserade in box 3 slot 5 (index 2/4) holding item 77,
// so swaps can be told apart.
MenuFixture makeMenuFixture() {
    MenuFixture f;
    f.dir = makeTempDir();
    f.path = io::joinPath(f.dir, "Pokemon Platinum.sav");
    f.config.scanRoot = f.dir;
    f.config.dataDir = io::joinPath(f.dir, "PKTOOL");
    Gen4Save save;
    CHECK(save.load(buildSave(kPt)) == Gen4Save::LoadResult::Ok);
    uint8_t stored[pk4::kStoredSize];
    CHECK(save.readBoxSlot(2, 5, stored) == Gen4Save::SlotState::Valid);
    util::writeU16(stored + pk4::offset::kHeldItem, kMarkedItem);
    pk4::refreshChecksum(stored);
    CHECK(save.writeBoxSlot(2, 4, stored));
    save.refreshChecksums();
    CHECK(io::safeWriteFile(f.path, save.data()) == io::WriteResult::Ok);
    return f;
}

int heldItemAt(const Gen4Save& save, int box, int slot) {
    if (box < 0) {
        uint8_t party[pk4::kPartySize];
        if (save.readPartySlot(slot, party) != Gen4Save::SlotState::Valid) return -1;
        return util::readU16(party + pk4::offset::kHeldItem);
    }
    uint8_t stored[pk4::kStoredSize];
    if (save.readBoxSlot(box, slot, stored) != Gen4Save::SlotState::Valid) return -1;
    return util::readU16(stored + pk4::offset::kHeldItem);
}

bool loadFrom(const std::string& path, Gen4Save& save) {
    std::vector<uint8_t> bytes;
    return io::readFile(path, bytes) && save.load(bytes) == Gen4Save::LoadResult::Ok;
}

// SELECT + confirm, then the file as it is on disk.
void writeSave(ui::App& app) {
    pressKey(app, ui::kKeySelect);
    pressKey(app, ui::kKeyA);
}

// Box 3 (index 2) with the cursor on slot `slot` (0-based).
void gotoBox3(ui::App& app, int slot) {
    pressKey(app, ui::kKeyR);
    pressKey(app, ui::kKeyR);
    pressKey(app, ui::kKeyR);
    for (int i = 0; i < slot; ++i) pressKey(app, ui::kKeyRight);
}

}  // namespace

TEST(ui_menu_opens_on_a_and_edit_opens_the_editor) {
    MenuFixture f = makeMenuFixture();
    ui::App app(f.config);
    CHECK(app.openSave(f.path));
    pressKey(app, ui::kKeyA);  // save menu: Boxes
    gotoBox3(app, 5);
    pressKey(app, ui::kKeyA);      // menu
    pressKey(app, ui::kKeyB);      // closed again
    pressKey(app, ui::kKeyA);      // menu
    pressKey(app, ui::kKeyA);      // Edit -> editor
    pressKey(app, ui::kKeyDown);   // species
    pressKey(app, ui::kKeyRight);  // +1
    pressKey(app, ui::kKeyStart);  // apply
    writeSave(app);
    Gen4Save after;
    CHECK(loadFrom(f.path, after));
    uint8_t stored[pk4::kStoredSize];
    CHECK(after.readBoxSlot(2, 5, stored) == Gen4Save::SlotState::Valid);
    CHECK(util::readU16(stored + pk4::offset::kSpecies) != 407);
}

TEST(ui_menu_move_swaps_and_moves_between_box_slots) {
    MenuFixture f = makeMenuFixture();
    Gen4Save before;
    CHECK(loadFrom(f.path, before));
    const int item = heldItemAt(before, 2, 5);
    ui::App app(f.config);
    CHECK(app.openSave(f.path));
    pressKey(app, ui::kKeyA);  // save menu: Boxes
    gotoBox3(app, 5);
    pressKey(app, ui::kKeyA);
    pressKey(app, ui::kKeyDown);  // Move
    pressKey(app, ui::kKeyA);
    pressKey(app, ui::kKeyLeft);  // onto the marked Pokemon: swap
    pressKey(app, ui::kKeyA);
    // The original is now in slot 5 (index 4): move it to the empty slot index 3.
    pressKey(app, ui::kKeyA);
    pressKey(app, ui::kKeyDown);
    pressKey(app, ui::kKeyA);
    pressKey(app, ui::kKeyLeft);
    pressKey(app, ui::kKeyA);
    writeSave(app);
    Gen4Save after;
    CHECK(loadFrom(f.path, after));
    CHECK_EQ(heldItemAt(after, 2, 3), item);         // moved to the empty slot
    CHECK_EQ(heldItemAt(after, 2, 4), -1);           // left empty
    CHECK_EQ(heldItemAt(after, 2, 5), kMarkedItem);  // swapped into the old slot
}

TEST(ui_menu_move_refuses_to_empty_the_party_and_swaps_with_a_box_slot) {
    MenuFixture f = makeMenuFixture();
    Gen4Save before;
    CHECK(loadFrom(f.path, before));
    const int partyItem = heldItemAt(before, -1, 0);
    ui::App app(f.config);
    CHECK(app.openSave(f.path));
    pressKey(app, ui::kKeyA);  // save menu: Boxes

    // The only party member cannot leave for an empty box slot.
    pressKey(app, ui::kKeyA);
    pressKey(app, ui::kKeyDown);  // Move
    pressKey(app, ui::kKeyA);
    pressKey(app, ui::kKeyR);     // box 1, slot 1 (empty)
    pressKey(app, ui::kKeyA);     // refused
    pressKey(app, ui::kKeyA);     // dismiss the message

    // Party <-> box swap with the marked Pokemon in box 3.
    pressKey(app, ui::kKeyL);     // party
    pressKey(app, ui::kKeyA);
    pressKey(app, ui::kKeyDown);
    pressKey(app, ui::kKeyA);     // Move
    gotoBox3(app, 4);             // slot index 4 holds the marked one
    pressKey(app, ui::kKeyA);     // drop
    writeSave(app);
    Gen4Save after;
    CHECK(loadFrom(f.path, after));
    CHECK_EQ(after.partyCount(), 1);
    CHECK_EQ(heldItemAt(after, 0, 0), -1);  // the refused move left box 1 empty
    CHECK_EQ(heldItemAt(after, -1, 0), kMarkedItem);
    CHECK_EQ(heldItemAt(after, 2, 4), partyItem);
}

TEST(ui_menu_move_box_to_party_and_back) {
    MenuFixture f = makeMenuFixture();
    ui::App app(f.config);
    CHECK(app.openSave(f.path));
    pressKey(app, ui::kKeyA);  // save menu: Boxes
    gotoBox3(app, 4);              // the marked one
    pressKey(app, ui::kKeyA);
    pressKey(app, ui::kKeyDown);
    pressKey(app, ui::kKeyA);      // Move
    pressKey(app, ui::kKeyL);
    pressKey(app, ui::kKeyL);
    pressKey(app, ui::kKeyL);      // party
    pressKey(app, ui::kKeyUp);
    pressKey(app, ui::kKeyUp);
    pressKey(app, ui::kKeyRight);  // empty party slot 2
    pressKey(app, ui::kKeyA);      // drop: joins the party
    writeSave(app);
    Gen4Save joined;
    CHECK(loadFrom(f.path, joined));
    CHECK_EQ(joined.partyCount(), 2);
    CHECK_EQ(heldItemAt(joined, -1, 1), kMarkedItem);
    CHECK_EQ(heldItemAt(joined, 2, 4), -1);

    // Back to the box: party slot 2 -> box 1 slot 2 (the cursor index carries over).
    pressKey(app, ui::kKeyA);  // dismiss "Saved"
    pressKey(app, ui::kKeyA);
    pressKey(app, ui::kKeyDown);
    pressKey(app, ui::kKeyA);
    pressKey(app, ui::kKeyR);
    pressKey(app, ui::kKeyA);
    writeSave(app);
    Gen4Save back;
    CHECK(loadFrom(f.path, back));
    CHECK_EQ(back.partyCount(), 1);
    CHECK_EQ(heldItemAt(back, 0, 1), kMarkedItem);
}

TEST(ui_menu_move_can_be_cancelled_with_b) {
    MenuFixture f = makeMenuFixture();
    ui::App app(f.config);
    CHECK(app.openSave(f.path));
    pressKey(app, ui::kKeyA);  // save menu: Boxes
    gotoBox3(app, 5);
    pressKey(app, ui::kKeyA);
    pressKey(app, ui::kKeyDown);
    pressKey(app, ui::kKeyA);      // Move
    pressKey(app, ui::kKeyLeft);
    pressKey(app, ui::kKeyLeft);   // empty slot
    pressKey(app, ui::kKeyB);      // cancel: nothing moved
    pressKey(app, ui::kKeySelect); // nothing unsaved
    pressKey(app, ui::kKeyA);      // dismiss "Nothing to save"
    Gen4Save same;
    CHECK(loadFrom(f.path, same));
    CHECK_EQ(heldItemAt(same, 2, 3), -1);
    CHECK(heldItemAt(same, 2, 5) >= 100);
}

TEST(ui_menu_release_asks_for_confirmation) {
    MenuFixture f = makeMenuFixture();
    ui::App app(f.config);
    CHECK(app.openSave(f.path));
    pressKey(app, ui::kKeyA);  // save menu: Boxes
    gotoBox3(app, 4);
    pressKey(app, ui::kKeyA);
    pressKey(app, ui::kKeyUp);    // Cancel
    pressKey(app, ui::kKeyUp);    // Release
    pressKey(app, ui::kKeyA);     // confirmation dialog
    pressKey(app, ui::kKeyB);     // No
    pressKey(app, ui::kKeySelect);
    pressKey(app, ui::kKeyA);     // "Nothing to save": the answer was No
    pressKey(app, ui::kKeyA);
    pressKey(app, ui::kKeyUp);
    pressKey(app, ui::kKeyUp);
    pressKey(app, ui::kKeyA);
    pressKey(app, ui::kKeyA);     // Yes
    writeSave(app);
    Gen4Save after;
    CHECK(loadFrom(f.path, after));
    CHECK_EQ(heldItemAt(after, 2, 4), -1);
    CHECK(heldItemAt(after, 2, 5) >= 100);
}

TEST(ui_menu_clone_copies_to_first_empty_slot) {
    MenuFixture f = makeMenuFixture();
    ui::App app(f.config);
    CHECK(app.openSave(f.path));
    pressKey(app, ui::kKeyA);  // save menu: Boxes
    gotoBox3(app, 4);
    pressKey(app, ui::kKeyA);
    for (int i = 0; i < 5; ++i) pressKey(app, ui::kKeyDown);  // Clone
    pressKey(app, ui::kKeyA);
    pressKey(app, ui::kKeyA);  // dismiss the message
    writeSave(app);
    Gen4Save after;
    CHECK(loadFrom(f.path, after));
    CHECK_EQ(heldItemAt(after, 2, 0), kMarkedItem);
    CHECK_EQ(heldItemAt(after, 2, 4), kMarkedItem);
}
