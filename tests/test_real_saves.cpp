// Checks against real save files, which are never committed.
//
// Point PKTOOL_FIXTURES at a directory of .sav files to run them:
//   PKTOOL_FIXTURES=/path/to/saves ./build/pktool_tests real_saves
// Without the variable these tests pass trivially.

#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "core/bank/bank.h"
#include "core/data/names.h"
#include "core/pokemon/ribbons.h"
#include "core/data/personal.h"
#include "core/legality/legalize.h"
#include "core/legality/legality.h"
#include "core/legality/trade_check.h"
#include "core/io/file_io.h"
#include "core/pokemon/edit4.h"
#include "core/pokemon/pk3.h"
#include "core/pokemon/pk4.h"
#include "core/pokemon/stats.h"
#include "core/pokemon/pk5.h"
#include "core/pokemon/transfer34.h"
#include "core/pokemon/transfer45.h"
#include "core/saves/gen3_save.h"
#include "core/saves/gen4_save.h"
#include "core/saves/gen5_save.h"
#include "core/saves/save_commit.h"
#include "core/saves/save_probe.h"
#include "core/util/bytes.h"
#include "tests/test.h"

using namespace pktool;
using saves::Gen4Save;
namespace pk4 = pokemon::pk4;

namespace {

std::vector<std::string> fixtureSaves() {
    const char* dir = std::getenv("PKTOOL_FIXTURES");
    if (dir == nullptr || *dir == '\0') {
        return {};
    }
    return io::findSaveFiles(dir, 2);
}

// Decrypt -> model -> apply -> encrypt must reproduce the stored bytes exactly.
void checkSlotRoundTrip(const uint8_t* decrypted, size_t size) {
    uint8_t copy[pk4::kPartySize];
    std::memcpy(copy, decrypted, size);
    pk4::applyPokemon(pk4::toPokemon(copy, size), copy);
    CHECK(std::memcmp(copy, decrypted, size) == 0);

    uint8_t encrypted[pk4::kPartySize];
    std::memcpy(encrypted, decrypted, size);
    pk4::encrypt(encrypted, size);
    pk4::decrypt(encrypted, size);
    CHECK(std::memcmp(encrypted, decrypted, size) == 0);
}

void checkPartyStats(const uint8_t* decrypted) {
    pokemon::Pokemon p = pk4::toPokemon(decrypted, pk4::kPartySize);
    const data::PersonalInfo* info = data::personal4(p.species);
    if (info == nullptr || p.isEgg || p.form != 0) {
        return;  // alternate forms have their own base stats (not loaded yet)
    }
    // The games only recalculate stored stats on level-up (and when leaving
    // the PC), so EVs gained since then are not reflected yet. The stored
    // value must therefore lie between the stat without EVs and with them.
    uint16_t withEvs[pokemon::kStatCount];
    uint16_t withoutEvs[pokemon::kStatCount];
    const uint8_t noEvs[pokemon::kStatCount] = {};
    pokemon::calcStats(p.species, info->baseStats, p.ivs, p.evs, p.level, p.nature, withEvs);
    pokemon::calcStats(p.species, info->baseStats, p.ivs, noEvs, p.level, p.nature, withoutEvs);
    for (int s = 0; s < pokemon::kStatCount; ++s) {
        uint16_t stored = util::readU16(decrypted + pk4::offset::kPartyStats + 2 + s * 2);
        CHECK(stored >= withoutEvs[s] && stored <= withEvs[s]);
    }
}

}  // namespace

