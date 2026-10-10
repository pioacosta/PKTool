// Japanese and Korean pages of the on-screen keyboard: kana marks, Hangul
// composition, and nicknames typed into Japanese and Korean Gen IV Pokémon
// that survive the save round trip.

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/io/file_io.h"
#include "core/pokemon/entity.h"
#include "core/pokemon/pk4.h"
#include "core/saves/gen4_save.h"
#include "core/text/gen4_text.h"
#include "core/util/bytes.h"
#include "ds/app/app.h"
#include "ds/app/keyboard_cjk.h"
#include "ds/gfx/canvas.h"
#include "tests/fake_saves.h"
#include "tests/test.h"

using namespace pktool;
using ui::kbcjk::Kind;

namespace {

// Types a sequence of UTF-8 keys (as a list of strings) through the typing rules.
std::string typed(Kind kind, const std::vector<std::string>& keys, size_t maxChars = 5) {
    std::string text;
    for (const std::string& key : keys) ui::kbcjk::type(kind, text, key, maxChars);
    return text;
}

// Hangul jamo and kana by escape (keeps the source ASCII).
const std::string kP = "\xE3\x85\x8D";   // ㅍ
const std::string kI = "\xE3\x85\xA3";   // ㅣ
const std::string kK = "\xE3\x85\x8B";   // ㅋ
const std::string kA = "\xE3\x85\x8F";   // ㅏ
const std::string kCh = "\xE3\x85\x8A";  // ㅊ
const std::string kYu = "\xE3\x85\xA0";  // ㅠ
const std::string kG = "\xE3\x84\xB1";   // ㄱ
const std::string kB = "\xE3\x85\x82";   // ㅂ
const std::string kS = "\xE3\x85\x85";   // ㅅ
const std::string kO = "\xE3\x85\x97";   // ㅗ
const std::string kNg = "\xE3\x85\x87";  // ㅇ
const std::string kDakuten = "\xEF\xBE\x9E";
const std::string kHandakuten = "\xEF\xBE\x9F";

const std::string kPika = "\xED\x94\xBC\xEC\xB9\xB4\xEC\xB8\x84";  // 피카츄

std::string makeDir() {
    char pattern[] = "/tmp/pktool-kbcjk-XXXXXX";
    return mkdtemp(pattern);
}

void writeFooter(uint8_t* block, uint32_t size, uint32_t footerSize, uint32_t counter) {
    uint8_t* end = block + size;
    util::writeU32(end - 0x14, counter);
    util::writeU32(end - 0x10, counter);
    util::writeU32(end - 0xC, size);
    util::writeU32(end - 0x8, 0x20060623);
    util::writeU16(end - 2, util::crc16Ccitt(block, size - footerSize));
}

// A Platinum save of `language` with one party Pokemon (partition 1 is the newest).
std::vector<uint8_t> languageSave(uint8_t language) {
    const fake::Gen4Layout& layout = fake::kPt;
    constexpr uint32_t kPartyOffset = 0xA0;  // the party count sits 4 bytes before
    pokemon::Pokemon p;
    p.format = 4;
    p.species = 25;
    p.level = 10;
    p.language = language;
    p.moves[0] = 33;
    p.trainer.name = "A";
    uint8_t raw[pokemon::entity::kMaxPartySize] = {};
    pokemon::entity::apply(p, raw);
    pokemon::pk4::refreshChecksum(raw);
    pokemon::pk4::encrypt(raw, pokemon::pk4::kPartySize);

    std::vector<uint8_t> data = fake::blankGen4(layout);
    for (int partition = 0; partition < 2; ++partition) {
        uint8_t* general = &data[partition * saves::Gen4Save::kPartitionSize];
        general[layout.trainer + 0x19] = language;
        general[kPartyOffset - 4] = 1;
        std::memcpy(general + kPartyOffset, raw, pokemon::pk4::kPartySize);
        const uint32_t counter = partition == 1 ? 10 : 9;
        writeFooter(general, layout.generalSize, layout.footerSize, counter);
        writeFooter(general + layout.storageOffset, layout.storageSize, layout.footerSize, counter);
        if (language == text::kLanguageKorean) {
            util::writeU32(general + layout.generalSize - 8, 0x20070903);
            util::writeU32(general + layout.storageOffset + layout.storageSize - 8, 0x20070903);
        }
    }
    return data;
}

void pressKey(ui::App& app, uint32_t keys) {
    ui::Input in;
    in.pressed = keys;
    in.repeated = keys;
    app.update(in);
    app.update(ui::Input());
}

// Taps the key showing `ch` on keyboard page `page` of `kind` (the page must be showing).
void tapKey(ui::App& app, Kind kind, int page, const std::string& ch) {
    for (int row = 0; row < 4; ++row) {
        const std::string chars = ui::kbcjk::pageRow(kind, page, row);
        for (size_t i = 0, col = 0; i < chars.size(); ++col) {
            size_t len = 1;
            unsigned char c = static_cast<unsigned char>(chars[i]);
            len = c < 0x80 ? 1 : c < 0xE0 ? 2 : 3;
            if (chars.compare(i, len, ch) == 0) {
                static std::vector<uint16_t> topPixels(gfx::kScreenWidth * gfx::kScreenHeight);
                static std::vector<uint16_t> bottomPixels(gfx::kScreenWidth * gfx::kScreenHeight);
                gfx::Canvas top(topPixels.data());
                gfx::Canvas bottom(bottomPixels.data());
                app.draw(top, bottom);  // touch zones come from the last draw
                ui::Input tap;
                tap.touched = true;
                tap.touchX = 8 + static_cast<int>(col) * 24 + 11;
                tap.touchY = 8 + row * 28 + 13;
                app.update(tap);
                app.update(ui::Input());
                return;
            }
            i += len;
        }
    }
    CHECK(false);  // key not on that page
}

// Types `chars` on pages (page, key) pairs, switching pages with SELECT.
struct Step {
    int page;
    std::string ch;
};

void typeSteps(ui::App& app, Kind kind, const std::vector<Step>& steps) {
    int page = 0;
    const int pages = ui::kbcjk::pageCount(kind) + 3;
    for (const Step& step : steps) {
        while (page != step.page) {
            pressKey(app, ui::kKeySelect);
            page = (page + 1) % pages;
        }
        tapKey(app, kind, page, step.ch);
    }
}

// Opens the party Pokemon's nickname keyboard of a `language` save, clears
// it, types `steps`, confirms, and returns the nickname read back from the file.
std::string nicknameThroughSave(uint8_t language, const std::vector<Step>& steps, Kind kind, bool* japaneseLimit) {
    std::string dir = makeDir();
    std::string path = io::joinPath(dir, "Pokemon Platinum.sav");
    CHECK(io::safeWriteFile(path, languageSave(language)) == io::WriteResult::Ok);
    ui::Config config;
    config.scanRoot = dir;
    config.dataDir = io::joinPath(dir, "PKTOOL");
    {
        ui::App app(config);
        CHECK(app.openSave(path));
        pressKey(app, ui::kKeyA);  // save menu: Boxes
        pressKey(app, ui::kKeyA);  // menu
        pressKey(app, ui::kKeyA);  // Edit
        pressKey(app, ui::kKeyA);  // keyboard
        CHECK(std::string(app.screenName()) == "Keyboard");
        for (int i = 0; i < 12; ++i) pressKey(app, ui::kKeyB);
        typeSteps(app, kind, steps);
        (void)japaneseLimit;
        pressKey(app, ui::kKeyStart);   // done
        pressKey(app, ui::kKeyStart);   // apply
        pressKey(app, ui::kKeySelect);  // write
        pressKey(app, ui::kKeyA);
    }
    std::vector<uint8_t> written;
    CHECK(io::readFile(path, written));
    saves::Gen4Save save;
    int loadResult = static_cast<int>(save.load(written));
    CHECK(loadResult == static_cast<int>(saves::Gen4Save::LoadResult::Ok));
    if (loadResult != 0) return std::string();
    uint8_t party[pokemon::pk4::kPartySize];
    CHECK(save.readPartySlot(0, party) == saves::Gen4Save::SlotState::Valid);
    pokemon::Pokemon p = pokemon::entity::decode(4, party, pokemon::pk4::kPartySize);
    CHECK_EQ(p.language, language);
    CHECK(p.isNicknamed);
    return p.nickname;
}

}  // namespace

