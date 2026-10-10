#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "core/saves/save_file.h"
#include "core/text/gen12_text.h"
#include "tests/fake_gb_saves.h"
#include "tests/test.h"

using namespace pktool;
using saves::BagSlot;

namespace {

std::unique_ptr<saves::SaveFile> load(const std::vector<uint8_t>& bytes) {
    return saves::loadSaveFile(bytes, nullptr);
}

}  // namespace

TEST(gb_korean_text_round_trip) {
    // Table 1 starts 가 각 간; Table 2 entries come after; digits and letters are single bytes.
    const std::string name = "가각A1";
    uint8_t buffer[11];
    CHECK(text::canEncodeGb(2, name, 8));
    CHECK(text::encodeGb(2, name, buffer, sizeof(buffer), 5, 8));
    CHECK_EQ(buffer[0], 1);
    CHECK_EQ(buffer[1], 1);
    CHECK_EQ(buffer[2], 1);
    CHECK_EQ(buffer[3], 2);
    CHECK_EQ(buffer[4], 0x80);  // 'A'
    CHECK_EQ(buffer[5], 0xF7);  // '1'
    CHECK_EQ(buffer[6], 0x50);
    CHECK(text::decodeGb(2, buffer, sizeof(buffer), 8) == name);
    // Gen II international text keeps working and Hangul is refused there.
    CHECK(!text::canEncodeGb(2, "가", 2));
    CHECK(!text::canEncodeGb(2, "\xEF\xBF\xBD", 8));
    // The buffer limit counts bytes: three Hangul need six, a 5-byte buffer holds two.
    uint8_t small[5];
    CHECK(text::encodeGb(2, "가각간", small, sizeof(small), 5, 8));
    CHECK(text::decodeGb(2, small, sizeof(small), 8) == "가각");
}

TEST(gb_gen1_bag_edit) {
    for (bool japanese : {false, true}) {
        auto save = load(fakegb::blankGen1(japanese, false));
        CHECK(save != nullptr);
        if (!save) continue;
        CHECK_EQ(save->pocketCount(), 2);
        CHECK_EQ(save->pocketInfo(0).capacity, 20);
        CHECK_EQ(save->pocketInfo(1).capacity, 50);
        CHECK(save->readPocket(0).empty());
        const size_t items = japanese ? 0x25C4 : 0x25C9;
        const size_t pc = japanese ? 0x27DC : 0x27E6;
        // Poke Ball (4), Potion (20), HM01 (196) and TM50 (250).
        std::vector<BagSlot> slots = {{4, 5}, {20, 99}, {196, 1}, {250, 3}};
        std::string error;
        CHECK(save->setPocket(0, slots, &error));
        const std::vector<uint8_t>& d = save->data();
        CHECK_EQ(d[items], 4);
        CHECK_EQ(d[items + 1], 4);
        CHECK_EQ(d[items + 2], 5);
        CHECK_EQ(d[items + 7], 250);
        CHECK_EQ(d[items + 8], 3);
        CHECK_EQ(d[items + 9], 0xFF);
        CHECK_EQ(d[pc], 0);  // the other list is untouched
        std::vector<BagSlot> back = save->readPocket(0);
        CHECK_EQ(back.size(), 4u);
        CHECK(back.size() == 4 && back[3].item == 250 && back[3].count == 3);
        CHECK(std::string(save->itemName(4)) != "???");
        // Limits: 99 per slot, 20 slots, IDs the game has.
        CHECK(!save->setPocket(0, {{4, 100}}, &error));
        CHECK(!save->setPocket(0, {{84, 1}}, &error));
        CHECK(!save->setPocket(0, std::vector<BagSlot>(21, BagSlot{4, 1}), &error));
        CHECK(save->setPocket(0, std::vector<BagSlot>(20, BagSlot{4, 1}), &error));
        CHECK_EQ(save->data()[items + 41], 0xFF);
        CHECK(save->setPocket(1, std::vector<BagSlot>(50, BagSlot{20, 99}), &error));
        CHECK_EQ(save->data()[pc + 101], 0xFF);
        CHECK(save->setPocket(1, {}, &error));
        CHECK_EQ(save->data()[pc], 0);
        CHECK_EQ(save->data()[pc + 1], 0xFF);
        CHECK_EQ(save->data()[pc + 2], 0);
    }
}

