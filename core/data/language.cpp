#include "core/data/language.h"

#include <algorithm>
#include <cstring>

#include "core/data/names.h"
#include "core/data/sorted_names.h"
#include "core/io/file_io.h"
#include "core/util/bytes.h"

namespace pktool::data {
namespace {

struct LanguageInfo {
    uint8_t id;
    const char* code;
    const char* name;
    const char* native;
};

const LanguageInfo kInfo[] = {
    {kLangEnglish, "en", "English", "English"},
    {kLangJapanese, "ja", "Japanese", "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"},
    {kLangFrench, "fr", "French", "Fran\xC3\xA7" "ais"},
    {kLangItalian, "it", "Italian", "Italiano"},
    {kLangGerman, "de", "German", "Deutsch"},
    {kLangSpanish, "es", "Spanish", "Espa\xC3\xB1ol"},
    {kLangKorean, "ko", "Korean", "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4"},
};

const LanguageInfo* find(uint8_t language) {
    for (const LanguageInfo& info : kInfo) {
        if (info.id == language) return &info;
    }
    return nullptr;
}

constexpr int kTables = 6;

// "PKN1", u8 language, u8 tables, u16 counts[tables], u32 offsets[sum], pool.
struct Pack {
    uint8_t language = 0;  // 0 = nothing loaded
    uint16_t counts[kTables] = {};
    size_t first[kTables] = {};  // index of each table's first offset
    std::vector<uint32_t> offsets;
    std::vector<char> pool;