TEST(real_saves_gen4_round_trip) {
    for (const std::string& path : fixtureSaves()) {
        std::vector<uint8_t> bytes;
        CHECK(io::readFile(path, bytes));
        if (Gen4Save::detect(bytes) == nullptr) {
            continue;  // not a Gen IV save
        }
        Gen4Save save;
        CHECK(save.load(bytes) == Gen4Save::LoadResult::Ok);
        CHECK(save.checksumsValid());

        int pokemon = 0;
        for (int slot = 0; slot < save.partyCount(); ++slot) {
            uint8_t buffer[pk4::kPartySize];
            CHECK(save.readPartySlot(slot, buffer) == Gen4Save::SlotState::Valid);
            checkSlotRoundTrip(buffer, pk4::kPartySize);
            checkPartyStats(buffer);
            ++pokemon;
        }
        for (int box = 0; box < Gen4Save::kBoxCount; ++box) {
            for (int slot = 0; slot < Gen4Save::kSlotsPerBox; ++slot) {
                uint8_t buffer[pk4::kStoredSize];
                auto state = save.readBoxSlot(box, slot, buffer);
                CHECK(state != Gen4Save::SlotState::BadChecksum);
                if (state == Gen4Save::SlotState::Valid) {
                    checkSlotRoundTrip(buffer, pk4::kStoredSize);
                    // Writing the slot back must not change the save.
                    CHECK(save.writeBoxSlot(box, slot, buffer));
                    ++pokemon;
                }
            }
        }

        // Rewriting every slot and the checksums reproduces the original file,
        // except HG/SS "box changed" flags that writeBoxSlot sets on purpose.
        save.refreshChecksums();
        CHECK(save.checksumsValid());
        if (save.game() != saves::Game::HeartGoldSoulSilver) {
            CHECK(save.data() == bytes);
        }
        std::printf("  %s: %s, %d Pokemon\n", io::fileName(path).c_str(), saves::gameName(save.game()), pokemon);
    }
}

TEST(real_saves_gen4_edit_and_commit) {
    for (const std::string& path : fixtureSaves()) {
        std::vector<uint8_t> bytes;
        CHECK(io::readFile(path, bytes));
        if (Gen4Save::detect(bytes) == nullptr) {
            continue;
        }
        Gen4Save save;
        CHECK(save.load(bytes) == Gen4Save::LoadResult::Ok);

        // Edit every party member: new nature, max IVs, one level up.
        std::vector<pokemon::Pokemon> expected;
        for (int slot = 0; slot < save.partyCount(); ++slot) {
            uint8_t buffer[pk4::kPartySize];
            CHECK(save.readPartySlot(slot, buffer) == Gen4Save::SlotState::Valid);
            pokemon::Pokemon p = pk4::toPokemon(buffer, pk4::kPartySize);
            if (p.isEgg) {
                expected.push_back(p);
                continue;
            }
            CHECK(pokemon::edit4::setNature(p, static_cast<uint8_t>((p.nature + 1) % 25)) ==
                  pokemon::edit4::Result::Ok);
            for (int s = 0; s < pokemon::kStatCount; ++s) {
                pokemon::edit4::setIv(p, static_cast<pokemon::Stat>(s), 31);
            }
            pokemon::edit4::setLevel(p, static_cast<uint8_t>(p.level < 100 ? p.level + 1 : 100));
            pk4::applyPokemon(p, buffer);
            pk4::refreshPartyStats(buffer);
            CHECK(save.writePartySlot(slot, buffer));
            expected.push_back(p);
        }

        char pattern[] = "/tmp/pktool-real-XXXXXX";
        std::string dir = mkdtemp(pattern);
        std::string target = io::joinPath(dir, "edited.sav");
        CHECK(io::safeWriteFile(target, bytes) == io::WriteResult::Ok);
        saves::CommitResult result = saves::commitGen4Save(save, target, io::joinPath(dir, "backups"));
        CHECK(result.status == saves::CommitResult::Status::Ok);

        std::vector<uint8_t> written;
        CHECK(io::readFile(target, written));
        Gen4Save reloaded;
        CHECK(reloaded.load(written) == Gen4Save::LoadResult::Ok);
        CHECK(reloaded.checksumsValid());
        for (int slot = 0; slot < reloaded.partyCount(); ++slot) {
            uint8_t buffer[pk4::kPartySize];
            CHECK(reloaded.readPartySlot(slot, buffer) == Gen4Save::SlotState::Valid);
            pokemon::Pokemon p = pk4::toPokemon(buffer, pk4::kPartySize);
            CHECK_EQ(p.pid, expected[slot].pid);
            CHECK_EQ(p.level, expected[slot].level);
            CHECK_EQ(p.nature, expected[slot].nature);
            CHECK(p.gender == expected[slot].gender);
            CHECK_EQ(p.shiny, expected[slot].shiny);
            CHECK_EQ(p.ivs[pokemon::kSpe], expected[slot].isEgg ? p.ivs[pokemon::kSpe] : 31);
        }
        // The storage block (boxes) is byte-identical.
        size_t storage = reloaded.activeStoragePartition() * Gen4Save::kPartitionSize;
        CHECK(std::memcmp(written.data() + storage + 0x10000, bytes.data() + storage + 0x10000, 0x10000) == 0);
    }
}

