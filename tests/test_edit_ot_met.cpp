// Tests of the OT / met data / Pokérus editing rules (core/pokemon/edit_origin)
// and of the editor fields that expose them, for Gen III, IV and V.

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "core/io/file_io.h"
#include "core/legality/legality.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/edit4.h"
#include "core/pokemon/edit_origin.h"
#include "core/pokemon/entity.h"
#include "core/pokemon/pk3.h"
#include "core/pokemon/pk4.h"
#include "core/saves/save_file.h"
#include "ds/app/app.h"
#include "ds/gfx/canvas.h"
#include "tests/fake_saves.h"
#include "tests/test.h"

using namespace pktool;
using namespace pktool::pokemon;
namespace eo = pokemon::editorigin;
namespace pk4 = pokemon::pk4;
namespace pk3 = pokemon::pk3;

namespace {

constexpr uint16_t kPikachu = 25;

struct Sample {
    uint8_t version;
    uint8_t format;
    const char* name;
};

const Sample kSamples[] = {
    {legality::version::kFireRed, 3, "FRLG"},
    {legality::version::kPlatinum, 4, "Pt"},
    {legality::version::kDiamond, 4, "D"},
    {legality::version::kHeartGold, 4, "HG"},
    {legality::version::kBlack2, 5, "B2"},
};

create4::Trainer trainerFor(uint8_t version) {
    create4::Trainer t;
    t.name = "ASH";
    t.tid = 12345;
    t.sid = 54321;
    t.version = version;
    t.today = {24, 5, 17};
    return t;
}

Pokemon makeMon(const Sample& s) {
    return create4::newPokemon(trainerFor(s.version), kPikachu, 30, 777);
}

Pokemon decode(uint8_t format, const uint8_t* raw) {
    return entity::decode(format, raw, entity::kStoredSize);
}

size_t storedSize(uint8_t format) {
    return format == 3 ? pk3::kStoredSize : pk4::kStoredSize;
}

// Offsets (decrypted layout) that editing OT, met data and Pokérus may change.
std::vector<size_t> allowedOffsets(uint8_t format) {
    std::vector<size_t> out;
    auto range = [&](size_t from, size_t count) {
        for (size_t i = 0; i < count; ++i) out.push_back(from + i);
    };
    if (format == 3) {
        range(pk3::offset::kTid, 4);
        range(pk3::offset::kOtName, pk3::kOtNameChars);
        range(pk3::offset::kChecksum, 2);
        range(pk3::offset::kPokerus, 1);
        range(pk3::offset::kMetLocation, 1);
        range(pk3::offset::kOrigins, 2);
        range(pk3::offset::kRibbons, 4);  // fateful encounter = bit 31
        return out;
    }
    range(pk4::offset::kTid, 4);
    range(pk4::offset::kChecksum, 2);
    range(pk4::offset::kFlags40, 1);
    range(pk4::offset::kEggLocationExt, 2);
    range(pk4::offset::kMetLocationExt, 2);
    range(pk4::offset::kOtName, pk4::kOtNameChars * 2);
    range(pk4::offset::kEggDate, 3);
    range(pk4::offset::kMetDate, 3);
    range(pk4::offset::kEggLocationDp, 2);
    range(pk4::offset::kMetLocationDp, 2);
    range(pk4::offset::kPokerus, 1);
    range(pk4::offset::kMetLevelOtGender, 1);
    return out;
}

bool onlyAllowedBytesDiffer(uint8_t format, const uint8_t* a, const uint8_t* b) {
    std::vector<size_t> allowed = allowedOffsets(format);
    for (size_t i = 0; i < storedSize(format); ++i) {
        if (a[i] == b[i]) continue;
        bool ok = false;
        for (size_t o : allowed) ok = ok || o == i;
        if (!ok) {
            std::printf("  unexpected byte change at 0x%02zX (format %d)\n", i, format);
            return false;
        }
    }
    return true;
}

bool checksumOk(uint8_t format, const uint8_t* raw) {
    return format == 3 ? pk3::calcChecksum(raw) == pk3::storedChecksum(raw) : pk4::calcChecksum(raw) == pk4::storedChecksum(raw);
}

}  // namespace

