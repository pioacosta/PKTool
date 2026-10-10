#include <string>

#include "core/text/gen4_text.h"
#include "core/util/bytes.h"
#include "tests/test.h"

using namespace pktool;

TEST(gen4_text_round_trip) {
    for (const std::string& s : {std::string("PKTool"), std::string("Mr. Mime"), std::string("Nidoran\xE2\x99\x80"),
                                 std::string("Pok\xC3\xA9mon")}) {
        uint8_t buffer[22] = {};
        text::encodeGen4(s, buffer, 11, 10);
        CHECK(text::decodeGen4(buffer, 11) == s);
    }
}

TEST(gen4_text_known_glyphs) {
    uint8_t buffer[4] = {};
    text::encodeGen4("A", buffer, 2, 1);
    CHECK_EQ(util::readU16(buffer), 0x012B);
    CHECK_EQ(util::readU16(buffer + 2), text::kGen4Terminator);
}

TEST(gen4_text_truncates_and_terminates) {
    uint8_t buffer[8];
    for (uint8_t& b : buffer) b = 0xAA;
    size_t written = text::encodeGen4("ABCDEFG", buffer, 4, 10);
    CHECK_EQ(written, 3u);
    CHECK_EQ(util::readU16(buffer + 6), text::kGen4Terminator);
    CHECK(text::decodeGen4(buffer, 4) == "ABC");
}

TEST(gen4_text_unknown_glyph_decodes_as_question_mark) {
    uint8_t buffer[4];
    util::writeU16(buffer, 0x0FFF);
    util::writeU16(buffer + 2, text::kGen4Terminator);
    CHECK(text::decodeGen4(buffer, 2) == "?");
}

TEST(gen3_text_latin_name_fits_the_japanese_table) {
    // Gen III eggs are always Japanese: a Latin OT name is stored with the
    // Japanese table's full-width letters (same byte values) instead of lost.
    uint8_t jp[8];
    CHECK_EQ(text::encodeGen3("PATE", jp, 8, 7, 1), 4u);
    uint8_t intl[8];
    CHECK_EQ(text::encodeGen3("PATE", intl, 8, 7, 2), 4u);
    for (int i = 0; i < 4; ++i) CHECK_EQ(jp[i], intl[i]);
    CHECK(text::decodeGen3(jp, 7, 2) == "PATE");
}
