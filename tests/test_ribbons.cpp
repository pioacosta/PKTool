// Tests of ribbon viewing and editing: the core get/set on raw PK3/PK4/PK5
// bytes, a round trip through saves, real saves (PKTOOL_FIXTURES) and the UI
// flow driven like the DS.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "core/io/file_io.h"
#include "core/pokemon/edit4.h"
#include "core/pokemon/entity.h"
#include "core/pokemon/pk3.h"
#include "core/pokemon/pk4.h"
#include "core/pokemon/pk5.h"
#include "core/pokemon/ribbons.h"
#include "core/saves/save_commit.h"
#include "core/saves/save_file.h"
#include "core/util/bytes.h"
#include "ds/app/app.h"
#include "ds/gfx/canvas.h"
#include "tests/fake_saves.h"
#include "tests/test.h"

using namespace pktool;
namespace rb = pokemon::ribbons;
namespace pk3 = pokemon::pk3;
namespace pk4 = pokemon::pk4;

namespace {

// A decrypted, stored-size entity of the format (PK3: 80 bytes, otherwise 136).
std::vector<uint8_t> makeEntity(uint8_t format, bool egg = false) {
    std::vector<uint8_t> raw(pokemon::entity::kMaxPartySize, 0);
    pokemon::Pokemon p;
    p.format = format;
    p.species = 25;
    p.pid = 0x12345678;
    p.nickname = "SPARKY";
    p.trainer.name = "ASH";
    p.trainer.tid = 12345;
    p.trainer.sid = 54321;
    p.language = 2;
    p.originGame = format == 3 ? 4 : format == 4 ? 12 : 23;
    p.met.ball = 4;
    p.met.metLevel = 5;
    p.met.metLocation = 16;
    p.gender = pokemon::edit4::genderFromPid(25, p.pid);
    p.ability = pokemon::edit4::abilityForSlot(25, 0);
    p.isEgg = egg;
    pokemon::edit4::setLevel(p, 5);
    pokemon::edit4::setMove(p, 0, 84);
    if (format == 3) {
        std::memset(raw.data() + pk3::offset::kNickname, 0xFF, pk3::kNicknameChars);
        std::memset(raw.data() + pk3::offset::kOtName, 0xFF, pk3::kOtNameChars);
    }
    pokemon::entity::apply(p, raw.data());
    return raw;
}

bool checksumOk(uint8_t format, const uint8_t* d) {
    return format == 3 ? pk3::calcChecksum(d) == pk3::storedChecksum(d) : pk4::calcChecksum(d) == pk4::storedChecksum(d);
}

// Bytes a ribbon change may touch: the ribbon words and the checksum.
bool isRibbonOrChecksumByte(uint8_t format, size_t i) {
    if (format == 3) return (i >= 0x4C && i < 0x50) || (i >= 0x1C && i < 0x1E);
    return (i >= 0x24 && i < 0x28) || (i >= 0x3C && i < 0x40) || (i >= 0x60 && i < 0x64) || (i >= 0x06 && i < 0x08);
}

size_t storedSizeOf(uint8_t format) { return format == 3 ? pk3::kStoredSize : pk4::kStoredSize; }

}  // namespace

TEST(ribbons_tables) {
    CHECK_EQ(rb::count(3), 17);
    CHECK_EQ(rb::count(4), 80);
    CHECK_EQ(rb::count(5), 80);
    CHECK_EQ(rb::count(1), 0);
    CHECK(rb::isRank(3, 0) && rb::isRank(3, 4) && !rb::isRank(3, 5) && !rb::isRank(4, 0));
    CHECK_EQ(rb::maxValue(3, 2), 4);
    CHECK_EQ(rb::maxValue(3, 10), 1);
    for (uint8_t f = 3; f <= 5; ++f) {
        for (int i = 0; i < rb::count(f); ++i) CHECK(rb::name(f, i)[0] != '\0');
    }
    CHECK(std::string(rb::name(4, 0)) == "Sinnoh Champion");
    CHECK(std::string(rb::name(4, 79)) == "Sinnoh Tough Master");
    CHECK(std::string(rb::rankName(4)) == "Master");
    CHECK(!rb::set(4, makeEntity(4).data(), 80, 1));
}

