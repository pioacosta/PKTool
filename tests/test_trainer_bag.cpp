// Trainer card and bag editing on in-memory saves of every game.

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "core/data/items.h"
#include "core/saves/game.h"
#include "core/saves/gen3_save.h"
#include "core/saves/gen4_save.h"
#include "core/saves/gen5_save.h"
#include "core/saves/save_file.h"
#include "core/text/gen4_text.h"
#include "core/util/bytes.h"
#include "core/util/crc16.h"
#include "tests/fake_saves.h"
#include "tests/test.h"

using namespace pktool;
using saves::BagSlot;
using saves::Game;
using saves::TrainerData;

namespace {

using namespace fake;

struct Variant {
    const char* name;
    Game game;
    std::vector<uint8_t> data;
};

std::vector<Variant> allVariants() {
    std::vector<Variant> v;
    v.push_back({"RS", Game::RubySapphire, blankGen3(Game::RubySapphire)});
    v.push_back({"E", Game::Emerald, blankGen3(Game::Emerald)});
    v.push_back({"FRLG", Game::FireRedLeafGreen, blankGen3(Game::FireRedLeafGreen)});
    v.push_back({"DP", Game::DiamondPearl, blankGen4(kDp)});
    v.push_back({"Pt", Game::Platinum, blankGen4(kPt)});
    v.push_back({"HGSS", Game::HeartGoldSoulSilver, blankGen4(kHgss)});
    v.push_back({"BW", Game::BlackWhite, blankGen5(false)});
    v.push_back({"B2W2", Game::Black2White2, blankGen5(true)});
    return v;
}

std::unique_ptr<saves::SaveFile> load(const Variant& v) {
    saves::LoadResult result;
    std::unique_ptr<saves::SaveFile> save = saves::loadSaveFile(v.data, &result);
    CHECK(result == saves::LoadResult::Ok);
    CHECK(save != nullptr);
    if (save) CHECK(save->game() == v.game);
    return save;
}

TrainerData sample(const saves::SaveFile& save) {
    TrainerData t;
    t.name = "RED";
    t.gender = 1;
    t.tid = 54321;
    t.sid = 65535;
    t.money = 999999;
    t.hours = 999;
    t.minutes = 59;
    t.seconds = 58;
    t.badges = save.badgeCount() == 16 ? 0xA5C3 : 0xA5;
    return t;
}

bool sameTrainer(const TrainerData& a, const TrainerData& b) {
    return a.name == b.name && a.gender == b.gender && a.tid == b.tid && a.sid == b.sid && a.money == b.money &&
           a.hours == b.hours && a.minutes == b.minutes && a.seconds == b.seconds && a.badges == b.badges;
}

}  // namespace

// ----------------------------------------------------------------- trainer

TEST(trainer_blank_saves_read_back) {
    for (const Variant& v : allVariants()) {
        std::unique_ptr<saves::SaveFile> save = load(v);
        if (!save) continue;
        TrainerData t = save->trainerData();
        CHECK(t.name == "ASH");
        CHECK_EQ(t.tid, 12345);
        CHECK_EQ(t.sid, 54321);
        CHECK_EQ(t.badges, 0);
        CHECK(save->checksumsValid());
    }
}

TEST(trainer_edit_round_trips_in_every_game) {
    for (const Variant& v : allVariants()) {
        std::unique_ptr<saves::SaveFile> save = load(v);
        if (!save) continue;
        TrainerData t = sample(*save);
        std::string error;
        CHECK(save->setTrainerData(t, &error));
        CHECK(sameTrainer(save->trainerData(), t));
        // Existing accessors keep working.
        CHECK(save->trainerName() == "RED");
        CHECK_EQ(save->trainerId(), 54321);
        CHECK_EQ(save->secretId(), 65535);
        CHECK_EQ(save->trainerGender(), 1);
        save->refreshChecksums();
        CHECK(save->checksumsValid());

        saves::LoadResult result;
        std::unique_ptr<saves::SaveFile> reloaded = saves::loadSaveFile(save->data(), &result);
        CHECK(result == saves::LoadResult::Ok);
        CHECK(reloaded != nullptr);
        if (!reloaded) continue;
        CHECK(reloaded->checksumsValid());
        CHECK(sameTrainer(reloaded->trainerData(), t));

        // A shorter name after a longer one leaves no residue.
        t.name = "A";
        t.badges = 0;
        CHECK(reloaded->setTrainerData(t, &error));
        CHECK(reloaded->trainerName() == "A");
        t.name = "ABCDEFG";
        CHECK(reloaded->setTrainerData(t, &error));
        CHECK(reloaded->trainerName() == "ABCDEFG");
    }
}

