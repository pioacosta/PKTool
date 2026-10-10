// pktool_cli — inspect saves on a PC using the same core as the DS app.
//
//   pktool_cli info <file.sav>                 game, trainer, party and box summary
//   pktool_cli show <file.sav> <box> <slot>    full details of one box slot (1-based)
//   pktool_cli party <file.sav> <slot>         full details of one party slot (1-based)
//   pktool_cli check <file.sav>                legality report of every Pokémon

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "core/data/names.h"
#include "core/io/file_io.h"
#include "core/legality/legality.h"
#include "core/pokemon/entity.h"
#include "core/saves/gen4_save.h"
#include "core/saves/save_file.h"
#include "core/saves/save_probe.h"

using namespace pktool;
namespace entity = pokemon::entity;

namespace {

const char* loadError(saves::LoadResult result) {
    switch (result) {
        case saves::LoadResult::Ok:
            return "ok";
        case saves::LoadResult::WrongSize:
            return "not a 512 KiB Gen IV/V save";
        case saves::LoadResult::UnknownFormat:
            return "unrecognised save format";
        case saves::LoadResult::BadChecksum:
            return "both copies of a save block are corrupt";
    }
    return "unknown error";
}

const char* genderSymbol(pokemon::Gender g) {
    switch (g) {
        case pokemon::Gender::Male:
            return "M";
        case pokemon::Gender::Female:
            return "F";
        case pokemon::Gender::Genderless:
            break;
    }
    return "-";
}

void printSummaryLine(const pokemon::Pokemon& p) {
    std::printf("%-12s Lv.%3u %s%s", data::speciesName(p.species), p.level, genderSymbol(p.gender),
                p.shiny ? " *shiny*" : "");
    if (p.isEgg) {
        std::printf(" (egg)");
    } else if (p.isNicknamed) {
        std::printf(" \"%s\"", p.nickname.c_str());
    }
    std::printf("\n");
}

void printDetails(const pokemon::Pokemon& p) {
    std::printf("Species:   %s (#%u)%s\n", data::speciesName(p.species), p.species, p.isEgg ? " [egg]" : "");
    std::printf("Nickname:  %s%s\n", p.nickname.c_str(), p.isNicknamed ? " (nicknamed)" : "");
    std::printf("Level:     %u (EXP %u)\n", p.level, p.exp);
    std::printf("PID:       %08X  Nature: %s  Shiny: %s\n", p.pid, data::natureName(p.nature), p.shiny ? "yes" : "no");
    std::printf("Ability:   %s\n", data::abilityName(p.ability));
    std::printf("Item:      %s\n", data::itemName(p.heldItem));
    std::printf("Gender:    %s  Form: %u\n", genderSymbol(p.gender), p.form);
    std::printf("IVs:       %u/%u/%u/%u/%u/%u (HP/Atk/Def/Spe/SpA/SpD)\n", p.ivs[0], p.ivs[1], p.ivs[2], p.ivs[3],
                p.ivs[4], p.ivs[5]);
    std::printf("EVs:       %u/%u/%u/%u/%u/%u\n", p.evs[0], p.evs[1], p.evs[2], p.evs[3], p.evs[4], p.evs[5]);
    for (int m = 0; m < 4; ++m) {
        std::printf("Move %d:    %s (PP %u, PP Ups %u)\n", m + 1, data::moveName(p.moves[m]), p.pp[m], p.ppUps[m]);
    }
    std::printf("OT:        %s (%s) %05u/%05u\n", p.trainer.name.c_str(), genderSymbol(p.trainer.gender), p.trainer.tid,
                p.trainer.sid);
    std::printf("Met:       location %u, level %u, ball %u, 20%02u-%02u-%02u, origin game %u\n", p.met.metLocation,
                p.met.metLevel, p.met.ball, p.met.metDate.year, p.met.metDate.month, p.met.metDate.day, p.originGame);
    std::printf("Friendship %u  Pokerus 0x%02X  Fateful %s\n", p.friendship, p.pokerus,
                p.met.fatefulEncounter ? "yes" : "no");
}

int usage() {
    std::fprintf(stderr,
                 "usage:\n"
                 "  pktool_cli info <file.sav>\n"
                 "  pktool_cli show <file.sav> <box 1-18> <slot 1-30>\n"
                 "  pktool_cli party <file.sav> <slot 1-6>\n"
                 "  pktool_cli check <file.sav>\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        return usage();
    }
    const char* command = argv[1];
    std::vector<uint8_t> bytes;
    if (!io::readFile(argv[2], bytes)) {
        std::fprintf(stderr, "cannot read %s\n", argv[2]);
        return 1;
    }
    saves::LoadResult result;
    std::unique_ptr<saves::SaveFile> loaded = saves::loadSaveFile(std::move(bytes), &result);
    if (!loaded) {
        std::fprintf(stderr, "cannot load %s: %s\n", argv[2], loadError(result));
        return 1;
    }
    saves::SaveFile& save = *loaded;

    if (std::strcmp(command, "info") == 0) {
        std::printf("Game:    %s (%s)\n", saves::gameName(save.game()), saves::versionName(save.gameVersion()));
        std::printf("Trainer: %s  TID %05u  SID %05u\n", save.trainerName().c_str(), save.trainerId(), save.secretId());
        if (save.format() == 4) {
            const auto& gen4 = static_cast<const saves::Gen4Save&>(save);
            std::printf("Active partitions: general %d, storage %d\n", gen4.activeGeneralPartition(),
                        gen4.activeStoragePartition());
        }
        std::printf("\n");
        std::printf("Party (%d):\n", save.partyCount());
        for (int i = 0; i < save.partyCount(); ++i) {
            uint8_t buf[entity::kMaxPartySize];
            auto state = save.readPartySlot(i, buf);
            std::printf("  %d. ", i + 1);
            if (state == saves::SlotState::Valid) {
                printSummaryLine(entity::decode(save.format(), buf, save.partySize()));
            } else {
                std::printf("%s\n", state == saves::SlotState::Empty ? "(empty)" : "(bad data)");
            }
        }
        for (int box = 0; box < save.boxCount(); ++box) {
            std::printf("\nBox %d: %s\n", box + 1, save.boxName(box).c_str());
            for (int slot = 0; slot < saves::SaveFile::kSlotsPerBox; ++slot) {
                uint8_t buf[entity::kStoredSize];
                auto state = save.readBoxSlot(box, slot, buf);
                if (state == saves::SlotState::Empty) {
                    continue;
                }
                std::printf("  %2d. ", slot + 1);
                if (state == saves::SlotState::Valid) {
                    printSummaryLine(entity::decode(save.format(), buf, entity::kStoredSize));
                } else {
                    std::printf("(bad data)\n");
                }
            }
        }
        return 0;
    }

    if (std::strcmp(command, "check") == 0) {
        int counts[3] = {0, 0, 0};
        auto report = [&](const char* where, const pokemon::Pokemon& p) {
            legality::Report r = legality::check(p);
            auto overall = r.overall();
            ++counts[static_cast<int>(overall)];
            if (overall == legality::Severity::Valid) return;
            std::printf("%-10s %-12s %s\n", where, data::speciesName(p.species),
                        overall == legality::Severity::Invalid ? "ILLEGAL" : "warning");
            for (const auto& f : r.findings) {
                if (f.severity != legality::Severity::Valid) std::printf("             - %s\n", f.text.c_str());
            }
        };
        char where[32];
        for (int i = 0; i < save.partyCount(); ++i) {
            uint8_t buf[entity::kMaxPartySize];
            if (save.readPartySlot(i, buf) != saves::SlotState::Valid) continue;
            std::snprintf(where, sizeof(where), "party %d", i + 1);
            report(where, entity::decode(save.format(), buf, save.partySize()));
        }
        for (int box = 0; box < save.boxCount(); ++box) {
            for (int slot = 0; slot < saves::SaveFile::kSlotsPerBox; ++slot) {
                uint8_t buf[entity::kStoredSize];
                if (save.readBoxSlot(box, slot, buf) != saves::SlotState::Valid) continue;
                std::snprintf(where, sizeof(where), "box %d/%d", box + 1, slot + 1);
                report(where, entity::decode(save.format(), buf, entity::kStoredSize));
            }
        }
        std::printf("\n%d legal, %d with warnings, %d illegal\n", counts[0], counts[1], counts[2]);
        return 0;
    }

    if (std::strcmp(command, "show") == 0 && argc == 5) {
        int box = std::atoi(argv[3]) - 1;
        int slot = std::atoi(argv[4]) - 1;
        uint8_t buf[entity::kStoredSize];
        auto state = save.readBoxSlot(box, slot, buf);
        if (state != saves::SlotState::Valid) {
            std::fprintf(stderr, "box %d slot %d is %s\n", box + 1, slot + 1,
                         state == saves::SlotState::Empty ? "empty" : "corrupt");
            return 1;
        }
        printDetails(entity::decode(save.format(), buf, entity::kStoredSize));
        return 0;
    }

    if (std::strcmp(command, "party") == 0 && argc == 4) {
        int slot = std::atoi(argv[3]) - 1;
        uint8_t buf[entity::kMaxPartySize];
        if (save.readPartySlot(slot, buf) != saves::SlotState::Valid) {
            std::fprintf(stderr, "party slot %d is empty or corrupt\n", slot + 1);
            return 1;
        }
        printDetails(entity::decode(save.format(), buf, save.partySize()));
        return 0;
    }

    return usage();
}
