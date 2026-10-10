#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Japanese and Korean pages of the on-screen keyboard (portable: tested on the
// host). Japanese: hiragana and katakana, two pages each, with dakuten and
// handakuten keys that modify the previous kana. Korean: 2-set (dubeolsik)
// jamo keys, composed into syllables as they are typed.
namespace pktool::ui::kbcjk {

enum class Kind : uint8_t { Latin, Japanese, Korean };

// Keyboard kind of a game text language (1 = Japanese, 8 = Korean).
Kind kindForLanguage(uint8_t language);

// Extra pages before the Latin ones (0 for Latin).
int pageCount(Kind kind);
// Ten UTF-8 characters of a key row (ASCII spaces are empty keys).
const char* pageRow(Kind kind, int page, int row);
const char* pageLabel(Kind kind, int page);

// Types the key `ch` (one UTF-8 character of a CJK page) at the end of `text`,
// composing it with the previous character where the script does. False when
// nothing changed (empty key, or no room for a new character).
bool type(Kind kind, std::string& text, const std::string& ch, size_t maxChars);
// Deletes the last typed unit; Hangul syllables lose their last jamo first.
void backspace(Kind kind, std::string& text);

// True when the game (format 3-5; the Gen I/II encoders judge themselves) can
// store `text` for `language`: every character survives a round trip.
bool storable(uint8_t format, uint8_t language, const std::string& text);

}  // namespace pktool::ui::kbcjk