// ----- Rules ------------------------------------------------------------------------

TEST(origin_pokerus_rules) {
    Pokemon p;
    CHECK(eo::pokerusText(p.pokerus) == "None");
    CHECK(!eo::setPokerusDays(p, 3));  // no strain: nothing to set
    CHECK_EQ(p.pokerus, 0);

    eo::setPokerusStrain(p, 6);  // (6 & 3) + 1 = 3 days
    CHECK_EQ(eo::pokerusStrain(p.pokerus), 6);
    CHECK_EQ(eo::pokerusDays(p.pokerus), 3);
    CHECK(eo::pokerusText(p.pokerus) == "Infected (strain 6, 3 days)");

    CHECK(eo::setPokerusDays(p, 9));  // cut to the strain's maximum
    CHECK_EQ(eo::pokerusDays(p.pokerus), 3);
    CHECK(eo::setPokerusDays(p, 1));
    CHECK(eo::pokerusText(p.pokerus) == "Infected (strain 6, 1 day)");
    eo::setPokerusStrain(p, 4);  // maximum is 1 day
    CHECK_EQ(eo::pokerusDays(p.pokerus), 1);
    CHECK(eo::setPokerusDays(p, 0));
    CHECK(eo::pokerusText(p.pokerus) == "Cured (strain 4)");
    eo::setPokerusStrain(p, 7);  // a cured Pokémon stays cured
    CHECK_EQ(p.pokerus, 0x70);
    eo::setPokerusStrain(p, 99);  // strain is 0-15
    CHECK_EQ(eo::pokerusStrain(p.pokerus), 15);
    CHECK_EQ(eo::pokerusDays(p.pokerus), 0);
    eo::setPokerusStrain(p, 0);  // no strain: no days
    CHECK_EQ(p.pokerus, 0);
    eo::setPokerusStrain(p, 15);
    CHECK_EQ(eo::pokerusDays(p.pokerus), 4);
    for (int strain = 0; strain < 16; ++strain) CHECK_EQ(eo::pokerusMaxDays(static_cast<uint8_t>(strain)), strain == 0 ? 0 : (strain & 3) + 1);
}

TEST(origin_dates_are_always_real_calendar_dates) {
    CHECK(eo::isValidDate(Date{}));
    CHECK(eo::isValidDate(Date{24, 2, 29}));
    CHECK(!eo::isValidDate(Date{23, 2, 29}));
    CHECK(!eo::isValidDate(Date{10, 4, 31}));
    CHECK(!eo::isValidDate(Date{10, 13, 1}));
    CHECK(!eo::isValidDate(Date{10, 0, 5}));
    CHECK(!eo::isValidDate(Date{100, 1, 1}));
    CHECK(eo::dateText(Date{}) == "None");
    CHECK(eo::dateText(Date{9, 3, 7}) == "2009-03-07");

    const Date today{26, 9, 29};
    Date d = eo::stepDay(Date{24, 2, 28}, 1, today);
    CHECK(d.year == 24 && d.month == 2 && d.day == 29);
    d = eo::stepDay(Date{23, 2, 28}, 1, today);
    CHECK(d.year == 23 && d.month == 3 && d.day == 1);
    d = eo::stepDay(Date{23, 12, 31}, 1, today);
    CHECK(d.year == 24 && d.month == 1 && d.day == 1);
    d = eo::stepDay(Date{24, 3, 1}, -1, today);
    CHECK(d.year == 24 && d.month == 2 && d.day == 29);
    d = eo::stepDay(Date{99, 12, 31}, 1, today);
    CHECK(d.year == 99 && d.month == 12 && d.day == 31);
    d = eo::stepDay(Date{0, 1, 1}, -1, today);
    CHECK(!eo::isDateSet(d));  // before 2000: unset
    d = eo::stepDay(Date{}, 1, today);
    CHECK(d.year == 26 && d.month == 9 && d.day == 29);
    d = eo::stepMonth(Date{23, 1, 31}, 1, today);
    CHECK(d.year == 23 && d.month == 2 && d.day == 28);
    d = eo::stepMonth(Date{24, 1, 31}, 1, today);
    CHECK(d.year == 24 && d.month == 2 && d.day == 29);
    d = eo::stepMonth(Date{23, 12, 15}, 1, today);
    CHECK(d.year == 24 && d.month == 1 && d.day == 15);
    // Stepping through years of days never produces an invalid date.
    d = Date{23, 12, 20};
    for (int i = 0; i < 800; ++i) {
        d = eo::stepDay(d, 1, today);
        CHECK(eo::isValidDate(d));
    }
    for (int i = 0; i < 120; ++i) {
        d = eo::stepMonth(d, -1, today);
        CHECK(eo::isValidDate(d));
    }
}