// With PKTOOL_DUMP_KBD_SAVES=<dir> the Japanese and Korean saves are written there
// (for pktool_preview with PKTOOL_PREVIEW_KEYBOARD=1).
TEST(kbcjk_dump_saves_for_the_preview) {
    const char* dir = std::getenv("PKTOOL_DUMP_KBD_SAVES");
    if (!dir) return;
    CHECK(io::safeWriteFile(io::joinPath(dir, "jp.sav"), languageSave(1)) == io::WriteResult::Ok);
    CHECK(io::safeWriteFile(io::joinPath(dir, "kr.sav"), languageSave(text::kLanguageKorean)) == io::WriteResult::Ok);
}

TEST(kbcjk_hangul_composes_and_decomposes) {
    // 피카츄
    CHECK(typed(Kind::Korean, {kP, kI, kK, kA, kCh, kYu}) == kPika);
    // 값: the final cluster ㅂㅅ, then a vowel pulls the ㅅ into a new syllable (갑사).
    std::string text = typed(Kind::Korean, {kG, kA, kB, kS});
    CHECK(text == "\xEA\xB0\x92");  // 값
    ui::kbcjk::type(Kind::Korean, text, kA, 5);
    CHECK(text == "\xEA\xB0\x91\xEC\x82\xAC");  // 갑사
    ui::kbcjk::backspace(Kind::Korean, text);   // 사 -> ㅅ
    ui::kbcjk::backspace(Kind::Korean, text);   // ㅅ -> gone
    CHECK(text == "\xEA\xB0\x91");              // 갑
    ui::kbcjk::backspace(Kind::Korean, text);
    CHECK(text == "\xEA\xB0\x80");  // 가
    ui::kbcjk::backspace(Kind::Korean, text);
    CHECK(text == kG);
    ui::kbcjk::backspace(Kind::Korean, text);
    CHECK(text.empty());
    // Compound vowel: ㅇ ㅗ ㅏ = 와, deleted back to 오 and ㅇ.
    text = typed(Kind::Korean, {kNg, kO, kA});
    CHECK(text == "\xEC\x99\x80");
    ui::kbcjk::backspace(Kind::Korean, text);
    CHECK(text == "\xEC\x98\xA4");
    // The length limit counts syllables: composing inside the last one is free.
    text = typed(Kind::Korean, {kG, kA, kG, kG}, 1);
    CHECK(text == "\xEA\xB0\x81");  // 각 -> the second ㄱ makes no final cluster: rejected, stays 각
    CHECK(!ui::kbcjk::type(Kind::Korean, text, kNg, 1));
}