TEST(ribbons_known_bit_positions) {
    // Gen IV / V: Sinnoh set 0x24, Hoenn set 0x3C, Sinnoh contest set 0x60.
    for (uint8_t f = 4; f <= 5; ++f) {
        struct Case {
            int index;
            size_t offset;
            int bit;
        };
        const Case cases[] = {{0, 0x24, 0},  {7, 0x24, 7},  {8, 0x25, 0},  {27, 0x27, 3},
                              {28, 0x3C, 0}, {48, 0x3E, 4}, {59, 0x3F, 7}, {60, 0x60, 0},
                              {79, 0x62, 3}};
        for (const Case& c : cases) {
            std::vector<uint8_t> raw = makeEntity(f);
            std::vector<uint8_t> before = raw;
            CHECK(rb::set(f, raw.data(), c.index, 1));
            for (size_t i = 0; i < storedSizeOf(f); ++i) {
                if (isRibbonOrChecksumByte(f, i) && (i < 0x08)) continue;  // checksum
                const uint8_t expected = i == c.offset ? static_cast<uint8_t>(before[i] | (1u << c.bit)) : before[i];
                CHECK_EQ(raw[i], expected);
            }
        }
    }
    // Gen III: ranks in 3-bit fields from bit 0, flags from bit 15, fateful flag in bit 31 untouched.
    std::vector<uint8_t> raw = makeEntity(3);
    util::writeU32(raw.data() + pk3::offset::kRibbons, 0x80000000u);
    pk3::refreshChecksum(raw.data());
    CHECK(rb::set(3, raw.data(), 2, 3));   // Cute Hyper
    CHECK(rb::set(3, raw.data(), 5, 1));   // Champion
    CHECK(rb::set(3, raw.data(), 16, 1));  // World
    CHECK_EQ(util::readU32(raw.data() + pk3::offset::kRibbons), 0x80000000u | (3u << 6) | (1u << 15) | (1u << 26));
    CHECK(checksumOk(3, raw.data()));
    CHECK(rb::set(3, raw.data(), 0, 9));  // clamped to Master
    CHECK_EQ(rb::get(3, raw.data(), 0), 4);
    CHECK_EQ(rb::total(3, raw.data()), 4 + 3 + 1 + 1);
}

TEST(ribbons_set_changes_only_its_bit_and_the_checksum) {
    for (uint8_t f = 3; f <= 5; ++f) {
        std::vector<uint8_t> clean = makeEntity(f);
        // A busy pattern in the ribbon bytes, to catch neighbours being disturbed.
        std::vector<uint8_t> busy = clean;
        if (f == 3) {
            // Ranks 1, 2, 3, 4, 2, flags on every other bit, fateful encounter set.
            util::writeU32(busy.data() + pk3::offset::kRibbons,
                           0x80000000u | 1u | (2u << 3) | (3u << 6) | (4u << 9) | (2u << 12) | 0x05550000u);
            pk3::refreshChecksum(busy.data());
        } else {
            for (size_t i : {0x24, 0x25, 0x26, 0x27, 0x3C, 0x3D, 0x3E, 0x3F, 0x60, 0x61, 0x62}) {
                busy[i] = static_cast<uint8_t>(0x55 ^ (i * 7));
            }
            pk4::refreshChecksum(busy.data());
        }

        for (const std::vector<uint8_t>* base : {&clean, &busy}) {
            const int count = rb::count(f);
            for (int i = 0; i < count; ++i) {
                for (int value = 0; value <= rb::maxValue(f, i); ++value) {
                    std::vector<uint8_t> raw = *base;
                    CHECK(rb::set(f, raw.data(), i, value));
                    CHECK_EQ(rb::get(f, raw.data(), i), value);
                    CHECK(checksumOk(f, raw.data()));
                    for (int j = 0; j < count; ++j) {
                        if (j != i) CHECK_EQ(rb::get(f, raw.data(), j), rb::get(f, base->data(), j));
                    }
                    for (size_t b = 0; b < storedSizeOf(f); ++b) {
                        if (!isRibbonOrChecksumByte(f, b)) CHECK_EQ(raw[b], (*base)[b]);
                    }
                    if (f == 3) CHECK_EQ(util::readU32(raw.data() + 0x4C) >> 31, util::readU32(base->data() + 0x4C) >> 31);
                    // Putting the old value back restores the exact bytes.
                    CHECK(rb::set(f, raw.data(), i, rb::get(f, base->data(), i)));
                    CHECK(std::memcmp(raw.data(), base->data(), storedSizeOf(f)) == 0);
                }
            }
        }
    }
}

