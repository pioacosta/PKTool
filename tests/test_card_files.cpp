// Wonder Card files (.pgt, .pcd, .pgf): parsing, receiving and the Add > Event
// list of /PKTOOL/events/. Test cards are built from the compiled tables' raw
// data in the real file layouts (PKHeX PGT / PCD / PGF).

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/data/events4.h"
#include "core/data/events5.h"
#include "core/data/names.h"
#include "core/io/file_io.h"
#include "core/legality/legality.h"
#include "core/pokemon/card_file.h"
#include "core/pokemon/gift4.h"
#include "core/pokemon/gift5.h"
#include "core/pokemon/pk4.h"
#include "core/util/bytes.h"
#include "ds/app/app.h"
#include "ds/gfx/canvas.h"
#include "tests/fake_saves.h"
#include "tests/test.h"

using namespace pktool;
namespace cf = pokemon::cardfile;
namespace pk4 = pokemon::pk4;

namespace {

std::vector<uint16_t> g_top(gfx::kScreenWidth * gfx::kScreenHeight);
std::vector<uint16_t> g_bottom(gfx::kScreenWidth * gfx::kScreenHeight);

std::string tempDir() {
    char pattern[] = "/tmp/pktool-cards-XXXXXX";
    return mkdtemp(pattern);
}

void press(ui::App& app, uint32_t keys) {
    ui::Input in;
    in.pressed = keys;
    in.repeated = keys;
    app.update(in);
    app.update(ui::Input());
    gfx::Canvas top(g_top.data());
    gfx::Canvas bottom(g_bottom.data());
    app.draw(top, bottom);
}

// A PGT: type, slot, detail, then the PK4 (encrypted like the game's files when asked).
std::vector<uint8_t> makePgt(const uint8_t* decryptedTemplate, uint16_t type, bool encrypted) {
    std::vector<uint8_t> pgt(0x104, 0);
    util::writeU16(pgt.data(), type);
    uint8_t stored[pk4::kStoredSize];
    std::memcpy(stored, decryptedTemplate, sizeof stored);
    if (encrypted) pk4::encrypt(stored, sizeof stored);
    std::memcpy(pgt.data() + 8, stored, sizeof stored);
    return pgt;
}

std::vector<uint8_t> makePcd(const uint8_t* decryptedTemplate, uint16_t cardId, bool encrypted) {
    std::vector<uint8_t> pcd(0x358, 0);
    std::vector<uint8_t> pgt = makePgt(decryptedTemplate, 1, encrypted);
    std::memcpy(pcd.data(), pgt.data(), pgt.size());
    util::writeU16(pcd.data() + 0x150, cardId);
    for (int i = 0; i < 0x48; i += 2) util::writeU16(pcd.data() + 0x104 + i, 0xFFFF);  // empty title
    return pcd;
}

std::vector<uint8_t> makePgf(const data::Event5& card, uint8_t type) {
    std::vector<uint8_t> pgf(0xCC, 0);
    std::memcpy(pgf.data(), card.pgf, sizeof card.pgf);
    util::writeU16(pgf.data() + 0xB0, card.cardId);
    pgf[0xB3] = type;
    return pgf;
}

}  // namespace

TEST(card_files_every_gen4_table_card_round_trips_as_pcd_and_pgt) {
    int tableMatches = 0;
    for (int i = 0; i < data::events4Count(); ++i) {
        const data::Event4& table = data::event4(i);
        for (int encrypted = 0; encrypted < 2; ++encrypted) {
            std::vector<uint8_t> pcd = makePcd(table.templatePk4, table.cardId, encrypted != 0);
            std::unique_ptr<cf::Card> card = cf::parse(pcd.data(), pcd.size(), ".PCD", "card.pcd");
            CHECK(card->status == cf::Status::Ok);
            CHECK_EQ(card->generation, 4);
            CHECK_EQ(card->species, table.species);
            CHECK(std::memcmp(card->event4.templatePk4, table.templatePk4, pk4::kStoredSize) == 0);
            CHECK_EQ(card->event4.cardId, table.cardId);
            CHECK(card->title == "card");  // the test PCD has no title: the file name is used
            CHECK(card->tableIndex >= 0);
            if (card->tableIndex >= 0) {
                // An identical table card lends its distribution periods.
                CHECK(std::memcmp(data::event4(card->tableIndex).templatePk4, table.templatePk4, pk4::kStoredSize) == 0);
                ++tableMatches;
            }
        }
        std::vector<uint8_t> pgt = makePgt(table.templatePk4, 1, true);
        std::unique_ptr<cf::Card> card = cf::parse(pgt.data(), pgt.size(), ".pgt", "x.pgt");
        CHECK(card->status == cf::Status::Ok);
        CHECK_EQ(card->species, table.species);
    }
    CHECK_EQ(tableMatches, 2 * data::events4Count());
}

