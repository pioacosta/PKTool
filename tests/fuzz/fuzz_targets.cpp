#include "tests/fuzz/fuzz_targets.h"

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "core/bank/bank.h"
#include "core/data/names.h"
#include "core/io/file_io.h"
#include "core/io/settings.h"
#include "core/legality/legalize.h"
#include "core/legality/legality.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/edit4.h"
#include "core/pokemon/edit_origin.h"
#include "core/pokemon/entity.h"
#include "core/pokemon/pk3.h"
#include "core/pokemon/pk4.h"
#include "core/pokemon/pk5.h"
#include "core/pokemon/ribbons.h"
#include "core/pokemon/transfer34.h"
#include "core/pokemon/transfer45.h"
#include "core/saves/save_file.h"
#include "core/saves/save_probe.h"
#include "core/showdown/import4.h"
#include "core/showdown/showdown.h"
#include "core/text/gen4_text.h"
#include "core/data/language.h"
#include "core/data/locks3.h"
#include "core/legality/ribbons_check.h"
#include "core/legality/shadow_locks.h"
#include "core/legality/trainer_check.h"
#include "core/pokemon/card_file.h"
#include "core/pokemon/gift4.h"
#include "core/pokemon/gift5.h"
#include "ds/gfx/cjk_font.h"
#include "ds/gfx/font.h"
#include "core/util/bytes.h"
#include "core/util/crc16.h"

namespace pktool::fuzz {

namespace {

namespace create4 = pokemon::create4;
namespace edit4 = pokemon::edit4;
namespace entity = pokemon::entity;

[[noreturn]] void failIn(const char* target, const char* message) { fail(target, message); }

uint64_t hashBytes(const uint8_t* data, size_t size) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < size; ++i) h = (h ^ data[i]) * 1099511628211ull;
    return h;
}

// ----- Entities --------------------------------------------------------------------

// Everything the app does with an entity, on arbitrary bytes. `size` is the
// stored or party size of `format`.
void exerciseEntity(uint8_t format, const uint8_t* decrypted, size_t size, bool full) {
    uint8_t work[entity::kMaxPartySize + 16] = {};
    std::memcpy(work, decrypted, size);
    pokemon::Pokemon p = entity::decode(format, work, size);

    // apply() is idempotent: the second round changes nothing.
    uint8_t once[entity::kMaxPartySize + 16];
    std::memcpy(once, work, sizeof once);
    entity::apply(p, once);
    pokemon::Pokemon q = entity::decode(format, once, size);
    uint8_t twice[entity::kMaxPartySize + 16];
    std::memcpy(twice, once, sizeof twice);
    entity::apply(q, twice);
    if (std::memcmp(once, twice, size) != 0) failIn("entity", "apply(decode(x)) is not stable");

    // Derived data of the model stays inside its documented ranges.
    if (p.level > 100) failIn("entity", "level above 100");
    if (p.nature > 24) failIn("entity", "nature above 24");
    if (p.nickname.size() > 64 || p.trainer.name.size() > 64) failIn("entity", "absurd name length");

    if (size >= static_cast<size_t>(format == 3 ? 100 : format == 4 ? 236 : 220)) {
        uint8_t party[entity::kMaxPartySize + 16];
        std::memcpy(party, work, sizeof party);
        entity::refreshParty(format, party);
        entity::decode(format, party, size);
    }

    for (int i = 0; i < pokemon::ribbons::count(format); ++i) {
        int v = pokemon::ribbons::get(format, work, i);
        uint8_t copy[entity::kMaxPartySize + 16];
        std::memcpy(copy, work, sizeof copy);
        pokemon::ribbons::set(format, copy, i, pokemon::ribbons::maxValue(format, i));
        pokemon::ribbons::set(format, copy, i, v);
    }
    pokemon::ribbons::total(format, work);

    if (!full) return;

    // Check and the showdown text of what was decoded.
    legality::check(p);
    std::string text = showdown::format(p);
    showdown::parse(text);

    // Conversions between generations.
    if (format == 3) {
        uint8_t out[136];
        pokemon::transfer34::convert(work, out, {26, 9, 29});
        entity::decode(4, out, 136);
        uint8_t out5[136];
        pokemon::transfer45::convert(out, out5);
        entity::decode(5, out5, 136);
    } else if (format == 4) {
        uint8_t out5[136];
        pokemon::transfer45::convert(work, out5);
        entity::decode(5, out5, 136);
    }

    // Editor operations.
    pokemon::Pokemon e = p;
    edit4::setNature(e, static_cast<uint8_t>((e.nature + 3) % 25));
    edit4::setShiny(e, !e.shiny);
    edit4::setGender(e, pokemon::Gender::Female);
    edit4::setAbilitySlot(e, 1);
    edit4::setSpecies(e, static_cast<uint16_t>(e.species % 400 + 1));
    edit4::setLevel(e, 55);
    edit4::setMove(e, 1, 33);
    pokemon::editorigin::setOtName(e, "Fuzz");
    pokemon::editorigin::setMetLevel(e, 200);
    pokemon::editorigin::setPokerusStrain(e, 7);
    uint8_t applied[entity::kMaxPartySize + 16];
    std::memcpy(applied, work, sizeof applied);
    entity::apply(e, applied);
    entity::decode(format, applied, size);
}

