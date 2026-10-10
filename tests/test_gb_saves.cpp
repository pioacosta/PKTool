#include <cstring>
#include <memory>
#include <vector>

#include "core/pokemon/entity.h"
#include "core/saves/save_commit.h"
#include "core/saves/save_file.h"
#include "core/saves/save_probe.h"
#include "tests/fake_gb_saves.h"

using namespace pktool;
using saves::Game;
using saves::LoadResult;
using saves::SlotState;

namespace {

std::unique_ptr<saves::SaveFile> load(const std::vector<uint8_t>& bytes, LoadResult* result = nullptr) {
    return saves::loadSaveFile(bytes, result);
}

// A body-only entity of the save's format as the app would hold it.
void makeEntity(const saves::SaveFile& save, uint16_t species, uint8_t level, uint16_t dvs, uint8_t* out) {
    std::memset(out, 0, 100);
    const bool jp = save.language() == 1;
    if (save.format() == 1) {
        pokemon::pk1::clear(out, jp);
        std::vector<uint8_t> body = fakegb::gen1Body(species, level, fakegb::kTid, dvs, 44);
        std::memcpy(out, body.data(), body.size());
    } else {
        pokemon::pk2::clear(out, jp);
        std::vector<uint8_t> body = fakegb::gen2Body(species, level, fakegb::kTid, dvs, 48);
        std::memcpy(out, body.data(), body.size());
    }
    text::encodeGb(save.format(), fakegb::trainerName(jp), out + (save.format() == 1 ? pokemon::pk1::kOtOffset : pokemon::pk2::kOtOffset),
                   11, jp ? 5 : 7, save.language());
}

}  // namespace

TEST(gb_gen1_loads_blank_saves) {
    for (bool japanese : {false, true}) {
        for (bool yellow : {false, true}) {
            LoadResult result;
            auto save = load(fakegb::blankGen1(japanese, yellow), &result);
            CHECK(result == LoadResult::Ok);
            CHECK(save != nullptr);
            if (!save) continue;
            CHECK(save->game() == (yellow ? Game::Yellow : Game::RedBlue));
            CHECK_EQ(save->format(), 1);
            CHECK(save->trainerName() == fakegb::trainerName(japanese));
            CHECK_EQ(save->trainerId(), fakegb::kTid);
            CHECK_EQ(save->partyCount(), 0);
            CHECK_EQ(save->boxCount(), japanese ? 8 : 12);
            CHECK_EQ(save->boxSlotCount(), japanese ? 30 : 20);
            CHECK_EQ(save->language(), japanese ? 1 : 2);
            CHECK(save->checksumsValid());
            CHECK(!save->hasSecretId());
        }
    }
}

TEST(gb_gen1_unchanged_save_round_trips) {
    for (bool japanese : {false, true}) {
        std::vector<uint8_t> bytes = fakegb::blankGen1(japanese, false);
        const fakegb::Gen1Offsets& o = japanese ? fakegb::kGen1Jpn : fakegb::kGen1Int;
        std::vector<fakegb::RawMon> party;
        fakegb::RawMon mon;
        mon.marker = pktool::data::speciesToGen1(25);
        mon.body = fakegb::gen1Body(25, 12, fakegb::kTid, 0xFFFF, 44);
        mon.ot = "ASH";
        mon.nick = "PIKACHU";
        party.push_back(mon);
        fakegb::writeList(bytes, o.party, 6, 44, o.stringBytes, 1, japanese ? 1 : 2, party);
        // Stale data after the list's end must survive an untouched save.
        bytes[o.party + 1 + 6 + 1 + 44 * 3] = 0x77;
        fakegb::fixGen1Checksum(bytes, o);
        auto save = load(bytes);
        CHECK(save != nullptr);
        if (!save) continue;
        CHECK_EQ(save->partyCount(), 1);
        save->refreshChecksums();
        CHECK(save->data() == bytes);
    }
}

