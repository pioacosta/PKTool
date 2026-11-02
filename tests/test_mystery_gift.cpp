// Mystery Gift albums of Gen IV saves (PKHeX MysteryBlock4) and Gen V saves
// (MysteryBlock5): adding, reading and removing Wonder Cards, the whole-card
// packs of the compiled tables, and the save menu's Mystery Gift screen.

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/data/events4.h"
#include "core/data/events5.h"
#include "core/io/file_io.h"
#include "core/pokemon/pk4.h"
#include "core/saves/gen4_save.h"
#include "core/saves/gen5_save.h"
#include "core/util/bytes.h"
#include "core/util/crc16.h"
#include "core/util/lcrng.h"
#include "ds/app/app.h"
#include "ds/gfx/canvas.h"
#include "tests/fake_saves.h"
#include "tests/test.h"

using namespace pktool;
using saves::Gen5Save;

namespace {

constexpr size_t kAlbum = 0x1C800;
constexpr size_t kAlbumSize = 0xA90;
const std::vector<std::string> kAssets = {PKTOOL_SOURCE_DIR "/assets/nitrofs"};

std::vector<uint16_t> g_top(gfx::kScreenWidth * gfx::kScreenHeight);
std::vector<uint16_t> g_bottom(gfx::kScreenWidth * gfx::kScreenHeight);

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

std::string tempDir() {
    char pattern[] = "/tmp/pktool-gifts-XXXXXX";
    return mkdtemp(pattern);
}

// Written here independently of Gen5Save: each u16 XOR the high half of the next LCRNG state.
void crypt(uint8_t* data, size_t size, uint32_t seed) {
    for (size_t i = 0; i + 1 < size; i += 2) {
        seed = util::lcrngNext(seed);
        util::writeU16(data + i, util::readU16(data + i) ^ static_cast<uint16_t>(seed >> 16));
    }
}

// A blank B2W2 save whose album was written by the game: empty, encrypted with `seed`.
Gen5Save albumSave(uint32_t seed) {
    std::vector<uint8_t> data = fake::blankGen5(true);
    crypt(&data[kAlbum], kAlbumSize, seed);
    util::writeU32(&data[kAlbum + kAlbumSize], seed);
    Gen5Save save;
    CHECK(save.load(data) == saves::LoadResult::Ok);
    save.refreshChecksums();
    return save;
}

std::vector<uint8_t> wholeCard(int index) {
    std::vector<uint8_t> card(data::kWholePgfSize);
    CHECK(data::findWholeCard5(data::event5(index), kAssets, card.data()));
    return card;
}

}  // namespace

TEST(gift5_every_table_card_is_in_the_whole_card_pack) {
    for (int i = 0; i < data::events5Count(); ++i) {
        const data::Event5& e = data::event5(i);
        uint8_t card[data::kWholePgfSize];
        CHECK(data::findWholeCard5(e, kAssets, card));
        CHECK(std::memcmp(card, e.pgf, sizeof e.pgf) == 0);
        CHECK_EQ(util::readU16(card + 0xB0), e.cardId);
        CHECK_EQ(card[0xB3], 1);
    }
    uint8_t card[data::kWholePgfSize];
    CHECK(!data::findWholeCard5(data::event5(0), {"/nonexistent"}, card));
}

TEST(gift5_album_add_read_and_encryption_match_pkhex_layout) {
    Gen5Save save = albumSave(0xCAFEBABE);
    CHECK_EQ(save.giftSlotCount(), 12);
    CHECK_EQ(save.giftCardSize(), size_t{0xCC});
    uint8_t out[0xCC];
    for (int i = 0; i < 12; ++i) CHECK(!save.readGift(i, out));

    std::vector<uint8_t> card = wholeCard(10);
    card[0xB4] |= 2;  // a card already picked up comes back waiting for the delivery man
    const uint16_t cardId = util::readU16(&card[0xB0]);
    CHECK(!save.giftReceived(cardId));
    std::string error;
    CHECK(save.addGift(card.data(), &error));
    CHECK(save.readGift(0, out));
    CHECK_EQ(out[0xB4] & 2, 0);
    card[0xB4] &= static_cast<uint8_t>(~2);
    CHECK(std::memcmp(out, card.data(), 0xCC) == 0);
    CHECK(!save.readGift(1, out));
    CHECK(save.giftReceived(cardId));

    // On disk: flags at 0, card 0 at 0x100, all encrypted with the stored seed.
    save.refreshChecksums();
    CHECK(save.checksumsValid());
    std::vector<uint8_t> album(save.data().begin() + kAlbum, save.data().begin() + kAlbum + kAlbumSize);
    CHECK(std::memcmp(album.data() + 0x100, card.data(), 0xCC) != 0);
    CHECK_EQ(util::readU32(save.data().data() + kAlbum + kAlbumSize), 0xCAFEBABEu);
    crypt(album.data(), album.size(), 0xCAFEBABE);
    CHECK(std::memcmp(album.data() + 0x100, card.data(), 0xCC) == 0);
    CHECK((album[cardId >> 3] >> (cardId & 7)) & 1);

    // The backup copy follows the main save, and a reload reads the same card.
    Gen5Save again;
    CHECK(again.load(save.data()) == saves::LoadResult::Ok);
    CHECK(again.readGift(0, out));
    CHECK(std::memcmp(out, card.data(), 0xCC) == 0);
}