void fuzzEntity(const uint8_t* data, size_t size) {
    if (size < 1) return;
    uint8_t format = static_cast<uint8_t>(3 + data[0] % 3);
    bool party = (data[0] >> 2) & 1;
    size_t entitySize = format == 3 ? (party ? 100 : 80) : format == 4 ? (party ? 236 : 136) : (party ? 220 : 136);
    uint8_t buffer[entity::kMaxPartySize + 16] = {};
    std::memcpy(buffer, data + 1, std::min(size - 1, entitySize));
    exerciseEntity(format, buffer, entitySize, (data[0] >> 3) & 1 || size < 16);
    // Encrypted view: the stored form must survive decrypt(encrypt(x)).
    if (format >= 4) {
        uint8_t enc[entity::kMaxPartySize + 16];
        std::memcpy(enc, buffer, sizeof enc);
        pokemon::pk4::encrypt(enc, entitySize);
        pokemon::pk4::decrypt(enc, entitySize);
        if (std::memcmp(enc, buffer, entitySize) != 0) failIn("entity", "pk4 encrypt/decrypt is not a round trip");
        pokemon::pk4::isEmptySlot(buffer, entitySize);
    } else {
        uint8_t enc[entity::kMaxPartySize + 16];
        std::memcpy(enc, buffer, sizeof enc);
        pokemon::pk3::encrypt(enc);
        pokemon::pk3::decrypt(enc);
        if (std::memcmp(enc, buffer, 80) != 0) failIn("entity", "pk3 encrypt/decrypt is not a round trip");
        pokemon::pk3::isEmptySlot(buffer);
    }
}

// ----- Saves --------------------------------------------------------------------------

void exerciseSave(saves::SaveFile& save) {
    const char* t = "save_load";
    save.trainerName();
    save.trainerId();
    save.secretId();
    save.trainerGender();
    save.language();
    save.gameVersion();
    save.badgeCount();
    saves::TrainerData td = save.trainerData();
    save.setTrainerData(td);

    for (int pocket = 0; pocket < save.pocketCount(); ++pocket) {
        save.pocketInfo(pocket);
        save.pocketItems(pocket);
        std::vector<saves::BagSlot> slots = save.readPocket(pocket);
        if (static_cast<int>(slots.size()) > save.pocketInfo(pocket).capacity) failIn(t, "pocket larger than its capacity");
        for (const saves::BagSlot& s : slots) save.itemName(s.item);
        save.setPocket(pocket, slots);
    }

    int party = save.partyCount();
    if (party < 0 || party > saves::SaveFile::kPartyMax) failIn(t, "party count out of range");
    int checked = 0;
    uint8_t buffer[entity::kMaxPartySize + 16];
    for (int slot = 0; slot < party; ++slot) {
        std::memset(buffer, 0, sizeof buffer);
        if (save.readPartySlot(slot, buffer) != saves::SlotState::Valid) continue;
        exerciseEntity(save.format(), buffer, save.partySize(), checked++ < 2);
        save.writePartySlot(slot, buffer);
    }
    for (int box = 0; box < save.boxCount(); ++box) {
        save.boxName(box);
        for (int slot = 0; slot < saves::SaveFile::kSlotsPerBox; ++slot) {
            std::memset(buffer, 0, sizeof buffer);
            saves::SlotState state = save.readBoxSlot(box, slot, buffer);
            if (state != saves::SlotState::Valid) continue;
            exerciseEntity(save.format(), buffer, save.storedSize(), checked++ < 4);
            save.writeBoxSlot(box, slot, buffer);
            if (slot == 3) save.clearBoxSlot(box, slot);
        }
    }
    save.setBoxName(0, "FUZZ");
    // Out-of-range accesses are refused, not crashes.
    save.readBoxSlot(-1, 0, buffer);
    save.readBoxSlot(0, 30, buffer);
    save.readBoxSlot(save.boxCount(), 0, buffer);
    save.readPartySlot(6, buffer);
    save.readPartySlot(-1, buffer);

    save.removePartySlot(0);
    std::memset(buffer, 0, sizeof buffer);
    save.appendParty(buffer);

    save.refreshChecksums();
    if (!save.checksumsValid()) failIn(t, "checksums invalid after refreshChecksums");
    saves::LoadResult result = saves::LoadResult::Ok;
    std::unique_ptr<saves::SaveFile> again = saves::loadSaveFile(save.data(), &result);
    if (!again) failIn(t, "a save that was refreshed no longer loads");
    if (again->data() != save.data()) failIn(t, "reloading changed the data");
}

