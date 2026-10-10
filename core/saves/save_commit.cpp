#include "core/saves/save_commit.h"

#include "core/pokemon/pk4.h"
#include "core/saves/gen1_save.h"
#include "core/saves/gen2_save.h"
#include "core/saves/gen3_save.h"
#include "core/saves/gen5_save.h"

namespace pktool::saves {

const char* describe(const CommitResult& result) {
    switch (result.status) {
        case CommitResult::Status::Ok:
            return "Saved";
        case CommitResult::Status::ValidationFailed:
            return "Validation failed; nothing was written";
        case CommitResult::Status::BackupFailed:
            return "Backup failed; nothing was written";
        case CommitResult::Status::WriteFailed:
            return io::describe(result.write);
    }
    return "Unknown error";
}

bool validateSave(const SaveFile& save) {
    // Checked in place: reloading the save would hold a second 512 KB copy,
    // which the DS heap cannot always afford while the file is being written.
    if (!save.checksumsValid()) return false;
    const std::vector<uint8_t>& bytes = save.data();
    if (save.format() == 1 && Gen1Save::detect(bytes) != save.game()) return false;
    if (save.format() == 2 && Gen2Save::detect(bytes) != save.game()) return false;
    if (save.format() == 3 && Gen3Save::detect(bytes) != save.game()) return false;
    if (save.format() >= 5 && Gen5Save::detect(bytes) != save.game()) return false;
    uint8_t buffer[pokemon::pk4::kPartySize];
    for (int slot = 0; slot < save.partyCount(); ++slot) {
        if (save.readPartySlot(slot, buffer) != SlotState::Valid) return false;
    }
    for (int box = 0; box < save.boxCount(); ++box) {
        for (int slot = 0; slot < SaveFile::kSlotsPerBox; ++slot) {
            if (save.readBoxSlot(box, slot, buffer) == SlotState::BadChecksum) return false;
        }
    }
    return true;
}

bool validateGen4Save(const Gen4Save& save) {
    return save.isLoaded() && validateSave(save);
}

CommitResult commitGen4Save(Gen4Save& save, const std::string& path, const std::string& backupDir) {
    return commitSave(save, path, backupDir);
}

CommitResult commitSave(SaveFile& save, const std::string& path, const std::string& backupDir) {
    CommitResult result;
    save.refreshChecksums();
    if (!validateSave(save)) {
        result.status = CommitResult::Status::ValidationFailed;
        return result;
    }
    // The current file becomes the timestamped backup (moved, not copied).
    result.write = io::safeWriteWithBackup(path, save.data(), backupDir, result.backupPath);
    if (result.write == io::WriteResult::BackupFailed) {
        result.status = CommitResult::Status::BackupFailed;
    } else if (result.write != io::WriteResult::Ok) {
        result.status = CommitResult::Status::WriteFailed;
    }
    return result;
}

}  // namespace pktool::saves