TEST(origin_ot_name_and_ids) {
    Pokemon p = makeMon(kSamples[1]);
    CHECK_EQ(eo::otNameMaxLength(3), 7u);
    CHECK_EQ(eo::otNameMaxLength(4), 7u);
    CHECK_EQ(eo::otNameMaxLength(5), 7u);
    CHECK(!eo::setOtName(p, ""));
    CHECK(p.trainer.name == "ASH");
    CHECK(eo::setOtName(p, "ABCDEFGHIJ"));
    CHECK(p.trainer.name == "ABCDEFG");

    // TID/SID change shininess but never the PID.
    const uint32_t pid = p.pid;
    eo::setTid(p, 100);
    eo::setSid(p, static_cast<uint16_t>(100 ^ (pid >> 16) ^ (pid & 0xFFFF)));  // XOR = 0: shiny
    CHECK(p.shiny);
    CHECK_EQ(p.pid, pid);
    eo::setSid(p, static_cast<uint16_t>(p.trainer.sid ^ 0x1000));
    CHECK(!p.shiny);
    CHECK_EQ(p.pid, pid);
    CHECK(p.shiny == isShiny(p.pid, p.trainer.tid, p.trainer.sid));
}

TEST(origin_location_stepping_stays_on_storable_named_ids) {
    for (const Sample& s : kSamples) {
        Pokemon p = makeMon(s);
        p.originGame = s.version;
        uint16_t location = 0;
        std::vector<uint16_t> seen;
        for (int guard = 0; guard < 5000; ++guard) {
            uint16_t next = eo::stepLocation(p, location, 1);
            if (next == location) break;
            CHECK(next > location);
            CHECK(eo::isNamedLocation(s.format, next));
            CHECK(eo::canStoreLocation(p, next));
            seen.push_back(next);
            location = next;
        }
        CHECK(seen.size() > 30);
        // Walking back reaches the start again and 10 steps is 10 entries.
        CHECK_EQ(eo::stepLocation(p, location, -static_cast<int>(seen.size()) + 1), seen[0]);
        if (seen.size() > 12) CHECK_EQ(eo::stepLocation(p, seen[0], 10), seen[10]);
        CHECK_EQ(eo::stepLocation(p, seen[5], -1), seen[4]);
        // An unnamed current ID steps to its named neighbours.
        uint16_t gap = static_cast<uint16_t>(seen[3] + 1);
        if (!eo::isNamedLocation(s.format, gap) && eo::canStoreLocation(p, gap)) {
            CHECK_EQ(eo::stepLocation(p, gap, 1), seen[4]);
            CHECK_EQ(eo::stepLocation(p, gap, -1), seen[3]);
        }
        CHECK(!eo::locationText(s.format, seen[3]).empty());
    }
    Pokemon p = makeMon(kSamples[1]);
    CHECK(eo::locationText(4, 65000) == "Location 65000");
}

// ----- Round trips on the raw bytes ----------------------------------------------------