void loadAndExercise(const std::vector<uint8_t>& bytes) {
    saves::LoadResult result;
    std::unique_ptr<saves::SaveFile> save = saves::loadSaveFile(bytes, &result);
    if (save) exerciseSave(*save);
}

void fuzzSaveLoad(const uint8_t* data, size_t size) {
    std::vector<uint8_t> bytes(data, data + size);
    loadAndExercise(bytes);
    // The same with the checksums fixed, so the mutation reaches the parsers.
    std::vector<uint8_t> repaired = bytes;
    repairSave(repaired);
    if (repaired != bytes) loadAndExercise(repaired);
}

void fuzzSaveProbe(const uint8_t* data, size_t size) {
    // probeSave works on files; the size filter rejects most inputs before reading.
    const char* dir = std::getenv("TMPDIR");
    std::string path = std::string(dir && *dir ? dir : "/tmp") + "/pktool-fuzz-probe-" + std::to_string(::getpid()) + ".sav";
    std::vector<uint8_t> bytes(data, data + size);
    repairSave(bytes);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    saves::SaveSummary summary;
    saves::probeSave(path, summary);
    std::remove(path.c_str());
}

// ----- Text ---------------------------------------------------------------------------

void fuzzText(const uint8_t* data, size_t size) {
    if (size < 1) return;
    const char* t = "text";
    size_t maxChars = 1 + data[0] % 24;
    uint8_t language = static_cast<uint8_t>(data[0] >> 5);  // 0-7: Japanese is 1
    const uint8_t* body = data + 1;
    size_t bodySize = size - 1;

    // Decoders: a buffer of exactly maxChars glyphs (no terminator guaranteed).
    std::vector<uint8_t> glyphs(maxChars * 2, 0);
    std::memcpy(glyphs.data(), body, std::min(bodySize, glyphs.size()));
    std::string d4 = text::decodeGen4(glyphs.data(), maxChars);
    std::string d5 = text::decodeGen5(glyphs.data(), maxChars);
    std::vector<uint8_t> chars3(maxChars, 0);
    std::memcpy(chars3.data(), body, std::min(bodySize, chars3.size()));
    std::string d3 = text::decodeGen3(chars3.data(), maxChars, language);

    // Encoders take any bytes, valid UTF-8 or not.
    std::string input(reinterpret_cast<const char*>(body), bodySize);
    for (const std::string* s : {&input, &d4, &d5, &d3}) {
        std::vector<uint8_t> out4(maxChars * 2 + 2, 0xAB);
        size_t n4 = text::encodeGen4(*s, out4.data(), maxChars + 1, maxChars);
        if (n4 > maxChars) failIn(t, "encodeGen4 wrote more than maxChars");
        std::vector<uint8_t> out5(maxChars * 2 + 2, 0xAB);
        size_t n5 = text::encodeGen5(*s, out5.data(), maxChars + 1, maxChars);
        if (n5 > maxChars) failIn(t, "encodeGen5 wrote more than maxChars");
        std::vector<uint8_t> out3(maxChars + 1, 0xAB);
        size_t n3 = text::encodeGen3(*s, out3.data(), maxChars + 1, maxChars, language);
        if (n3 > maxChars) failIn(t, "encodeGen3 wrote more than maxChars");

        // Decode(encode(x)) is stable after one round.
        std::string a = text::decodeGen4(out4.data(), maxChars);
        std::vector<uint8_t> again(maxChars * 2 + 2, 0);
        text::encodeGen4(a, again.data(), maxChars + 1, maxChars);
        if (text::decodeGen4(again.data(), maxChars) != a) failIn(t, "Gen IV text is not stable");
        std::string b = text::decodeGen5(out5.data(), maxChars);
        std::fill(again.begin(), again.end(), 0);
        text::encodeGen5(b, again.data(), maxChars + 1, maxChars);
        if (text::decodeGen5(again.data(), maxChars) != b) failIn(t, "Gen V text is not stable");
        std::string c = text::decodeGen3(out3.data(), maxChars, language);
        std::vector<uint8_t> again3(maxChars + 1, 0);
        text::encodeGen3(c, again3.data(), maxChars + 1, maxChars, language);
        if (text::decodeGen3(again3.data(), maxChars, language) != c) failIn(t, "Gen III text is not stable");
    }
}