TEST(trainer_validation_rejects_bad_input_and_changes_nothing) {
    for (const Variant& v : allVariants()) {
        std::unique_ptr<saves::SaveFile> save = load(v);
        if (!save) continue;
        const std::vector<uint8_t> before = save->data();
        TrainerData good = sample(*save);

        struct Bad {
            const char* what;
            TrainerData data;
        };
        std::vector<Bad> bad;
        auto add = [&](const char* what, auto change) {
            TrainerData t = good;
            change(t);
            bad.push_back({what, t});
        };
        add("empty name", [](TrainerData& t) { t.name = ""; });
        add("8 characters", [](TrainerData& t) { t.name = "ABCDEFGH"; });
        add("emoji", [](TrainerData& t) { t.name = "A\xF0\x9F\x98\x80"; });
        add("gender 2", [](TrainerData& t) { t.gender = 2; });
        uint32_t tooMuch = save->maxMoney() + 1;
        add("money", [tooMuch](TrainerData& t) { t.money = tooMuch; });
        add("hours", [](TrainerData& t) { t.hours = 1000; });
        add("minutes", [](TrainerData& t) { t.minutes = 60; });
        add("seconds", [](TrainerData& t) { t.seconds = 60; });
        if (save->badgeCount() == 8) add("badges", [](TrainerData& t) { t.badges = 0x100; });

        for (const Bad& b : bad) {
            std::string error;
            bool ok = save->setTrainerData(b.data, &error);
            if (ok) std::printf("  %s accepted by %s\n", b.what, v.name);
            CHECK(!ok);
            CHECK(!error.empty());
            CHECK(save->data() == before);
        }
        // Valid input still works afterwards.
        CHECK(save->setTrainerData(good));
    }
}

TEST(trainer_gen3_money_is_encrypted_with_the_security_key) {
    // The newest slot is slot 1: sector ID n sits at (14 + n) * 0x1000.
    for (Game game : {Game::RubySapphire, Game::Emerald, Game::FireRedLeafGreen}) {
        Variant v{"gen3", game, blankGen3(game)};
        std::unique_ptr<saves::SaveFile> save = load(v);
        if (!save) continue;
        TrainerData t = sample(*save);
        t.money = 123456;
        CHECK(save->setTrainerData(t));
        size_t moneyAt = (14 + 1) * kSector + (game == Game::FireRedLeafGreen ? 0x290 : 0x490);
        uint32_t key = game == Game::Emerald ? kEmeraldKey : game == Game::FireRedLeafGreen ? kFrlgKey : 0;
        CHECK_EQ(util::readU32(&save->data()[moneyAt]), 123456u ^ key);
        CHECK_EQ(save->trainerData().money, 123456u);
    }
}

TEST(trainer_gen3_badges_are_event_flags) {
    // Flag number -> byte: base + flag / 8 inside the large block.
    struct Case {
        Game game;
        size_t base;
        int first;
    };
    for (const Case& c : {Case{Game::RubySapphire, 0x1220, 0x807}, Case{Game::Emerald, 0x1270, 0x867},
                          Case{Game::FireRedLeafGreen, 0xEE0, 0x820}}) {
        Variant v{"gen3", c.game, blankGen3(c.game)};
        std::unique_ptr<saves::SaveFile> save = load(v);
        if (!save) continue;
        TrainerData t = sample(*save);
        t.badges = 0x81;  // first and last badge
        CHECK(save->setTrainerData(t));
        auto flag = [&](int number) {
            size_t large = c.base + number / 8;  // the large block spans sectors 1-4
            size_t at = (14 + 1 + large / 0xF80) * kSector + large % 0xF80;
            return (save->data()[at] >> (number % 8)) & 1;
        };
        CHECK_EQ(flag(c.first), 1);
        CHECK_EQ(flag(c.first + 1), 0);
        CHECK_EQ(flag(c.first + 7), 1);
    }
}

