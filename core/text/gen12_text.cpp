#include "core/text/gen12_text.h"

#include <cstring>

#include "core/data/gen12.h"

namespace pktool::text {
namespace {

constexpr uint16_t kUnused = 0xFFFF;
constexpr uint32_t kApostropheCurly = 0x2019;
constexpr uint32_t kDotLeader = 0x2024;  // the "." of MR.MIME

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

uint32_t nextUtf8(const std::string& s, size_t& i) {
    auto c = static_cast<unsigned char>(s[i++]);
    if (c < 0x80) return c;
    int extra = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 0;
    uint32_t cp = extra == 1 ? c & 0x1F : extra == 2 ? c & 0x0F : extra == 3 ? c & 0x07 : '?';
    for (int k = 0; k < extra && i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80; ++k) {
        cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 0x3F);
    }
    return cp;
}

// Gen II ligature characters: full-width digits 0-9, A, B stand for 'd 'l 'm ...
// (English, Italian, Spanish: apostrophe before the letter) or c' d' j' ...
// (French, German: after).
bool ligature(uint32_t cp, uint8_t language, std::string& out) {
    int index;
    if (cp >= 0xFF10 && cp <= 0xFF19) {
        index = static_cast<int>(cp - 0xFF10);
    } else if (cp == 0xFF21 || cp == 0xFF22) {
        index = static_cast<int>(cp - 0xFF21) + 10;
    } else {
        return false;
    }
    const bool after = language == 3 || language == 5;
    const char* letters = after ? "cdjlmnpsstuy" : "dlmrstv";
    if (index >= static_cast<int>(std::strlen(letters))) return false;
    const char letter = letters[index];
    if (after && index != 8) {
        out.push_back(letter);
        out.push_back('\'');
    } else {
        out.push_back('\'');
        out.push_back(letter);
    }
    return true;
}

}  // namespace

namespace {

constexpr uint8_t kKoreanLanguage = 8;
constexpr uint8_t kKoreanTableMax = 11;  // first bytes 1-11 start a two-byte Hangul character

bool isKorean(uint8_t format, uint8_t language) {
    return format == 2 && language == kKoreanLanguage;
}

// PKHeX StringConverter2KOR: bytes above 11 are single characters, 1-11 select a
// 256-entry Hangul table by the byte that follows; 0 or an unused entry ends the string.
std::string decodeKorean(const uint8_t* data, size_t length) {
    std::string out;
    for (size_t i = 0; i < length; ++i) {
        uint8_t value = data[i];
        if (value == 0) break;
        uint16_t cp;
        if (value > kKoreanTableMax) {
            cp = data::generated::kGen2KorSingle[value];
        } else {
            if (++i == length) break;
            cp = data::generated::kGen2KorHangul[(value - 1) * 256 + data[i]];
        }
        if (cp == kUnused) break;
        appendUtf8(out, cp);
    }
    return out;
}

// Whether a character has a code; `first`/`second` get its bytes (second = -1 for one byte).
bool koreanBytes(uint32_t cp, int& first, int& second) {
    for (int t = 0; t < kKoreanTableMax; ++t) {
        for (int b = 0; b < 256; ++b) {
            if (data::generated::kGen2KorHangul[t * 256 + b] == cp && cp != kUnused) {
                first = t + 1;
                second = b;
                return true;
            }
        }
    }
    for (int b = kKoreanTableMax + 1; b < 256; ++b) {
        if (b == kGb12Terminator || b == kGb12TradeOt) continue;
        if (data::generated::kGen2KorSingle[b] == cp && cp != kUnused) {
            first = b;
            second = -1;
            return true;
        }
    }
    return false;
}

bool encodeKorean(const std::string& utf8, uint8_t* data, size_t bufferBytes, size_t maxChars) {
    size_t used = 0;
    size_t count = 0;
    size_t i = 0;
    while (i < utf8.size()) {
        int first = 0;
        int second = -1;
        if (!koreanBytes(nextUtf8(utf8, i), first, second)) return false;
        const size_t need = second < 0 ? 1 : 2;
        if (count >= maxChars || used + need > bufferBytes) break;
        data[used++] = static_cast<uint8_t>(first);
        if (second >= 0) data[used++] = static_cast<uint8_t>(second);
        ++count;
    }
    return true;
}

}  // namespace

std::string decodeGb(uint8_t format, const uint8_t* data, size_t length, uint8_t language) {
    std::string out;
    if (length == 0) return out;
    if (data[0] == kGb12TradeOt) return "*";
    if (isKorean(format, language)) return decodeKorean(data, length);
    if (language == 0) language = 2;
    for (size_t i = 0; i < length; ++i) {
        const uint16_t cp = data::charGb(format, language, data[i]);
        if (cp == kUnused) break;
        if (format == 2 && language != 1 && ligature(cp, language, out)) continue;
        if (cp == kApostropheCurly) {
            out.push_back('\'');
        } else if (cp == kDotLeader) {
            out.push_back('.');
        } else {
            appendUtf8(out, cp);
        }
    }
    return out;
}

namespace {

// The byte of a character, or -1.
int byteFor(uint8_t format, uint8_t language, uint32_t cp) {
    if (cp == '\'' || cp == kApostropheCurly) {
        // The games' apostrophe (FARFETCH'D) is 0xE0.
        if (data::charGb(format, language, 0xE0) == kApostropheCurly) return 0xE0;
    }
    for (int b = 1; b < 256; ++b) {
        if (b == kGb12Terminator || b == kGb12TradeOt) continue;
        if (data::charGb(format, language, static_cast<uint8_t>(b)) == cp) return b;
    }
    if (cp == kApostropheCurly) return byteFor(format, language, '\'');
    return -1;
}

}  // namespace

bool encodeGb(uint8_t format, const std::string& utf8, uint8_t* data, size_t bufferBytes, size_t maxChars,
              uint8_t language) {
    std::memset(data, kGb12Terminator, bufferBytes);
    if (language == 0) language = 2;
    if (utf8.empty()) return true;
    if (utf8[0] == '*') {
        if (bufferBytes > 0) data[0] = kGb12TradeOt;
        return true;
    }
    if (maxChars > bufferBytes) maxChars = bufferBytes;
    if (isKorean(format, language)) return encodeKorean(utf8, data, bufferBytes, maxChars);
    size_t count = 0;
    size_t i = 0;
    while (i < utf8.size()) {
        const uint32_t cp = nextUtf8(utf8, i);
        const int b = byteFor(format, language, cp);
        if (b < 0) return false;
        if (count >= maxChars) break;
        data[count++] = static_cast<uint8_t>(b);
    }
    return true;
}

bool canEncodeGb(uint8_t format, const std::string& utf8, uint8_t language) {
    if (language == 0) language = 2;
    size_t i = 0;
    while (i < utf8.size()) {
        if (isKorean(format, language)) {
            int first, second;
            if (!koreanBytes(nextUtf8(utf8, i), first, second)) return false;
            continue;
        }
        if (byteFor(format, language, nextUtf8(utf8, i)) < 0) return false;
    }
    return true;
}

}  // namespace pktool::text