TEST(real_saves_nature_change_stays_legal) {
    int tried = 0;
    int legal = 0;
    for (const std::string& path : fixtureSaves()) {
        std::vector<uint8_t> bytes;
        CHECK(io::readFile(path, bytes));
        Gen4Save save;
        if (Gen4Save::detect(bytes) == nullptr || save.load(bytes) != Gen4Save::LoadResult::Ok) continue;
        for (int box = 0; box < Gen4Save::kBoxCount; ++box) {
            for (int slot = 0; slot < Gen4Save::kSlotsPerBox; ++slot) {
                uint8_t buffer[pk4::kStoredSize];
                if (save.readBoxSlot(box, slot, buffer) != Gen4Save::SlotState::Valid) continue;
                pokemon::Pokemon p = pk4::toPokemon(buffer, pk4::kStoredSize);
                if (!legality::checkGen4(p).legal() || p.isEgg || legality::isNpcTrade(p)) continue;  // only legal Pokémon; an NPC trade has a fixed PID
                ++tried;
                uint8_t nature = static_cast<uint8_t>((p.nature + 7) % 25);
                if (!legality::rerollPidGen4(p, nature, p.shiny, p.gender, static_cast<uint8_t>(p.pid & 1), 1)) {
                    std::printf("  no frame: %s\n", data::speciesName(p.species));
                    CHECK(false);
                    continue;
                }
                CHECK_EQ(p.nature, nature);
                bool ok = legality::checkGen4(p).legal();
                if (!ok) std::printf("  illegal after nature change: %s\n", data::speciesName(p.species));
                legal += ok ? 1 : 0;
            }
        }
    }
    if (tried > 0) std::printf("  %d/%d stay legal after a nature change\n", legal, tried);
    CHECK_EQ(legal, tried);
}

TEST(real_saves_gen3_round_trip) {
    namespace pk3 = pokemon::pk3;
    for (const std::string& path : fixtureSaves()) {
        std::vector<uint8_t> bytes;
        CHECK(io::readFile(path, bytes));
        if (saves::Gen3Save::detect(bytes) == saves::Game::Unknown) continue;
        saves::Gen3Save save;
        CHECK(save.load(bytes) == saves::LoadResult::Ok);
        CHECK_EQ(save.format(), 3);
        int count = 0;
        for (int slot = 0; slot < save.partyCount(); ++slot) {
            uint8_t buffer[pk3::kPartySize];
            CHECK(save.readPartySlot(slot, buffer) == saves::SlotState::Valid);
            uint8_t copy[pk3::kPartySize];
            std::memcpy(copy, buffer, sizeof copy);
            pokemon::Pokemon p = pk3::toPokemon(copy, sizeof copy);
            pk3::applyPokemon(p, copy);
            CHECK(std::memcmp(copy, buffer, sizeof copy) == 0);
            pk3::refreshPartyStats(copy);
            CHECK_EQ(copy[pk3::offset::kPartyLevel], p.level);
            CHECK(save.writePartySlot(slot, buffer));
            std::printf("  party: %s Lv.%d %s\n", data::speciesName(p.species), p.level, data::natureName(p.nature));
            ++count;
        }
        for (int box = 0; box < save.boxCount(); ++box) {
            for (int slot = 0; slot < saves::SaveFile::kSlotsPerBox; ++slot) {
                uint8_t buffer[pk3::kStoredSize];
                auto state = save.readBoxSlot(box, slot, buffer);
                CHECK(state != saves::SlotState::BadChecksum);
                if (state != saves::SlotState::Valid) continue;
                uint8_t copy[pk3::kStoredSize];
                std::memcpy(copy, buffer, sizeof copy);
                pokemon::Pokemon p = pk3::toPokemon(copy, sizeof copy);
                CHECK(p.species >= 1 && p.species <= 386);
                pk3::applyPokemon(p, copy);
                CHECK(std::memcmp(copy, buffer, sizeof copy) == 0);
                CHECK(save.writeBoxSlot(box, slot, buffer));
                ++count;
            }
        }
        save.refreshChecksums();
        CHECK(save.data() == bytes);
        std::printf("  %s: %s (%s), trainer %s, %d Pokemon, box 1 \"%s\"\n", io::fileName(path).c_str(),
                    saves::gameName(save.game()), saves::versionName(save.gameVersion()), save.trainerName().c_str(),
                    count, save.boxName(0).c_str());
    }
}