TEST(trainer_hgss_has_kanto_badges_in_a_second_byte) {
    Variant v{"HGSS", Game::HeartGoldSoulSilver, blankGen4(kHgss)};
    std::unique_ptr<saves::SaveFile> save = load(v);
    if (!save) return;
    CHECK_EQ(save->badgeCount(), 16);
    TrainerData t = sample(*save);
    t.badges = 0x0FF1;
    CHECK(save->setTrainerData(t));
    saves::Gen4Save& gen4 = static_cast<saves::Gen4Save&>(*save);
    size_t general = gen4.activeGeneralPartition() * saves::Gen4Save::kPartitionSize;
    CHECK_EQ(save->data()[general + 0x64 + 0x1A], 0xF1);
    CHECK_EQ(save->data()[general + 0x64 + 0x1F], 0x0F);
}

// --------------------------------------------------------------------- bag

TEST(bag_pockets_match_the_games) {
    for (const Variant& v : allVariants()) {
        std::unique_ptr<saves::SaveFile> save = load(v);
        if (!save) continue;
        int expected = v.game == Game::BlackWhite || v.game == Game::Black2White2 ? 5
                       : save->format() == 3                                     ? 5
                                                                                 : 8;
        CHECK_EQ(save->pocketCount(), expected);
        for (int p = 0; p < save->pocketCount(); ++p) {
            saves::BagPocket info = save->pocketInfo(p);
            CHECK(info.capacity > 0);
            CHECK(std::strlen(info.name) > 0);
            CHECK(!save->pocketItems(p).empty());
            CHECK(save->readPocket(p).empty());
        }
        CHECK_EQ(save->pocketCount(), expected);
        if (save->format() == 3) {
            CHECK_EQ(save->pocketInfo(0).capacity, v.game == Game::FireRedLeafGreen ? 42 : v.game == Game::Emerald ? 30 : 20);
        } else {
            CHECK_EQ(save->pocketInfo(0).capacity, static_cast<int>(save->pocketItems(0).size()));
        }
        CHECK(save->pocketInfo(save->pocketCount()).capacity == 0);
    }
    // Item names come from the save's own generation.
    Variant e{"E", Game::Emerald, blankGen3(Game::Emerald)};
    Variant pt{"Pt", Game::Platinum, blankGen4(kPt)};
    std::unique_ptr<saves::SaveFile> gen3 = load(e);
    std::unique_ptr<saves::SaveFile> gen4 = load(pt);
    if (gen3 && gen4) {
        CHECK(std::string(gen3->itemName(13)) == "Potion");  // Gen III ID
        CHECK(std::string(gen4->itemName(17)) == "Potion");  // Gen IV ID
        CHECK(std::string(gen3->itemName(1)) == "Master Ball");
    }
}

TEST(bag_edit_round_trips_in_every_game) {
    for (const Variant& v : allVariants()) {
        std::unique_ptr<saves::SaveFile> save = load(v);
        if (!save) continue;
        std::vector<std::vector<BagSlot>> written(save->pocketCount());
        for (int p = 0; p < save->pocketCount(); ++p) {
            std::vector<uint16_t> allowed = save->pocketItems(p);
            std::vector<BagSlot> slots;
            for (size_t i = 0; i < 3 && i < allowed.size(); ++i) {
                int max = save->itemMaxCount(p, allowed[i]);
                slots.push_back({allowed[i], static_cast<uint16_t>(max - static_cast<int>(i) % max)});
            }
            std::string error;
            CHECK(save->setPocket(p, slots, &error));
            written[p] = slots;
        }
        save->refreshChecksums();
        saves::LoadResult result;
        std::unique_ptr<saves::SaveFile> reloaded = saves::loadSaveFile(save->data(), &result);
        CHECK(result == saves::LoadResult::Ok);
        CHECK(reloaded && reloaded->checksumsValid());
        if (!reloaded) continue;
        for (int p = 0; p < reloaded->pocketCount(); ++p) {
            std::vector<BagSlot> got = reloaded->readPocket(p);
            CHECK_EQ(got.size(), written[p].size());
            for (size_t i = 0; i < got.size() && i < written[p].size(); ++i) {
                CHECK_EQ(got[i].item, written[p][i].item);
                CHECK_EQ(got[i].count, written[p][i].count);
            }
        }
        // Writing fewer items clears the rest of the pocket.
        std::vector<BagSlot> one(written[0].begin(), written[0].begin() + 1);
        CHECK(reloaded->setPocket(0, one));
        CHECK_EQ(reloaded->readPocket(0).size(), 1u);
        CHECK(reloaded->setPocket(0, {}));
        CHECK(reloaded->readPocket(0).empty());
        // Other pockets are untouched.
        CHECK_EQ(reloaded->readPocket(1).size(), written[1].size());
    }
}

