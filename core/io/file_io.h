#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Portable file helpers built only on the C standard library and POSIX
// directory functions, which libnds/libfat also provide on the DS.
namespace pktool::io {

enum class WriteResult : uint8_t {
    Ok,
    TempWriteFailed,   // could not create/write the temporary file
    VerifyFailed,      // read-back of the temporary file did not match
    ReplaceFailed,     // could not move the temporary file into place
    BackupFailed,      // could not keep the current file as a backup
};

const char* describe(WriteResult result);

// How much the read helpers (readFile, FileReader) have read since the last reset. Tests and
// profiling use it to check how much of an SD card a flow reads (the DS reads ~1 MB/s).
struct ReadStats {
    uint64_t bytes = 0;  // bytes requested from files
    uint32_t reads = 0;  // read calls
    uint32_t opens = 0;  // files opened
};
const ReadStats& readStats();
void resetReadStats();

// Random access to part of a file without loading all of it.
class FileReader {
public:
    FileReader() = default;
    ~FileReader();
    FileReader(const FileReader&) = delete;
    FileReader& operator=(const FileReader&) = delete;

    bool open(const std::string& path);
    // Size of the open file, or 0.
    size_t size();
    // Reads exactly `length` bytes at `offset`; false if the file is shorter.
    bool read(size_t offset, size_t length, uint8_t* out);

private:
    std::FILE* file_ = nullptr;
};

bool fileExists(const std::string& path);
// Size in bytes, or -1 if the file cannot be read.
long fileSize(const std::string& path);
// Size and last modification time (seconds) of a file; false if it does not exist. Together
// they tell whether a file changed since it was last looked at.
bool fileStamp(const std::string& path, long& size, long& modified);
bool readFile(const std::string& path, std::vector<uint8_t>& out);

// Writes `data` to `path` without ever leaving a half-written file:
//   write <path>.tmp -> read back and compare -> move the current file to
//   <path>.bak -> rename <path>.tmp to <path>.
// If anything fails before the final rename, the original file is untouched.
WriteResult safeWriteFile(const std::string& path, const std::vector<uint8_t>& data);

// Like safeWriteFile, but the current file is kept as a timestamped backup in
// `backupDir` (see createBackup) by moving it there instead of copying it:
//   write <path>.tmp -> read back and compare -> move <path> into backupDir ->
//   rename <path>.tmp to <path>.
// If the move is impossible it falls back to createBackup + safeWriteFile's
// replace. The original file is untouched until the read-back has passed.
// `backupPath` receives the backup (empty if `path` did not exist).
WriteResult safeWriteWithBackup(const std::string& path, const std::vector<uint8_t>& data,
                                const std::string& backupDir, std::string& backupPath);

// Creates `path` and any missing parents. Succeeds if it already exists.
bool makeDirectories(const std::string& path);

// Copies `savePath` into `backupDir` as "<file name>.<YYYYMMDD-HHMMSS>.bak".
// Returns the created backup path, or an empty string on failure.
std::string createBackup(const std::string& savePath, const std::string& backupDir);

// Lists backups of `savePath` in `backupDir`, newest first (full paths).
std::vector<std::string> listBackups(const std::string& savePath, const std::string& backupDir);

// Restores a backup onto `savePath` using safeWriteFile. The current save is
// itself backed up first, so a restore can always be undone.
WriteResult restoreBackup(const std::string& backupPath, const std::string& savePath,
                          const std::string& backupDir);

// Recursively finds "*.sav" files under `root`, up to `maxDepth` directory
// levels, skipping hidden and system directories. Results are sorted.
std::vector<std::string> findSaveFiles(const std::string& root, int maxDepth);

// Files in `dir` (not recursive) whose name ends with `extension` (any
// case), sorted by name.
std::vector<std::string> listFiles(const std::string& dir, const std::string& extension);

std::string fileName(const std::string& path);
std::string joinPath(const std::string& dir, const std::string& name);

}  // namespace pktool::io