TEST(card_files_every_gen5_table_card_round_trips_as_pgf) {
    for (int i = 0; i < data::events5Count(); ++i) {
        const data::Event5& table = data::event5(i);
        std::vector<uint8_t> pgf = makePgf(table, 1);
        std::unique_ptr<cf::Card> card = cf::parse(pgf.data(), pgf.size(), ".pgf", "w.pgf");
        CHECK(card->status == cf::Status::Ok);
        CHECK_EQ(card->generation, 5);
        CHECK_EQ(card->species, table.species);
        CHECK(std::memcmp(card->event5.pgf, table.pgf, sizeof table.pgf) == 0);
        CHECK(card->tableIndex >= 0);
        if (card->tableIndex >= 0) {
            const data::Event5& found = data::event5(card->tableIndex);
            CHECK_EQ(card->event5.periodCount, found.periodCount);
            CHECK_EQ(card->event5.restrictVersion, found.restrictVersion);
        }
    }
}

TEST(card_files_non_pokemon_and_broken_files_are_classified) {
    const data::Event4& t4 = data::event4(0);
    // Item (3), rule (4), Manaphy egg (7), egg (2) and movie (13) PGTs: valid but not receivable.
    for (uint16_t type : {2, 3, 4, 7, 13}) {
        std::vector<uint8_t> pgt = makePgt(t4.templatePk4, type, true);
        std::unique_ptr<cf::Card> card = cf::parse(pgt.data(), pgt.size(), ".pgt", "a.pgt");
        CHECK(card->status == cf::Status::NotPokemon);
        CHECK(!card->reason.empty());
    }
    {
        std::vector<uint8_t> pgt = makePgt(t4.templatePk4, 3, true);
        CHECK(cf::parse(pgt.data(), pgt.size(), ".pgt", "a.pgt")->reason == "Not a Pok\xC3\xA9mon card");
    }
    // Gen V item and Pass Power cards.
    for (uint8_t type : {2, 3}) {
        std::vector<uint8_t> pgf = makePgf(data::event5(0), type);
        CHECK(cf::parse(pgf.data(), pgf.size(), ".pgf", "i.pgf")->status == cf::Status::NotPokemon);
    }
    // Broken files.
    std::vector<uint8_t> pgt = makePgt(t4.templatePk4, 1, true);
    std::vector<uint8_t> shortPgt(pgt.begin(), pgt.end() - 1);
    CHECK(cf::parse(shortPgt.data(), shortPgt.size(), ".pgt", "s.pgt")->status == cf::Status::Invalid);
    CHECK(cf::parse(pgt.data(), pgt.size(), ".pcd", "s.pcd")->status == cf::Status::Invalid);  // wrong size for a PCD
    CHECK(cf::parse(pgt.data(), pgt.size(), ".xyz", "s.xyz")->status == cf::Status::Invalid);
    std::vector<uint8_t> bad = pgt;
    bad[8 + 20] ^= 0x55;  // damages the encrypted data: the checksum no longer matches
    CHECK(cf::parse(bad.data(), bad.size(), ".pgt", "b.pgt")->status == cf::Status::Invalid);
    std::vector<uint8_t> zero(0x104, 0);
    CHECK(cf::parse(zero.data(), zero.size(), ".pgt", "z.pgt")->status == cf::Status::Invalid);
    std::vector<uint8_t> pgf = makePgf(data::event5(0), 1);
    util::writeU16(pgf.data() + 0x1A, 900);  // no such species
    CHECK(cf::parse(pgf.data(), pgf.size(), ".pgf", "b.pgf")->status == cf::Status::Invalid);
    pgf = makePgf(data::event5(0), 1);
    pgf[0x5B] = 200;  // level 200
    CHECK(cf::parse(pgf.data(), pgf.size(), ".pgf", "b.pgf")->status == cf::Status::Invalid);
    CHECK(cf::parse(pgf.data(), 10, ".pgf", "b.pgf")->status == cf::Status::Invalid);
}