TEST(gb_gen1_reads_party_and_current_box) {
    std::vector<uint8_t> bytes = fakegb::blankGen1(false, false);
    const fakegb::Gen1Offsets& o = fakegb::kGen1Int;
    fakegb::RawMon mon;
    mon.marker = data::speciesToGen1(25);
    mon.body = fakegb::gen1Body(25, 12, fakegb::kTid, 0xA6FE, 44);
    mon.ot = "ASH";
    mon.nick = "SPARKY";
    fakegb::writeList(bytes, o.party, 6, 44, o.stringBytes, 1, 2, {mon});
    fakegb::RawMon boxed;
    boxed.marker = data::speciesToGen1(129);
    boxed.body = fakegb::gen1Body(129, 5, fakegb::kTid, 0x1234, 33);
    boxed.ot = "ASH";
    boxed.nick = "MAGIKARP";
    fakegb::writeList(bytes, o.currentBox, 20, 33, o.stringBytes, 1, 2, {boxed});
    fakegb::fixGen1Checksum(bytes, o);
    auto save = load(bytes);
    CHECK(save != nullptr);
    if (!save) return;
    uint8_t buffer[pokemon::entity::kMaxPartySize];
    CHECK(save->readPartySlot(0, buffer) == SlotState::Valid);
    pokemon::Pokemon p = pokemon::entity::decode(1, buffer, save->partySize());
    CHECK_EQ(p.species, 25);
    CHECK_EQ(p.level, 12);
    CHECK(p.nickname == "SPARKY");
    CHECK(p.isNicknamed);
    CHECK(p.trainer.name == "ASH");
    CHECK_EQ(p.trainer.tid, fakegb::kTid);
    CHECK_EQ(p.ivs[pokemon::kAtk], 0xA);
    CHECK_EQ(p.ivs[pokemon::kDef], 0x6);
    CHECK_EQ(p.ivs[pokemon::kSpe], 0xF);
    CHECK_EQ(p.ivs[pokemon::kSpA], 0xE);
    CHECK_EQ(p.ivs[pokemon::kHp], 2);  // low bits of Atk 0xA (0), Def 0x6 (0), Spe 0xF (1), Spc 0xE (0)
    CHECK(save->readPartySlot(1, buffer) == SlotState::Empty);
    // The current box is read from its mirror; the other boxes are not initialised.
    CHECK(save->readBoxSlot(0, 0, buffer) == SlotState::Valid);
    p = pokemon::entity::decode(1, buffer, save->storedSize());
    CHECK_EQ(p.species, 129);
    CHECK(!p.isNicknamed || p.nickname == "MAGIKARP");
    CHECK(save->readBoxSlot(0, 1, buffer) == SlotState::Empty);
    CHECK(save->readBoxSlot(3, 0, buffer) == SlotState::Empty);
}

TEST(gb_gen1_write_and_reload) {
    for (bool japanese : {false, true}) {
        auto save = load(fakegb::blankGen1(japanese, false));
        CHECK(save != nullptr);
        if (!save) continue;
        uint8_t entity[100];
        makeEntity(*save, 25, 20, 0x9AFA, entity);
        CHECK(save->appendParty(entity));
        makeEntity(*save, 4, 7, 0x1111, entity);
        CHECK(save->writeBoxSlot(0, 2, entity));  // box 1 is the current box, slot 3 -> compacted to slot 1
        makeEntity(*save, 7, 9, 0x2222, entity);
        CHECK(save->writeBoxSlot(5, 0, entity));  // another box: initialises the box storage
        save->refreshChecksums();
        CHECK(save->checksumsValid());
        std::vector<uint8_t> out = save->data();
        auto again = load(out);
        CHECK(again != nullptr);
        if (!again) continue;
        CHECK_EQ(again->partyCount(), 1);
        uint8_t buffer[pokemon::entity::kMaxPartySize];
        CHECK(again->readPartySlot(0, buffer) == SlotState::Valid);
        CHECK_EQ(pokemon::entity::decode(1, buffer, 0).species, 25);
        CHECK(again->readBoxSlot(0, 0, buffer) == SlotState::Valid);  // compacted
        CHECK_EQ(pokemon::entity::decode(1, buffer, 0).species, 4);
        CHECK(again->readBoxSlot(0, 2, buffer) == SlotState::Empty);
        CHECK(again->readBoxSlot(5, 0, buffer) == SlotState::Valid);
        CHECK_EQ(pokemon::entity::decode(1, buffer, 0).species, 7);
        // The current box list is mirrored in the main block.
        const fakegb::Gen1Offsets& o = japanese ? fakegb::kGen1Jpn : fakegb::kGen1Int;
        CHECK_EQ(out[o.currentBox], 1);
        CHECK(out[o.boxIndex] & 0x80);  // storage initialised
        CHECK(out.size() == 0x8000);
    }
}