TEST(ribbons_survive_the_model_round_trip) {
    // Editing other fields through the codecs keeps the ribbons.
    for (uint8_t f = 3; f <= 5; ++f) {
        std::vector<uint8_t> raw = makeEntity(f);
        rb::set(f, raw.data(), 1, 1);
        rb::set(f, raw.data(), 6, 1);
        const int before = rb::total(f, raw.data());
        pokemon::Pokemon p = pokemon::entity::decode(f, raw.data(), storedSizeOf(f));
        p.friendship = 200;
        pokemon::entity::apply(p, raw.data());
        CHECK_EQ(rb::total(f, raw.data()), before);
        CHECK(checksumOk(f, raw.data()));
    }
}

namespace {

std::vector<uint8_t> blankFor(uint8_t format) {
    return format == 3 ? fake::blankGen3(saves::Game::FireRedLeafGreen)
         : format == 4 ? fake::blankGen4(fake::kPt)
                       : fake::blankGen5(false);
}

// A blank save of the format holding `entity` in party slot 0 and box 1 slot 3.
std::vector<uint8_t> saveWith(uint8_t format, const std::vector<uint8_t>& entity) {
    std::unique_ptr<saves::SaveFile> save = saves::loadSaveFile(blankFor(format));
    CHECK(save != nullptr);
    uint8_t party[pokemon::entity::kMaxPartySize] = {};
    std::memcpy(party, entity.data(), save->storedSize());
    pokemon::entity::refreshParty(format, party);
    CHECK(save->appendParty(party));
    CHECK(save->writeBoxSlot(1, 3, entity.data()));
    save->refreshChecksums();
    return save->data();
}

}  // namespace

TEST(ribbons_through_a_save_commit) {
    for (uint8_t f = 3; f <= 5; ++f) {
        std::vector<uint8_t> entity = makeEntity(f);
        std::unique_ptr<saves::SaveFile> save = saves::loadSaveFile(saveWith(f, entity));
        CHECK(save != nullptr);
        if (!save) continue;

        uint8_t box[pokemon::entity::kMaxPartySize] = {};
        CHECK(save->readBoxSlot(1, 3, box) == saves::SlotState::Valid);
        CHECK(rb::set(f, box, 0, f == 3 ? 4 : 1));
        CHECK(rb::set(f, box, 12, 1));
        CHECK(save->writeBoxSlot(1, 3, box));

        uint8_t party[pokemon::entity::kMaxPartySize] = {};
        CHECK(save->readPartySlot(0, party) == saves::SlotState::Valid);
        CHECK(rb::set(f, party, 6, 1));
        CHECK(save->writePartySlot(0, party));

        char pattern[] = "/tmp/pktool-rib-XXXXXX";
        std::string dir = mkdtemp(pattern);
        std::string path = io::joinPath(dir, "game.sav");
        CHECK(io::safeWriteFile(path, blankFor(f)) == io::WriteResult::Ok);
        CHECK(saves::commitSave(*save, path, io::joinPath(dir, "backups")).status == saves::CommitResult::Status::Ok);

        std::vector<uint8_t> written;
        CHECK(io::readFile(path, written));
        std::unique_ptr<saves::SaveFile> back = saves::loadSaveFile(written);
        CHECK(back != nullptr);
        if (!back) continue;
        CHECK(back->checksumsValid());
        uint8_t readBox[pokemon::entity::kMaxPartySize] = {};
        CHECK(back->readBoxSlot(1, 3, readBox) == saves::SlotState::Valid);  // Valid = entity checksum matches
        CHECK_EQ(rb::get(f, readBox, 0), f == 3 ? 4 : 1);
        CHECK_EQ(rb::get(f, readBox, 12), 1);
        CHECK_EQ(rb::total(f, readBox), (f == 3 ? 4 : 1) + 1);
        // Nothing but the ribbons changed in the stored entity.
        for (size_t i = 0; i < back->storedSize(); ++i) {
            if (!isRibbonOrChecksumByte(f, i)) CHECK_EQ(readBox[i], entity[i]);
        }
        uint8_t readParty[pokemon::entity::kMaxPartySize] = {};
        CHECK(back->readPartySlot(0, readParty) == saves::SlotState::Valid);
        CHECK_EQ(rb::get(f, readParty, 6), 1);
        CHECK_EQ(rb::total(f, readParty), 1);
    }
}