TEST(card_files_receive_like_the_table_card_and_unknown_cards_warn) {
    const data::Event4& t4 = data::event4(0);
    std::vector<uint8_t> pcd = makePcd(t4.templatePk4, t4.cardId, true);
    std::unique_ptr<cf::Card> file = cf::parse(pcd.data(), pcd.size(), ".pcd", "t.pcd");
    CHECK(file->status == cf::Status::Ok);
    const pokemon::Date today{12, 6, 1};
    uint8_t rawFile[pk4::kStoredSize];
    uint8_t rawTable[pk4::kStoredSize];
    pokemon::Pokemon a = pokemon::gift4::receive(file->event4, today, 77, rawFile);
    pokemon::Pokemon b = pokemon::gift4::receive(t4, today, 77, rawTable);
    CHECK(std::memcmp(rawFile, rawTable, sizeof rawFile) == 0);  // same Pokémon, same met date
    CHECK(a.species == t4.species);
    CHECK(pokemon::gift4::matchingEvent(a) != nullptr);
    CHECK(legality::check(a).overall() != legality::Severity::Invalid);

    // A card that is not in the tables (other trainer IDs) is received but has no database entry.
    uint8_t custom[pk4::kStoredSize];
    std::memcpy(custom, t4.templatePk4, sizeof custom);
    util::writeU16(custom + pk4::offset::kTid, 4242);
    util::writeU16(custom + pk4::offset::kSid, 2424);
    pk4::refreshChecksum(custom);
    std::vector<uint8_t> pgt = makePgt(custom, 1, true);
    std::unique_ptr<cf::Card> unknown = cf::parse(pgt.data(), pgt.size(), ".pgt", "mine.pgt");
    CHECK(unknown->status == cf::Status::Ok);
    CHECK_EQ(unknown->tableIndex, -1);
    CHECK_EQ(unknown->event4.periodCount, 0);
    uint8_t rawUnknown[pk4::kStoredSize];
    pokemon::Pokemon c = pokemon::gift4::receive(unknown->event4, today, 5, rawUnknown);
    CHECK_EQ(c.species, t4.species);
    CHECK_EQ(c.trainer.tid, 4242);
    CHECK(c.met.metDate.year == 12 && c.met.metDate.month == 6 && c.met.metDate.day == 1);  // no periods: today
    CHECK(pokemon::gift4::matchingEvent(c) == nullptr);
    bool warned = false;
    for (const auto& finding : legality::check(c).findings) {
        if (finding.severity == legality::Severity::Warning) warned = true;
    }
    CHECK(warned);  // "event not in database"

    // Gen V: received from the file like from the table.
    const data::Event5& t5 = data::event5(10);
    std::vector<uint8_t> pgf = makePgf(t5, 1);
    std::unique_ptr<cf::Card> file5 = cf::parse(pgf.data(), pgf.size(), ".pgf", "w.pgf");
    pokemon::create4::Trainer trainer;
    trainer.name = "ASH";
    trainer.tid = 1;
    trainer.sid = 2;
    trainer.version = 21;
    trainer.language = 2;
    trainer.today = today;
    uint8_t r5[pokemon::pk5::kStoredSize];
    uint8_t r5t[pokemon::pk5::kStoredSize];
    pokemon::gift5::receive(file5->event5, trainer, 9, r5);
    pokemon::gift5::receive(t5, trainer, 9, r5t);
    CHECK(std::memcmp(r5, r5t, sizeof r5) == 0);
}