// ----- Showdown -----------------------------------------------------------------------

void fuzzShowdown(const uint8_t* data, size_t size) {
    std::string text(reinterpret_cast<const char*>(data), size);
    std::vector<showdown::Set> sets = showdown::parse(text);
    Rng rng(hashBytes(data, size));
    int built = 0;
    for (const showdown::Set& s : sets) {
        showdown::findSpecies(s.title);
        showdown::findMove(s.nickname);
        showdown::findItem(s.nickname);
        showdown::findAbility(s.nickname);
        showdown::findNature(s.nickname);
        if (s.species == 0 || built >= 2) continue;
        if (s.level > 100) failIn("showdown", "set level above 100");
        ++built;
        int generation = 3 + static_cast<int>(rng.below(3));
        showdown::Imported imp = showdown::buildGen4(s, randomTrainer(rng, generation), rng.u32(), 1);
        if (imp.pokemon.species == 0) continue;  // refused: species does not exist in the game
        if (imp.pokemon.format != generation) failIn("showdown", "imported Pokemon has the wrong format");
        if (imp.report.overall() != legality::check(imp.pokemon).overall()) failIn("showdown", "import report disagrees with the checker");
        std::string back = showdown::format(imp.pokemon);
        showdown::parse(back);
    }
    // The name lookups take any bytes too.
    showdown::findSpecies(text);
    showdown::findMove(text);
    showdown::findItem(text);
    showdown::findAbility(text);
    showdown::findNature(text);
}

// ----- Bank ---------------------------------------------------------------------------

void fuzzBank(const uint8_t* data, size_t size) {
    std::vector<uint8_t> bytes(data, data + size);
    bank::Bank b;
    bank::Bank::LoadResult result = b.loadBytes(bytes);
    // A file with a bad CRC is rejected; make the CRC right so the body is parsed.
    if (result == bank::Bank::LoadResult::Corrupt && bytes.size() >= 10) {
        bytes[0] = 'P';
        bytes[1] = 'K';
        bytes[2] = 'B';
        bytes[3] = 'K';
        util::writeU16(&bytes[bytes.size() - 2], util::crc16Ccitt(bytes.data(), bytes.size() - 2));
        result = b.loadBytes(bytes);
    }
    if (result == bank::Bank::LoadResult::Corrupt && !b.readOnly()) failIn("bank", "a corrupt Bank must be read-only");
    if (result != bank::Bank::LoadResult::Ok) return;

    int count = b.count();
    if (count < 0 || count > bank::Bank::kBoxCount * bank::Bank::kSlotsPerBox) failIn("bank", "absurd Pokemon count");
    for (int box = 0; box < bank::Bank::kBoxCount; ++box) {
        b.boxName(box);
        for (int slot = 0; slot < bank::Bank::kSlotsPerBox; ++slot) {
            uint8_t fmt = b.format(box, slot);
            if (fmt == 0) continue;
            if (fmt < 3 || fmt > 5) failIn("bank", "slot with an invalid format");
            exerciseEntity(fmt, b.entity(box, slot), fmt == 3 ? 80 : 136, false);
        }
    }
    bank::Bank::SearchFilter filter;
    filter.text = "a";
    b.search(filter);
    filter.shinyOnly = true;
    filter.format = 4;
    b.search(filter);
    b.setBoxName(2, "A very long name with \xC3\xA9\xE3\x82\xBF");
    b.sort(bank::Bank::SortKey::Name, -1);
    b.sort(bank::Bank::SortKey::Level, 3);
    b.sort(bank::Bank::SortKey::Dex, -1);
    if (b.count() != count) failIn("bank", "sorting changed the number of Pokemon");
    b.release(0, 0);

    // The serialized form loads back identically.
    std::vector<uint8_t> out = b.serialize();
    bank::Bank c;
    if (c.loadBytes(out) != bank::Bank::LoadResult::Ok) failIn("bank", "serialize() output does not load");
    if (c.serialize() != out) failIn("bank", "serialize/load is not a round trip");
}