TEST(real_saves_ribbons_round_trip) {
    const char* dir = std::getenv("PKTOOL_FIXTURES");
    if (dir == nullptr || *dir == '\0') return;
    for (const std::string& path : io::findSaveFiles(dir, 2)) {
        std::vector<uint8_t> bytes;
        CHECK(io::readFile(path, bytes));
        std::unique_ptr<saves::SaveFile> save = saves::loadSaveFile(bytes);
        if (!save) continue;
        const uint8_t f = save->format();
        int seen = 0;
        int withRibbons = 0;
        auto visit = [&](uint8_t* raw) {
            ++seen;
            if (rb::total(f, raw) > 0) ++withRibbons;
            // Writing back what is read leaves every byte alone.
            std::vector<uint8_t> copy(raw, raw + pokemon::entity::kMaxPartySize);
            for (int i = 0; i < rb::count(f); ++i) {
                int v = rb::get(f, copy.data(), i);
                if (v <= rb::maxValue(f, i)) CHECK(rb::set(f, copy.data(), i, v));
            }
            CHECK(std::memcmp(copy.data(), raw, save->storedSize()) == 0);
        };
        for (int s = 0; s < save->partyCount(); ++s) {
            uint8_t raw[pokemon::entity::kMaxPartySize] = {};
            if (save->readPartySlot(s, raw) == saves::SlotState::Valid) visit(raw);
        }
        for (int b = 0; b < save->boxCount(); ++b) {
            for (int s = 0; s < saves::SaveFile::kSlotsPerBox; ++s) {
                uint8_t raw[pokemon::entity::kMaxPartySize] = {};
                if (save->readBoxSlot(b, s, raw) == saves::SlotState::Valid) visit(raw);
            }
        }
        std::printf("  %s: %d Pokemon, %d with ribbons\n", io::fileName(path).c_str(), seen, withRibbons);

        // Edit the first party member and write the save through the safe pipeline.
        uint8_t party[pokemon::entity::kMaxPartySize] = {};
        CHECK(save->readPartySlot(0, party) == saves::SlotState::Valid);
        const int total = rb::total(f, party);
        const int old = rb::get(f, party, rb::count(f) - 1);
        CHECK(rb::set(f, party, rb::count(f) - 1, old ? 0 : 1));
        CHECK(save->writePartySlot(0, party));
        char pattern[] = "/tmp/pktool-realrib-XXXXXX";
        std::string tmp = mkdtemp(pattern);
        std::string target = io::joinPath(tmp, "game.sav");
        CHECK(io::safeWriteFile(target, bytes) == io::WriteResult::Ok);
        CHECK(saves::commitSave(*save, target, io::joinPath(tmp, "backups")).status == saves::CommitResult::Status::Ok);
        std::vector<uint8_t> written;
        CHECK(io::readFile(target, written));
        std::unique_ptr<saves::SaveFile> back = saves::loadSaveFile(written);
        CHECK(back != nullptr);
        if (!back) continue;
        uint8_t again[pokemon::entity::kMaxPartySize] = {};
        CHECK(back->readPartySlot(0, again) == saves::SlotState::Valid);
        CHECK_EQ(rb::total(f, again), total + (old ? -1 : 1));
    }
}

// ----- UI ---------------------------------------------------------------------