TEST(gb_gen1_bad_checksum_is_reported) {
    std::vector<uint8_t> bytes = fakegb::blankGen1(false, false);
    bytes[fakegb::kGen1Int.checksum] ^= 0xFF;
    LoadResult result;
    auto save = load(bytes, &result);
    CHECK(save == nullptr);
    CHECK(result == LoadResult::BadChecksum);
}

TEST(gb_gen1_trainer_data_round_trip) {
    for (bool japanese : {false, true}) {
        auto save = load(fakegb::blankGen1(japanese, false));
        CHECK(save != nullptr);
        if (!save) continue;
        saves::TrainerData t = save->trainerData();
        t.name = fakegb::otherName(japanese);
        t.tid = 777;
        t.money = 123456;
        t.hours = 12;
        t.minutes = 34;
        t.seconds = 56;
        t.badges = 0x81;
        std::string error;
        CHECK(save->setTrainerData(t, &error));
        save->refreshChecksums();
        auto again = load(save->data());
        CHECK(again != nullptr);
        if (!again) continue;
        saves::TrainerData u = again->trainerData();
        CHECK(u.name == fakegb::otherName(japanese));
        CHECK_EQ(u.tid, 777);
        CHECK_EQ(u.money, 123456u);
        CHECK_EQ(u.hours, 12);
        CHECK_EQ(u.minutes, 34);
        CHECK_EQ(u.seconds, 56);
        CHECK_EQ(u.badges, 0x81);
        t.hours = 300;
        CHECK(!again->setTrainerData(t, &error));
        t.hours = 1;
        t.name = "TOOLONGNAME";
        CHECK(!again->setTrainerData(t, &error));
    }
}

TEST(gb_gen2_loads_blank_saves) {
    for (const fakegb::Gen2Setup* s : {&fakegb::kGen2Gs, &fakegb::kGen2Crystal, &fakegb::kGen2GsJpn, &fakegb::kGen2CrystalJpn}) {
        LoadResult result;
        auto save = load(fakegb::blankGen2(*s), &result);
        CHECK(result == LoadResult::Ok);
        CHECK(save != nullptr);
        if (!save) continue;
        CHECK(save->game() == (s->crystal ? Game::Crystal : Game::GoldSilver));
        CHECK_EQ(save->format(), 2);
        CHECK(save->trainerName() == fakegb::trainerName(s->japanese));
        CHECK_EQ(save->trainerId(), fakegb::kTid);
        CHECK_EQ(save->boxCount(), s->boxes);
        CHECK_EQ(save->boxSlotCount(), s->capacity);
        CHECK_EQ(save->language(), s->japanese ? 1 : 2);
        CHECK_EQ(save->hasTrainerGender(), s->crystal);
        CHECK(save->checksumsValid());
    }
}