TEST(gift5_album_fills_up_rejects_non_cards_and_closes_gaps) {
    Gen5Save save = albumSave(1);
    std::string error;
    std::vector<uint8_t> notCard(0xCC, 0);
    CHECK(!save.addGift(notCard.data(), &error));
    CHECK(error == "Not a Wonder Card");
    for (int i = 0; i < 12; ++i) CHECK(save.addGift(wholeCard(i).data()));
    CHECK(!save.addGift(wholeCard(12).data(), &error));
    CHECK(error == "The Mystery Gift album is full");

    CHECK(save.removeGift(0));
    uint8_t out[0xCC];
    CHECK(save.readGift(0, out));
    CHECK(std::memcmp(out, data::event5(1).pgf, 0x60) == 0);  // the next card moved up
    CHECK(save.readGift(10, out));
    CHECK(!save.readGift(11, out));
    CHECK(!save.removeGift(11));
    CHECK(save.addGift(wholeCard(12).data()));
    CHECK(save.readGift(11, out));
    CHECK(std::memcmp(out, data::event5(12).pgf, 0x60) == 0);
}

TEST(gift5_never_written_album_reads_empty) {
    // Synthetic saves leave the block zero (seed too): no noise cards.
    Gen5Save save;
    CHECK(save.load(fake::blankGen5(false)) == saves::LoadResult::Ok);
    uint8_t out[0xCC];
    for (int i = 0; i < 12; ++i) CHECK(!save.readGift(i, out));
    CHECK(save.addGift(wholeCard(3).data()));
    CHECK(save.readGift(0, out));
    CHECK(!save.readGift(1, out));
}

TEST(gift5_save_menu_adds_a_table_card_and_writes_it) {
    std::string dir = tempDir();
    ui::Config config;
    config.scanRoot = dir;
    config.dataDir = io::joinPath(dir, "PKTOOL");
    config.assetDirs = kAssets;
    std::string path = io::joinPath(dir, "test.sav");
    CHECK(io::safeWriteFile(path, albumSave(77).data()) == io::WriteResult::Ok);
    ui::App app(config);
    CHECK(app.openSave(path));
    for (int i = 0; i < 3; ++i) press(app, ui::kKeyDown);  // Boxes, Trainer, Bag, Mystery Gift
    press(app, ui::kKeyA);
    CHECK(std::strcmp(app.screenName(), "Gifts") == 0);
    press(app, ui::kKeyA);
    CHECK(std::strcmp(app.screenName(), "Picker") == 0);
    CHECK(!app.pickerRows().empty());
    press(app, ui::kKeyB);  // back without adding
    CHECK(std::strcmp(app.screenName(), "Gifts") == 0);
    press(app, ui::kKeyA);
    press(app, ui::kKeyA);  // add the first card of the list
    CHECK(std::strcmp(app.screenName(), "Gifts") == 0);
    press(app, ui::kKeyA);  // "Card added"
    press(app, ui::kKeySelect);
    press(app, ui::kKeyA);  // confirm the write
    press(app, ui::kKeyA);  // "Saved"

    std::vector<uint8_t> written;
    CHECK(io::readFile(path, written));
    Gen5Save save;
    CHECK(save.load(written) == saves::LoadResult::Ok);
    CHECK(save.checksumsValid());
    uint8_t out[0xCC];
    CHECK(save.readGift(0, out));
    CHECK_EQ(out[0xB3], 1);
    CHECK(!save.readGift(1, out));
}

TEST(gift_save_menu_hides_mystery_gift_without_an_album) {
    std::string dir = tempDir();
    ui::Config config;
    config.scanRoot = dir;
    config.dataDir = io::joinPath(dir, "PKTOOL");
    std::string path = io::joinPath(dir, "test.sav");
    CHECK(io::safeWriteFile(path, fake::blankGen3(saves::Game::Emerald)) == io::WriteResult::Ok);
    ui::App app(config);
    CHECK(app.openSave(path));
    for (int i = 0; i < 3; ++i) press(app, ui::kKeyDown);  // Boxes, Trainer, Bag, Bank
    press(app, ui::kKeyA);
    CHECK(std::strcmp(app.screenName(), "Bank") == 0);
}