TEST(origin_edits_round_trip_in_every_format) {
    for (const Sample& s : kSamples) {
        Pokemon p = makeMon(s);
        uint8_t raw[pk4::kStoredSize];
        create4::encode(p, raw);
        CHECK(checksumOk(s.format, raw));
        Pokemon before = decode(s.format, raw);

        Pokemon e = before;
        CHECK(eo::setOtName(e, "MAY"));
        e.trainer.gender = e.trainer.gender == Gender::Female ? Gender::Male : Gender::Female;
        eo::setTid(e, 4242);
        eo::setSid(e, 31337);
        e.met.metLocation = eo::stepLocation(e, e.met.metLocation, 5);
        eo::setMetLevel(e, 77);
        e.met.fatefulEncounter = !e.met.fatefulEncounter;
        eo::setPokerusStrain(e, 10);  // (10 & 3) + 1 = 3 days
        CHECK(eo::setPokerusDays(e, 2));
        if (eo::hasMetDate(s.format)) e.met.metDate = Date{12, 8, 31};
        if (eo::hasEggData(s.format)) {
            e.met.eggLocation = eo::stepLocation(e, e.met.eggLocation, 4);
            e.met.eggDate = eo::stepDay(Date{12, 8, 31}, 1, Date{});
        }

        uint8_t edited[pk4::kStoredSize];
        std::memcpy(edited, raw, sizeof edited);
        entity::apply(e, edited);
        CHECK(checksumOk(s.format, edited));
        CHECK(onlyAllowedBytesDiffer(s.format, raw, edited));

        Pokemon after = decode(s.format, edited);
        CHECK(after.trainer.name == "MAY");
        CHECK(after.trainer.gender == e.trainer.gender);
        CHECK_EQ(after.trainer.tid, 4242);
        CHECK_EQ(after.trainer.sid, 31337);
        CHECK_EQ(after.met.metLocation, e.met.metLocation);
        CHECK_EQ(after.met.metLevel, 77);
        CHECK(after.met.fatefulEncounter == e.met.fatefulEncounter);
        CHECK_EQ(after.pokerus, 0xA2);
        CHECK_EQ(after.pid, before.pid);
        CHECK(after.shiny == isShiny(after.pid, 4242, 31337));
        if (eo::hasMetDate(s.format)) {
            CHECK(after.met.metDate.year == 12 && after.met.metDate.month == 8 && after.met.metDate.day == 31);
        }
        if (eo::hasEggData(s.format)) {
            CHECK_EQ(after.met.eggLocation, e.met.eggLocation);
            CHECK(after.met.eggDate.year == 12 && after.met.eggDate.month == 9 && after.met.eggDate.day == 1);
        }
        // Everything else is unchanged.
        CHECK_EQ(after.species, before.species);
        CHECK_EQ(after.exp, before.exp);
        CHECK_EQ(after.nature, before.nature);
        CHECK_EQ(after.heldItem, before.heldItem);
        CHECK_EQ(after.met.ball, before.met.ball);
        CHECK_EQ(after.originGame, before.originGame);
        CHECK(after.nickname == before.nickname);
        for (int m = 0; m < 4; ++m) CHECK_EQ(after.moves[m], before.moves[m]);

        // The checkers cope with the edited values.
        legality::Report report = legality::check(after);
        (void)report;

        // And undoing the edits restores the original bytes.
        Pokemon undone = after;
        undone.trainer = before.trainer;
        undone.met = before.met;
        undone.pokerus = before.pokerus;
        uint8_t restored[pk4::kStoredSize];
        std::memcpy(restored, edited, sizeof restored);
        entity::apply(undone, restored);
        CHECK(std::memcmp(restored, raw, storedSize(s.format)) == 0);
    }
}

TEST(origin_every_steppable_location_round_trips) {
    for (const Sample& s : kSamples) {
        Pokemon p = makeMon(s);
        uint8_t raw[pk4::kStoredSize];
        create4::encode(p, raw);
        Pokemon base = decode(s.format, raw);
        base.originGame = s.version;
        uint16_t location = 0;
        for (int guard = 0; guard < 5000; ++guard) {
            uint16_t next = eo::stepLocation(base, location, 1);
            if (next == location) break;
            location = next;
            Pokemon e = base;
            e.met.metLocation = location;
            e.met.eggLocation = location;
            uint8_t edited[pk4::kStoredSize];
            std::memcpy(edited, raw, sizeof edited);
            entity::apply(e, edited);
            Pokemon after = decode(s.format, edited);
            if (after.met.metLocation != location) std::printf("  %s: location %u read back as %u\n", s.name, location, after.met.metLocation);
            CHECK_EQ(after.met.metLocation, location);
            if (eo::hasEggData(s.format)) CHECK_EQ(after.met.eggLocation, location);
        }
    }
}

