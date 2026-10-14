// Backing up a GBA cartridge's save (beta, read-only), against a simulated
// cartridge whose flash chip answers the bank-switch command like the real ones.
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/io/file_io.h"
#include "core/saves/gba_cart.h"
#include "core/saves/save_file.h"
#include "core/saves/save_probe.h"
#include "ds/app/app.h"
#include "ds/gfx/canvas.h"
#include "tests/fake_saves.h"
#include "tests/test.h"

using namespace pktool;
namespace gba = saves::gba;

namespace {

class FakeCart final : public gba::Bus {
public:
    std::vector<uint8_t> rom = std::vector<uint8_t>(0x200, 0);
    std::vector<uint8_t> flash = std::vector<uint8_t>(0x20000, 0xFF);
    bool empty = false;       // nothing in the slot: open bus
    int flakyReads = 0;       // the first N save reads return a wrong byte
    bool dataWritten = false;  // a write that is not part of a command

    FakeCart(const char* code, const char* title) {
        for (int i = 0; title[i] != 0 && i < 12; ++i) rom[0xA0 + i] = static_cast<uint8_t>(title[i]);
        for (int i = 0; i < 4; ++i) rom[0xAC + i] = static_cast<uint8_t>(code[i]);
        rom[0xB2] = 0x96;
        uint8_t sum = 0;
        for (int i = 0xA0; i < 0xBD; ++i) sum = static_cast<uint8_t>(sum + rom[i]);
        rom[0xBD] = static_cast<uint8_t>(-(sum + 0x19));
    }
    uint8_t readRom(uint32_t offset) override {
        if (empty) return static_cast<uint8_t>(offset >> 1);  // open bus: the address
        return offset < rom.size() ? rom[offset] : 0;
    }
    uint8_t readSave(uint16_t offset) override {
        if (empty) return 0xFF;
        uint8_t value = flash[bank_ * 0x10000 + offset];
        if (flakyReads > 0) {
            --flakyReads;
            value ^= 0x01;
        }
        return value;
    }
    void writeSave(uint16_t offset, uint8_t value) override {
        // AA@5555, 55@2AAA, then a command@5555; after B0 the bank number @0000.
        if (step_ == 0 && offset == 0x5555 && value == 0xAA) {
            step_ = 1;
        } else if (step_ == 1 && offset == 0x2AAA && value == 0x55) {
            step_ = 2;
        } else if (step_ == 2 && offset == 0x5555 && value == 0xB0) {
            step_ = 3;
        } else if (step_ == 3 && offset == 0x0000 && value <= 1) {
            bank_ = value;
            step_ = 0;
        } else {
            dataWritten = true;  // anything else could erase or program the chip
            step_ = 0;
        }
    }
    int bank() const { return bank_; }

private:
    int step_ = 0;
    int bank_ = 0;
};

FakeCart emeraldWithSave() {
    FakeCart cart("BPEE", "POKEMON EMER");
    cart.flash = fake::blankGen3(saves::Game::Emerald);
    return cart;
}

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

}  // namespace

TEST(gba_cart_identifies_the_pokemon_games) {
    FakeCart emerald("BPEE", "POKEMON EMER");
    gba::CartInfo info = gba::identify(emerald);
    CHECK(info.present);
    CHECK(info.gameCode == "BPEE");
    CHECK(info.title == "POKEMON EMER");
    CHECK(info.gameName != nullptr && std::string(info.gameName) == "Pok\xC3\xA9mon Emerald");
    FakeCart fireRedSpanish("BPRS", "POKEMON FIRE");
    CHECK(gba::identify(fireRedSpanish).gameName != nullptr);
    FakeCart other("AMKE", "MARIOKART");
    CHECK(gba::identify(other).present);
    CHECK(gba::identify(other).gameName == nullptr);
    FakeCart broken("BPEE", "POKEMON EMER");
    broken.rom[0xBD] ^= 0xFF;  // bad header checksum
    CHECK(!gba::identify(broken).present);
    FakeCart none("BPEE", "POKEMON EMER");
    none.empty = true;
    CHECK(!gba::identify(none).present);
}