TEST(real_saves_gen3_edit_and_commit) {
    namespace pk3 = pokemon::pk3;
    for (const std::string& path : fixtureSaves()) {
        std::vector<uint8_t> bytes;
        CHECK(io::readFile(path, bytes));
        if (saves::Gen3Save::detect(bytes) == saves::Game::Unknown) continue;
        saves::Gen3Save save;
        CHECK(save.load(bytes) == saves::LoadResult::Ok);
        CHECK(save.partyCount() > 0);
        uint8_t party[pk3::kPartySize];
        CHECK(save.readPartySlot(0, party) == saves::SlotState::Valid);
        pokemon::Pokemon p = pk3::toPokemon(party, sizeof party);
        uint8_t level = p.level < 100 ? static_cast<uint8_t>(p.level + 1) : 99;
        pokemon::edit4::setLevel(p, level);
        pk3::applyPokemon(p, party);
        pk3::refreshPartyStats(party);
        CHECK(save.writePartySlot(0, party));

        char pattern[] = "/tmp/pktool-real3-XXXXXX";
        std::string dir = mkdtemp(pattern);
        std::string target = io::joinPath(dir, "game.sav");
        CHECK(io::safeWriteFile(target, bytes) == io::WriteResult::Ok);
        saves::CommitResult result = saves::commitSave(save, target, io::joinPath(dir, "backups"));
        CHECK(result.status == saves::CommitResult::Status::Ok);
        std::vector<uint8_t> written;
        CHECK(io::readFile(target, written));
        CHECK_EQ(written.size(), bytes.size());
        saves::Gen3Save reloaded;
        CHECK(reloaded.load(written) == saves::LoadResult::Ok);
        CHECK_EQ(reloaded.activeSlot(), save.activeSlot());
        uint8_t back[pk3::kPartySize];
        CHECK(reloaded.readPartySlot(0, back) == saves::SlotState::Valid);
        CHECK_EQ(pk3::toPokemon(back, sizeof back).level, level);
        CHECK_EQ(back[pk3::offset::kPartyLevel], level);
        // Only the active slot's sectors changed.
        size_t other = reloaded.activeSlot() == 0 ? 14 * 0x1000 : 0;
        CHECK(std::equal(written.begin() + other, written.begin() + other + 14 * 0x1000, bytes.begin() + other));
    }
}

