#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Generation IV string encoding (also used by the DS-era Gen V games for most
// characters). Strings are arrays of 16-bit glyph values terminated by 0xFFFF.
namespace pktool::text {

constexpr uint16_t kGen4Terminator = 0xFFFF;

// Decodes up to `maxChars` glyphs into UTF-8. Unknown glyphs become '?'.
std::string decodeGen4(const uint8_t* data, size_t maxChars);

// Korean games (glyph values 0x400-0xD65) are decoded too.
//
// Encodes UTF-8 into glyphs, writing at most `maxChars` characters plus a
// terminator into a buffer of `bufferChars` 16-bit slots. Slots after the
// terminator are left untouched. Characters without a glyph become '?'.
// Returns the number of characters written (excluding the terminator).
// ♂ and ♀ use the half-width glyphs, except in a full-width (Japanese or
// Hangul-less East Asian) string of a non-Korean `language` (PKHeX
// StringConverter4.SetString); language 0 = unknown, judged by the text only.
constexpr uint8_t kLanguageKorean = 8;
size_t encodeGen4(const std::string& utf8, uint8_t* data, size_t bufferChars, size_t maxChars, uint8_t language = 0);

// Generation V encoding: UTF-16LE terminated by 0xFFFF (or 0), with the
// half-width gender symbols at U+246D/U+246E. Same parameters as above.
std::string decodeGen5(const uint8_t* data, size_t maxChars);
size_t encodeGen5(const std::string& utf8, uint8_t* data, size_t bufferChars, size_t maxChars);

// Generation III encoding: one byte per character, terminated by 0xFF (a
// full-length string has no terminator). Japanese games (language 1) use
// their own character set. Unknown characters become '?' when decoding and
// are skipped when encoding. encodeGen3 writes at most `maxChars` characters
// and a terminator if it fits in `bufferChars`; later bytes are untouched.
constexpr uint8_t kGen3Terminator = 0xFF;
std::string decodeGen3(const uint8_t* data, size_t maxChars, uint8_t language);
size_t encodeGen3(const std::string& utf8, uint8_t* data, size_t bufferChars, size_t maxChars, uint8_t language);

namespace generated {
extern const uint16_t kGen4CharTable[];
extern const int kGen4CharTableCount;
// Korean games: Hangul syllables at kGen4KorStart and above.
extern const int kGen4KorStart;
extern const uint16_t kGen4KorTable[];
extern const int kGen4KorTableCount;
}  // namespace generated

}  // namespace pktool::text
