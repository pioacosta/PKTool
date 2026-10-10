// Japanese/Korean text (the CJK fallback font) and the language of the names
// (language packs, sorting per language, default nicknames, the setting).

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/data/events3.h"
#include "core/data/gen3.h"
#include "core/data/items.h"
#include "core/data/language.h"
#include "core/data/names.h"
#include "core/data/sorted_names.h"
#include "core/io/file_io.h"
#include "core/io/settings.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/edit4.h"
#include "core/pokemon/gift3.h"
#include "core/pokemon/pk3.h"
#include "core/showdown/showdown.h"
#include "core/text/gen4_text.h"
#include "ds/app/app.h"
#include "ds/gfx/canvas.h"
#include "ds/gfx/cjk_font.h"
#include "tests/test.h"

using namespace pktool;

namespace {

const std::string kAssets = std::string(PKTOOL_SOURCE_DIR) + "/assets/nitrofs";

// Leaves the process-wide language and font state as other tests expect it.
struct Reset {
    ~Reset() {
        data::setNameLanguage(data::kLangEnglish);
        data::setLanguageDirs({});
        gfx::closeCjkFont();
        data::setOrder(data::Order::Alphabetical);
    }
};

int litPixels(const gfx::Font& font, const std::string& text) {
    std::vector<uint16_t> pixels(gfx::kScreenWidth * gfx::kScreenHeight, 0);
    gfx::Canvas canvas(pixels.data());
    canvas.text(font, 0, 0, text, gfx::rgb(255, 255, 255));
    int count = 0;
    for (uint16_t p : pixels) count += p != 0 ? 1 : 0;
    return count;
}

std::string makeDir() {
    char pattern[] = "/tmp/pktool-lang-XXXXXX";
    return mkdtemp(pattern);
}

void press(ui::App& app, uint32_t keys) {
    static std::vector<uint16_t> top(gfx::kScreenWidth * gfx::kScreenHeight);
    static std::vector<uint16_t> bottom(gfx::kScreenWidth * gfx::kScreenHeight);
    ui::Input in;
    in.pressed = keys;
    in.repeated = keys;
    app.update(in);
    app.update(ui::Input());
    gfx::Canvas t(top.data());
    gfx::Canvas b(bottom.data());
    app.draw(t, b);
}

}  // namespace

TEST(cjk_font_draws_japanese_and_korean) {
    Reset reset;
    const std::string kana = "\xE3\x83\x94\xE3\x82\xAB\xE3\x83\x81\xE3\x83\xA5\xE3\x82\xA6";           // ピカチュウ
    const std::string hangul = "\xED\x94\xBC\xEC\xB9\xB4\xEC\xB8\x84";                                  // 피카츄
    const std::string kanji = "\xE5\x86\x86\xE6\x97\xA5\xE6\x9C\xAC";                                    // 円日本
    const std::string question(kana.size() / 3, '?');

    // Without the font every such character is drawn as "?".
    CHECK(!gfx::cjkFontAvailable());
    CHECK(!gfx::Canvas::hasGlyph(gfx::kFontSmall, 0x30D4));
    int missing = litPixels(gfx::kFontSmall, kana);
    CHECK_EQ(missing, litPixels(gfx::kFontSmall, question));

    CHECK(!gfx::openCjkFont({"/tmp/pktool-no-such-dir"}));
    CHECK(gfx::openCjkFont({kAssets}));
    CHECK(gfx::cjkFontAvailable());
    for (uint32_t cp : {0x30D4u, 0x30ABu, 0x3042u, 0xFF21u, 0x5186u, 0x65E5u, 0xD53Cu, 0xCE74u, 0xCE04u, 0x2605u}) {
        CHECK(gfx::Canvas::hasGlyph(gfx::kFontSmall, cp));
        CHECK(gfx::Canvas::hasGlyph(gfx::kFontLarge, cp));
    }
    CHECK(!gfx::Canvas::hasGlyph(gfx::kFontSmall, 0x1F600));  // not a game character
    for (const std::string& text : {kana, hangul, kanji}) {
        CHECK(litPixels(gfx::kFontSmall, text) > litPixels(gfx::kFontSmall, std::string(text.size() / 3, '.')));
        CHECK(litPixels(gfx::kFontSmall, text) != litPixels(gfx::kFontSmall, std::string(text.size() / 3, '?')));
        CHECK_EQ(gfx::Canvas::textWidth(gfx::kFontSmall, text), static_cast<int>(text.size() / 3 * 12));  // full width
    }
    // Latin text is untouched, and mixed text keeps the Latin widths.
    CHECK_EQ(litPixels(gfx::kFontSmall, "Pikachu"), litPixels(gfx::kFontSmall, "Pikachu"));
    CHECK_EQ(gfx::Canvas::textWidth(gfx::kFontSmall, "A" + kana),
             gfx::Canvas::textWidth(gfx::kFontSmall, "A") + 60);
    // Half-width katakana take half a cell.
    CHECK_EQ(gfx::Canvas::textWidth(gfx::kFontSmall, "\xEF\xBD\xB1"), 6);
    // Ellipsizing works on whole characters.
    std::string cut = gfx::Canvas::ellipsize(gfx::kFontSmall, kana, 40);
    CHECK(gfx::Canvas::textWidth(gfx::kFontSmall, cut) <= 40);
    CHECK(cut.size() >= 3 && cut.compare(cut.size() - 3, 3, "...") == 0);

    gfx::closeCjkFont();
    CHECK_EQ(litPixels(gfx::kFontSmall, kana), missing);
}

