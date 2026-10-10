// Japanese and Korean Gen IV: saves built in memory (no real saves exist),
// following PKHeX's SAV4 (same offsets for every language, the Korean footer
// magic, the language byte at trainer + 0x19) and StringConverter4 (Korean
// glyph table at 0x400-0xD65, half/full-width gender symbols).

#include <cstring>
#include <string>
#include <vector>

#include "core/pokemon/entity.h"
#include "core/pokemon/pk4.h"
#include "core/saves/gen4_save.h"
#include "core/text/gen4_text.h"
#include "core/util/bytes.h"
#include "tests/fake_saves.h"
#include "tests/test.h"

using namespace pktool;

namespace {

constexpr uint32_t kMagicKorean = 0x20070903;

// A blank save of `layout` for a language: the trainer name, the language
// byte and (Korean releases) the other footer magic.
std::vector<uint8_t> languageSave(const fake::Gen4Layout& layout, uint8_t language, const std::string& name) {
    std::vector<uint8_t> data = fake::blankGen4(layout);
    for (int partition = 0; partition < 2; ++partition) {
        uint8_t* general = &data[partition * saves::Gen4Save::kPartitionSize];
        std::memset(general + layout.trainer, 0, 16);
        text::encodeGen4(name, general + layout.trainer, 8, 7, language);
        general[layout.trainer + 0x19] = language;
        // The checksums cover the trainer; the magic sits in the unchecked footer.
        fake::writeFooter(general, layout.generalSize, layout.footerSize);
        fake::writeFooter(general + layout.storageOffset, layout.storageSize, layout.footerSize);
        if (language == text::kLanguageKorean) {
            util::writeU32(general + layout.generalSize - 8, kMagicKorean);
            util::writeU32(general + layout.storageOffset + layout.storageSize - 8, kMagicKorean);
        }
    }
    return data;
}

uint16_t glyphAt(const uint8_t* data, int index) { return util::readU16(data + index * 2); }

}  // namespace

TEST(gen4_text_korean_glyph_table) {
    // Spot values from PKHeX's TableKOR (0x400 is the first, 0xD65 the last).
    uint8_t buffer[8] = {};
    util::writeU16(buffer, 0x401);
    util::writeU16(buffer + 2, 0xD31);
    util::writeU16(buffer + 4, 0xD65);
    util::writeU16(buffer + 6, 0xFFFF);
    CHECK(text::decodeGen4(buffer, 4) == "\xEA\xB0\x80\xE3\x84\xB1\xEC\xAC\xAC");  // 가 ㄱ 쬬
    // Unused values stay '?'.
    util::writeU16(buffer, 0x400);
    CHECK(text::decodeGen4(buffer, 1) == "?");
    util::writeU16(buffer, 0xD66);
    CHECK(text::decodeGen4(buffer, 1) == "?");

    // Every Korean character round-trips through encode and decode.
    int checked = 0;
    for (int v = 1; v < text::generated::kGen4KorTableCount; ++v) {
        const uint16_t cp = text::generated::kGen4KorTable[v];
        if (cp == 0xFFFF) continue;
        std::string utf8;
        utf8 += static_cast<char>(0xE0 | (cp >> 12));
        utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        utf8 += static_cast<char>(0x80 | (cp & 0x3F));
        uint8_t out[4] = {};
        text::encodeGen4(utf8, out, 2, 1, text::kLanguageKorean);
        CHECK_EQ(glyphAt(out, 0), text::generated::kGen4KorStart + v);
        CHECK(text::decodeGen4(out, 2) == utf8);
        ++checked;
    }
    CHECK(checked > 2300);
    CHECK_EQ(text::generated::kGen4KorTableCount, 0x966);
}