namespace {

std::vector<uint16_t> g_top(gfx::kScreenWidth * gfx::kScreenHeight);
std::vector<uint16_t> g_bottom(gfx::kScreenWidth * gfx::kScreenHeight);

void draw(ui::App& app) {
    gfx::Canvas top(g_top.data());
    gfx::Canvas bottom(g_bottom.data());
    app.draw(top, bottom);
}

void press(ui::App& app, uint32_t keys) {
    ui::Input in;
    in.pressed = keys;
    in.repeated = keys;
    app.update(in);
    app.update(ui::Input());
    draw(app);
}

void tap(ui::App& app, int x, int y) {
    ui::Input in;
    in.touched = true;
    in.touchX = x;
    in.touchY = y;
    app.update(in);
    app.update(ui::Input());
    draw(app);
}

struct Fixture {
    ui::Config config;
    std::string path;
};

Fixture makeFixture(const std::vector<uint8_t>& bytes) {
    char pattern[] = "/tmp/pktool-ribui-XXXXXX";
    std::string dir = mkdtemp(pattern);
    Fixture f;
    f.config.scanRoot = dir;
    f.config.dataDir = io::joinPath(dir, "PKTOOL");
    f.path = io::joinPath(dir, "test.sav");
    CHECK(io::safeWriteFile(f.path, bytes) == io::WriteResult::Ok);
    return f;
}

std::unique_ptr<saves::SaveFile> loadFile(const std::string& path) {
    std::vector<uint8_t> bytes;
    CHECK(io::readFile(path, bytes));
    return saves::loadSaveFile(bytes);
}

// SELECT + confirm.
void writeSave(ui::App& app) {
    press(app, ui::kKeySelect);
    press(app, ui::kKeyA);
}

// Opens the party view of the save and the slot menu of the first Pokémon,
// then picks "Ribbons" (the menu lists Edit, Move, Legality, [Showdown], Bank,
// Clone, Ribbons; the party has one member so there is no Release).
void openRibbons(ui::App& app, bool egg, bool fromSaveMenu = true) {
    if (fromSaveMenu) press(app, ui::kKeyA);  // save menu -> Boxes (the party view)
    CHECK(std::string(app.screenName()) == "Box");
    press(app, ui::kKeyA);  // slot menu
    for (int i = 0; i < (egg ? 5 : 6); ++i) press(app, ui::kKeyDown);
    press(app, ui::kKeyA);
}

}  // namespace

TEST(ui_ribbons_pk4_pk5_toggle_apply_and_write) {
    for (uint8_t f = 4; f <= 5; ++f) {
        std::vector<uint8_t> entity = makeEntity(f);
        Fixture fx = makeFixture(saveWith(f, entity));
        ui::App app(fx.config);
        CHECK(app.openSave(fx.path));
        openRibbons(app, false);
        CHECK(std::string(app.screenName()) == "Ribbons");

        press(app, ui::kKeyA);      // Sinnoh Champion
        press(app, ui::kKeyDown);
        press(app, ui::kKeyDown);
        press(app, ui::kKeyA);      // Great Ability
        press(app, ui::kKeyRight);  // Left/Right on a flag set it ...
        press(app, ui::kKeyLeft);   // ... and clear it
        press(app, ui::kKeyStart);
        CHECK(std::string(app.screenName()) == "Box");
        writeSave(app);

        std::unique_ptr<saves::SaveFile> saved = loadFile(fx.path);
        CHECK(saved != nullptr);
        if (!saved) continue;
        CHECK(saved->checksumsValid());
        uint8_t raw[pokemon::entity::kMaxPartySize] = {};
        CHECK(saved->readPartySlot(0, raw) == saves::SlotState::Valid);
        CHECK_EQ(rb::get(f, raw, 0), 1);
        CHECK_EQ(rb::get(f, raw, 1), 0);
        CHECK_EQ(rb::get(f, raw, 2), 0);
        CHECK_EQ(rb::total(f, raw), 1);
        // The box copy was not touched.
        uint8_t box[pokemon::entity::kMaxPartySize] = {};
        CHECK(saved->readBoxSlot(1, 3, box) == saves::SlotState::Valid);
        CHECK_EQ(rb::total(f, box), 0);
    }
}