TEST(bag_validation_enforces_only_the_game_limits) {
    for (const Variant& v : allVariants()) {
        std::unique_ptr<saves::SaveFile> save = load(v);
        if (!save) continue;
        std::vector<uint16_t> items = save->pocketItems(0);
        std::vector<uint16_t> keys = save->pocketItems(1);  // Key Items in every game
        CHECK(save->pocketInfo(1).kind == data::PocketKind::KeyItems);
        const uint16_t keyItem = keys[0];
        const uint16_t item = items[0];
        const uint16_t item2 = items[1];
        const int max = save->itemMaxCount(0, item);
        CHECK(max >= 99);

        CHECK(save->setPocket(0, {{item, 5}}));  // a baseline to compare against
        const std::vector<uint8_t> before = save->data();

        // What the game cannot hold is rejected and nothing changes.
        struct Bad {
            const char* what;
            int pocket;
            std::vector<BagSlot> slots;
        };
        std::vector<Bad> bad = {
            {"quantity 0", 0, {{item, 0}}},
            {"quantity too big", 0, {{item, static_cast<uint16_t>(max + 1)}}},
            {"quantity 65535", 0, {{item, 65535}}},
            {"item 0", 0, {{0, 3}}},
            {"unknown item", 0, {{0xFFFF, 3}}},
            {"key items keep the pocket limit", 1, {{keyItem, static_cast<uint16_t>(save->itemMaxCount(1, keyItem) + 1)}}},
            {"bad pocket", 99, {{item, 1}}},
            {"bad pocket -1", -1, {{item, 1}}},
            {"second slot invalid", 0, {{item, 1}, {0xFFFF, 1}}},
        };
        // More slots than the pocket has.
        std::vector<BagSlot> tooMany;
        for (int i = 0; i <= save->pocketInfo(0).capacity; ++i) tooMany.push_back({items[i % items.size()], 1});
        bad.push_back({"too many items", 0, tooMany});
        for (const Bad& b : bad) {
            std::string error;
            bool ok = save->setPocket(b.pocket, b.slots, &error);
            if (ok) std::printf("  %s accepted by %s\n", b.what, v.name);
            CHECK(!ok);
            CHECK(!error.empty());
            CHECK(save->data() == before);
        }
        CHECK(!save->itemExists(0));
        CHECK(!save->itemExists(0xFFFF));
        CHECK(save->itemExists(item));
        CHECK(!save->pocketAllows(0, keyItem));  // the pocket's own list is unchanged

        // Illegal but within the game's numbers is fine: a key item in the Items pocket,
        // an item twice, HMs by the dozen.
        CHECK(save->itemExists(keyItem));
        CHECK(save->setPocket(0, {{keyItem, 1}, {item, 1}, {item2, static_cast<uint16_t>(max)}, {item, 7}}));
        CHECK_EQ(save->readPocket(0).size(), 4u);
        CHECK(save->setPocket(1, {{item, 1}, {item, 1}}));  // a regular item in Key Items, twice
        CHECK_EQ(save->readPocket(1).size(), 2u);
        for (int p = 0; p < save->pocketCount(); ++p) {
            if (save->pocketInfo(p).kind != data::PocketKind::TMsHMs) continue;
            uint16_t hm = save->pocketItems(p).back();
            const int limit = save->itemMaxCount(p, hm);
            std::string error;
            const bool ok = save->setPocket(p, {{hm, static_cast<uint16_t>(limit)}}, &error);
            if (!ok) std::printf("  HM %u limit %d in %s: %s\n", hm, limit, v.name, error.c_str());
            CHECK(ok);
            if (ok) CHECK_EQ(save->readPocket(p)[0].count, limit);
        }
    }
}