TEST(gb_gen2_bag_edit) {
    struct Case {
        fakegb::Gen2Setup setup;
        size_t items, key, balls, tm, pc;
        bool crystal;
    };
    const Case cases[] = {
        {fakegb::kGen2Gs, 0x241F, 0x2449, 0x2464, 0x23E6, 0x247E, false},
        {fakegb::kGen2Crystal, 0x2420, 0x244A, 0x2465, 0x23E7, 0x247F, true},
        {fakegb::kGen2GsJpn, 0x2400, 0x242A, 0x2445, 0x23C7, 0x245F, false},
        {fakegb::kGen2CrystalJpn, 0x2402, 0x242C, 0x2447, 0x23C9, 0x2461, true},
    };
    for (const Case& c : cases) {
        auto save = load(fakegb::blankGen2(c.setup));
        CHECK(save != nullptr);
        if (!save) continue;
        CHECK_EQ(save->pocketCount(), 5);
        CHECK_EQ(save->pocketInfo(0).capacity, 20);
        CHECK_EQ(save->pocketInfo(1).capacity, 25);
        CHECK_EQ(save->pocketInfo(2).capacity, 12);
        CHECK_EQ(save->pocketInfo(3).capacity, 57);
        CHECK_EQ(save->pocketInfo(4).capacity, 50);
        std::string error;
        // Items: Potion (18) x7, Max Revive (36?) is whatever ID 36 is; only the byte layout is checked.
        CHECK(save->setPocket(0, {{18, 7}, {26, 99}}, &error));
        CHECK_EQ(save->data()[c.items], 2);
        CHECK_EQ(save->data()[c.items + 1], 18);
        CHECK_EQ(save->data()[c.items + 2], 7);
        CHECK_EQ(save->data()[c.items + 4], 99);
        CHECK_EQ(save->data()[c.items + 5], 0xFF);
        // Key items have no quantity; Bicycle (7).
        CHECK(save->setPocket(1, {{7, 1}, {54, 1}}, &error));
        CHECK_EQ(save->data()[c.key], 2);
        CHECK_EQ(save->data()[c.key + 1], 7);
        CHECK_EQ(save->data()[c.key + 2], 54);
        CHECK_EQ(save->data()[c.key + 3], 0xFF);
        CHECK(save->setPocket(1, std::vector<BagSlot>(25, BagSlot{7, 1}), &error));
        CHECK_EQ(save->data()[c.key + 26], 0xFF);
        CHECK_EQ(save->data()[c.balls], 0);  // the Balls count is not overwritten
        CHECK(!save->setPocket(1, std::vector<BagSlot>(26, BagSlot{7, 1}), &error));
        CHECK(save->setPocket(1, {}, &error));
        // Balls: Poke Ball (1) and Master Ball (2).
        CHECK(save->setPocket(2, {{1, 10}, {2, 1}}, &error));
        CHECK_EQ(save->data()[c.balls], 2);
        CHECK_EQ(save->data()[c.balls + 5], 0xFF);
        // TMs and HMs: one count per machine in item order (191 first, HMs 243-249 last).
        CHECK(save->setPocket(3, {{191, 5}, {249, 1}}, &error));
        CHECK_EQ(save->data()[c.tm], 5);
        CHECK_EQ(save->data()[c.tm + 56], 1);
        CHECK_EQ(save->data()[c.tm + 1], 0);
        CHECK_EQ(save->data()[c.items], 2);
        std::vector<BagSlot> machines = save->readPocket(3);
        CHECK_EQ(machines.size(), 2u);
        CHECK(machines.size() == 2 && machines[0].item == 191 && machines[0].count == 5 && machines[1].item == 249);
        CHECK(!save->setPocket(3, {{18, 1}}, &error));       // not a machine
        CHECK(!save->setPocket(3, {{191, 1}, {191, 2}}, &error));  // listed twice
        // PC: Crystal's four extra key items are PC-valid only there.
        CHECK(save->setPocket(4, {{18, 99}}, &error));
        CHECK_EQ(save->data()[c.pc], 1);
        CHECK_EQ(save->data()[c.pc + 3], 0xFF);
        CHECK_EQ(save->pocketAllows(4, 70), c.crystal);
        CHECK_EQ(save->pocketAllows(1, 70), c.crystal);
        CHECK(std::string(save->itemName(1)) != "???");
    }
}