TEST(gen4_text_gender_symbols_follow_the_string_width) {
    uint8_t buffer[24] = {};
    const std::string male = "\xE2\x99\x82";
    // Latin and Korean strings use the half-width symbol (0x1BB), whatever the language.
    text::encodeGen4("NIDORAN" + male, buffer, 12, 10, 2);
    CHECK_EQ(glyphAt(buffer, 7), 0x1BB);
    text::encodeGen4("\xED\x94\xBC" + male, buffer, 12, 10, text::kLanguageKorean);
    CHECK_EQ(glyphAt(buffer, 1), 0x1BB);
    // A kana (full-width) name of a Japanese game keeps the full-width symbol (0xEE).
    text::encodeGen4("\xE3\x83\x8B\xE3\x83\x89\xE3\x83\xA9\xE3\x83\xB3" + male, buffer, 12, 10, 1);
    CHECK_EQ(glyphAt(buffer, 4), 0xEE);
    // Both read back as the same character.
    CHECK(text::decodeGen4(buffer, 12) == "\xE3\x83\x8B\xE3\x83\x89\xE3\x83\xA9\xE3\x83\xB3" + male);
    // Korean Hangul in a Korean game: half-width even though the string is full-width.
    text::encodeGen4("\xEA\xB0\x80" + male, buffer, 12, 10, text::kLanguageKorean);
    CHECK_EQ(glyphAt(buffer, 1), 0x1BB);
}

TEST(gen4_korean_and_japanese_saves_load_and_edit) {
    const std::string korean = "\xED\x94\xBC\xEC\xB9\xB4\xEC\xB8\x84";  // 피카츄
    const std::string japanese = "\xE3\x83\x92\xE3\x82\xAB\xE3\x83\xAA";  // ヒカリ
    for (const fake::Gen4Layout* layout : {&fake::kDp, &fake::kPt, &fake::kHgss}) {
        for (uint8_t language : {uint8_t(1), uint8_t(text::kLanguageKorean)}) {
            const std::string& name = language == 1 ? japanese : korean;
            saves::Gen4Save save;
            CHECK(save.load(languageSave(*layout, language, name)) == saves::Gen4Save::LoadResult::Ok);
            CHECK(save.game() == layout->game);
            CHECK(save.checksumsValid());
            CHECK_EQ(save.language(), language);
            CHECK(save.trainerName() == name);
            CHECK_EQ(save.trainerId(), 12345);

            // Edit the trainer and a box name in the save's own language.
            saves::TrainerData trainer = save.trainerData();
            trainer.name = language == 1 ? "\xE3\x82\xB5\xE3\x83\x88\xE3\x82\xB7" : "\xEB\xA0\x88\xEB\x93\x9C";  // サトシ / 레드
            std::string error;
            CHECK(save.setTrainerData(trainer, &error));
            CHECK(save.trainerName() == trainer.name);
            CHECK(save.setBoxName(3, name));
            CHECK(save.boxName(3) == name);
            save.refreshChecksums();
            CHECK(save.checksumsValid());

            // The edited save loads again with the same data.
            saves::Gen4Save again;
            CHECK(again.load(save.data()) == saves::Gen4Save::LoadResult::Ok);
            CHECK(again.trainerName() == trainer.name);
            CHECK(again.boxName(3) == name);
            CHECK_EQ(again.language(), language);
        }
    }
}

TEST(gen4_korean_pokemon_names_round_trip) {
    for (uint8_t language : {uint8_t(1), uint8_t(text::kLanguageKorean)}) {
        pokemon::Pokemon p;
        p.format = 4;
        p.species = 25;
        p.language = language;
        p.isNicknamed = true;
        p.nickname = language == 1 ? "\xE3\x83\x94\xE3\x82\xAB\xE3\x83\x81\xE3\x83\xA5\xE3\x82\xA6"
                                   : "\xED\x94\xBC\xEC\xB9\xB4\xEC\xB8\x84";
        p.trainer.name = language == 1 ? "\xE3\x82\xB5\xE3\x83\x88\xE3\x82\xB7" : "\xEB\xA0\x88\xEB\x93\x9C";
        uint8_t raw[pokemon::entity::kMaxPartySize] = {};
        pokemon::entity::apply(p, raw);
        pokemon::Pokemon q = pokemon::entity::decode(4, raw, pokemon::pk4::kStoredSize);
        CHECK(q.nickname == p.nickname);
        CHECK(q.trainer.name == p.trainer.name);
        CHECK_EQ(q.language, language);
    }
}