// ----- Settings -----------------------------------------------------------------------

void fuzzSettings(const uint8_t* data, size_t size) {
    std::string text(reinterpret_cast<const char*>(data), size);
    io::Settings s;
    io::parseSettings(text, s);
    if (s.newLevel < io::kMinNewLevel || s.newLevel > io::kMaxNewLevel) failIn("settings", "new_level out of range");
    io::Settings back;
    io::parseSettings(io::settingsToText(s), back);
    if (back != s) failIn("settings", "settings text is not a round trip");
    // A second pass over the same text changes nothing more.
    io::Settings s2 = s;
    io::parseSettings(text, s2);
    if (s2 != s) failIn("settings", "parsing the same text twice gives different settings");
}

// ----- Legality ---------------------------------------------------------------------------

void fuzzLegality(const uint8_t* data, size_t size) {
    if (size < 1) return;
    pokemon::Pokemon p;
    uint8_t mode = data[0] >> 6;
    if (mode == 0 && size >= 9) {
        // A structured Pokemon from the creator, requests included.
        uint64_t seed = 0;
        std::memcpy(&seed, data + 1, 8);
        Rng rng(seed);
        p = randomPokemon(rng, 3 + data[0] % 3);
    } else {
        uint8_t format = static_cast<uint8_t>(3 + data[0] % 3);
        size_t entitySize = format == 3 ? 80 : 136;
        uint8_t buffer[136] = {};
        std::memcpy(buffer, data + 1, std::min(size - 1, entitySize));
        p = entity::decode(format, buffer, entitySize);
    }
    legality::Report r = legality::check(p);
    legality::LegalizeResult l = legality::legalize(p, 1);
    if (l.pokemon.format != p.format) failIn("legality", "legalize changed the format");
    if (l.report.overall() != legality::check(l.pokemon).overall())
        failIn("legality", "LEGALIZE report disagrees with the checker");
    if (l.unresolved.empty() && !l.report.legal()) failIn("legality", "LEGALIZE: not legal but nothing reported as unresolved");
    // The result survives the entity codec.
    uint8_t raw[136];
    encodeStored(l.pokemon, raw);
    pokemon::Pokemon back = entity::decode(l.pokemon.format, raw, l.pokemon.format == 3 ? 80 : 136);
    if (back.species != l.pokemon.species || back.pid != l.pokemon.pid) failIn("legality", "codec changed species or PID");
    (void)r;
}

// ----- Ribbon and trainer checks, Colosseum/XD locks ------------------------------------------

void fuzzChecks(const uint8_t* data, size_t size) {
    if (size < 1) return;
    uint8_t format = static_cast<uint8_t>(3 + data[0] % 3);
    size_t entitySize = format == 3 ? 80 : 136;
    uint8_t buffer[136] = {};
    std::memcpy(buffer, data + 1, std::min(size - 1, entitySize));
    pokemon::Pokemon p = entity::decode(format, buffer, entitySize);
    // Ribbons: the analysis and the fix agree (after the fix nothing is invalid).
    legality::analyzeRibbons(p);
    pokemon::Pokemon fixed = p;
    std::vector<std::string> changes;
    legality::fixRibbons(fixed, changes);
    for (const legality::RibbonIssue& issue : legality::analyzeRibbons(fixed)) {
        if (issue.invalid) failIn("checks", "fixRibbons left an invalid ribbon");
    }
    legality::Report r;
    legality::checkRibbons(p, r);
    legality::checkTrainer(p, r);
    uint32_t word = 0;
    std::memcpy(&word, buffer, 4);
    legality::rsTrainerIdPossible(static_cast<uint16_t>(word), static_cast<uint16_t>(word >> 16));
    // Shadow locks: any encounter row against an arbitrary origin and shiny value.
    int count = data::shadowEnc3Count();
    if (count > 0) {
        const data::ShadowEnc3& enc = data::shadowEnc3(buffer[8] % count);
        uint32_t origin = 0;
        std::memcpy(&origin, buffer + 4, 4);
        legality::shadow::locksSatisfied(legality::shadow::setOf(enc), origin, buffer[9] & 1 ? legality::shadow::kNoTsv : buffer[10]);
    }
}