TEST(ui_ribbons_pk3_ranks_all_none_and_cancel) {
    std::vector<uint8_t> entity = makeEntity(3);
    Fixture fx = makeFixture(saveWith(3, entity));
    ui::App app(fx.config);
    CHECK(app.openSave(fx.path));
    openRibbons(app, false);
    CHECK(std::string(app.screenName()) == "Ribbons");

    press(app, ui::kKeyRight);  // Cool contest: Normal
    press(app, ui::kKeyRight);  // Super
    press(app, ui::kKeyRight);  // Hyper
    press(app, ui::kKeyLeft);   // Super
    press(app, ui::kKeyDown);
    press(app, ui::kKeyA);      // Beauty contest: A raises the rank
    press(app, ui::kKeyA);
    press(app, ui::kKeyDown);
    press(app, ui::kKeyLeft);   // Cute at None stays None
    for (int i = 0; i < 3; ++i) press(app, ui::kKeyDown);  // Tough contest, then Champion (flag)
    press(app, ui::kKeyA);
    press(app, ui::kKeyStart);
    CHECK(std::string(app.screenName()) == "Box");

    // B with changes asks first; B again keeps editing, A discards.
    openRibbons(app, false, false);
    press(app, ui::kKeyX);  // clear all
    press(app, ui::kKeyB);
    press(app, ui::kKeyB);  // "No": stay
    CHECK(std::string(app.screenName()) == "Ribbons");
    press(app, ui::kKeyY);  // every ribbon
    press(app, ui::kKeyX);
    press(app, ui::kKeyB);
    press(app, ui::kKeyA);  // discard
    CHECK(std::string(app.screenName()) == "Box");
    writeSave(app);

    std::unique_ptr<saves::SaveFile> saved = loadFile(fx.path);
    CHECK(saved != nullptr);
    if (!saved) return;
    uint8_t raw[pokemon::entity::kMaxPartySize] = {};
    CHECK(saved->readPartySlot(0, raw) == saves::SlotState::Valid);
    CHECK_EQ(rb::get(3, raw, 0), 2);
    CHECK_EQ(rb::get(3, raw, 1), 2);
    CHECK_EQ(rb::get(3, raw, 2), 0);
    CHECK_EQ(rb::get(3, raw, 5), 1);
    CHECK_EQ(rb::total(3, raw), 5);
}

TEST(ui_ribbons_touch_all_and_scrolling) {
    Fixture fx = makeFixture(saveWith(5, makeEntity(5)));
    ui::App app(fx.config);
    CHECK(app.openSave(fx.path));
    openRibbons(app, false);
    CHECK(std::string(app.screenName()) == "Ribbons");
    tap(app, 120, 6 + 15 * 2 + 7);  // third row: Great Ability
    press(app, ui::kKeyL);          // page up stays at the top
    press(app, ui::kKeyUp);         // wraps to the last ribbon
    press(app, ui::kKeyR);          // pages clamp at the end
    for (int i = 0; i < 12; ++i) press(app, ui::kKeyUp);
    press(app, ui::kKeyY);
    press(app, ui::kKeyStart);
    writeSave(app);
    std::unique_ptr<saves::SaveFile> saved = loadFile(fx.path);
    CHECK(saved != nullptr);
    if (!saved) return;
    uint8_t raw[pokemon::entity::kMaxPartySize] = {};
    CHECK(saved->readPartySlot(0, raw) == saves::SlotState::Valid);
    CHECK_EQ(rb::total(5, raw), 80);
    CHECK(saved->checksumsValid());
}

TEST(ui_ribbons_egg_shows_a_message) {
    Fixture fx = makeFixture(saveWith(4, makeEntity(4, true)));
    ui::App app(fx.config);
    CHECK(app.openSave(fx.path));
    openRibbons(app, true);
    CHECK(std::string(app.screenName()) == "Box");  // no ribbon screen for an egg
    press(app, ui::kKeyA);                          // dismiss the message
    CHECK(std::string(app.screenName()) == "Box");
}