TEST(card_files_event_list_shows_files_and_greys_what_cannot_be_received) {
    std::string dir = tempDir();
    std::string events = io::joinPath(dir, "PKTOOL/events");
    CHECK(io::makeDirectories(events));
    const data::Event4& t4 = data::event4(0);
    uint8_t custom[pk4::kStoredSize];
    std::memcpy(custom, t4.templatePk4, sizeof custom);
    util::writeU16(custom + pk4::offset::kTid, 4242);
    pk4::refreshChecksum(custom);
    auto write = [&events](const char* name, const std::vector<uint8_t>& bytes) {
        CHECK(io::safeWriteFile(io::joinPath(events, name), bytes) == io::WriteResult::Ok);
    };
    write("mine.pgt", makePgt(custom, 1, true));
    write("item.pgt", makePgt(custom, 3, true));
    std::vector<uint8_t> broken = makePgt(custom, 1, true);
    broken.resize(100);
    write("broken.pcd", broken);
    write("genv.pgf", makePgf(data::event5(10), 1));
    write("notes.txt", std::vector<uint8_t>(10, 'x'));  // ignored

    ui::Config config;
    config.scanRoot = dir;
    config.dataDir = io::joinPath(dir, "PKTOOL");
    std::string save = io::joinPath(dir, "test.sav");
    CHECK(io::safeWriteFile(save, fake::blankGen4(fake::kPt)) == io::WriteResult::Ok);
    ui::App app(config);
    CHECK(app.openSave(save));
    press(app, ui::kKeyA);  // Boxes
    press(app, ui::kKeyR);
    press(app, ui::kKeyA);  // empty slot: Add menu
    press(app, ui::kKeyDown);
    press(app, ui::kKeyDown);
    press(app, ui::kKeyA);  // Event list
    CHECK(std::strcmp(app.screenName(), "Picker") == 0);

    const std::vector<std::string>& rows = app.pickerRows();
    int mine = -1, item = -1, brokenRow = -1, genv = -1, files = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        const std::string& r = rows[i];
        if (r.find("mine") != std::string::npos && r.find("(file)") != std::string::npos) mine = static_cast<int>(i);
        if (r.find("item.pgt") != std::string::npos) item = static_cast<int>(i);
        if (r.find("broken.pcd") != std::string::npos) brokenRow = static_cast<int>(i);
        if (r.find("genv.pgf") != std::string::npos) genv = static_cast<int>(i);
        if (r.find("notes.txt") != std::string::npos) CHECK(false);
        if (r.find("(file)") != std::string::npos || r.compare(0, 3, "---") == 0) ++files;
    }
    CHECK(mine >= 0 && item >= 0 && brokenRow >= 0 && genv >= 0);
    CHECK_EQ(files, 4);
    CHECK(rows[static_cast<size_t>(item)].find("Not a Pok\xC3\xA9mon card") != std::string::npos);
    CHECK(rows[static_cast<size_t>(brokenRow)].find("size") != std::string::npos);
    CHECK(rows[static_cast<size_t>(genv)].find("Gen V card") != std::string::npos);

    // A on a greyed row only explains why; the list stays.
    auto goTo = [&](int row) {
        for (int guard = 0; guard < 1000 && app.pickerCursor() != row; ++guard) {
            press(app, app.pickerCursor() < row ? ui::kKeyDown : ui::kKeyUp);
        }
        CHECK_EQ(app.pickerCursor(), row);
    };
    goTo(item);
    press(app, ui::kKeyA);
    CHECK(std::strcmp(app.screenName(), "Picker") == 0);
    press(app, ui::kKeyA);  // dismiss the message
    CHECK(std::strcmp(app.screenName(), "Picker") == 0);

    // A on a good card receives it: the editor opens with the card's Pokémon and trainer.
    goTo(mine);
    press(app, ui::kKeyA);
    CHECK(std::strcmp(app.screenName(), "Edit") == 0);
    CHECK_EQ(app.editedPokemon().species, t4.species);
    CHECK_EQ(app.editedPokemon().trainer.tid, 4242);
}

TEST(card_files_gen5_save_lists_pgf_files_and_skips_gen4_cards) {
    std::string dir = tempDir();
    std::string events = io::joinPath(dir, "PKTOOL/events");
    CHECK(io::makeDirectories(events));
    const data::Event5& t5 = data::event5(10);
    CHECK(io::safeWriteFile(io::joinPath(events, "five.pgf"), makePgf(t5, 1)) == io::WriteResult::Ok);
    CHECK(io::safeWriteFile(io::joinPath(events, "four.pgt"), makePgt(data::event4(0).templatePk4, 1, true)) ==
          io::WriteResult::Ok);
    ui::Config config;
    config.scanRoot = dir;
    config.dataDir = io::joinPath(dir, "PKTOOL");
    std::string save = io::joinPath(dir, "test.sav");
    CHECK(io::safeWriteFile(save, fake::blankGen5(true)) == io::WriteResult::Ok);
    ui::App app(config);
    CHECK(app.openSave(save));
    press(app, ui::kKeyA);
    press(app, ui::kKeyR);
    press(app, ui::kKeyA);
    press(app, ui::kKeyDown);
    press(app, ui::kKeyDown);
    press(app, ui::kKeyA);
    int five = -1;
    bool four = false;
    const std::vector<std::string>& rows = app.pickerRows();
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].find("(file)") != std::string::npos) five = static_cast<int>(i);
        if (rows[i].find("four.pgt") != std::string::npos && rows[i].find("Gen IV card") != std::string::npos) four = true;
    }
    CHECK(five >= 0);
    CHECK(four);
    for (int guard = 0; guard < 2000 && app.pickerCursor() != five; ++guard) press(app, ui::kKeyDown);
    press(app, ui::kKeyA);
    CHECK(std::strcmp(app.screenName(), "Edit") == 0);
    CHECK_EQ(app.editedPokemon().species, t5.species);
}