TEST(origin_tid_sid_change_keeps_pid_and_shiny_follows) {
    for (const Sample& s : kSamples) {
        Pokemon p = makeMon(s);
        uint8_t raw[pk4::kStoredSize];
        create4::encode(p, raw);
        Pokemon e = decode(s.format, raw);
        const uint32_t pid = e.pid;
        eo::setTid(e, 7);
        eo::setSid(e, static_cast<uint16_t>(7 ^ (pid >> 16) ^ (pid & 0xFFFF) ^ 3));  // XOR = 3 < 8: shiny
        CHECK(e.shiny);
        entity::apply(e, raw);
        Pokemon after = decode(s.format, raw);
        CHECK(after.shiny);
        CHECK_EQ(after.pid, pid);
        // Gen III re-encrypts with PID ^ TID/SID: the encrypted form must still decode.
        if (s.format == 3) {
            uint8_t enc[pk3::kStoredSize];
            std::memcpy(enc, raw, sizeof enc);
            pk3::encrypt(enc);
            pk3::decrypt(enc);
            CHECK(std::memcmp(enc, raw, sizeof enc) == 0);
        }
    }
}

TEST(origin_egg_edits_round_trip) {
    for (const Sample& s : kSamples) {
        if (s.format == 3) continue;  // Gen III eggs are not created by PKTool
        Pokemon egg = create4::newEgg(trainerFor(s.version), kPikachu, 99);
        uint8_t raw[pk4::kStoredSize];
        create4::encode(egg, raw);
        Pokemon e = decode(s.format, raw);
        CHECK(e.isEgg);
        e.met.eggLocation = eo::stepLocation(e, e.met.eggLocation, 3);
        e.met.eggDate = eo::stepDay(e.met.eggDate, -40, Date{});
        eo::setPokerusStrain(e, 1);
        uint8_t edited[pk4::kStoredSize];
        std::memcpy(edited, raw, sizeof edited);
        entity::apply(e, edited);
        CHECK(checksumOk(s.format, edited));
        Pokemon after = decode(s.format, edited);
        CHECK(after.isEgg);
        CHECK_EQ(after.met.eggLocation, e.met.eggLocation);
        CHECK(after.met.eggDate.year == e.met.eggDate.year && after.met.eggDate.month == e.met.eggDate.month &&
              after.met.eggDate.day == e.met.eggDate.day);
        CHECK_EQ(after.met.metLocation, 0);
        CHECK(!eo::isDateSet(after.met.metDate));
    }
}

// ----- Editor UI -----------------------------------------------------------------------

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

void tapKey(ui::App& app, int row, int col) { tap(app, 8 + col * 24 + 10, 8 + row * 28 + 10); }
void tapDelete(ui::App& app) { tap(app, 114, 134); }
void tapOk(ui::App& app) { tap(app, 210, 134); }

// Editor rows in order (see the Field enum in ds/app/app.cpp).
constexpr int kRowOtName = 26;
constexpr int kRowOtGender = 27;
constexpr int kRowTid = 28;
constexpr int kRowSid = 29;
constexpr int kRowMetLocation = 30;
constexpr int kRowMetLevel = 31;
constexpr int kRowMetDate = 32;
constexpr int kRowEggLocation = 33;
constexpr int kRowEggDate = 34;
constexpr int kRowFateful = 35;
constexpr int kRowPokerusStrain = 36;
constexpr int kRowPokerusDays = 37;

bool rowVisible(uint8_t format, bool egg, int row) {
    if (row == kRowMetDate) return format >= 4 && !egg;
    if (row == kRowEggLocation || row == kRowEggDate) return format >= 4;
    if (row == kRowMetLocation || row == kRowMetLevel) return !(egg && format >= 4);
    return true;
}