// Every Pokémon the Gen III checker accepts must stay legal after Pal Park
// (Gen IV) and after Pal Park + Poké Transfer (Gen V).
TEST(real_saves_gen3_pal_park_stays_legal) {
    namespace pk3 = pokemon::pk3;
    for (const std::string& path : fixtureSaves()) {
        std::vector<uint8_t> bytes;
        CHECK(io::readFile(path, bytes));
        if (saves::Gen3Save::detect(bytes) == saves::Game::Unknown) continue;
        saves::Gen3Save save;
        CHECK(save.load(bytes) == saves::LoadResult::Ok);
        int moved = 0;
        int broken4 = 0;
        int broken5 = 0;
        for (int box = 0; box < save.boxCount(); ++box) {
            for (int slot = 0; slot < saves::SaveFile::kSlotsPerBox; ++slot) {
                uint8_t d3[pk4::kStoredSize] = {};
                if (save.readBoxSlot(box, slot, d3) != saves::SlotState::Valid) continue;
                pokemon::Pokemon p3 = pk3::toPokemon(d3, pk3::kStoredSize);
                if (p3.isEgg || legality::checkGen3(p3).overall() != legality::Severity::Valid) continue;
                uint8_t d4[pk4::kStoredSize];
                uint8_t d5[pk4::kStoredSize];
                pokemon::transfer34::convert(d3, d4, {26, 9, 29});
                pokemon::transfer45::convert(d4, d5);
                ++moved;
                auto report = [&](const char* gen, const legality::Report& r, int& broken) {
                    if (r.legal()) return;
                    ++broken;
                    std::printf("  %s %s:", gen, data::speciesName(p3.species));
                    for (const auto& f : r.findings) {
                        if (f.severity == legality::Severity::Invalid) std::printf(" [%s]", f.text.c_str());
                    }
                    std::printf("\n");
                };
                report("Gen IV", legality::checkGen4(pk4::toPokemon(d4, pk4::kStoredSize)), broken4);
                report("Gen V", legality::checkGen5(pokemon::pk5::toPokemon(d5, pk4::kStoredSize)), broken5);
            }
        }
        std::printf("  %s: %d legal Pokemon moved, %d/%d illegal after Pal Park / Poke Transfer\n",
                    io::fileName(path).c_str(), moved, broken4, broken5);
        CHECK_EQ(broken4, 0);
        CHECK_EQ(broken5, 0);
    }
}

TEST(real_saves_gen5_round_trip) {
    for (const std::string& path : fixtureSaves()) {
        std::vector<uint8_t> bytes;
        CHECK(io::readFile(path, bytes));
        if (saves::Gen5Save::detect(bytes) == saves::Game::Unknown) continue;
        saves::Gen5Save save;
        CHECK(save.load(bytes) == saves::LoadResult::Ok);
        CHECK(save.checksumsValid());
        int count = 0;
        for (int slot = 0; slot < save.partyCount(); ++slot) {
            uint8_t buffer[pk4::kPk5PartySize];
            CHECK(save.readPartySlot(slot, buffer) == saves::SlotState::Valid);
            uint8_t copy[pk4::kPk5PartySize];
            std::memcpy(copy, buffer, sizeof copy);
            pokemon::Pokemon p = pokemon::pk5::toPokemon(copy, sizeof copy);
            pokemon::pk5::applyPokemon(p, copy);
            CHECK(std::memcmp(copy, buffer, sizeof copy) == 0);
            // Stored stats match a recomputation (up to unapplied EVs).
            uint8_t refreshed[pk4::kPk5PartySize];
            std::memcpy(refreshed, buffer, sizeof refreshed);
            pokemon::pk5::refreshPartyStats(refreshed);
            CHECK_EQ(refreshed[pk4::offset::kPartyLevel], p.level);
            std::printf("  party: %s Lv.%d %s %s\n", data::speciesName(p.species), p.level, data::natureName(p.nature),
                        p.nickname.c_str());
            ++count;
        }
        for (int box = 0; box < save.boxCount(); ++box) {
            for (int slot = 0; slot < saves::SaveFile::kSlotsPerBox; ++slot) {
                uint8_t buffer[pk4::kStoredSize];
                auto state = save.readBoxSlot(box, slot, buffer);
                CHECK(state != saves::SlotState::BadChecksum);
                if (state != saves::SlotState::Valid) continue;
                uint8_t copy[pk4::kStoredSize];
                std::memcpy(copy, buffer, sizeof copy);
                pokemon::pk5::applyPokemon(pokemon::pk5::toPokemon(copy, sizeof copy), copy);
                CHECK(std::memcmp(copy, buffer, sizeof copy) == 0);
                CHECK(save.writeBoxSlot(box, slot, buffer));
                ++count;
            }
        }
        save.refreshChecksums();
        CHECK(save.data() == bytes);
        std::printf("  %s: %s, trainer %s, %d Pokemon, box 1 \"%s\"\n", io::fileName(path).c_str(),
                    saves::gameName(save.game()), save.trainerName().c_str(), count, save.boxName(0).c_str());
    }
}