TEST(cjk_font_covers_every_game_character) {
    Reset reset;
    CHECK(gfx::openCjkFont({kAssets}));
    // Gen IV international table (kana, full-width forms, symbols) and the Korean table.
    for (int v = 1; v < text::generated::kGen4CharTableCount; ++v) {
        uint32_t cp = text::generated::kGen4CharTable[v];
        if (cp == 0xFFFF || cp <= 0xFF || cp == 0x2642 || cp == 0x2640 || cp == 0x246D || cp == 0x246E) continue;
        CHECK(gfx::Canvas::hasGlyph(gfx::kFontSmall, cp));
    }
    int hangul = 0;
    for (int v = 1; v < text::generated::kGen4KorTableCount; ++v) {
        if (text::generated::kGen4KorTable[v] == 0xFFFF) continue;  // unused value
        CHECK(gfx::Canvas::hasGlyph(gfx::kFontSmall, text::generated::kGen4KorTable[v]));
        ++hangul;
    }
    CHECK(hangul > 2000);
    for (const uint16_t* table : {data::generated::kGen3CharsInt, data::generated::kGen3CharsJpn}) {
        for (int v = 0; v < 256; ++v) {
            uint32_t cp = table[v];
            if (cp == 0xFFFF || cp <= 0xFF || cp == 0x2642 || cp == 0x2640 || cp == 0x246D || cp == 0x246E) continue;
            CHECK(gfx::Canvas::hasGlyph(gfx::kFontSmall, cp));
        }
    }
}

TEST(gen4_korean_text_decodes_and_encodes) {
    uint8_t data[8] = {};
    data[0] = 0x01;  // first Hangul syllable of the Korean table
    data[1] = 0x04;
    data[2] = 0x02;
    data[3] = 0x04;
    data[4] = 0xFF;
    data[5] = 0xFF;
    std::string decoded = text::decodeGen4(data, 4);
    CHECK(decoded == "\xEA\xB0\x80\xEA\xB0\x81");  // U+AC00 U+AC01
    uint8_t out[12];
    std::memset(out, 0, sizeof out);
    CHECK_EQ(text::encodeGen4(decoded, out, 6, 5), 2u);
    CHECK(out[0] == 0x01 && out[1] == 0x04 && out[2] == 0x02 && out[3] == 0x04);
    CHECK(out[4] == 0xFF && out[5] == 0xFF);
}