// Moves the editor cursor from row `from` down to row `to`, as a player would.
void goDown(ui::App& app, uint8_t format, bool egg, int from, int to) {
    for (int row = from; row < to; ++row) {
        if (rowVisible(format, egg, row + 1)) press(app, ui::kKeyDown);
    }
}

struct Fixture {
    ui::Config config;
    std::string path;
};

Fixture makeFixture(const std::vector<uint8_t>& bytes) {
    char pattern[] = "/tmp/pktool-otmet-XXXXXX";
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

// A blank save of `kind` (0 Gen III, 1 Gen IV, 2 Gen V) with `p` in box 1 slot 1.
std::vector<uint8_t> saveWith(int kind, const Pokemon& p) {
    std::vector<uint8_t> bytes = kind == 0   ? fake::blankGen3(saves::Game::FireRedLeafGreen)
                                 : kind == 1 ? fake::blankGen4(fake::kPt)
                                             : fake::blankGen5(false);
    std::unique_ptr<saves::SaveFile> save = saves::loadSaveFile(bytes);
    CHECK(save != nullptr);
    if (!save) return bytes;
    uint8_t raw[pk4::kStoredSize];
    create4::encode(p, raw);
    CHECK(save->writeBoxSlot(0, 0, raw));
    save->refreshChecksums();
    return save->data();
}

void writeSave(ui::App& app) {
    press(app, ui::kKeySelect);
    press(app, ui::kKeyA);
}

// Opens the editor on box 1 slot 1.
void openEditor(ui::App& app) {
    press(app, ui::kKeyA);  // Boxes
    CHECK(std::string(app.screenName()) == "Box");
    press(app, ui::kKeyR);  // party -> box 1
    press(app, ui::kKeyA);  // slot menu
    press(app, ui::kKeyA);  // Edit
    CHECK(std::string(app.screenName()) == "Edit");
}

}  // namespace

