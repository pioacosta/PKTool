#include "core/pokemon/card_file.h"

#include <cstring>

#include "core/pokemon/pk4.h"
#include "core/text/gen4_text.h"
#include "core/util/bytes.h"

namespace pktool::pokemon::cardfile {
namespace {

constexpr size_t kPgtSize = 0x104;
constexpr size_t kPcdSize = 0x358;
constexpr size_t kPgfSize = 0xCC;
constexpr uint16_t kMaxSpecies4 = 493;
constexpr uint16_t kMaxSpecies5 = 649;

// PGT gift types (PKHeX GiftType4).
constexpr uint16_t kGiftPokemon = 1;
constexpr uint16_t kGiftEgg = 2;
constexpr uint16_t kGiftManaphyEgg = 7;
constexpr uint16_t kGiftMovie = 13;
constexpr uint16_t kGiftTypeMax = 15;

std::string lowerCase(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

std::string stem(const std::string& fileName) {
    size_t dot = fileName.find_last_of('.');
    return dot == std::string::npos || dot == 0 ? fileName : fileName.substr(0, dot);
}

// Titles are shown in the Latin-only DS fonts: anything else is not shown.
bool latinOnly(const std::string& text) {
    for (char ch : text) {
        const uint8_t c = static_cast<uint8_t>(ch);
        if (c == '?' || c < 0x20) return false;
        if (c >= 0xC9) return false;  // lead bytes above U+024F
    }
    return true;
}

std::string cleanTitle(const std::string& raw, const std::string& fallback) {
    std::string out;
    bool space = true;
    for (char c : raw) {
        if (c == ' ' || c == '\n' || c == '\t') {
            if (!space) out += ' ';
            space = true;
        } else {
            out += c;
            space = false;
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    if (out.empty() || !latinOnly(out)) return fallback;
    if (out.size() > 56) {
        out.resize(56);
        while (!out.empty() && (static_cast<uint8_t>(out.back()) & 0xC0) == 0x80) out.pop_back();  // whole characters
        if (!out.empty() && static_cast<uint8_t>(out.back()) >= 0xC0) out.pop_back();
    }
    return out;
}

// A decrypted stored PK4 is valid when its checksum matches and the species exists.
bool validTemplate4(const uint8_t* decrypted) {
    const uint16_t species = util::readU16(decrypted + pk4::offset::kSpecies);
    return species >= 1 && species <= kMaxSpecies4 &&
           pk4::calcChecksum(decrypted) == util::readU16(decrypted + pk4::offset::kChecksum);
}

void setStatus(Card& card, Status status, const char* reason) {
    card.status = status;
    card.reason = reason;
}

// Parses a PGT (the first 0x104 bytes of `pgt`); `cardId` and `rawTitle` come from a PCD, else 0 / "".
void parsePgt(Card& card, const uint8_t* pgt, uint16_t cardId, const std::string& rawTitle) {
    card.generation = 4;
    const uint16_t type = util::readU16(pgt);
    if (type == kGiftManaphyEgg) return setStatus(card, Status::NotPokemon, "Use the built-in Manaphy egg");
    if (type == kGiftEgg) return setStatus(card, Status::NotPokemon, "Egg cards are not supported");
    if (type == kGiftMovie) return setStatus(card, Status::NotPokemon, "Movie cards are not supported");
    if (type == 0 || type > kGiftTypeMax) return setStatus(card, Status::Invalid, "Bad gift type");
    if (type != kGiftPokemon) return setStatus(card, Status::NotPokemon, "Not a Pok\xC3\xA9mon card");

    // The template may be stored encrypted (as in the files the game uses) or decrypted.
    uint8_t stored[pk4::kStoredSize];
    std::memcpy(stored, pgt + 8, sizeof stored);
    if (!validTemplate4(stored)) {
        pk4::decrypt(stored, sizeof stored);
        if (!validTemplate4(stored)) return setStatus(card, Status::Invalid, "Bad Pok\xC3\xA9mon data (checksum)");
    }
    const uint16_t species = util::readU16(stored + pk4::offset::kSpecies);
    card.title = cleanTitle(rawTitle, card.title);
    data::Event4& e = card.event4;
    e.cardId = cardId;
    e.species = species;
    e.language = stored[pk4::offset::kLanguage];
    e.title = card.title.c_str();
    e.periodFirst = 0;
    e.periodCount = 0;
    std::memcpy(e.templatePk4, stored, sizeof stored);
    card.species = species;
    card.status = Status::Ok;
    for (int i = 0; i < data::events4Count(); ++i) {
        const data::Event4& table = data::event4(i);
        if (table.species == species && std::memcmp(table.templatePk4, stored, sizeof stored) == 0) {
            card.tableIndex = i;
            e.periodFirst = table.periodFirst;
            e.periodCount = table.periodCount;
            if (e.cardId == 0) e.cardId = table.cardId;
            break;
        }
    }
}

void parsePgf(Card& card, const uint8_t* pgf) {
    card.generation = 5;
    const uint8_t type = pgf[0xB3];
    if (type == 2 || type == 3) return setStatus(card, Status::NotPokemon, "Not a Pok\xC3\xA9mon card");
    if (type != 1) return setStatus(card, Status::Invalid, "Bad card type");
    const uint16_t species = util::readU16(pgf + data::pgf::kSpecies);
    if (species < 1 || species > kMaxSpecies5) return setStatus(card, Status::Invalid, "Bad species");
    if (pgf[data::pgf::kLevel] > 100 || pgf[data::pgf::kIsEgg] > 1 || pgf[data::pgf::kPidType] > 2 ||
        pgf[data::pgf::kAbilityType] > 4) {
        return setStatus(card, Status::Invalid, "Bad Pok\xC3\xA9mon data");
    }
    card.title = cleanTitle(text::decodeGen5(pgf + 0x60, 36), card.title);
    data::Event5& e = card.event5;
    e.cardId = util::readU16(pgf + 0xB0);
    e.species = species;
    e.restrictVersion = 0;  // a file does not say which games may receive it
    e.restrictLanguage = 0;
    const uint16_t year = util::readU16(pgf + 0xAE);
    const uint8_t month = pgf[0xAD];
    const uint8_t day = pgf[0xAC];
    if (year >= 2000 && year < 2100 && month >= 1 && month <= 12 && day >= 1 && day <= 31) {
        e.date[0] = static_cast<uint8_t>(year - 2000);
        e.date[1] = month;
        e.date[2] = day;
    }
    e.title = card.title.c_str();
    e.periodFirst = 0;
    e.periodCount = 0;
    std::memcpy(e.pgf, pgf, sizeof e.pgf);
    card.species = species;
    card.status = Status::Ok;
    for (int i = 0; i < data::events5Count(); ++i) {
        const data::Event5& table = data::event5(i);
        if (table.species == species && std::memcmp(table.pgf, e.pgf, sizeof e.pgf) == 0) {
            card.tableIndex = i;
            e.restrictVersion = table.restrictVersion;
            e.restrictLanguage = table.restrictLanguage;
            e.periodFirst = table.periodFirst;
            e.periodCount = table.periodCount;
            break;
        }
    }
}

}  // namespace

std::unique_ptr<Card> parse(const uint8_t* bytes, size_t size, const std::string& extension,
                            const std::string& fileName) {
    std::unique_ptr<Card> card(new Card());
    card->fileName = fileName;
    card->title = stem(fileName);
    const std::string ext = lowerCase(extension);
    if (ext == ".pgt") {
        if (size != kPgtSize) {
            setStatus(*card, Status::Invalid, "Not a PGT file (size)");
        } else {
            parsePgt(*card, bytes, 0, "");
        }
    } else if (ext == ".pcd") {
        if (size != kPcdSize) {
            setStatus(*card, Status::Invalid, "Not a PCD file (size)");
        } else {
            parsePgt(*card, bytes, util::readU16(bytes + 0x150), text::decodeGen4(bytes + 0x104, 36));
        }
    } else if (ext == ".pgf") {
        if (size != kPgfSize) {
            setStatus(*card, Status::Invalid, "Not a PGF file (size)");
        } else {
            parsePgf(*card, bytes);
        }
    } else {
        setStatus(*card, Status::Invalid, "Unknown card file type");
    }
    return card;
}

}  // namespace pktool::pokemon::cardfile