TEST(language_packs_load_and_fall_back) {
    Reset reset;
    CHECK_EQ(data::nameLanguage(), data::kLangEnglish);
    CHECK(std::string(data::speciesName(25)) == "Pikachu");

    // No pack directory: only English works.
    CHECK(!data::setNameLanguage(data::kLangJapanese));
    CHECK(!data::languageAvailable(data::kLangFrench));
    CHECK_EQ(data::nameLanguage(), data::kLangEnglish);
    CHECK(std::string(data::speciesNameIn(data::kLangGerman, 25)) == "Pikachu");  // falls back

    data::setLanguageDirs({kAssets});
    CHECK(data::languageAvailable(data::kLangFrench));
    CHECK(!data::setNameLanguage(0));
    CHECK(!data::setNameLanguage(6));  // not a language of the games' name lists

    CHECK(data::setNameLanguage(data::kLangJapanese));
    CHECK_EQ(data::nameLanguage(), data::kLangJapanese);
    CHECK(std::string(data::speciesName(25)) == "\xE3\x83\x94\xE3\x82\xAB\xE3\x83\x81\xE3\x83\xA5\xE3\x82\xA6");
    CHECK(std::string(data::speciesName(0)) == "(None)");  // ID 0 stays English
    CHECK(std::string(data::speciesName(9999)) == "???");
    CHECK(std::string(data::speciesNameEn(25)) == "Pikachu");
    CHECK(std::string(data::moveName(85)) != std::string(data::moveNameEn(85)));
    CHECK(std::string(data::natureName(0)) != std::string(data::natureNameEn(0)));
    CHECK(std::string(data::abilityName(9)) != std::string(data::abilityNameEn(9)));
    CHECK(std::string(data::itemName(234)) != std::string(data::itemNameEn(234)));
    CHECK(std::string(data::itemNameGen3(200)) != "Leftovers");

    // Names that mean "no such ID" stay "???" in every language, so validity checks do not change.
    for (uint8_t lang : {data::kLangJapanese, data::kLangFrench, data::kLangItalian, data::kLangGerman,
                         data::kLangSpanish, data::kLangKorean}) {
        CHECK(data::setNameLanguage(lang));
        for (uint16_t item = 0; item <= 638; ++item) {
            CHECK_EQ(std::string(data::itemName(item)) == "???", std::string(data::itemNameEn(item)) == "???");
        }
        for (uint16_t species = 1; species <= data::kMaxSpeciesGen5; ++species) {
            CHECK(data::speciesName(species)[0] != '\0');
        }
        CHECK(std::string(data::ballName(4)) == data::itemName(4));
        CHECK(std::string(data::ballName(17)) == data::itemName(492));  // Fast Ball
        CHECK(std::string(data::ballName(25)) == data::itemName(500));  // Park Ball
    }
    // The ball ID -> item mapping lands on the English ball names too.
    CHECK(std::string(data::itemNameEn(492)) == "Fast Ball");
    CHECK(std::string(data::itemNameEn(500)) == "Park Ball");
    CHECK(std::string(data::itemNameEn(16)) == "Cherish Ball");

    CHECK(data::setNameLanguage(data::kLangFrench));
    CHECK(std::string(data::speciesName(1)) == "Bulbizarre");
    CHECK(data::setNameLanguage(data::kLangKorean));
    CHECK(std::string(data::speciesName(25)) == "\xED\x94\xBC\xEC\xB9\xB4\xEC\xB8\x84");
    CHECK(data::setNameLanguage(data::kLangEnglish));
    CHECK(std::string(data::speciesName(25)) == "Pikachu");
    CHECK(std::string(data::ballName(4)) == "Poke Ball");

    // A pack that cannot be read leaves the language as it was.
    CHECK(data::setNameLanguage(data::kLangGerman));
    data::setLanguageDirs({"/tmp/pktool-no-such-dir"});
    CHECK(!data::setNameLanguage(data::kLangItalian));
    CHECK_EQ(data::nameLanguage(), data::kLangGerman);
}

TEST(showdown_text_stays_english_in_any_language) {
    Reset reset;
    data::setLanguageDirs({kAssets});
    CHECK(data::setNameLanguage(data::kLangJapanese));
    CHECK_EQ(showdown::findSpecies("Pikachu"), 25);
    CHECK_EQ(showdown::findMove("Thunderbolt"), 85);
    CHECK_EQ(showdown::findSpecies(data::speciesName(25)), 0);  // the Japanese name is not Showdown text
    pokemon::Pokemon p;
    p.species = 25;
    p.moves[0] = 85;
    p.level = 50;
    std::string text = showdown::format(p);
    CHECK(text.find("Pikachu") == 0);
    CHECK(text.find("- Thunderbolt") != std::string::npos);
}