TEST(ui_edit_ot_met_and_pokerus_fields_write_to_the_save) {
    const Sample ui_samples[] = {{legality::version::kFireRed, 3, "FRLG"},
                                 {legality::version::kPlatinum, 4, "Pt"},
                                 {legality::version::kBlack, 5, "BW"}};
    for (int kind = 0; kind < 3; ++kind) {
        const Sample& s = ui_samples[kind];
        Pokemon mon = makeMon(s);
        Fixture f = makeFixture(saveWith(kind, mon));
        ui::App app(f.config);
        CHECK(app.openSave(f.path));
        openEditor(app);

        Pokemon original;
        {
            std::unique_ptr<saves::SaveFile> before = loadFile(f.path);
            uint8_t raw[entity::kStoredSize];
            CHECK(before->readBoxSlot(0, 0, raw) == saves::SlotState::Valid);
            original = decode(s.format, raw);
        }

        int row = 0;
        goDown(app, s.format, false, row, kRowOtName);
        row = kRowOtName;
        press(app, ui::kKeyA);  // keyboard
        CHECK(std::string(app.screenName()) == "Keyboard");
        for (int i = 0; i < 8; ++i) tapDelete(app);
        tapKey(app, 1, 7);  // R
        tapKey(app, 0, 4);  // E
        tapKey(app, 0, 3);  // D
        tapOk(app);
        CHECK(std::string(app.screenName()) == "Edit");

        auto next = [&](int target) {
            goDown(app, s.format, false, row, target);
            row = target;
        };
        next(kRowOtGender);
        press(app, ui::kKeyA);      // toggle
        next(kRowTid);
        press(app, ui::kKeyRight);  // +1
        press(app, ui::kKeyR);      // +1000
        next(kRowSid);
        press(app, ui::kKeyY);      // 65535
        next(kRowMetLocation);
        press(app, ui::kKeyRight);
        press(app, ui::kKeyRight);
        next(kRowMetLevel);
        press(app, ui::kKeyRight);
        press(app, ui::kKeyL);      // -10
        if (s.format >= 4) {
            next(kRowMetDate);
            press(app, ui::kKeyY);      // today
            press(app, ui::kKeyLeft);   // one day earlier
            next(kRowEggLocation);
            press(app, ui::kKeyRight);
            next(kRowEggDate);
            press(app, ui::kKeyRight);  // unset -> today
        }
        next(kRowFateful);
        press(app, ui::kKeyA);
        next(kRowPokerusStrain);
        press(app, ui::kKeyRight);
        press(app, ui::kKeyRight);  // strain 2, still 2 days
        next(kRowPokerusDays);
        press(app, ui::kKeyLeft);   // 1 day
        press(app, ui::kKeyStart);  // apply
        CHECK(std::string(app.screenName()) == "Box");
        writeSave(app);

        std::unique_ptr<saves::SaveFile> saved = loadFile(f.path);
        CHECK(saved != nullptr);
        if (!saved) continue;
        CHECK(saved->checksumsValid());
        uint8_t raw[entity::kStoredSize];
        CHECK(saved->readBoxSlot(0, 0, raw) == saves::SlotState::Valid);
        Pokemon after = decode(s.format, raw);
        if (after.trainer.name != "RED") std::printf("  %s: OT name %s\n", s.name, after.trainer.name.c_str());
        CHECK(after.trainer.name == "RED");
        CHECK(after.trainer.gender != original.trainer.gender);
        CHECK_EQ(after.trainer.tid, original.trainer.tid + 1001);
        CHECK_EQ(after.trainer.sid, 65535);
        CHECK_EQ(after.pid, original.pid);
        CHECK(after.shiny == isShiny(after.pid, after.trainer.tid, after.trainer.sid));
        CHECK(after.met.metLocation != original.met.metLocation);
        CHECK(eo::isNamedLocation(s.format, after.met.metLocation));
        CHECK_EQ(after.met.metLevel, original.met.metLevel + 1 - 10);
        CHECK(after.met.fatefulEncounter != original.met.fatefulEncounter);
        CHECK_EQ(after.pokerus, 0x21);  // strain 1 (2 days) -> 2 keeps 2 days -> days -1
        if (s.format >= 4) {
            CHECK(eo::isDateSet(after.met.metDate));
            CHECK(eo::isValidDate(after.met.metDate));
            CHECK(eo::isDateSet(after.met.eggDate));
            CHECK(after.met.eggLocation != original.met.eggLocation);
        }
        (void)legality::check(after);  // must cope with the edited values
    }
}

TEST(ui_edit_pokerus_days_need_a_strain_and_hidden_fields_are_skipped) {
    // An egg of Gen IV has no met data: Down from SID goes to the egg location.
    Pokemon egg = create4::newEgg(trainerFor(legality::version::kPlatinum), kPikachu, 5);
    Fixture f = makeFixture(saveWith(1, egg));
    ui::App app(f.config);
    CHECK(app.openSave(f.path));
    openEditor(app);
    goDown(app, 4, true, 0, kRowSid);
    press(app, ui::kKeyDown);   // -> egg location (met location, level and date are hidden)
    press(app, ui::kKeyRight);
    press(app, ui::kKeyDown);   // egg date
    press(app, ui::kKeyDown);   // fateful
    press(app, ui::kKeyDown);   // Pokérus strain
    press(app, ui::kKeyDown);   // Pokérus days: no strain yet
    press(app, ui::kKeyRight);
    press(app, ui::kKeyStart);
    writeSave(app);
    std::unique_ptr<saves::SaveFile> saved = loadFile(f.path);
    uint8_t raw[entity::kStoredSize];
    CHECK(saved->readBoxSlot(0, 0, raw) == saves::SlotState::Valid);
    Pokemon after = decode(4, raw);
    CHECK(after.isEgg);
    CHECK(after.met.eggLocation != egg.met.eggLocation);
    CHECK_EQ(after.met.metLocation, 0);
    CHECK_EQ(after.pokerus, 0);  // days cannot be set without a strain
    CHECK(saved->checksumsValid());
}