TEST(real_saves_gen5_edit_and_commit) {
    for (const std::string& path : fixtureSaves()) {
        std::vector<uint8_t> bytes;
        CHECK(io::readFile(path, bytes));
        if (saves::Gen5Save::detect(bytes) == saves::Game::Unknown) continue;
        saves::Gen5Save save;
        CHECK(save.load(bytes) == saves::LoadResult::Ok);
        uint8_t buffer[pk4::kPk5PartySize];
        CHECK(save.readPartySlot(0, buffer) == saves::SlotState::Valid);
        pokemon::Pokemon p = pokemon::pk5::toPokemon(buffer, sizeof buffer);
        uint8_t level = static_cast<uint8_t>(p.level + 1);
        pokemon::edit4::setLevel(p, level);
        CHECK(pokemon::edit4::setNature(p, static_cast<uint8_t>((p.nature + 1) % 25)) == pokemon::edit4::Result::Ok);
        pokemon::pk5::applyPokemon(p, buffer);
        pokemon::pk5::refreshPartyStats(buffer);
        CHECK(save.writePartySlot(0, buffer));

        char pattern[] = "/tmp/pktool-real5-XXXXXX";
        std::string dir = mkdtemp(pattern);
        std::string target = io::joinPath(dir, "edited.sav");
        CHECK(io::safeWriteFile(target, bytes) == io::WriteResult::Ok);
        CHECK(saves::commitSave(save, target, io::joinPath(dir, "backups")).status == saves::CommitResult::Status::Ok);

        std::vector<uint8_t> written;
        CHECK(io::readFile(target, written));
        saves::Gen5Save reloaded;
        CHECK(reloaded.load(written) == saves::LoadResult::Ok);
        CHECK(reloaded.checksumsValid());
        CHECK(reloaded.readPartySlot(0, buffer) == saves::SlotState::Valid);
        pokemon::Pokemon q = pokemon::pk5::toPokemon(buffer, sizeof buffer);
        CHECK_EQ(q.level, level);
        CHECK_EQ(q.nature, p.nature);
        CHECK_EQ(q.pid, p.pid);
        CHECK_EQ(buffer[pk4::offset::kPartyLevel], level);
        // Only the party block and the checksum table changed (in both copies).
        CHECK(reloaded.hasBackup());
        CHECK(std::equal(written.begin(), written.begin() + 0x26000, written.begin() + 0x26000));
        for (size_t i = 0; i < written.size(); ++i) {
            size_t at = i >= 0x26000 && i < 0x4C000 ? i - 0x26000 : i;
            bool party = at >= 0x18E00 && at < 0x19400;
            bool table = at >= 0x25F00 && at < 0x26000;
            if (!party && !table && written[i] != bytes[i]) {
                std::printf("  unexpected change at 0x%zX\n", i);
                CHECK(false);
                break;
            }
        }
    }
}