TEST(sorted_names_per_language) {
    Reset reset;
    data::setLanguageDirs({kAssets});

    // Accents fold (French, Spanish), full-width letters too.
    CHECK(data::foldName("\xC3\x89""cr\xC3\xA9meur") == "ecremeur");
    CHECK(data::foldName("Espa\xC3\xB1ol") == "espanol");
    CHECK(data::foldName("Stra\xC3\x9F""e") == "strase");
    CHECK(data::foldName("\xEF\xBC\xA1\xEF\xBC\xA2") == "ab");
    CHECK_EQ(data::initialOf("\xC3\x89""cr\xC3\xA9meur"), 'e');

    // Kana: katakana, voiced and small kana sort like their plain hiragana.
    CHECK(data::foldName("\xE3\x82\xAC") == data::foldName("\xE3\x81\x8B"));  // ガ = か
    CHECK(data::foldName("\xE3\x83\x83") == data::foldName("\xE3\x81\xA4"));  // ッ = つ
    CHECK(data::foldName("\xE3\x83\x91") == data::foldName("\xE3\x81\xAF"));  // パ = は
    CHECK(data::foldName("\xE3\x83\x95\xE3\x82\x9A") != "");
    CHECK(data::foldName("\xE3\x82\xA2") < data::foldName("\xE3\x82\xAB"));    // ア < カ
    CHECK_EQ(data::initialOf("\xE3\x82\xAC"), data::initialOf("\xE3\x82\xAD"));  // ガ and キ: the ka row
    CHECK(data::initialOf("\xE3\x82\xA2") != data::initialOf("\xE3\x82\xAB"));

    // Hangul: the initial consonant is the group.
    CHECK_EQ(data::initialOf("\xEA\xB0\x80"), data::initialOf("\xEA\xB9\x8C"));  // 가 and 까
    CHECK(data::initialOf("\xEA\xB0\x80") != data::initialOf("\xEB\x82\x98"));   // 가 and 나

    for (uint8_t lang : {data::kLangJapanese, data::kLangFrench, data::kLangSpanish, data::kLangGerman,
                         data::kLangKorean}) {
        CHECK(data::setNameLanguage(lang));
        const std::vector<uint16_t>& ids = data::sortedIds(data::NameKind::Species, 5);
        CHECK_EQ(ids.size(), 649u);
        for (size_t i = 1; i < ids.size(); ++i) {
            CHECK(data::foldName(data::speciesName(ids[i - 1])) <= data::foldName(data::speciesName(ids[i])));
        }
        // The cached view follows the language.
        CHECK(ids[0] != 1 || lang == data::kLangEnglish);
        data::setOrder(data::Order::ById);
        CHECK_EQ(data::sortedIds(data::NameKind::Species, 5)[0], 1);
        data::setOrder(data::Order::Alphabetical);
    }
    CHECK(data::setNameLanguage(data::kLangEnglish));
    CHECK_EQ(data::sortedIds(data::NameKind::Species, 5)[0], 460);  // Abomasnow
    CHECK(data::setNameLanguage(data::kLangJapanese));
    int before = data::sortedIndex(data::NameKind::Species, 5, 25);
    CHECK(before >= 0);
    // Jumping by initial moves to the first entry of another kana row.
    uint16_t next = data::jumpSorted(data::NameKind::Species, 5, 25, 1);
    CHECK(data::initialOf(data::speciesName(next)) != data::initialOf(data::speciesName(25)));
    CHECK(data::setNameLanguage(data::kLangEnglish));
    CHECK(data::sortedIndex(data::NameKind::Species, 5, 25) != before);
}