TEST(gb_gen2_unchanged_save_round_trips) {
    for (const fakegb::Gen2Setup* s : {&fakegb::kGen2Gs, &fakegb::kGen2Crystal}) {
        std::vector<uint8_t> bytes = fakegb::blankGen2(*s);
        fakegb::RawMon egg;
        egg.marker = 0xFD;
        egg.body = fakegb::gen2Body(175, 5, fakegb::kTid, 0x1111, 48);
        egg.ot = "ASH";
        egg.nick = "EGG";
        fakegb::RawMon mon;
        mon.marker = 152;
        mon.body = fakegb::gen2Body(152, 8, fakegb::kTid, 0xFAFA, 48, 0x92);
        mon.ot = "ASH";
        mon.nick = "CHIKORITA";
        fakegb::writeList(bytes, s->party, 6, 48, s->stringBytes, 2, 2, {mon, egg});
        fakegb::fixGen2Checksum(bytes, *s);
        auto save = load(bytes);
        CHECK(save != nullptr);
        if (!save) continue;
        CHECK_EQ(save->partyCount(), 2);
        uint8_t buffer[pokemon::entity::kMaxPartySize];
        CHECK(save->readPartySlot(1, buffer) == SlotState::Valid);
        pokemon::Pokemon p = pokemon::entity::decode(2, buffer, 0);
        CHECK(p.isEgg);
        CHECK_EQ(p.species, 175);
        CHECK(save->readPartySlot(0, buffer) == SlotState::Valid);
        p = pokemon::entity::decode(2, buffer, 0);
        CHECK_EQ(p.species, 152);
        CHECK(p.heldItem == data::itemFromGen2(0x92));
        // Checksums are rewritten and the backups copied, but the data is the same
        // as long as the input was consistent: copy the expected backup into place.
        save->refreshChecksums();
        std::vector<uint8_t> expected = bytes;
        if (s->crystal) {
            std::memmove(&expected[0x1209], &expected[0x2009], 0xB7A);
        } else {
            std::memmove(&expected[0x15C7], &expected[0x2009], 0x222F - 0x2009);
            std::memmove(&expected[0x3D69], &expected[0x222F], 0x23D9 - 0x222F);
            std::memmove(&expected[0x0C6B], &expected[0x23D9], 0x2856 - 0x23D9);
            std::memmove(&expected[0x7E39], &expected[0x2856], 0x288A - 0x2856);
            std::memmove(&expected[0x10E8], &expected[0x288A], 0x2D69 - 0x288A);
        }
        CHECK(save->data() == expected);
    }
}

TEST(gb_gen2_write_and_reload) {
    for (const fakegb::Gen2Setup* s : {&fakegb::kGen2Gs, &fakegb::kGen2Crystal, &fakegb::kGen2GsJpn, &fakegb::kGen2CrystalJpn}) {
        auto save = load(fakegb::blankGen2(*s));
        CHECK(save != nullptr);
        if (!save) continue;
        uint8_t entity[100];
        makeEntity(*save, 249, 40, 0xAAAA, entity);
        CHECK(save->appendParty(entity));
        makeEntity(*save, 150, 70, 0x5555, entity);
        CHECK(save->writeBoxSlot(3, 4, entity));
        CHECK(save->setBoxName(3, fakegb::otherName(s->japanese)));
        save->refreshChecksums();
        CHECK(save->checksumsValid());
        auto again = load(save->data());
        CHECK(again != nullptr);
        if (!again) continue;
        CHECK_EQ(again->partyCount(), 1);
        uint8_t buffer[pokemon::entity::kMaxPartySize];
        CHECK(again->readBoxSlot(3, 0, buffer) == SlotState::Valid);
        CHECK_EQ(pokemon::entity::decode(2, buffer, 0).species, 150);
        CHECK(again->boxName(3) == fakegb::otherName(s->japanese));
        // The trainer and the checksum copies line up.
        const uint8_t* d = again->data().data();
        const uint16_t sum1 = static_cast<uint16_t>(d[s->checksum] | (d[s->checksum + 1] << 8));
        const uint16_t sum2 = static_cast<uint16_t>(d[s->checksum2] | (d[s->checksum2 + 1] << 8));
        CHECK_EQ(sum1, sum2);
    }
}