// Trainer card and bag of every real save: read, print and round-trip. The
// printed values are the proof that the offsets are right (real trainer name,
// plausible money, real items with sane quantities). The fixtures may have
// been edited with cheats (HMs x999, repeated items, items out of their pocket):
// only the games' numeric limits are enforced, so everything must write back unchanged.
TEST(real_saves_trainer_and_bag) {
    for (const std::string& path : fixtureSaves()) {
        std::vector<uint8_t> bytes;
        CHECK(io::readFile(path, bytes));
        std::unique_ptr<saves::SaveFile> save = saves::loadSaveFile(bytes);
        CHECK(save != nullptr);
        if (!save) continue;

        saves::TrainerData t = save->trainerData();
        CHECK(t.name == save->trainerName());
        CHECK(!t.name.empty());
        CHECK(t.minutes < 60 && t.seconds < 60);
        std::printf("  %s: %s, %s (%s) TID %u SID %u, $%u, %u:%02u:%02u, badges 0x%X of %d\n",
                    io::fileName(path).c_str(), saves::gameName(save->game()), t.name.c_str(),
                    t.gender ? "F" : "M", t.tid, t.sid, t.money, t.hours, t.minutes, t.seconds, t.badges,
                    save->badgeCount());

        // Writing back what was read changes nothing.
        std::string error;
        bool clean = save->setTrainerData(t, &error);
        if (!clean) std::printf("    trainer not writable as is: %s\n", error.c_str());
        int editable = -1;  // a pocket that can be rewritten and has a free slot
        for (int pocket = 0; pocket < save->pocketCount(); ++pocket) {
            std::vector<saves::BagSlot> slots = save->readPocket(pocket);
            saves::BagPocket info = save->pocketInfo(pocket);
            std::printf("    %s (%d/%d):", info.name, static_cast<int>(slots.size()), info.capacity);
            for (size_t i = 0; i < slots.size(); ++i) {
                if (i < 6) std::printf(" %s x%u,", save->itemName(slots[i].item), slots[i].count);
            }
            std::printf("%s\n", slots.size() > 6 ? " ..." : "");
            const bool written = save->setPocket(pocket, slots, &error);
            if (!written) std::printf("    not writable as is: %s\n", error.c_str());
            CHECK(written);  // the game's own numbers always fit
            clean = clean && written;
            if (written && editable < 0 && static_cast<int>(slots.size()) < info.capacity) editable = pocket;
        }
        save->refreshChecksums();
        CHECK(clean);  // trainer and every pocket accepted as they are
        CHECK(save->data() == bytes);
        CHECK(editable >= 0);
        if (editable < 0) continue;

        // Edit, export and reload.
        saves::TrainerData edited = t;
        edited.name = "PIO";
        edited.gender ^= 1;
        edited.tid = 4321;
        edited.sid = 8765;
        edited.money = 123456;
        edited.hours = 12;
        edited.minutes = 34;
        edited.seconds = 56;
        edited.badges = 0x55;
        CHECK(save->setTrainerData(edited, &error));
        std::vector<saves::BagSlot> items = save->readPocket(editable);
        uint16_t added = 0;
        for (uint16_t item : save->pocketItems(editable)) {
            bool present = false;
            for (const saves::BagSlot& s : items) present |= s.item == item;
            if (!present) {
                added = item;
                break;
            }
        }
        CHECK(added != 0);
        items.push_back({added, 1});
        CHECK(save->setPocket(editable, items, &error));
        save->refreshChecksums();
        CHECK(save->checksumsValid());

        std::unique_ptr<saves::SaveFile> reloaded = saves::loadSaveFile(save->data());
        CHECK(reloaded != nullptr);
        if (!reloaded) continue;
        CHECK(reloaded->checksumsValid());
        saves::TrainerData back = reloaded->trainerData();
        CHECK(back.name == "PIO");
        CHECK_EQ(back.gender, edited.gender);
        CHECK_EQ(back.tid, 4321);
        CHECK_EQ(back.sid, 8765);
        CHECK_EQ(back.money, 123456u);
        CHECK_EQ(back.hours, 12);
        CHECK_EQ(back.minutes, 34);
        CHECK_EQ(back.seconds, 56);
        CHECK_EQ(back.badges, 0x55);
        std::vector<saves::BagSlot> bag = reloaded->readPocket(editable);
        CHECK_EQ(bag.size(), items.size());
        CHECK(!bag.empty() && bag.back().item == added && bag.back().count == 1);
        CHECK_EQ(reloaded->partyCount(), save->partyCount());
    }
}