TEST(bag_gen3_stacks_and_encryption) {
    // Emerald: quantities are XOR the low 16 bits of the security key, the
    // item ID is plain; Ruby/Sapphire stores both plain; FireRed/LeafGreen
    // encrypts and has 999 per slot with no stacking.
    struct Case {
        Game game;
        size_t base;  // bag start in the large block
        uint16_t key;
    };
    for (const Case& c : {Case{Game::RubySapphire, 0x498, 0}, Case{Game::Emerald, 0x498, static_cast<uint16_t>(kEmeraldKey)},
                          Case{Game::FireRedLeafGreen, 0x298, static_cast<uint16_t>(kFrlgKey)}}) {
        Variant v{"gen3", c.game, blankGen3(c.game)};
        std::unique_ptr<saves::SaveFile> save = load(v);
        if (!save) continue;
        std::string error;
        const bool stacks = c.game != Game::FireRedLeafGreen;
        std::vector<BagSlot> slots = {{13, 99}, {14, 5}, {13, 42}};
        CHECK(save->setPocket(0, slots, &error));  // repeats are allowed in every game now
        slots = {{13, 77}, {14, 5}};
        CHECK(save->setPocket(0, slots, &error));
        size_t offset = c.game == Game::FireRedLeafGreen ? 0x78 : 0xC8;
        size_t large = c.base + offset;
        size_t at = (14 + 1 + large / 0xF80) * kSector + large % 0xF80;
        CHECK_EQ(util::readU16(&save->data()[at]), 13);
        CHECK_EQ(util::readU16(&save->data()[at + 2]), 77 ^ c.key);
        CHECK_EQ(util::readU16(&save->data()[at + 4]), 14);
        CHECK_EQ(util::readU16(&save->data()[at + 6]), 5 ^ c.key);
        // Empty slots hold the encrypted zero, like the games write them.
        CHECK_EQ(util::readU16(&save->data()[at + 8]), 0);
        CHECK_EQ(util::readU16(&save->data()[at + 10]), c.key);
        // FireRed/LeafGreen allow 999 in the Items pocket.
        CHECK_EQ(save->itemMaxCount(0, 13), c.game == Game::FireRedLeafGreen ? 999 : 99);
        if (stacks) {
            CHECK(save->setPocket(0, {{13, 99}, {13, 42}}));
            CHECK_EQ(save->readPocket(0).size(), 2u);
        }
    }
}

TEST(bag_gen4_rejects_gen5_only_items) {
    Variant pt{"Pt", Game::Platinum, blankGen4(kPt)};
    Variant bw{"BW", Game::BlackWhite, blankGen5(false)};
    Variant b2w2{"B2W2", Game::Black2White2, blankGen5(true)};
    std::unique_ptr<saves::SaveFile> gen4 = load(pt);
    std::unique_ptr<saves::SaveFile> gen5 = load(bw);
    std::unique_ptr<saves::SaveFile> gen5b = load(b2w2);
    if (!gen4 || !gen5 || !gen5b) return;
    // Dowsing Machine (Gen IV Key Item 464 exists in Pt; 590 is a Gen V item).
    CHECK(!gen4->pocketAllows(0, 590));
    CHECK(gen5->pocketAllows(0, 590));
    CHECK(!gen4->setPocket(0, {{590, 1}}));
    // Key items differ per game: the Gen V B2W2-only DNA Splicers do not fit in B/W.
    CHECK(gen5b->pocketAllows(1, 628));
    CHECK(!gen5->pocketAllows(1, 628));
}