TEST(default_nicknames_follow_the_pokemon_language) {
    Reset reset;
    data::setLanguageDirs({kAssets});
    // English and the apostrophe of Farfetch'd.
    CHECK(data::defaultNickname(25, data::kLangEnglish, 4) == "PIKACHU");
    CHECK(data::defaultNickname(25, data::kLangEnglish, 3) == "PIKACHU");
    CHECK(data::defaultNickname(25, data::kLangEnglish, 5) == "Pikachu");  // Gen V keeps the case
    CHECK(data::defaultNickname(83, data::kLangEnglish, 4) == "FARFETCH'D");
    // Japanese and Korean are never in capitals.
    CHECK(data::defaultNickname(25, data::kLangJapanese, 4) ==
          "\xE3\x83\x94\xE3\x82\xAB\xE3\x83\x81\xE3\x83\xA5\xE3\x82\xA6");
    CHECK(data::defaultNickname(25, data::kLangKorean, 5) == "\xED\x94\xBC\xEC\xB9\xB4\xEC\xB8\x84");
    CHECK(data::defaultNickname(1, data::kLangFrench, 4) == "BULBIZARRE");
    CHECK(data::defaultNickname(1, data::kLangGerman, 3) == "BISASAM");
    // French Gen III/IV names lose the accents the old games lack.
    for (uint16_t species = 1; species <= data::kMaxSpeciesGen4; ++species) {
        std::string name = data::defaultNickname(species, data::kLangFrench, 4);
        for (size_t i = 0; i + 1 < name.size(); ++i) {
            CHECK(!(static_cast<uint8_t>(name[i]) == 0xC3 && static_cast<uint8_t>(name[i + 1]) >= 0x88 &&
                    static_cast<uint8_t>(name[i + 1]) <= 0x8A));
            CHECK(!(static_cast<uint8_t>(name[i]) == 0xC3 && static_cast<uint8_t>(name[i + 1]) == 0x8F));
        }
    }
    // No pack: the English name (the game still shows something sensible).
    data::setLanguageDirs({"/tmp/pktool-no-such-dir"});
    CHECK(data::defaultNickname(25, data::kLangJapanese, 4) == "Pikachu");

    // The creator and the species editor use the Pokemon's own language.
    data::setLanguageDirs({kAssets});
    pokemon::create4::Trainer t;
    t.name = "PATE";
    t.tid = 1;
    t.sid = 2;
    t.version = 10;  // Diamond
    t.language = data::kLangGerman;
    t.today = {26, 9, 29};
    pokemon::Pokemon p = pokemon::create4::newPokemon(t, 1, 5, 7);
    CHECK(p.nickname == "BISASAM");
    CHECK(!p.isNicknamed);
    CHECK_EQ(pokemon::edit4::setSpecies(p, 4), pokemon::edit4::Result::Ok);
    CHECK(p.nickname == data::defaultNickname(4, data::kLangGerman, 4));
    CHECK(p.nickname != "CHARMANDER");
    t.language = data::kLangJapanese;
    p = pokemon::create4::newPokemon(t, 25, 5, 7);
    CHECK(p.nickname == "\xE3\x83\x94\xE3\x82\xAB\xE3\x83\x81\xE3\x83\xA5\xE3\x82\xA6");
}