TEST(gba_cart_reads_both_banks_without_writing_data) {
    FakeCart cart = emeraldWithSave();
    cart.flash[0x0123] = 0x5A;   // bank 0
    cart.flash[0x10123] = 0xA5;  // bank 1
    std::vector<uint8_t> expected = cart.flash;
    gba::CartInfo info;
    std::vector<uint8_t> out;
    CHECK(gba::readSave(cart, info, out) == gba::ReadResult::Ok);
    CHECK(out == expected);
    CHECK(!cart.dataWritten);  // only bank-switch commands reached the chip
    CHECK(cart.flash == expected);
    CHECK_EQ(cart.bank(), 0);  // left on bank 0, as the game expects
    CHECK(gba::backupFileName(info, "20261014-153000") == "BPEE_20261014-153000.sav");
}

TEST(gba_cart_reports_what_went_wrong) {
    gba::CartInfo info;
    std::vector<uint8_t> out;
    FakeCart none("BPEE", "POKEMON EMER");
    none.empty = true;
    CHECK(gba::readSave(none, info, out) == gba::ReadResult::NoCartridge);
    FakeCart other("AMKE", "MARIOKART");
    CHECK(gba::readSave(other, info, out) == gba::ReadResult::NotPokemon);
    FakeCart flaky = emeraldWithSave();
    flaky.flakyReads = 10;  // a bad contact during the first read
    CHECK(gba::readSave(flaky, info, out) == gba::ReadResult::ReadMismatch);
    CHECK(out.empty());
}

TEST(gba_cart_ui_backs_up_and_opens_the_save) {
    char pattern[] = "/tmp/pktool-gba-XXXXXX";
    const std::string dir = mkdtemp(pattern);
    FakeCart cart = emeraldWithSave();
    ui::Config config;
    config.scanRoot = dir;
    config.dataDir = io::joinPath(dir, "PKTOOL");
    config.gbaSlot = &cart;
    ui::App app(config);
    press(app, ui::kKeyA);  // Open save: browser (no saves on the card)
    CHECK(std::string(app.screenName()) == "Browser");
    press(app, ui::kKeySelect);  // back up the cartridge
    CHECK(std::string(app.screenName()) == "SaveMenu");
    const std::string copy = io::joinPath(config.dataDir, "gba");
    std::vector<saves::SaveSummary> files = saves::findPokemonSaves(copy, 0);
    CHECK_EQ(files.size(), 1u);
    std::vector<uint8_t> bytes;
    if (!files.empty()) CHECK(io::readFile(files[0].path, bytes) && bytes == cart.flash);
    CHECK(!cart.dataWritten);

    // A cartridge with no save (new game): nothing is written.
    char pattern2[] = "/tmp/pktool-gba-XXXXXX";
    const std::string dir2 = mkdtemp(pattern2);
    FakeCart blank("BPEE", "POKEMON EMER");
    config.scanRoot = dir2;
    config.dataDir = io::joinPath(dir2, "PKTOOL");
    config.gbaSlot = &blank;
    ui::App app2(config);
    press(app2, ui::kKeyA);
    press(app2, ui::kKeySelect);
    CHECK(std::string(app2.screenName()) == "Browser");
    CHECK(saves::findPokemonSaves(io::joinPath(config.dataDir, "gba"), 0).empty());
}

TEST(gba_cart_no_slot_means_no_option) {
    char pattern[] = "/tmp/pktool-gba-XXXXXX";
    const std::string dir = mkdtemp(pattern);
    ui::Config config;
    config.scanRoot = dir;
    config.dataDir = io::joinPath(dir, "PKTOOL");
    ui::App app(config);  // gbaSlot stays nullptr (DSi, 3DS, PC)
    press(app, ui::kKeyA);
    press(app, ui::kKeySelect);
    CHECK(std::string(app.screenName()) == "Browser");
    CHECK(saves::findPokemonSaves(io::joinPath(config.dataDir, "gba"), 0).empty());
}
