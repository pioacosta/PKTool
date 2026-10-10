#include "core/text/gen4_text.h"

#include "core/data/gen3.h"
#include "core/util/bytes.h"

namespace pktool::text {
namespace {

// The games store half-width gender symbols at these code points.
constexpr uint32_t kHalfWidthMale = 0x246D;
constexpr uint32_t kHalfWidthFemale = 0x246E;
constexpr uint32_t kMale = 0x2642;
constexpr uint32_t kFemale = 0x2640;

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// Reads one code point from UTF-8, advancing `i`. Invalid bytes yield '?'.
uint32_t nextUtf8(const std::string& s, size_t& i) {
    auto byte = [&](size_t k) { return static_cast<uint8_t>(s[k]); };
    uint8_t lead = byte(i);
    size_t extra = 0;
    uint32_t cp = 0;
    if (lead < 0x80) {
        ++i;
        return lead;
    } else if ((lead & 0xE0) == 0xC0) {
        extra = 1;
        cp = lead & 0x1F;
    } else if ((lead & 0xF0) == 0xE0) {
        extra = 2;
        cp = lead & 0x0F;
    } else if ((lead & 0xF8) == 0xF0) {
        extra = 3;
        cp = lead & 0x07;
    } else {
        ++i;
        return '?';
    }
    if (i + extra >= s.size()) {
        i = s.size();
        return '?';
    }
    for (size_t k = 1; k <= extra; ++k) {
        uint8_t b = byte(i + k);
        if ((b & 0xC0) != 0x80) {
            i += k;
            return '?';
        }
        cp = (cp << 6) | (b & 0x3F);
    }
    i += extra + 1;
    return cp;
}

// PKHeX GetIsFullWidthString: any character outside U+0000-U+0FFF and
// U+E000-U+EFFF (kana, full-width Latin, Hangul...) other than the plain
// gender symbols.
bool isFullWidthString(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        uint32_t cp = nextUtf8(s, i);
        if ((cp >> 12) == 0 || (cp >> 12) == 0xE) continue;
        if (cp == kMale || cp == kFemale) continue;
        return true;
    }
    return false;
}

// Glyph value of a character: the international table first, then the
// Korean one; 0 if none. `halfWidthGender` picks the half-width male and
// female glyphs.
uint16_t glyphFor(uint32_t cp, bool halfWidthGender = true) {
    if (cp == 0x2019) {
        cp = '\'';  // species names use the typographic apostrophe
    }
    if (halfWidthGender) {
        if (cp == kMale) {
            cp = kHalfWidthMale;
        } else if (cp == kFemale) {
            cp = kHalfWidthFemale;
        }
    }
    for (int v = 1; v < generated::kGen4CharTableCount; ++v) {
        if (generated::kGen4CharTable[v] == cp) {
            return static_cast<uint16_t>(v);
        }
    }
    for (int v = 1; v < generated::kGen4KorTableCount; ++v) {
        if (generated::kGen4KorTable[v] == cp) {
            return static_cast<uint16_t>(generated::kGen4KorStart + v);
        }
    }
    return 0;
}

}  // namespace

std::string decodeGen4(const uint8_t* data, size_t maxChars) {
    std::string out;
    for (size_t i = 0; i < maxChars; ++i) {
        uint16_t value = util::readU16(data + i * 2);
        if (value == kGen4Terminator || value == 0) {
            break;
        }
        uint32_t cp = '?';
        if (value < generated::kGen4CharTableCount &&
            generated::kGen4CharTable[value] != kGen4Terminator) {
            cp = generated::kGen4CharTable[value];
        } else if (value >= generated::kGen4KorStart && value - generated::kGen4KorStart < generated::kGen4KorTableCount &&
                   generated::kGen4KorTable[value - generated::kGen4KorStart] != kGen4Terminator) {
            cp = generated::kGen4KorTable[value - generated::kGen4KorStart];
        }
        if (cp == kHalfWidthMale) {
            cp = kMale;
        } else if (cp == kHalfWidthFemale) {
            cp = kFemale;
        }
        appendUtf8(out, cp);
    }
    return out;
}

