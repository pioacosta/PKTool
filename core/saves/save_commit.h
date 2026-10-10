#pragma once

#include <string>

#include "core/io/file_io.h"
#include "core/saves/gen4_save.h"

namespace pktool::saves {

struct CommitResult {
    enum class Status : uint8_t {
        Ok,
        ValidationFailed,  // the modified save did not pass validation; nothing written
        BackupFailed,      // the current file could not be backed up; nothing written
        WriteFailed,       // see `write`; the original file is untouched
    };
    Status status = Status::Ok;
    io::WriteResult write = io::WriteResult::Ok;
    std::string backupPath;
};

const char* describe(const CommitResult& result);

// Writes a modified save of any generation back to `path` with the same
// pipeline as commitGen4Save below.
CommitResult commitSave(SaveFile& save, const std::string& path, const std::string& backupDir);

// Structural validation used by commitSave, done in place (no second copy of
// the save): valid checksums, the data still detects as the same game and no
// corrupt slot.
bool validateSave(const SaveFile& save);

// Writes a modified save back to `path` following the save-safety pipeline:
//   recalculate checksums -> validate the whole save (valid checksums, same
//   game, no corrupt slots) -> back up the current file into
//   `backupDir` -> safe write (temporary file, read-back, replace).
CommitResult commitGen4Save(Gen4Save& save, const std::string& path, const std::string& backupDir);

// Structural validation used by commitGen4Save.
bool validateGen4Save(const Gen4Save& save);

}  // namespace pktool::saves