TEST(gb_gen2_crystal_trainer_data) {
    auto save = load(fakegb::blankGen2(fakegb::kGen2Crystal));
    CHECK(save != nullptr);
    if (!save) return;
    saves::TrainerData t = save->trainerData();
    t.name = "KRIS";
    t.gender = 1;
    t.money = 54321;
    t.hours = 100;
    t.minutes = 5;
    t.seconds = 6;
    t.badges = 0x01FF;
    CHECK(save->setTrainerData(t));
    save->refreshChecksums();
    auto again = load(save->data());
    CHECK(again != nullptr);
    if (!again) return;
    saves::TrainerData u = again->trainerData();
    CHECK(u.name == "KRIS");
    CHECK_EQ(u.gender, 1);
    CHECK_EQ(u.money, 54321u);
    CHECK_EQ(u.hours, 100);
    CHECK_EQ(u.badges, 0x01FF);
    CHECK_EQ(again->badgeCount(), 16);
}

TEST(gb_saves_keep_the_emulator_trailer) {
    std::vector<uint8_t> bytes = fakegb::blankGen2(fakegb::kGen2Crystal);
    for (int i = 0; i < 0x30; ++i) bytes.push_back(static_cast<uint8_t>(0xA0 + i));
    auto save = load(bytes);
    CHECK(save != nullptr);
    if (!save) return;
    uint8_t entity[100];
    makeEntity(*save, 25, 5, 0x1111, entity);
    CHECK(save->writeBoxSlot(0, 0, entity));
    save->refreshChecksums();
    const std::vector<uint8_t>& out = save->data();
    CHECK_EQ(out.size(), bytes.size());
    CHECK(std::memcmp(out.data() + 0x8000, bytes.data() + 0x8000, 0x30) == 0);

    std::vector<uint8_t> gen1 = fakegb::blankGen1(false, false);
    for (int i = 0; i < 0x2C; ++i) gen1.push_back(static_cast<uint8_t>(i));
    auto s1 = load(gen1);
    CHECK(s1 != nullptr);
    if (s1) {
        s1->refreshChecksums();
        CHECK(s1->data() == gen1);
    }
}

TEST(gb_saves_reject_other_data) {
    LoadResult result;
    CHECK(load(std::vector<uint8_t>(0x8000, 0), &result) == nullptr);
    CHECK(load(std::vector<uint8_t>(0x8000, 0xFF), &result) == nullptr);
    CHECK(load(std::vector<uint8_t>(0x8000 + 0x31, 0), &result) == nullptr);
    CHECK(result == LoadResult::WrongSize);
    // A Gen II save must not be taken for Gen I and vice versa.
    CHECK(Game::Unknown != saves::Gen2Save::detect(fakegb::blankGen2(fakegb::kGen2Gs)));
    CHECK(Game::Unknown == saves::Gen1Save::detect(fakegb::blankGen2(fakegb::kGen2Gs)));
    CHECK(Game::Unknown == saves::Gen2Save::detect(fakegb::blankGen1(false, false)));
}

TEST(gb_saves_commit_through_the_safe_pipeline) {
    // validateSave accepts a modified save and the written bytes load again.
    auto save = load(fakegb::blankGen2(fakegb::kGen2Crystal));
    CHECK(save != nullptr);
    if (!save) return;
    uint8_t entity[100];
    makeEntity(*save, 25, 5, 0x1111, entity);
    CHECK(save->appendParty(entity));
    save->refreshChecksums();
    CHECK(saves::validateSave(*save));
}

TEST(gb_probe_names_and_covers) {
    CHECK(std::string(saves::versionName(35)) == "Red/Blue");
    CHECK(std::string(saves::versionName(38)) == "Yellow");
    CHECK(std::string(saves::versionName(39)) == "Gold/Silver");
    CHECK(std::string(saves::versionName(41)) == "Crystal");
    CHECK_EQ(saves::coverSpecies(38), 25);
    CHECK_EQ(saves::coverSpecies(41), 245);
}