size_t encodeGen4(const std::string& utf8, uint8_t* data, size_t bufferChars, size_t maxChars, uint8_t language) {
    if (bufferChars == 0) {
        return 0;
    }
    if (maxChars > bufferChars - 1) {
        maxChars = bufferChars - 1;
    }
    static const uint16_t question = glyphFor('?');
    const bool halfWidth = language == kLanguageKorean || !isFullWidthString(utf8);
    size_t count = 0;
    size_t i = 0;
    while (i < utf8.size() && count < maxChars) {
        uint16_t glyph = glyphFor(nextUtf8(utf8, i), halfWidth);
        util::writeU16(data + count * 2, glyph != 0 ? glyph : question);
        ++count;
    }
    util::writeU16(data + count * 2, kGen4Terminator);
    return count;
}

std::string decodeGen5(const uint8_t* data, size_t maxChars) {
    std::string out;
    for (size_t i = 0; i < maxChars; ++i) {
        uint32_t value = util::readU16(data + i * 2);
        if (value == kGen4Terminator || value == 0) break;
        if (value == kHalfWidthMale) value = kMale;
        if (value == kHalfWidthFemale) value = kFemale;
        if (value >= 0xD800 && value < 0xE000) value = '?';  // no surrogate pairs in the games
        appendUtf8(out, value);
    }
    return out;
}

std::string decodeGen3(const uint8_t* data, size_t maxChars, uint8_t language) {
    const uint16_t* table = language == 1 ? data::generated::kGen3CharsJpn : data::generated::kGen3CharsInt;
    std::string out;
    for (size_t i = 0; i < maxChars && data[i] != kGen3Terminator; ++i) {
        uint32_t value = table[data[i]];
        if (value == kGen4Terminator) value = '?';
        if (value == kHalfWidthMale) value = kMale;
        if (value == kHalfWidthFemale) value = kFemale;
        appendUtf8(out, value);
    }
    return out;
}

size_t encodeGen3(const std::string& utf8, uint8_t* data, size_t bufferChars, size_t maxChars, uint8_t language) {
    const uint16_t* table = language == 1 ? data::generated::kGen3CharsJpn : data::generated::kGen3CharsInt;
    if (maxChars > bufferChars) maxChars = bufferChars;
    size_t count = 0;
    size_t i = 0;
    while (i < utf8.size() && count < maxChars) {
        uint32_t cp = nextUtf8(utf8, i);
        if (cp == kMale) cp = kHalfWidthMale;
        if (cp == kFemale) cp = kHalfWidthFemale;
        // The Japanese table has full-width Latin letters and digits where the
        // international one has the ASCII ones (same byte values), so a Latin OT
        // name still fits a Gen III egg, whose language is always Japanese.
        uint32_t alt = cp >= 0x21 && cp <= 0x7E       ? cp + 0xFEE0
                       : cp >= 0xFF01 && cp <= 0xFF5E ? cp - 0xFEE0
                                                      : 0;
        int found = -1;
        for (int b = 0; b < 0xFF && found < 0; ++b) {
            if (table[b] == cp) found = b;
        }
        for (int b = 0; b < 0xFF && found < 0 && alt != 0; ++b) {
            if (table[b] == alt) found = b;
        }
        if (found >= 0) data[count++] = static_cast<uint8_t>(found);
    }
    if (count < bufferChars) data[count] = kGen3Terminator;
    return count;
}

size_t encodeGen5(const std::string& utf8, uint8_t* data, size_t bufferChars, size_t maxChars) {
    if (bufferChars == 0) return 0;
    if (maxChars >= bufferChars) maxChars = bufferChars - 1;
    size_t count = 0;
    size_t i = 0;
    while (i < utf8.size() && count < maxChars) {
        uint32_t cp = nextUtf8(utf8, i);
        if (cp == kMale) cp = kHalfWidthMale;
        if (cp == kFemale) cp = kHalfWidthFemale;
        if (cp > 0xFFFF || (cp >= 0xD800 && cp < 0xE000)) cp = '?';
        util::writeU16(data + count * 2, static_cast<uint16_t>(cp));
        ++count;
    }
    util::writeU16(data + count * 2, kGen4Terminator);
    return count;
}

}  // namespace pktool::text