// ----- Gen IV -------------------------------------------------------------------

namespace {

struct Album4 {
    const char* name;
    const fake::Gen4Layout* layout;
    size_t offset;     // Mystery Gift block in the general block (PKHeX OffsetMystery)
    size_t pgtStart;   // first gift, relative to the block
    bool hgss;
};

const Album4 kAlbums4[] = {
    {"DP", &fake::kDp, 0xA6D0, 0x100 + 11 * 4, false},
    {"Pt", &fake::kPt, 0xB4C0, 0x100, false},
    {"HGSS", &fake::kHgss, 0x9D3C, 0x100, true},
};

std::vector<uint8_t> wholeCard4(int index) {
    std::vector<uint8_t> card(data::kWholePcdSize);
    CHECK(data::findWholeCard4(data::event4(index), kAssets, card.data()));
    return card;
}

// The card's Pokémon decrypted (the save keeps it encrypted).
std::vector<uint8_t> templateOf(const uint8_t* pcd) {
    std::vector<uint8_t> pk(pcd + 8, pcd + 8 + pokemon::pk4::kStoredSize);
    pokemon::pk4::decrypt(pk.data(), pk.size());
    return pk;
}

}  // namespace

TEST(gift4_every_table_card_is_in_the_whole_card_pack) {
    for (int i = 0; i < data::events4Count(); ++i) {
        const data::Event4& e = data::event4(i);
        uint8_t card[data::kWholePcdSize];
        CHECK(data::findWholeCard4(e, kAssets, card));
        CHECK_EQ(util::readU16(card), 1);  // Pokémon gift
        CHECK_EQ(util::readU16(card + 0x150), e.cardId);
        CHECK(std::memcmp(card + 8, e.templatePk4, sizeof e.templatePk4) == 0);
    }
}

TEST(gift4_album_puts_the_card_and_its_gift_where_pkhex_has_them) {
    for (const Album4& a : kAlbums4) {
        saves::Gen4Save save;
        CHECK(save.load(fake::blankGen4(*a.layout)) == saves::LoadResult::Ok);
        CHECK_EQ(save.giftSlotCount(), 3);
        uint8_t out[0x358];
        CHECK(!save.readGift(0, out));
        CHECK(!save.deliveryManActive());
        CHECK(!save.mysteryGiftUnlocked());

        const std::vector<uint8_t> card = wholeCard4(5);
        const uint16_t cardId = util::readU16(&card[0x150]);
        std::string error;
        CHECK(save.addGift(card.data(), &error));
        CHECK(!save.readGift(1, out));
        CHECK(save.readGift(0, out));
        CHECK(templateOf(out) == std::vector<uint8_t>(card.begin() + 8, card.begin() + 8 + 136));
        CHECK(std::memcmp(out + 0x104, &card[0x104], 0x358 - 0x104) == 0);  // card text untouched
        CHECK(save.giftWaiting(0));
        CHECK(save.giftReceived(cardId));
        CHECK(save.deliveryManActive());
        CHECK(save.mysteryGiftUnlocked());

        // On disk, from PKHeX's offsets: gift 0, then the album's card 0 after the 8 gifts.
        save.refreshChecksums();
        CHECK(save.checksumsValid());
        const uint8_t* general = save.data().data() + save.activeGeneralPartition() * saves::Gen4Save::kPartitionSize;
        const uint8_t* block = general + a.offset;
        const uint8_t* pgt = block + a.pgtStart;
        const uint8_t* pcd = pgt + 8 * 0x104;
        CHECK_EQ(util::readU16(pgt), 1);
        CHECK_EQ(pgt[2], a.hgss ? 0 : 1);  // the card it came from: HG/SS count from 0
        CHECK(std::memcmp(pgt + 3, pcd + 3, 0x104 - 3) == 0);
        CHECK(std::memcmp(pcd, out, 0x358) == 0);
        CHECK_EQ((block[2047 >> 3] >> (2047 & 7)) & 1, 1);
        CHECK_EQ(general[72] & 1, 1);
        if (a.layout == &fake::kDp) {
            CHECK_EQ(util::readU32(block + 0x100), 0xEDB88320u);           // gift 0 in use
            CHECK_EQ(util::readU32(block + 0x100 + 4), 0u);                // gift 1 free
            CHECK_EQ(util::readU32(block + 0x100 + 8 * 4), 0xEDB88320u);   // card 0 in use
        }

        saves::Gen4Save again;
        CHECK(again.load(save.data()) == saves::LoadResult::Ok);
        CHECK(again.readGift(0, out));
        CHECK(again.giftWaiting(0));
    }
}