    const char* get(int table, uint16_t id) const {
        if (language == 0 || id >= counts[table]) return nullptr;
        const char* s = pool.data() + offsets[first[table] + id];
        return *s ? s : nullptr;
    }
};

std::vector<std::string> g_dirs;
Pack g_active;  // the chosen language (empty while English is chosen)
// Other languages, only their species names, loaded for default nicknames (a few Pokemon of
// other languages are common in a save; checking them must not reload a pack every time).
constexpr int kAuxSlots = 3;
Pack g_aux[kAuxSlots];
int g_auxNext = 0;  // slot the next pack goes to
uint8_t g_chosen = kLangEnglish;
bool g_auxFailed[16] = {};

bool readHeader(io::FileReader& file, uint8_t language, Pack& p, size_t& total, size_t& head) {
    head = 6 + 2 * kTables;
    uint8_t bytes[6 + 2 * kTables];
    if (!file.read(0, head, bytes) || std::memcmp(bytes, "PKN1", 4) != 0 || bytes[4] != language ||
        bytes[5] != kTables) {
        return false;
    }
    total = 0;
    for (int t = 0; t < kTables; ++t) {
        p.counts[t] = util::readU16(&bytes[6 + 2 * t]);
        p.first[t] = total;
        total += p.counts[t];
    }
    return true;
}

// Loads the pack of `language`; with `speciesOnly` just its species names (~10 KB instead of
// 35-52 KB read and held), which is all a default nickname needs.
bool load(uint8_t language, Pack& pack, bool speciesOnly = false) {
    const LanguageInfo* info = find(language);
    if (info == nullptr || language == kLangEnglish) return false;
    io::FileReader file;
    bool found = false;
    for (const std::string& dir : g_dirs) {
        if (file.open(io::joinPath(dir, std::string("lang/") + info->code + ".bin"))) {
            found = true;
            break;
        }
    }
    if (!found) return false;
    Pack p;
    size_t total = 0;
    size_t head = 0;
    if (!readHeader(file, language, p, total, head)) return false;
    const size_t fileSize = file.size();
    if (fileSize < head + total * 4 + 1) return false;
    const size_t poolStart = head + total * 4;
    const size_t poolSize = fileSize - poolStart;
    // The offsets: all of them, or the species ones (the first table).
    const size_t wanted = speciesOnly ? p.counts[0] : total;
    std::vector<uint8_t> raw(wanted * 4);
    if (wanted != 0 && !file.read(head, raw.size(), raw.data())) return false;
    p.offsets.resize(wanted);
    uint32_t highest = 0;
    for (size_t i = 0; i < wanted; ++i) {
        p.offsets[i] = util::readU32(&raw[i * 4]);
        if (p.offsets[i] >= poolSize) return false;
        if (p.offsets[i] > highest) highest = p.offsets[i];
    }
    raw = std::vector<uint8_t>();  // release before the pool is allocated
    // The pool: all of it, or up to the end of the last species name. The last byte must end a
    // string, as the loader of the whole file checks.
    size_t poolBytes = poolSize;
    if (speciesOnly) {
        poolBytes = std::min<size_t>(poolSize, static_cast<size_t>(highest) + 128);
        for (int t = 1; t < kTables; ++t) p.counts[t] = 0;  // other tables are not held
    }
    p.pool.resize(poolBytes);
    if (!file.read(poolStart, poolBytes, reinterpret_cast<uint8_t*>(p.pool.data()))) return false;
    if (speciesOnly) {
        // Cut the pool after the last species name (the read may have run into the next table).
        size_t end = highest;
        while (end < p.pool.size() && p.pool[end] != 0) ++end;
        if (end >= p.pool.size()) return false;
        p.pool.resize(end + 1);
        p.pool.shrink_to_fit();
    } else if (p.pool.empty() || p.pool.back() != 0) {
        return false;
    }
    p.language = language;
    pack = std::move(p);
    return true;
}

uint32_t upperCp(uint32_t cp) {
    if ((cp >= 'a' && cp <= 'z') || (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7)) return cp - 0x20;
    if (cp == 0xFF) return 0x178;
    if (cp == 0x153) return 0x152;
    if (cp == 0x15F) return 0x15E;
    return cp;
}

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

}  // namespace

bool isNameLanguage(uint8_t language) {
    return find(language) != nullptr;
}

const char* languageCode(uint8_t language) {
    const LanguageInfo* info = find(language);
    return info ? info->code : "";
}

uint8_t languageFromCode(const std::string& code) {
    for (const LanguageInfo& info : kInfo) {
        if (code == info.code) return info.id;
    }
    return 0;
}

const char* languageName(uint8_t language) {
    const LanguageInfo* info = find(language);
    return info ? info->name : "?";
}

const char* languageNativeName(uint8_t language) {
    const LanguageInfo* info = find(language);
    return info ? info->native : "?";
}

void setLanguageDirs(const std::vector<std::string>& directories) {
    g_dirs = directories;
    for (Pack& aux : g_aux) aux = Pack();
    g_auxNext = 0;
    for (bool& failed : g_auxFailed) failed = false;
}

bool setNameLanguage(uint8_t language) {
    if (!isNameLanguage(language)) return false;
    if (language == kLangEnglish) {
        g_active = Pack();
        g_chosen = kLangEnglish;
        resetSortedViews();
        return true;
    }
    Pack pack;
    if (!load(language, pack)) return false;
    g_active = std::move(pack);
    g_chosen = language;
    resetSortedViews();
    return true;
}

uint8_t nameLanguage() {
    return g_chosen;
}

bool languageAvailable(uint8_t language) {
    if (language == kLangEnglish) return true;
    if (g_active.language == language) return true;
    Pack probe;
    return load(language, probe);
}

const char* packName(NameTable table, uint16_t id) {
    return g_active.get(static_cast<int>(table), id);
}

const char* speciesNameIn(uint8_t language, uint16_t species) {
    if (find(language) != nullptr && language != kLangEnglish) {
        const Pack* pack = nullptr;
        if (g_active.language == language) {
            pack = &g_active;
        } else {
            for (const Pack& aux : g_aux) {
                if (aux.language == language) pack = &aux;
            }
            if (pack == nullptr && !g_auxFailed[language & 15]) {
                Pack& slot = g_aux[g_auxNext];
                if (load(language, slot, true)) {
                    g_auxNext = (g_auxNext + 1) % kAuxSlots;
                    pack = &slot;
                } else {
                    g_auxFailed[language & 15] = true;
                }
            }
        }
        if (pack != nullptr) {
            if (const char* name = pack->get(static_cast<int>(NameTable::Species), species)) return name;
        }
    }
    return speciesNameEn(species);
}

std::string defaultNickname(uint16_t species, uint8_t language, uint8_t format) {
    const char* name = speciesNameIn(language, species);
    // Gen I/II write MR. MIME without the space.
    if (format <= 2 && species == 122 && std::string(name) == "Mr. Mime") name = "Mr.Mime";
    const bool capitals = format <= 4 && language != kLangJapanese && language != kLangKorean;
    std::string out;
    for (const char* s = name; *s;) {
        auto c = static_cast<unsigned char>(*s);
        uint32_t cp;
        int extra = 0;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            extra = 2;
        } else {
            cp = '?';
        }
        ++s;
        for (int k = 0; k < extra && *s; ++k, ++s) cp = (cp << 6) | (static_cast<unsigned char>(*s) & 0x3F);
        if (cp == 0x2019) cp = '\'';  // the games have no slanted apostrophe (Farfetch'd)
        if (capitals) {
            cp = upperCp(cp);
            if (language == kLangFrench) {
                if (cp >= 0xC8 && cp <= 0xCA) cp = 'E';  // old games strip these accents
                if (cp == 0xCF) cp = 'I';
            }
        }
        appendUtf8(out, cp);
    }
    return out;
}

}  // namespace pktool::data