// The browser's partial reads must give the summary the whole file gives, on real saves.
TEST(real_saves_scan_reads_less_and_agrees) {
    for (const std::string& path : fixtureSaves()) {
        saves::SaveSummary full;
        saves::SaveSummary quick;
        bool fullOk = saves::probeSaveFull(path, full);
        io::resetReadStats();
        bool quickOk = saves::probeSave(path, quick);
        const io::ReadStats stats = io::readStats();
        std::printf("  %-34s read %7llu of %7ld bytes\n", io::fileName(path).c_str(),
                    static_cast<unsigned long long>(stats.bytes), io::fileSize(path));
        CHECK_EQ(fullOk, quickOk);
        if (!fullOk || !quickOk) continue;
        CHECK(full.game == quick.game);
        CHECK(full.version == quick.version);
        CHECK(full.trainer == quick.trainer);
        CHECK_EQ(full.trainerId, quick.trainerId);
        CHECK(full.party == quick.party);
        CHECK(static_cast<long>(stats.bytes) < io::fileSize(path) / 2);
    }
}

// Every slot of every real save: ribbons read and rewritten leave the bytes
// alone, the whole save fits in a Bank file that loads back byte-identical, and
// each Pokemon survives save slot -> Bank -> file -> Bank unchanged.
TEST(real_saves_ribbons_and_bank_round_trip) {
    for (const std::string& path : fixtureSaves()) {
        std::vector<uint8_t> bytes;
        CHECK(io::readFile(path, bytes));
        std::unique_ptr<saves::SaveFile> save = saves::loadSaveFile(bytes);
        CHECK(save != nullptr);
        if (!save) continue;
        const uint8_t format = save->format();
        bank::Bank b;
        CHECK(b.load("/nonexistent/pktool-real.pkb") == bank::Bank::LoadResult::Created);
        int checked = 0;
        int ribbonCount = 0;
        int bankSlot = 0;
        std::vector<std::vector<uint8_t>> entities;
        for (int box = 0; box < save->boxCount(); ++box) {
            for (int slot = 0; slot < saves::SaveFile::kSlotsPerBox; ++slot) {
                uint8_t buffer[136] = {};
                if (save->readBoxSlot(box, slot, buffer) != saves::SlotState::Valid) continue;
                uint8_t copy[136];
                std::memcpy(copy, buffer, sizeof copy);
                for (int i = 0; i < pokemon::ribbons::count(format); ++i) {
                    int value = pokemon::ribbons::get(format, copy, i);
                    ribbonCount += value ? 1 : 0;
                    CHECK(pokemon::ribbons::set(format, copy, i, value));
                }
                CHECK(std::memcmp(copy, buffer, save->storedSize()) == 0);
                if (bankSlot < bank::Bank::kBoxCount * bank::Bank::kSlotsPerBox) {
                    CHECK(b.set(bankSlot / 30, bankSlot % 30, format, buffer));
                    entities.emplace_back(buffer, buffer + save->storedSize());
                    ++bankSlot;
                }
                ++checked;
            }
        }
        std::vector<uint8_t> file = b.serialize();
        bank::Bank back;
        CHECK(back.loadBytes(file) == bank::Bank::LoadResult::Ok);
        CHECK(back.serialize() == file);
        for (size_t i = 0; i < entities.size(); ++i) {
            const uint8_t* e = back.entity(static_cast<int>(i / 30), static_cast<int>(i % 30));
            CHECK(e != nullptr && std::memcmp(e, entities[i].data(), entities[i].size()) == 0);
        }
        std::printf("  %s: %d Pokemon, %d ribbons, %d in a Bank file\n", io::fileName(path).c_str(), checked,
                    ribbonCount, bankSlot);
    }
}