TEST(gift4_album_fills_up_and_removing_a_card_takes_its_gift) {
    for (const Album4& a : kAlbums4) {
        saves::Gen4Save save;
        CHECK(save.load(fake::blankGen4(*a.layout)) == saves::LoadResult::Ok);
        std::string error;
        std::vector<uint8_t> notCard(0x358, 0);
        CHECK(!save.addGift(notCard.data(), &error));
        CHECK(error == "Not a Wonder Card");
        for (int i = 0; i < 3; ++i) CHECK(save.addGift(wholeCard4(i * 10).data()));
        CHECK(!save.addGift(wholeCard4(40).data(), &error));
        CHECK(error == "The Mystery Gift album is full");

        CHECK(save.removeGift(0));
        uint8_t out[0x358];
        CHECK(save.readGift(0, out));
        CHECK(std::memcmp(templateOf(out).data(), data::event4(10).templatePk4, 136) == 0);
        CHECK(!save.readGift(2, out));
        CHECK(save.giftWaiting(0));
        CHECK(save.giftWaiting(1));
        // The remaining gifts point at the cards' new places.
        const uint8_t* block =
            save.data().data() + save.activeGeneralPartition() * saves::Gen4Save::kPartitionSize + a.offset;
        int linked = 0;
        for (int i = 0; i < 8; ++i) {
            const uint8_t* pgt = block + a.pgtStart + i * 0x104;
            if (util::readU16(pgt) == 0) continue;
            CHECK(pgt[2] == (a.hgss ? 0 : 1) || pgt[2] == (a.hgss ? 1 : 2));
            ++linked;
        }
        CHECK_EQ(linked, 2);

        // The game clears a gift when the delivery man hands it out: the card stays.
        saves::Gen4Save copy;
        std::vector<uint8_t> bytes = save.data();
        uint8_t* general = &bytes[save.activeGeneralPartition() * saves::Gen4Save::kPartitionSize];
        for (int i = 0; i < 8; ++i) std::memset(general + a.offset + a.pgtStart + i * 0x104, 0, 0x104);
        fake::writeFooter(general, a.layout->generalSize, a.layout->footerSize);
        CHECK(copy.load(bytes) == saves::LoadResult::Ok);
        CHECK(copy.readGift(0, out));
        CHECK(!copy.giftWaiting(0));
    }
}

TEST(gift4_save_menu_adds_a_table_card_and_writes_it) {
    for (const Album4& a : kAlbums4) {
        std::string dir = tempDir();
        ui::Config config;
        config.scanRoot = dir;
        config.dataDir = io::joinPath(dir, "PKTOOL");
        config.assetDirs = kAssets;
        std::string path = io::joinPath(dir, "test.sav");
        CHECK(io::safeWriteFile(path, fake::blankGen4(*a.layout)) == io::WriteResult::Ok);
        ui::App app(config);
        CHECK(app.openSave(path));
        for (int i = 0; i < 3; ++i) press(app, ui::kKeyDown);  // Boxes, Trainer, Bag, Mystery Gift
        press(app, ui::kKeyA);
        CHECK(std::strcmp(app.screenName(), "Gifts") == 0);
        press(app, ui::kKeyA);
        CHECK(std::strcmp(app.screenName(), "Picker") == 0);
        bool manaphyEgg = false;
        for (const std::string& row : app.pickerRows()) manaphyEgg = manaphyEgg || row.find("Ranger Manaphy egg") != std::string::npos;
        CHECK(!manaphyEgg);  // made by the game, not a card
        press(app, ui::kKeyA);  // add the first card of the list
        CHECK(std::strcmp(app.screenName(), "Gifts") == 0);
        press(app, ui::kKeyA);  // "Card added"
        press(app, ui::kKeySelect);
        press(app, ui::kKeyA);  // confirm the write
        press(app, ui::kKeyA);  // "Saved"

        std::vector<uint8_t> written;
        CHECK(io::readFile(path, written));
        saves::Gen4Save save;
        CHECK(save.load(written) == saves::LoadResult::Ok);
        CHECK(save.checksumsValid());
        uint8_t out[0x358];
        CHECK(save.readGift(0, out));
        CHECK(save.giftWaiting(0));
        CHECK(!save.readGift(1, out));
    }
}
