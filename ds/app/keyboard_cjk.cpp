#include "ds/app/keyboard_cjk.h"

#include "core/text/gen4_text.h"

namespace pktool::ui::kbcjk {

namespace {

// ----- UTF-8 (BMP only) --------------------------------------------------------

size_t charCount(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) n += (c & 0xC0) != 0x80 ? 1 : 0;
    return n;
}

// Decodes the character at `start` of `s`.
uint32_t decodeAt(const std::string& s, size_t start, size_t* length) {
    unsigned char c = static_cast<unsigned char>(s[start]);
    size_t len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    if (start + len > s.size()) len = s.size() - start;
    uint32_t cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
    for (size_t i = 1; i < len; ++i) cp = (cp << 6) | (static_cast<unsigned char>(s[start + i]) & 0x3F);
    if (length) *length = len;
    return cp;
}

uint32_t firstChar(const std::string& s) {
    return s.empty() ? 0 : decodeAt(s, 0, nullptr);
}

// Removes and returns the last character (0 when empty).
uint32_t popChar(std::string& s) {
    if (s.empty()) return 0;
    size_t start = s.size() - 1;
    while (start > 0 && (static_cast<unsigned char>(s[start]) & 0xC0) == 0x80) --start;
    uint32_t cp = decodeAt(s, start, nullptr);
    s.resize(start);
    return cp;
}

void pushChar(std::string& s, uint32_t cp) {
    if (cp < 0x80) {
        s += static_cast<char>(cp);
    } else if (cp < 0x800) {
        s += static_cast<char>(0xC0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// ----- Pages -------------------------------------------------------------------

const char* const kHira[2][4] = {
    {"\xE3\x81\x82\xE3\x81\x84\xE3\x81\x86\xE3\x81\x88\xE3\x81\x8A\xE3\x81\x8B\xE3\x81\x8D\xE3\x81\x8F\xE3\x81\x91\xE3\x81\x93",    // あいうえおかきくけこ
     "\xE3\x81\x95\xE3\x81\x97\xE3\x81\x99\xE3\x81\x9B\xE3\x81\x9D\xE3\x81\x9F\xE3\x81\xA1\xE3\x81\xA4\xE3\x81\xA6\xE3\x81\xA8",    // さしすせそたちつてと
     "\xE3\x81\xAA\xE3\x81\xAB\xE3\x81\xAC\xE3\x81\xAD\xE3\x81\xAE\xE3\x81\xAF\xE3\x81\xB2\xE3\x81\xB5\xE3\x81\xB8\xE3\x81\xBB",    // なにぬねのはひふへほ
     "\xE3\x81\xBE\xE3\x81\xBF\xE3\x82\x80\xE3\x82\x81\xE3\x82\x82\xE3\x82\x84\xE3\x82\x86\xE3\x82\x88\xE3\x82\x8F\xE3\x82\x92"},  // まみむめもやゆよわを
    {"\xE3\x82\x89\xE3\x82\x8A\xE3\x82\x8B\xE3\x82\x8C\xE3\x82\x8D\xE3\x82\x93\xE3\x83\xBC\xEF\xBE\x9E\xEF\xBE\x9F\xEF\xBC\x81",  // らりるれろんーﾞﾟ！
     "\xE3\x81\x81\xE3\x81\x83\xE3\x81\x85\xE3\x81\x87\xE3\x81\x89\xE3\x82\x83\xE3\x82\x85\xE3\x82\x87\xE3\x81\xA3\xEF\xBC\x9F",  // ぁぃぅぇぉゃゅょっ？
     "\xE3\x80\x82\xE3\x80\x81\xE3\x83\xBB\xE3\x80\x8C\xE3\x80\x8D\xE2\x80\xA6    ",                                           // 。、・「」…
     "\xEF\xBC\x90\xEF\xBC\x91\xEF\xBC\x92\xEF\xBC\x93\xEF\xBC\x94\xEF\xBC\x95\xEF\xBC\x96\xEF\xBC\x97\xEF\xBC\x98\xEF\xBC\x99"}};  // ０-９

// Katakana pages: the hiragana rows shifted by 0x60 (built on first use).
std::string g_kata[2][4];
bool g_kataReady = false;

void buildKata() {
    if (g_kataReady) return;
    for (int page = 0; page < 2; ++page) {
        for (int row = 0; row < 4; ++row) {
            const std::string src = kHira[page][row];
            std::string out;
            for (size_t i = 0; i < src.size();) {
                size_t len = 1;
                uint32_t cp = decodeAt(src, i, &len);
                i += len;
                if (cp >= 0x3041 && cp <= 0x3096) cp += 0x60;
                pushChar(out, cp);
            }
            g_kata[page][row] = out;
        }
    }
    g_kataReady = true;
}

const char* const kHangul[2][4] = {
    {"\xE3\x85\x82\xE3\x85\x88\xE3\x84\xB7\xE3\x84\xB1\xE3\x85\x85\xE3\x85\x9B\xE3\x85\x95\xE3\x85\x91\xE3\x85\x90\xE3\x85\x94",  // ㅂㅈㄷㄱㅅㅛㅕㅑㅐㅔ
     "\xE3\x85\x81\xE3\x84\xB4\xE3\x85\x87\xE3\x84\xB9\xE3\x85\x8E\xE3\x85\x97\xE3\x85\x93\xE3\x85\x8F\xE3\x85\xA3 ",              // ㅁㄴㅇㄹㅎㅗㅓㅏㅣ
     "\xE3\x85\x8B\xE3\x85\x8C\xE3\x85\x8A\xE3\x85\x8D\xE3\x85\xA0\xE3\x85\x9C\xE3\x85\xA1   ",                                    // ㅋㅌㅊㅍㅠㅜㅡ
     "0123456789"},
    {"\xE3\x85\x83\xE3\x85\x89\xE3\x84\xB8\xE3\x84\xB2\xE3\x85\x86\xE3\x85\x9B\xE3\x85\x95\xE3\x85\x91\xE3\x85\x92\xE3\x85\x96",  // ㅃㅉㄸㄲㅆㅛㅕㅑㅒㅖ
     "\xE3\x85\x81\xE3\x84\xB4\xE3\x85\x87\xE3\x84\xB9\xE3\x85\x8E\xE3\x85\x97\xE3\x85\x93\xE3\x85\x8F\xE3\x85\xA3 ",
     "\xE3\x85\x8B\xE3\x85\x8C\xE3\x85\x8A\xE3\x85\x8D\xE3\x85\xA0\xE3\x85\x9C\xE3\x85\xA1   ",
     "0123456789"}};

// ----- Kana marks ----------------------------------------------------------------

bool isHaRow(uint32_t h) {
    return h >= 0x306F && h <= 0x307D;  // は ば ぱ ひ び ぴ ... ほ ぼ ぽ
}

bool voiceable(uint32_t h) {
    return (h >= 0x304B && h <= 0x3061 && h % 2 == 1) ||  // か..ち
           (h >= 0x3064 && h <= 0x3068 && h % 2 == 0) ||  // つ て と
           h == 0x3046;                                    // う
}

bool voiced(uint32_t h) {
    return (h >= 0x304C && h <= 0x3062 && h % 2 == 0) || h == 0x3065 || h == 0x3067 || h == 0x3069 || h == 0x3094;
}

// Applies ゛ (dakuten) or ゜ (handakuten) to a kana; returns it unchanged when it does not apply.
uint32_t applyMark(uint32_t cp, bool handakuten) {
    const bool kata = cp >= 0x30A1 && cp <= 0x30F6;
    const uint32_t h = kata ? cp - 0x60 : cp;
    uint32_t out = h;
    if (isHaRow(h)) {
        uint32_t phase = (h - 0x306F) % 3;  // 0 plain, 1 voiced, 2 semi-voiced
        if (handakuten) out = phase == 2 ? h - 2 : h - phase + 2;
        else out = phase == 0 ? h + 1 : h - 1;
    } else if (!handakuten) {
        if (h == 0x3046) out = 0x3094;      // う -> ゔ
        else if (voiceable(h)) out = h + 1;
        else if (voiced(h)) out = h == 0x3094 ? 0x3046 : h - 1;
    }
    return out == h ? cp : (kata ? out + 0x60 : out);
}

// ----- Hangul --------------------------------------------------------------------

// Compatibility jamo, by syllable index.
const uint32_t kInitial[19] = {0x3131, 0x3132, 0x3134, 0x3137, 0x3138, 0x3139, 0x3141, 0x3142, 0x3143, 0x3145,
                               0x3146, 0x3147, 0x3148, 0x3149, 0x314A, 0x314B, 0x314C, 0x314D, 0x314E};
const uint32_t kFinal[28] = {0,      0x3131, 0x3132, 0x3133, 0x3134, 0x3135, 0x3136, 0x3137, 0x3139, 0x313A,
                             0x313B, 0x313C, 0x313D, 0x313E, 0x313F, 0x3140, 0x3141, 0x3142, 0x3144, 0x3145,
                             0x3146, 0x3147, 0x3148, 0x314A, 0x314B, 0x314C, 0x314D, 0x314E};
struct Pair {
    uint32_t whole, a, b;
};
const Pair kFinalPairs[] = {{0x3133, 0x3131, 0x3145}, {0x3135, 0x3134, 0x3148}, {0x3136, 0x3134, 0x314E},
                            {0x313A, 0x3139, 0x3131}, {0x313B, 0x3139, 0x3141}, {0x313C, 0x3139, 0x3142},
                            {0x313D, 0x3139, 0x3145}, {0x313E, 0x3139, 0x314C}, {0x313F, 0x3139, 0x314D},
                            {0x3140, 0x3139, 0x314E}, {0x3144, 0x3142, 0x3145}};
const Pair kVowelPairs[] = {{0x3158, 0x3157, 0x314F}, {0x3159, 0x3157, 0x3150}, {0x315A, 0x3157, 0x3163},
                            {0x315D, 0x315C, 0x3153}, {0x315E, 0x315C, 0x3154}, {0x315F, 0x315C, 0x3163},
                            {0x3162, 0x3161, 0x3163}};
constexpr int kFinalPairCount = 11;
constexpr int kVowelPairCount = 7;

bool isVowel(uint32_t cp) { return cp >= 0x314F && cp <= 0x3163; }
bool isConsonant(uint32_t cp) { return cp >= 0x3131 && cp <= 0x314E; }
bool isSyllable(uint32_t cp) { return cp >= 0xAC00 && cp <= 0xD7A3; }

int indexIn(const uint32_t* table, int count, uint32_t cp) {
    for (int i = 0; i < count; ++i) {
        if (table[i] == cp) return i;
    }
    return -1;
}

uint32_t join(const Pair* pairs, int count, uint32_t a, uint32_t b) {
    for (int i = 0; i < count; ++i) {
        if (pairs[i].a == a && pairs[i].b == b) return pairs[i].whole;
    }
    return 0;
}

const Pair* splitPair(const Pair* pairs, int count, uint32_t whole) {
    for (int i = 0; i < count; ++i) {
        if (pairs[i].whole == whole) return &pairs[i];
    }
    return nullptr;
}

uint32_t syllable(int initial, uint32_t vowel, int final) {
    return 0xAC00 + (initial * 21 + static_cast<int>(vowel - 0x314F)) * 28 + final;
}

bool typeJamo(std::string& text, uint32_t jamo, size_t maxChars) {
    const bool room = charCount(text) < maxChars;
    std::string head = text;
    const uint32_t last = popChar(head);
    auto fresh = [&]() {
        if (!room) return false;
        pushChar(text, jamo);
        return true;
    };
    auto replaceLast = [&](uint32_t cp) {
        text = head;
        pushChar(text, cp);
        return true;
    };
    if (isSyllable(last)) {
        const int code = static_cast<int>(last - 0xAC00);
        const int fin = code % 28;
        const uint32_t vowel = 0x314F + (code / 28) % 21;
        const int ini = code / 28 / 21;
        if (isConsonant(jamo)) {
            if (fin == 0) {
                int f = indexIn(kFinal, 28, jamo);
                if (f > 0) return replaceLast(syllable(ini, vowel, f));
            } else if (uint32_t whole = join(kFinalPairs, kFinalPairCount, kFinal[fin], jamo)) {
                return replaceLast(syllable(ini, vowel, indexIn(kFinal, 28, whole)));
            }
            return fresh();
        }
        if (fin == 0) {
            if (uint32_t whole = join(kVowelPairs, kVowelPairCount, vowel, jamo)) return replaceLast(syllable(ini, whole, 0));
            return fresh();
        }
        // A vowel after a final: the final (or the second half of a double one) starts a new syllable.
        if (!room) return false;
        uint32_t moved = kFinal[fin];
        int keep = 0;
        if (const Pair* p = splitPair(kFinalPairs, kFinalPairCount, moved)) {
            keep = indexIn(kFinal, 28, p->a);
            moved = p->b;
        }
        int next = indexIn(kInitial, 19, moved);
        if (next < 0) return fresh();
        replaceLast(syllable(ini, vowel, keep));
        pushChar(text, syllable(next, jamo, 0));
        return true;
    }
    if (isConsonant(last) && isVowel(jamo)) {
        int ini = indexIn(kInitial, 19, last);
        if (ini >= 0) return replaceLast(syllable(ini, jamo, 0));
    }
    return fresh();
}

}  // namespace

Kind kindForLanguage(uint8_t language) {
    return language == 1 ? Kind::Japanese : language == 8 ? Kind::Korean : Kind::Latin;
}

int pageCount(Kind kind) {
    return kind == Kind::Japanese ? 4 : kind == Kind::Korean ? 2 : 0;
}

const char* pageRow(Kind kind, int page, int row) {
    if (row < 0 || row > 3 || page < 0) return "";
    if (kind == Kind::Japanese && page < 4) {
        if (page >= 2) {
            buildKata();
            return g_kata[page - 2][row].c_str();
        }
        return kHira[page][row];
    }
    if (kind == Kind::Korean && page < 2) return kHangul[page][row];
    return "";
}

const char* pageLabel(Kind kind, int page) {
    static const char* const jp[4] = {"\xE3\x81\x82", "\xE3\x82\x89", "\xE3\x82\xA2", "\xE3\x83\xA9"};  // あ ら ア ラ
    static const char* const kr[2] = {"\xE3\x84\xB1", "\xE3\x84\xB2"};                                  // ㄱ ㄲ
    if (kind == Kind::Japanese && page >= 0 && page < 4) return jp[page];
    if (kind == Kind::Korean && page >= 0 && page < 2) return kr[page];
    return "";
}

bool type(Kind kind, std::string& text, const std::string& ch, size_t maxChars) {
    const uint32_t cp = firstChar(ch);
    if (cp == 0 || cp == ' ') return false;
    if (kind == Kind::Korean && (isConsonant(cp) || isVowel(cp))) return typeJamo(text, cp, maxChars);
    const bool dakuten = cp == 0x309B || cp == 0xFF9E;  // the keys use the half-width marks (the font lacks U+309B/C)
    const bool handakuten = cp == 0x309C || cp == 0xFF9F;
    if (kind == Kind::Japanese && (dakuten || handakuten)) {
        std::string head = text;
        const uint32_t last = popChar(head);
        const uint32_t changed = applyMark(last, handakuten);
        if (last == 0 || changed == last) return false;
        pushChar(head, changed);
        text = head;
        return true;
    }
    if (charCount(text) >= maxChars) return false;
    text += ch;
    return true;
}

void backspace(Kind kind, std::string& text) {
    const uint32_t last = popChar(text);
    if (kind != Kind::Korean || !isSyllable(last)) return;
    const int code = static_cast<int>(last - 0xAC00);
    const int fin = code % 28;
    const uint32_t vowel = 0x314F + (code / 28) % 21;
    const int ini = code / 28 / 21;
    if (fin != 0) {
        const Pair* p = splitPair(kFinalPairs, kFinalPairCount, kFinal[fin]);
        pushChar(text, syllable(ini, vowel, p ? indexIn(kFinal, 28, p->a) : 0));
    } else if (const Pair* p = splitPair(kVowelPairs, kVowelPairCount, vowel)) {
        pushChar(text, syllable(ini, p->a, 0));
    } else {
        pushChar(text, kInitial[ini]);
    }
}

bool storable(uint8_t format, uint8_t language, const std::string& text) {
    if (format < 3 || format > 5) return true;
    // Hangul belongs to the Korean games only (the Gen IV encoder would write it anyway).
    if (language != 8) {
        for (size_t i = 0; i < text.size();) {
            size_t len = 1;
            uint32_t cp = decodeAt(text, i, &len);
            i += len;
            if ((cp >= 0x3130 && cp <= 0x318F) || (cp >= 0xAC00 && cp <= 0xD7A3)) return false;
        }
    }
    constexpr size_t kChars = 16;
    uint8_t buffer[(kChars + 1) * 2] = {};
    std::string back;
    if (format == 3) {
        text::encodeGen3(text, buffer, kChars + 1, kChars, language);
        back = text::decodeGen3(buffer, kChars, language);
    } else if (format == 4) {
        text::encodeGen4(text, buffer, kChars + 1, kChars, language);
        back = text::decodeGen4(buffer, kChars);
    } else {
        text::encodeGen5(text, buffer, kChars + 1, kChars);
        back = text::decodeGen5(buffer, kChars);
    }
    return back == text;
}

}  // namespace pktool::ui::kbcjk