TEST(gen3_default_names_and_events_use_the_pokemon_language) {
    Reset reset;
    data::setLanguageDirs({kAssets});
    // A Japanese Gen III Pokemon with the katakana species name is not nicknamed.
    pokemon::Pokemon jp;
    jp.format = 3;
    jp.species = 25;
    jp.language = data::kLangJapanese;
    jp.pid = 0x12345678;
    jp.trainer.name = "PATE";
    jp.nickname = data::defaultNickname(25, data::kLangJapanese, 3);
    uint8_t raw[pokemon::pk3::kPartySize];
    std::memset(raw, 0, sizeof raw);
    std::memset(raw + pokemon::pk3::offset::kNickname, 0xFF, pokemon::pk3::kNicknameChars);
    std::memset(raw + pokemon::pk3::offset::kOtName, 0xFF, pokemon::pk3::kOtNameChars);
    pokemon::pk3::applyPokemon(jp, raw);
    pokemon::Pokemon back = pokemon::pk3::toPokemon(raw, sizeof raw);
    CHECK_EQ(back.language, data::kLangJapanese);
    CHECK(back.nickname == jp.nickname);
    CHECK(!back.isNicknamed);
    jp.nickname = "\xE3\x83\x94\xE3\x82\xAB";
    pokemon::pk3::applyPokemon(jp, raw);
    CHECK(pokemon::pk3::toPokemon(raw, sizeof raw).isNicknamed);

    // Events received by a German trainer carry the German species name.
    pokemon::create4::Trainer t;
    t.name = "PATE";
    t.tid = 12345;
    t.sid = 54321;
    t.gender = pokemon::Gender::Female;
    t.version = 4;  // FireRed
    t.language = data::kLangGerman;
    t.today = {26, 9, 29};
    int checked = 0;
    for (int i = 0; i < data::events3Count() && checked < 6; ++i) {
        const data::Event3& e = data::event3(i);
        if (e.language != 0 || (e.flags & data::kEvEgg) != 0) continue;  // eggs are always Japanese
        uint8_t stored[pokemon::pk3::kStoredSize];
        pokemon::Pokemon p = pokemon::gift3::receive(e, t, 99, pokemon::gift3::Criteria(), stored);
        CHECK_EQ(p.language, data::kLangGerman);
        CHECK(p.nickname == data::defaultNickname(e.species, data::kLangGerman, 3));
        CHECK(!pokemon::pk3::toPokemon(stored, sizeof stored).isNicknamed);
        ++checked;
    }
    CHECK(checked > 0);
    // A Japanese-only event keeps its full-width English letters without the pack ...
    data::setLanguageDirs({"/tmp/pktool-no-such-dir"});
    for (int i = 0; i < data::events3Count(); ++i) {
        const data::Event3& e = data::event3(i);
        if (e.language != data::kLangJapanese) continue;
        uint8_t stored[pokemon::pk3::kStoredSize];
        pokemon::Pokemon p = pokemon::gift3::receive(e, t, 99, pokemon::gift3::Criteria(), stored);
        CHECK(static_cast<uint8_t>(p.nickname[0]) >= 0x80);
        break;
    }
    // ... and gets the katakana name with it.
    data::setLanguageDirs({kAssets});
    for (int i = 0; i < data::events3Count(); ++i) {
        const data::Event3& e = data::event3(i);
        if (e.language != data::kLangJapanese) continue;
        uint8_t stored[pokemon::pk3::kStoredSize];
        pokemon::Pokemon p = pokemon::gift3::receive(e, t, 99, pokemon::gift3::Criteria(), stored);
        CHECK(p.nickname == data::defaultNickname(e.species, data::kLangJapanese, 3));
        break;
    }
}