// ----- Card files, language packs, CJK font ------------------------------------------------------

std::string scratchDir() {
    static std::string dir;
    if (dir.empty()) {
        char pattern[] = "/tmp/pktool-fuzz-XXXXXX";
        const char* made = mkdtemp(pattern);
        dir = made ? made : "/tmp";
        io::makeDirectories(dir + "/lang");
        io::makeDirectories(dir + "/fonts");
    }
    return dir;
}

void fuzzCardFile(const uint8_t* data, size_t size) {
    if (size < 1) return;
    static const char* const kExt[] = {".pgt", ".PCD", ".pgf", ".txt"};
    std::string ext = kExt[data[0] % 4];
    std::unique_ptr<pokemon::cardfile::Card> card = pokemon::cardfile::parse(data + 1, size - 1, ext, "fuzz");
    if (!card) failIn("card_file", "parse returned null");
    if (card->status != pokemon::cardfile::Status::Ok) return;
    Rng rng(hashBytes(data, size));
    uint8_t raw[136];
    pokemon::Pokemon p;
    if (card->generation == 4) {
        p = pokemon::gift4::receive(card->event4, {26, 9, 29}, rng.u32(), raw);
    } else if (card->generation == 5) {
        p = pokemon::gift5::receive(card->event5, randomTrainer(rng, 5), rng.u32(), raw);
    } else {
        failIn("card_file", "an Ok card with a generation other than 4 or 5");
    }
    legality::check(p);
}

void fuzzLanguagePack(const uint8_t* data, size_t size) {
    if (size < 1) return;
    const uint8_t language = data::kNameLanguages[data[0] % data::kNameLanguageCount];
    std::string dir = scratchDir();
    std::string path = dir + "/lang/" + data::languageCode(language) + ".bin";
    io::safeWriteFile(path, std::vector<uint8_t>(data + 1, data + size));
    data::setLanguageDirs({dir});
    if (data::setNameLanguage(language)) {
        for (uint16_t id : {0, 1, 25, 151, 493, 649, 650, 65535}) {
            data::speciesName(id);
            data::moveName(id);
            data::abilityName(id);
            data::itemName(id);
            data::speciesNameIn(language, id);
            data::defaultNickname(id, language, 3);
            data::defaultNickname(id, language, 4);
        }
        for (uint8_t nature = 0; nature < 26; ++nature) data::natureName(nature);
    }
    data::languageAvailable(language);
    data::setNameLanguage(data::kLangEnglish);
    data::setLanguageDirs({});
}

void fuzzCjkFont(const uint8_t* data, size_t size) {
    std::string dir = scratchDir();
    io::safeWriteFile(dir + "/fonts/cjk.bin", std::vector<uint8_t>(data, data + size));
    if (gfx::openCjkFont({dir})) {
        const uint8_t* bits = nullptr;
        for (uint32_t cp : {0x20u, 0x3042u, 0x30A2u, 0x4E00u, 0x7F8Eu, 0xAC00u, 0xD7A3u, 0xFFFFu, 0x10FFFFu}) {
            gfx::cjkHas(cp);
            gfx::cjkGlyph(gfx::kFontSmall, cp, &bits);
            gfx::cjkGlyph(gfx::kFontLarge, cp, &bits);
        }
        // Whatever the index holds must be readable.
        for (uint32_t cp = 0x3000; cp < 0x3100; ++cp) gfx::cjkGlyph(gfx::kFontSmall, cp, &bits);
    }
    gfx::closeCjkFont();
}

}  // namespace

void fail(const char* target, const char* message) {
    std::fprintf(stderr, "FUZZ INVARIANT FAILED [%s]: %s\n", target, message);
    std::abort();
}

const std::vector<Target>& targets() {
    static const std::vector<Target> all = {
        {"save_load", fuzzSaveLoad}, {"save_probe", fuzzSaveProbe}, {"entity", fuzzEntity},
        {"text", fuzzText},          {"showdown", fuzzShowdown},    {"bank", fuzzBank},
        {"settings", fuzzSettings},  {"legality", fuzzLegality},    {"checks", fuzzChecks},
        {"card_file", fuzzCardFile}, {"language_pack", fuzzLanguagePack}, {"cjk_font", fuzzCjkFont},
    };
    return all;
}

const Target* findTarget(const std::string& name) {
    for (const Target& t : targets()) {
        if (name == t.name) return &t;
    }
    return nullptr;
}

}  // namespace pktool::fuzz