TEST(kbcjk_kana_marks_and_limits) {
    CHECK(typed(Kind::Japanese, {"\xE3\x81\x8B", kDakuten}) == "\xE3\x81\x8C");              // か゛ -> が
    CHECK(typed(Kind::Japanese, {"\xE3\x81\x8B", kDakuten, kDakuten}) == "\xE3\x81\x8B");    // toggles back
    CHECK(typed(Kind::Japanese, {"\xE3\x81\xB2", kHandakuten}) == "\xE3\x81\xB4");           // ひ゜ -> ぴ
    CHECK(typed(Kind::Japanese, {"\xE3\x83\x8F", kHandakuten}) == "\xE3\x83\x91");           // ハ゜ -> パ
    CHECK(typed(Kind::Japanese, {"\xE3\x82\xA6", kDakuten}) == "\xE3\x83\xB4");              // ウ゛ -> ヴ
    CHECK(typed(Kind::Japanese, {"\xE3\x81\x82", kHandakuten}).size() == 3);                  // あ゜: unchanged
    CHECK(typed(Kind::Japanese, {kDakuten}).empty());                                          // nothing to modify
    CHECK(typed(Kind::Japanese, {"\xE3\x81\x82", "\xE3\x81\x84", "\xE3\x81\x86"}, 2).size() == 6);  // limit 2
    // Empty keys type nothing; the pages are 4 rows of 10 characters.
    std::string text;
    CHECK(!ui::kbcjk::type(Kind::Japanese, text, " ", 5));
    for (Kind kind : {Kind::Japanese, Kind::Korean}) {
        for (int page = 0; page < ui::kbcjk::pageCount(kind); ++page) {
            for (int row = 0; row < 4; ++row) {
                const std::string chars = ui::kbcjk::pageRow(kind, page, row);
                size_t count = 0;
                for (unsigned char c : chars) count += (c & 0xC0) != 0x80 ? 1 : 0;
                CHECK_EQ(count, 10u);
            }
        }
    }
}

TEST(kbcjk_game_storage_check) {
    const std::string pika = "\xE3\x83\x94\xE3\x82\xAB";  // ピカ
    CHECK(ui::kbcjk::storable(3, 1, pika));
    CHECK(ui::kbcjk::storable(4, 1, pika));
    CHECK(ui::kbcjk::storable(5, 1, pika));
    CHECK(ui::kbcjk::storable(4, 8, kPika));
    CHECK(ui::kbcjk::storable(5, 8, kPika));
    CHECK(!ui::kbcjk::storable(4, 1, kPika));  // Hangul in a Japanese game
    CHECK(!ui::kbcjk::storable(3, 1, kPika));
}

TEST(kbcjk_japanese_nickname_round_trips_through_a_gen4_save) {
    // ぴかちゅう (ひ ゜ か ち ゅ う), then a sixth character the 5-letter limit refuses.
    const std::string hi = "\xE3\x81\xB2", ka = "\xE3\x81\x8B", chi = "\xE3\x81\xA1", yu = "\xE3\x82\x85", u = "\xE3\x81\x86";
    std::string nickname = nicknameThroughSave(
        1, {{0, hi}, {1, kHandakuten}, {0, ka}, {0, chi}, {1, yu}, {0, u}, {0, ka}}, Kind::Japanese, nullptr);
    CHECK(nickname == "\xE3\x81\xB4\xE3\x81\x8B\xE3\x81\xA1\xE3\x82\x85\xE3\x81\x86");
}

TEST(kbcjk_katakana_and_korean_nicknames_round_trip) {
    // ピカ in katakana (page 2 and 3), typed after switching pages with SELECT.
    const std::string hi = "\xE3\x83\x92", ka = "\xE3\x82\xAB";
    std::string nickname = nicknameThroughSave(1, {{2, hi}, {3, kHandakuten}, {2, ka}}, Kind::Japanese, nullptr);
    CHECK(nickname == "\xE3\x83\x94\xE3\x82\xAB");
    // 피카츄 in a Korean save, jamo by jamo on the Hangul page.
    std::string korean = nicknameThroughSave(
        text::kLanguageKorean, {{0, kP}, {0, kI}, {0, kK}, {0, kA}, {0, kCh}, {0, kYu}}, Kind::Korean, nullptr);
    CHECK(korean == kPika);
}
