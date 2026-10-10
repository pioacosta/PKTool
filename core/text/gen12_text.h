#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Generation I and II string encoding (PKHeX StringConverter1/2): one byte per
// character, terminated by 0x50. Japanese games (language 1) use their own
// character set, Gen II also has French/German and Italian/Spanish variants.
namespace pktool::text {

constexpr uint8_t kGb12Terminator = 0x50;
constexpr uint8_t kGb12TradeOt = 0x5D;  // OT of in-game trade Pokemon ("*" in UTF-8)

// Decodes up to `length` bytes of a Gen I (format 1) or Gen II (format 2)
// string to UTF-8. A string starting with the in-game trade code is "*".
// Bytes without a character end the string, like PKHeX. Gen II ligature
// characters (the 'd 'l 'm ... pairs) become apostrophe + letter.
std::string decodeGb(uint8_t format, const uint8_t* data, size_t length, uint8_t language);

// Encodes `utf8` into `data` (`bufferBytes` bytes, the whole buffer is first
// filled with terminators): at most `maxChars` characters, then one
// terminator if it fits. Characters without a code make the encoding fail
// (false) with the buffer left holding the part before them. "*" at the start
// is the in-game trade OT. A straight apostrophe is the games' apostrophe.
bool encodeGb(uint8_t format, const std::string& utf8, uint8_t* data, size_t bufferBytes, size_t maxChars,
              uint8_t language);

// True if every character of `utf8` has a code in the character set.
bool canEncodeGb(uint8_t format, const std::string& utf8, uint8_t language);

}  // namespace pktool::text
