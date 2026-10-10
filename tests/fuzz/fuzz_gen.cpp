// Generators for the fuzz targets and the mass test: random trainers and
// Pokemon, encoders, checksum repair for mutated saves and the synthetic seed
// inputs (never derived from real saves).

#include <algorithm>
#include <cstring>

#include "core/bank/bank.h"
#include "core/data/events4.h"
#include "core/data/events5.h"
#include "core/data/items.h"
#include "core/io/file_io.h"
#include "core/pokemon/pk4.h"
#include "core/data/moves.h"
#include "core/data/names.h"
#include "core/legality/legality.h"
#include "core/legality/legalize.h"
#include "core/pokemon/create4.h"
#include "core/pokemon/edit4.h"
#include "core/pokemon/entity.h"
#include "core/saves/save_file.h"
#include "core/showdown/showdown.h"
#include "core/util/bytes.h"
#include "core/util/crc16.h"
#include "tests/fake_saves.h"
#include "tests/fuzz/fuzz_targets.h"

namespace pktool::fuzz {

namespace {

namespace create4 = pokemon::create4;
namespace edit4 = pokemon::edit4;

constexpr uint8_t kVersions3[] = {1, 2, 3, 4, 5};
constexpr uint8_t kVersions4[] = {7, 8, 10, 11, 12};
constexpr uint8_t kVersions5[] = {20, 21, 22, 23};
// Korean (8) is left out: there are no Korean Gen III games, so the generator
// would make Pokemon no game can hold.
constexpr uint8_t kLanguages[] = {1, 2, 3, 4, 5, 7};

uint16_t maxSpecies(int generation) {
    return generation >= 5 ? data::kMaxSpeciesGen5 : generation == 4 ? data::kMaxSpeciesGen4 : 386;
}

std::string randomName(Rng& rng, size_t maxLen) {
    static const char kLetters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string s;
    size_t n = 1 + rng.below(static_cast<uint32_t>(maxLen));
    for (size_t i = 0; i < n; ++i) s += kLetters[rng.below(sizeof kLetters - 1)];
    return s;
}

}  // namespace

create4::Trainer randomTrainer(Rng& rng, int generation) {
    create4::Trainer t;
    t.name = randomName(rng, 7);
    t.tid = static_cast<uint16_t>(rng.u32());
    t.sid = static_cast<uint16_t>(rng.u32());
    t.gender = rng.chance(50) ? pokemon::Gender::Female : pokemon::Gender::Male;
    t.language = kLanguages[rng.below(sizeof kLanguages)];
    // The Japanese Gen III charset has no Latin letters (the checker reports an
    // OT name that cannot be stored as empty).
    if (generation == 3 && t.language == 1) t.name = "\xE3\x82\xA2\xE3\x82\xB7\xE3\x83\xA5";  // アシュ
    if (generation == 3) {
        t.version = kVersions3[rng.below(sizeof kVersions3)];
    } else if (generation == 4) {
        t.version = kVersions4[rng.below(sizeof kVersions4)];
    } else {
        t.version = kVersions5[rng.below(sizeof kVersions5)];
    }
    t.today = {static_cast<uint8_t>(5 + rng.below(30)), static_cast<uint8_t>(1 + rng.below(12)),
               static_cast<uint8_t>(1 + rng.below(28))};
    return t;
}

pokemon::Pokemon randomPokemon(Rng& rng, int generation, bool clean) {
    create4::Trainer trainer = randomTrainer(rng, generation);
    uint16_t species = static_cast<uint16_t>(1 + rng.below(maxSpecies(generation)));
    uint32_t seed = rng.u32();
    pokemon::Pokemon p;
    if (rng.chance(5)) {
        p = create4::newEgg(trainer, species, seed);
    } else {
        uint8_t level = static_cast<uint8_t>(1 + rng.below(100));
        if (rng.chance(40)) {
            // The lowest level where the species has a legal source, the case
            // the creator offers by default.
            level = static_cast<uint8_t>(generation == 3 ? 5 : 1 + rng.below(30));
        }
        if (clean) {
            uint8_t lowest = generation == 3 ? legality::minCreateLevel3(species)
                             : generation == 4 ? legality::minCreateLevel4(species)
                                               : legality::minCreateLevel5(species);
            level = std::max<uint8_t>(level, std::min<uint8_t>(lowest, 100));
        }
        p = create4::newPokemon(trainer, species, level, seed);
    }
    if (p.isEgg) {
        // Eggs only get a few requests: they cannot have moves chosen or a level.
        if (rng.chance(50)) edit4::setNature(p, static_cast<uint8_t>(rng.below(25)));
        if (rng.chance(30)) edit4::setShiny(p, rng.chance(50));
        return p;
    }

    if (rng.chance(60)) edit4::setNature(p, static_cast<uint8_t>(rng.below(25)));
    if (rng.chance(40)) edit4::setShiny(p, rng.chance(60));
    if (rng.chance(40)) edit4::setGender(p, static_cast<pokemon::Gender>(rng.below(3)));
    if (rng.chance(40)) edit4::setAbilitySlot(p, static_cast<uint8_t>(rng.below(generation >= 5 ? 3 : 2)));
    if (rng.chance(50)) {
        for (int s = 0; s < pokemon::kStatCount; ++s) edit4::setIv(p, static_cast<pokemon::Stat>(s), static_cast<uint8_t>(rng.below(32)));
    }
    if (rng.chance(30)) {
        for (int s = 0; s < pokemon::kStatCount; ++s) edit4::setEv(p, static_cast<pokemon::Stat>(s), static_cast<uint8_t>(rng.below(90)));
    }
    if (clean) return p;
    if (rng.chance(60)) {
        // Random moves of the generation (any move, learnable or not).
        int count = 1 + static_cast<int>(rng.below(4));
        for (int i = 0; i < count; ++i) {
            edit4::setMove(p, i, static_cast<uint16_t>(1 + rng.below(data::maxMove(p.format))));
        }
    }
    if (rng.chance(30)) p.heldItem = static_cast<uint16_t>(rng.below(generation >= 5 ? 640 : 540));
    if (rng.chance(30)) p.met.ball = static_cast<uint8_t>(1 + rng.below(26));
    if (rng.chance(20)) edit4::setSpecies(p, static_cast<uint16_t>(1 + rng.below(maxSpecies(generation))));
    if (rng.chance(20)) edit4::setLevel(p, static_cast<uint8_t>(1 + rng.below(100)));
    if (rng.chance(10)) {
        // Outright broken values, like a hand-edited entity.
        p.met.metLocation = static_cast<uint16_t>(rng.below(rng.chance(50) ? 256 : 70000));
        p.met.metLevel = static_cast<uint8_t>(rng.below(128));
        p.friendship = static_cast<uint8_t>(rng.below(256));
        // (Gen III keeps its language: Latin OT names cannot be stored with Japanese.)
        if (generation != 3) p.language = static_cast<uint8_t>(rng.below(10));
        p.originGame = static_cast<uint8_t>(rng.below(32));
        p.pokerus = static_cast<uint8_t>(rng.below(256));
        p.ability = static_cast<uint16_t>(rng.below(200));
        for (int s = 0; s < pokemon::kStatCount; ++s) p.evs[s] = static_cast<uint8_t>(rng.below(256));
        p.isNicknamed = rng.chance(50);
        if (p.isNicknamed) p.nickname = randomName(rng, 10);
    }
    return p;
}

size_t encodeStored(const pokemon::Pokemon& p, uint8_t out[136]) {
    std::memset(out, 0, 136);
    create4::encode(p, out);
    return p.format == 3 ? 80 : 136;
}

// ----- Checksum repair -------------------------------------------------------

void repairSave(std::vector<uint8_t>& bytes) {
    if (bytes.size() >= 0x10000 && bytes.size() <= 0x20100) {
        // Gen III: 2 slots x 14 sectors of 0x1000 bytes, checksum at +0xFF6.
        for (size_t off = 0; off + fake::kSector <= bytes.size() && off < 28 * fake::kSector; off += fake::kSector) {
            util::writeU16(&bytes[off + 0xFF6], fake::sectorChecksum(&bytes[off]));
        }
        return;
    }
    if (bytes.size() != 0x80000) return;
    // Gen IV: general + storage block of each partition, when the footer
    // (size, SDK magic) looks right for a known layout.
    struct Layout {
        uint32_t general, storageOffset, storageSize, footer;
    };
    static const Layout kLayouts[] = {
        {0xC100, 0xC100, 0x121E0, 0x14},
        {0xCF2C, 0xCF2C, 0x121E4, 0x14},
        {0xF628, 0xF700, 0x12310, 0x10},
    };
    for (const Layout& l : kLayouts) {
        for (size_t partition = 0; partition < 2; ++partition) {
            uint8_t* base = &bytes[partition * 0x40000];
            const struct {
                uint32_t offset, size;
            } blocks[] = {{0, l.general}, {l.storageOffset, l.storageSize}};
            for (const auto& b : blocks) {
                uint8_t* end = base + b.offset + b.size;
                uint32_t magic = util::readU32(end - 0x8);
                if (util::readU32(end - 0xC) == b.size && (magic == 0x20060623 || magic == 0x20070903)) {
                    util::writeU16(end - 2, util::crc16Ccitt(base + b.offset, b.size - l.footer));
                }
            }
        }
    }
    // Gen V: the checksum table that identifies the game, and the block CRCs
    // are only checked by checksumsValid(), which the targets also exercise.
    const struct {
        uint32_t table, length;
    } tables[] = {{0x23F00, 0x8C}, {0x25F00, 0x94}};
    for (const auto& t : tables) {
        util::writeU16(&bytes[t.table + t.length + 0xE], util::crc16Ccitt(&bytes[t.table], t.length));
    }
}

// ----- Seeds -----------------------------------------------------------------------

namespace {

std::vector<uint8_t> populatedSave(std::vector<uint8_t> blank, Rng& rng, int generation) {
    std::unique_ptr<saves::SaveFile> save = saves::loadSaveFile(std::move(blank));
    if (!save) return {};
    for (int i = 0; i < 40; ++i) {
        pokemon::Pokemon p = randomPokemon(rng, generation);
        uint8_t stored[136];
        encodeStored(p, stored);
        if (i < 3) {
            uint8_t party[pokemon::entity::kMaxPartySize] = {};
            std::memcpy(party, stored, save->storedSize());
            pokemon::entity::refreshParty(save->format(), party);
            save->appendParty(party);
        } else {
            save->writeBoxSlot(static_cast<int>(rng.below(static_cast<uint32_t>(save->boxCount()))),
                               static_cast<int>(rng.below(30)), stored);
        }
    }
    for (int pocket = 0; pocket < save->pocketCount(); ++pocket) {
        std::vector<uint16_t> items = save->pocketItems(pocket);
        std::vector<saves::BagSlot> slots;
        for (size_t i = 0; i < items.size() && i < 5; ++i) slots.push_back({items[i], static_cast<uint16_t>(1 + i)});
        save->setPocket(pocket, slots);
    }
    save->setBoxName(0, "SEED");
    save->refreshChecksums();
    return save->data();
}

std::vector<uint8_t> selectorPlusEntity(uint8_t selector, const pokemon::Pokemon& p) {
    uint8_t stored[136];
    size_t size = encodeStored(p, stored);
    std::vector<uint8_t> out(1 + size);
    out[0] = selector;
    std::memcpy(out.data() + 1, stored, size);
    return out;
}

const char* kShowdownSeed =
    "Chompy (Garchomp) (F) @ Choice Scarf\nAbility: Sand Veil\nLevel: 50\nShiny: Yes\n"
    "EVs: 4 HP / 252 Atk / 252 Spe\nJolly Nature\nIVs: 0 SpA\n- Earthquake\n- Dragon Claw\n- Stone Edge\n- Fire Fang\n\n"
    "Pikachu @ Light Ball  \nAbility: Static\n- Volt Tackle\n- Hidden Power [Ice]\n- Not A Move\n\n"
    "Nidoran-F\nLevel: 5\nTimid Nature\n- Tackle\n";

}  // namespace

std::vector<std::vector<uint8_t>> seeds(const std::string& target) {
    Rng rng(0xFEED);
    std::vector<std::vector<uint8_t>> out;
    if (target == "save_load" || target == "save_probe") {
        std::vector<uint8_t> blanks[] = {
            fake::blankGen3(saves::Game::RubySapphire), fake::blankGen3(saves::Game::Emerald),
            fake::blankGen3(saves::Game::FireRedLeafGreen), fake::blankGen4(fake::kDp),
            fake::blankGen4(fake::kPt), fake::blankGen4(fake::kHgss), fake::blankGen5(false), fake::blankGen5(true)};
        int index = 0;
        for (auto& blank : blanks) {
            int generation = index < 3 ? 3 : index < 6 ? 4 : 5;
            out.push_back(populatedSave(blank, rng, generation));
            out.push_back(std::move(blank));
            ++index;
        }
        out.erase(std::remove_if(out.begin(), out.end(), [](const auto& v) { return v.empty(); }), out.end());
    } else if (target == "entity" || target == "legality") {
        for (int generation = 3; generation <= 5; ++generation) {
            for (int i = 0; i < 6; ++i) {
                // Selector byte: low 2 bits = format (3, 4, 5), bit 2 = party data.
                out.push_back(selectorPlusEntity(static_cast<uint8_t>((generation - 3) | (i & 1) << 2),
                                                 randomPokemon(rng, generation)));
            }
        }
    } else if (target == "checks") {
        for (int generation = 3; generation <= 5; ++generation) {
            for (int i = 0; i < 8; ++i) {
                out.push_back(selectorPlusEntity(static_cast<uint8_t>(generation - 3), randomPokemon(rng, generation)));
            }
        }
    } else if (target == "card_file") {
        for (int i = 0; i < 3 && i < data::events4Count(); ++i) {
            const data::Event4& card = data::event4(i * 7 % data::events4Count());
            std::vector<uint8_t> pgt(0x104, 0);
            util::writeU16(pgt.data(), 1);
            uint8_t stored[136];
            std::memcpy(stored, card.templatePk4, sizeof stored);
            pokemon::pk4::encrypt(stored, sizeof stored);
            std::memcpy(pgt.data() + 8, stored, sizeof stored);
            std::vector<uint8_t> file = {0};
            file.insert(file.end(), pgt.begin(), pgt.end());
            out.push_back(file);
            std::vector<uint8_t> pcd(0x358, 0);
            std::memcpy(pcd.data(), pgt.data(), pgt.size());
            util::writeU16(pcd.data() + 0x150, card.cardId);
            for (int k = 0; k < 0x48; k += 2) util::writeU16(pcd.data() + 0x104 + k, 0xFFFF);
            std::vector<uint8_t> file2 = {1};
            file2.insert(file2.end(), pcd.begin(), pcd.end());
            out.push_back(file2);
        }
        for (int i = 0; i < 3 && i < data::events5Count(); ++i) {
            const data::Event5& card = data::event5(i * 11 % data::events5Count());
            std::vector<uint8_t> pgf(0xCC, 0);
            std::memcpy(pgf.data(), card.pgf, sizeof card.pgf);
            util::writeU16(pgf.data() + 0xB0, card.cardId);
            pgf[0xB3] = 1;
            std::vector<uint8_t> file = {2};
            file.insert(file.end(), pgf.begin(), pgf.end());
            out.push_back(file);
        }
    } else if (target == "language_pack" || target == "cjk_font") {
        // The shipped assets are the seeds (repository data, not user data).
        const char* codes[] = {"en", "ja", "fr", "it", "de", "es", "ko"};
        std::string base = std::string(PKTOOL_SOURCE_DIR) + "/assets/nitrofs/";
        if (target == "cjk_font") {
            std::vector<uint8_t> bytes;
            if (io::readFile(base + "fonts/cjk.bin", bytes)) out.push_back(bytes);
        } else {
            for (int i = 0; i < 7; ++i) {
                std::vector<uint8_t> bytes;
                if (!io::readFile(base + "lang/" + codes[i] + ".bin", bytes)) continue;
                // Selector: index in kNameLanguages (English, Japanese, French, Italian, German, Spanish, Korean).
                bytes.insert(bytes.begin(), static_cast<uint8_t>(i));
                out.push_back(bytes);
            }
        }
        if (out.empty()) out.push_back(std::vector<uint8_t>(32, 0));
    } else if (target == "text") {
        const char* strings[] = {"PKTOOL", "Chompy", "Farfetch'd", "Nidoran\xE2\x99\x80", "\xC3\xA9\xC3\xA8 Mr. Mime", "\xE3\x82\xBF\xE3\x83\x9E\xE3\x82\xB4"};
        for (const char* s : strings) {
            std::vector<uint8_t> v = {0};
            for (const char* c = s; *c; ++c) v.push_back(static_cast<uint8_t>(*c));
            out.push_back(v);
        }
        uint8_t raw[40];
        for (int i = 0; i < 40; ++i) raw[i] = static_cast<uint8_t>(rng.u32());
        out.push_back(std::vector<uint8_t>(raw, raw + 40));
    } else if (target == "showdown") {
        out.push_back(std::vector<uint8_t>(kShowdownSeed, kShowdownSeed + std::strlen(kShowdownSeed)));
    } else if (target == "bank") {
        bank::Bank b;
        b.load("/nonexistent/pktool-fuzz-seed.pkb");  // a missing file gives an empty, writable bank
        std::vector<uint8_t> blank = b.serialize();
        for (int i = 0; i < 12; ++i) {
            pokemon::Pokemon p = randomPokemon(rng, 3 + i % 3);
            uint8_t stored[136];
            encodeStored(p, stored);
            b.set(i / 6, i % 30, p.format, stored);
        }
        b.setBoxName(1, "Mine");
        out.push_back(b.serialize());
        out.push_back(blank);
    } else if (target == "settings") {
        const char* text = "# PKTool settings\nsort_order=id\nnew_level=42\nbogus line\n";
        out.push_back(std::vector<uint8_t>(text, text + std::strlen(text)));
    }
    return out;
}

std::vector<uint8_t> mutate(Rng& rng, const std::vector<uint8_t>& base, const std::vector<uint8_t>* other,
                            size_t maxLen) {
    std::vector<uint8_t> v = base;
    int rounds = 1 + static_cast<int>(rng.below(4));
    for (int r = 0; r < rounds; ++r) {
        if (v.empty()) {
            v.resize(1 + rng.below(32));
            for (auto& b : v) b = static_cast<uint8_t>(rng.u32());
        }
        // Half of the positions move to the next non-zero byte nearby: saves
        // are mostly zeros and the interesting bytes are the others.
        size_t pos = rng.below(static_cast<uint32_t>(v.size()));
        if (rng.chance(50)) {
            for (size_t k = 0; k < 512 && pos + 1 < v.size() && v[pos] == 0; ++k) ++pos;
        }
        static const uint32_t kInteresting[] = {0, 1, 0x7F, 0x80, 0xFF, 0x100, 0x7FFF, 0x8000, 0xFFFF, 0x10000, 0x7FFFFFFF, 0xFFFFFFFF};
        switch (rng.below(11)) {
            case 0: v[pos] ^= static_cast<uint8_t>(1u << rng.below(8)); break;
            case 1: v[pos] = static_cast<uint8_t>(rng.u32()); break;
            case 2: v[pos] = static_cast<uint8_t>(v[pos] + static_cast<int>(rng.below(9)) - 4); break;
            case 3: {
                uint32_t value = kInteresting[rng.below(sizeof kInteresting / sizeof kInteresting[0])];
                size_t n = size_t{1} << rng.below(3);
                for (size_t i = 0; i < n && pos + i < v.size(); ++i) v[pos + i] = static_cast<uint8_t>(value >> (8 * i));
                break;
            }
            case 4: {
                size_t n = 1 + rng.below(16);
                for (size_t i = 0; i < n && pos + i < v.size(); ++i) v[pos + i] = static_cast<uint8_t>(rng.u32());
                break;
            }
            case 5: {
                size_t n = 1 + rng.below(64);
                size_t from = rng.below(static_cast<uint32_t>(v.size()));
                for (size_t i = 0; i < n && pos + i < v.size() && from + i < v.size(); ++i) v[pos + i] = v[from + i];
                break;
            }
            case 6: {
                size_t n = std::min<size_t>(1 + rng.below(64), v.size() - pos);
                v.erase(v.begin() + static_cast<long>(pos), v.begin() + static_cast<long>(pos + n));
                break;
            }
            case 7: {
                size_t n = 1 + rng.below(32);
                if (v.size() + n <= maxLen) {
                    std::vector<uint8_t> ins(n);
                    for (auto& b : ins) b = rng.chance(50) ? 0 : static_cast<uint8_t>(rng.u32());
                    v.insert(v.begin() + static_cast<long>(pos), ins.begin(), ins.end());
                }
                break;
            }
            case 8:
                if (other && !other->empty()) {
                    size_t n = 1 + rng.below(256);
                    size_t from = rng.below(static_cast<uint32_t>(other->size()));
                    for (size_t i = 0; i < n && pos + i < v.size() && from + i < other->size(); ++i) v[pos + i] = (*other)[from + i];
                }
                break;
            case 9: v.resize(std::max<size_t>(1, v.size() - std::min<size_t>(v.size() - 1, rng.below(8)))); break;
            default:
                if (rng.chance(10) && v.size() < maxLen) {
                    v.resize(std::min(maxLen, v.size() + 1 + rng.below(8)));
                } else {
                    v[pos] = static_cast<uint8_t>(rng.u32());
                }
        }
    }
    if (v.size() > maxLen) v.resize(maxLen);
    return v;
}

}  // namespace pktool::fuzz
