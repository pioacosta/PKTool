#include "core/saves/save_probe.h"

#include "core/io/file_io.h"
#include "core/pokemon/entity.h"
#include "core/saves/gb_save.h"
#include "core/saves/gen3_save.h"
#include "core/saves/gen4_save.h"
#include "core/saves/gen5_save.h"
#include "core/saves/save_file.h"

namespace pktool::saves {

namespace {

// Loads only the parts of the file a summary needs (footers, the trainer card, the party and,
// for the version of D/P, HG/SS and Gen III, the boxes): a Gen V save is read in ~2 KiB, a Gen IV
// one in ~125 KiB and a Gen III one in ~56 KiB, instead of the whole 512 / 128 KiB. Null when the
// file is not a save (kNotSave: reading it all would not change that) or the fast path cannot
// vouch for the result (kRetry: the caller then reads it all).
enum class Partial { kLoaded, kNotSave, kRetry };

Partial loadPartially(const std::string& path, long size, std::unique_ptr<SaveFile>& save) {
    io::FileReader file;
    if (!file.open(path)) return Partial::kRetry;
    RangeReader read = [&file](size_t offset, size_t length, uint8_t* out) { return file.read(offset, length, out); };
    std::vector<uint8_t> image(static_cast<size_t>(size), 0);
    int generalPartition = -1;
    int storagePartition = -1;
    bool gen4 = false;
    // A failing readProbeImage means the footers do not describe a save of that format; a read
    // error shows up as a failure of the next format and then of the retry.
    if (size == 0x80000) {
        gen4 = !Gen5Save::readProbeImage(image, read);
        if (gen4 && !Gen4Save::readProbeImage(image, read, &generalPartition, &storagePartition)) {
            return Partial::kNotSave;
        }
    } else if (!Gen3Save::readProbeImage(image, read)) {
        return Partial::kNotSave;
    }
    save = loadSaveFile(std::move(image));
    if (!save) return Partial::kRetry;
    if (gen4) {
        // Were a block of the newest copy corrupt, load() would have fallen back to the older
        // copy, which was not read: the answer would come from zeros.
        const Gen4Save& g4 = static_cast<const Gen4Save&>(*save);
        if (g4.activeGeneralPartition() != generalPartition || g4.activeStoragePartition() != storagePartition) {
            save.reset();
            return Partial::kRetry;
        }
    }
    return Partial::kLoaded;
}

bool plausibleSize(long size) {
    // Gen IV and Gen V saves are exactly 512 KiB; Gen III ones 128 KiB (64 KiB
    // with some emulator settings), maybe with a small emulator trailer.
    bool gen3Size = size == 0x10000 || (size >= 0x20000 && size <= 0x20100);
    return size == 0x80000 || gen3Size || GbSave::sizeOk(static_cast<size_t>(size));
}

void summarize(const SaveFile* save, const std::string& path, SaveSummary& out) {
    out.path = path;
    out.game = save->game();
    out.version = save->gameVersion();
    out.trainer = save->trainerName();
    out.trainerId = save->trainerId();
    out.party.clear();
    uint8_t buffer[pokemon::entity::kMaxPartySize];
    for (int slot = 0; slot < save->partyCount(); ++slot) {
        if (save->readPartySlot(slot, buffer) != SlotState::Valid) continue;
        pokemon::Pokemon p = pokemon::entity::decode(save->format(), buffer, save->partySize());
        out.party.push_back(p.isEgg ? 0 : p.species);
    }
}

}  // namespace

bool probeSaveFull(const std::string& path, SaveSummary& out) {
    if (!plausibleSize(io::fileSize(path))) return false;
    std::vector<uint8_t> bytes;
    if (!io::readFile(path, bytes)) return false;
    std::unique_ptr<SaveFile> save = loadSaveFile(std::move(bytes));
    if (!save) return false;
    summarize(save.get(), path, out);
    return true;
}

bool probeSave(const std::string& path, SaveSummary& out) {
    long size = io::fileSize(path);
    if (!plausibleSize(size)) return false;
    // Game Boy saves are 32-64 KiB: read whole (a 64 KiB file may also be a Gen III one).
    if (GbSave::sizeOk(static_cast<size_t>(size))) return probeSaveFull(path, out);
    std::unique_ptr<SaveFile> save;
    Partial partial = loadPartially(path, size, save);
    if (partial == Partial::kNotSave) return false;
    if (partial == Partial::kRetry) return probeSaveFull(path, out);
    summarize(save.get(), path, out);
    return true;
}

std::vector<SaveSummary> findPokemonSaves(const std::string& root, int maxDepth,
                                          const std::vector<SaveSummary>* previous) {
    std::vector<SaveSummary> found;
    for (const std::string& path : io::findSaveFiles(root, maxDepth)) {
        long size = 0;
        long modified = 0;
        if (!io::fileStamp(path, size, modified)) continue;
        bool cached = false;
        if (previous != nullptr) {
            for (const SaveSummary& old : *previous) {
                if (old.path == path && old.fileSize == size && old.modified == modified) {
                    found.push_back(old);
                    cached = true;
                    break;
                }
            }
        }
        if (cached) continue;
        SaveSummary summary;
        if (!probeSave(path, summary)) continue;
        summary.fileSize = size;
        summary.modified = modified;
        found.push_back(std::move(summary));
    }
    return found;
}

const char* versionName(uint8_t version) {
    switch (version) {
        case 1: return "Sapphire";
        case 2: return "Ruby";
        case 3: return "Emerald";
        case 4: return "FireRed";
        case 5: return "LeafGreen";
        case 35: return "Red/Blue";
        case 38: return "Yellow";
        case 39: return "Gold/Silver";
        case 41: return "Crystal";
        case 7: return "HeartGold";
        case 8: return "SoulSilver";
        case 10: return "Diamond";
        case 11: return "Pearl";
        case 12: return "Platinum";
        case 20: return "White";
        case 21: return "Black";
        case 22: return "White 2";
        case 23: return "Black 2";
        default: return "Pokemon";
    }
}

uint16_t coverSpecies(uint8_t version) {
    switch (version) {
        case 1: return 382;   // Kyogre
        case 2: return 383;   // Groudon
        case 3: return 384;   // Rayquaza
        case 4: return 6;     // Charizard
        case 5: return 3;     // Venusaur
        case 35: return 6;    // Charizard (Red; Blue's Blastoise shares the save)
        case 38: return 25;   // Pikachu
        case 39: return 250;  // Ho-Oh (Gold; Silver's Lugia shares the save)
        case 41: return 245;  // Suicune
        case 7: return 250;   // Ho-Oh
        case 8: return 249;   // Lugia
        case 10: return 483;  // Dialga
        case 11: return 484;  // Palkia
        case 12: return 487;  // Giratina
        case 20: return 644;  // Zekrom
        case 21: return 643;  // Reshiram
        case 22:
        case 23: return 646;  // Kyurem
        default: return 0;
    }
}

}  // namespace pktool::saves