TEST(language_setting_text_and_screen) {
    Reset reset;
    io::Settings s;
    CHECK_EQ(s.language, data::kLangEnglish);
    CHECK(io::settingsToText(s).find("language") == std::string::npos);  // English: the file is as before
    s.language = data::kLangKorean;
    CHECK(io::settingsToText(s) == "# PKTool settings\nsort_order=alphabetical\nnew_level=50\nanimations=on\nlanguage=ko\n");
    io::Settings back;
    io::parseSettings(io::settingsToText(s), back);
    CHECK_EQ(back.language, data::kLangKorean);
    CHECK(back == s);
    io::parseSettings("language = JA \n", back);
    CHECK_EQ(back.language, data::kLangJapanese);
    io::parseSettings("language=xx\nlanguage=\nlanguage=klingon", back);
    CHECK_EQ(back.language, data::kLangJapanese);  // bad values are ignored

    // The Settings screen: change, leave (writes), restart (reads and applies).
    std::string dir = makeDir();
    ui::Config config;
    config.scanRoot = dir;
    config.dataDir = io::joinPath(dir, "PKTOOL");
    config.assetDirs = {kAssets};
    std::string file = io::joinPath(config.dataDir, "settings.ini");
    {
        ui::App app(config);
        CHECK_EQ(data::nameLanguage(), data::kLangEnglish);
        press(app, ui::kKeyDown);
        press(app, ui::kKeyRight);
        press(app, ui::kKeyA);  // Settings
        press(app, ui::kKeyDown);
        press(app, ui::kKeyDown);  // Names
        press(app, ui::kKeyRight);
        CHECK_EQ(data::nameLanguage(), data::kLangJapanese);
        CHECK(std::string(data::speciesName(25)) != "Pikachu");
        press(app, ui::kKeyLeft);
        CHECK_EQ(data::nameLanguage(), data::kLangEnglish);
        press(app, ui::kKeyLeft);  // wraps to the last language
        CHECK_EQ(data::nameLanguage(), data::kLangKorean);
        press(app, ui::kKeyRight);
        press(app, ui::kKeyRight);
        press(app, ui::kKeyRight);
        press(app, ui::kKeyRight);  // en ja fr it
        CHECK_EQ(data::nameLanguage(), data::kLangItalian);
        press(app, ui::kKeyR);  // fr ... one step further
        CHECK_EQ(data::nameLanguage(), data::kLangGerman);
        CHECK(!io::fileExists(file));
        press(app, ui::kKeyB);
        CHECK(io::fileExists(file));
    }
    CHECK(io::fileExists(file));
    {
        std::vector<uint8_t> bytes;
        CHECK(io::readFile(file, bytes));
        CHECK(std::string(bytes.begin(), bytes.end()).find("language=de\n") != std::string::npos);
    }
    data::setNameLanguage(data::kLangEnglish);
    {
        ui::App app(config);  // a new session applies the stored language
        CHECK_EQ(data::nameLanguage(), data::kLangGerman);
        CHECK(std::string(data::speciesName(1)) == "Bisasam");
    }
    // Without the packs the stored language falls back to English instead of failing.
    config.assetDirs = {"/tmp/pktool-no-such-dir"};
    {
        ui::App app(config);
        CHECK_EQ(data::nameLanguage(), data::kLangEnglish);
        press(app, ui::kKeyDown);
        press(app, ui::kKeyRight);
        press(app, ui::kKeyA);
        press(app, ui::kKeyDown);
        press(app, ui::kKeyDown);
        press(app, ui::kKeyRight);  // tries Japanese: no pack, stays English
        CHECK_EQ(data::nameLanguage(), data::kLangEnglish);
        press(app, ui::kKeyB);
    }
}

// Default nicknames need one language's species names only: those are read alone (a few KB, not
// the whole 35-52 KB pack), kept for a few languages and agree with the active-language pack.
TEST(language_aux_packs_hold_only_the_species_names) {
    Reset reset;
    data::setLanguageDirs({kAssets});
    io::resetReadStats();
    const char* ja25 = data::speciesNameIn(data::kLangJapanese, 25);
    CHECK(std::strcmp(ja25, "ピカチュウ") == 0);
    const uint64_t first = io::readStats().bytes;
    CHECK(first > 0 && first < 14 * 1024);  // ja.bin is 52 KB
    CHECK(data::speciesNameIn(data::kLangKorean, 25) != nullptr);
    CHECK(data::speciesNameIn(data::kLangFrench, 25) != nullptr);
    // Three languages are kept: asking again reads nothing.
    io::resetReadStats();
    CHECK(std::strcmp(data::speciesNameIn(data::kLangJapanese, 25), ja25) == 0);
    CHECK(data::speciesNameIn(data::kLangKorean, 150) != nullptr);
    CHECK(data::speciesNameIn(data::kLangFrench, 150) != nullptr);
    CHECK_EQ(io::readStats().bytes, 0u);
    // A species-only pack gives the names the full pack gives, for every species.
    std::vector<std::string> aux;
    for (uint8_t lang : {data::kLangJapanese, data::kLangKorean, data::kLangGerman, data::kLangSpanish}) {
        data::setLanguageDirs({kAssets});
        aux.clear();
        for (uint16_t s = 1; s <= data::kMaxSpeciesGen5; ++s) aux.push_back(data::speciesNameIn(lang, s));
        CHECK(data::setNameLanguage(lang));
        for (uint16_t s = 1; s <= data::kMaxSpeciesGen5; ++s) CHECK(aux[s - 1] == data::speciesName(s));
        CHECK(data::setNameLanguage(data::kLangEnglish));
    }
}
