#include "core/io/file_io.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace pktool::io {
namespace {

bool endsWithIgnoreCase(const std::string& s, const char* suffix) {
    size_t n = std::char_traits<char>::length(suffix);
    if (s.size() < n) {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        char a = s[s.size() - n + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') {
            a = static_cast<char>(a - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

bool isDirectory(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool writeWhole(const std::string& path, const std::vector<uint8_t>& data) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) {
        return false;
    }
    bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = (std::fflush(f) == 0) && ok;
    ok = (std::fclose(f) == 0) && ok;
    return ok;
}

std::string timestamp() {
    std::time_t now = std::time(nullptr);
    std::tm* t = std::localtime(&now);
    char buffer[32];
    if (t == nullptr) {
        return "00000000-000000";
    }
    std::snprintf(buffer, sizeof(buffer), "%04d%02d%02d-%02d%02d%02d", t->tm_year + 1900, t->tm_mon + 1,
                  t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
    return buffer;
}

// Streaming helpers: a whole save is 512 KB and the DS heap is ~2.5 MB, so
// copies and read-back checks go through this small buffer instead of
// loading the file again.
constexpr size_t kChunk = 4096;

// True if the file at `path` holds exactly `data`.
bool fileEquals(const std::string& path, const uint8_t* data, size_t size) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    // Static, not on the stack (the DS stack is 16 KB): bigger reads are much
    // faster on a flashcart than many small ones.
    static uint8_t buffer[4 * kChunk];
    size_t done = 0;
    bool ok = true;
    while (ok) {
        size_t got = std::fread(buffer, 1, sizeof(buffer), f);
        if (got == 0) break;
        ok = done + got <= size && std::equal(buffer, buffer + got, data + done);
        done += got;
    }
    ok = ok && std::ferror(f) == 0 && done == size;
    std::fclose(f);
    return ok;
}

// True if both files have the same contents.
bool filesEqual(const std::string& a, const std::string& b) {
    FILE* fa = std::fopen(a.c_str(), "rb");
    FILE* fb = fa ? std::fopen(b.c_str(), "rb") : nullptr;
    bool ok = fa != nullptr && fb != nullptr;
    uint8_t ba[kChunk];
    uint8_t bb[kChunk];
    while (ok) {
        size_t ga = std::fread(ba, 1, sizeof(ba), fa);
        size_t gb = std::fread(bb, 1, sizeof(bb), fb);
        ok = ga == gb && std::equal(ba, ba + ga, bb);
        if (ga == 0) break;
    }
    ok = ok && std::ferror(fa) == 0 && std::ferror(fb) == 0;
    if (fb) std::fclose(fb);
    if (fa) std::fclose(fa);
    return ok;
}

// Copies `from` to `to` (created or truncated).
bool copyWhole(const std::string& from, const std::string& to) {
    FILE* in = std::fopen(from.c_str(), "rb");
    if (in == nullptr) return false;
    FILE* out = std::fopen(to.c_str(), "wb");
    bool ok = out != nullptr;
    uint8_t buffer[kChunk];
    while (ok) {
        size_t got = std::fread(buffer, 1, sizeof(buffer), in);
        if (got == 0) break;
        ok = std::fwrite(buffer, 1, got, out) == got;
    }
    ok = ok && std::ferror(in) == 0;
    std::fclose(in);
    if (out) {
        ok = (std::fflush(out) == 0) && ok;
        ok = (std::fclose(out) == 0) && ok;
    }
    return ok;
}

// Makes sure renames and deletions reach the card. libfat keeps FAT and
// directory sectors in a write-back cache that it flushes when a file opened
// for writing is closed; reopening `path` for update (without writing) forces
// that flush, so a crash or power-off afterwards cannot leave the volume
// half-updated. Elsewhere this is a no-op.
void flushVolume(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "r+b");
    if (f != nullptr) std::fclose(f);
}

// Moves `tmp` onto `path`, keeping the previous file as <path>.bak.
WriteResult replaceWith(const std::string& tmp, const std::string& path) {
    const std::string bak = path + ".bak";
    // FAT (and Windows) cannot rename onto an existing file, so move the
    // original out of the way first and put it back if the final step fails.
    bool hadOriginal = fileExists(path);
    if (hadOriginal) {
        std::remove(bak.c_str());
        if (std::rename(path.c_str(), bak.c_str()) != 0) {
            std::remove(tmp.c_str());
            flushVolume(path);
            return WriteResult::ReplaceFailed;
        }
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        if (hadOriginal) {
            std::rename(bak.c_str(), path.c_str());
        }
        std::remove(tmp.c_str());
        flushVolume(path);
        return WriteResult::ReplaceFailed;
    }
    flushVolume(path);
    return WriteResult::Ok;
}

void scan(const std::string& dir, int depthLeft, std::vector<std::string>& out) {
    DIR* d = opendir(dir.c_str());
    if (d == nullptr) {
        return;
    }
    std::vector<std::string> subdirs;
    while (dirent* entry = readdir(d)) {
        std::string name = entry->d_name;
        if (name.empty() || name[0] == '.' || name[0] == '_') {
            continue;  // ".", "..", hidden files, "_system_" style folders
        }
        std::string path = joinPath(dir, name);
        if (isDirectory(path)) {
            if (depthLeft > 0) {
                subdirs.push_back(path);
            }
        } else if (endsWithIgnoreCase(name, ".sav")) {
            out.push_back(path);
        }
    }
    closedir(d);
    for (const std::string& sub : subdirs) {
        scan(sub, depthLeft - 1, out);
    }
}

}  // namespace

const char* describe(WriteResult result) {
    switch (result) {
        case WriteResult::Ok:
            return "OK";
        case WriteResult::TempWriteFailed:
            return "Could not write temporary file";
        case WriteResult::VerifyFailed:
            return "Verification of written data failed";
        case WriteResult::ReplaceFailed:
            return "Could not replace the original file";
        case WriteResult::BackupFailed:
            return "Could not back up the current file";
    }
    return "Unknown error";
}

long fileSize(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) return -1;
    return static_cast<long>(st.st_size);
}

bool fileStamp(const std::string& path, long& size, long& modified) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) return false;
    size = static_cast<long>(st.st_size);
    modified = static_cast<long>(st.st_mtime);
    return true;
}

bool fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && !S_ISDIR(st.st_mode);
}

namespace {
ReadStats g_readStats;
}  // namespace

const ReadStats& readStats() { return g_readStats; }
void resetReadStats() { g_readStats = ReadStats(); }

FileReader::~FileReader() {
    if (file_ != nullptr) std::fclose(file_);
}

bool FileReader::open(const std::string& path) {
    if (file_ != nullptr) std::fclose(file_);
    file_ = std::fopen(path.c_str(), "rb");
    if (file_ != nullptr) ++g_readStats.opens;
    return file_ != nullptr;
}

size_t FileReader::size() {
    if (file_ == nullptr || std::fseek(file_, 0, SEEK_END) != 0) return 0;
    long end = std::ftell(file_);
    return end < 0 ? 0 : static_cast<size_t>(end);
}

bool FileReader::read(size_t offset, size_t length, uint8_t* out) {
    if (file_ == nullptr) return false;
    if (std::fseek(file_, static_cast<long>(offset), SEEK_SET) != 0) return false;
    ++g_readStats.reads;
    g_readStats.bytes += length;
    return std::fread(out, 1, length, file_) == length;
}

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        return false;
    }
    ++g_readStats.opens;
    bool ok = std::fseek(f, 0, SEEK_END) == 0;
    long size = ok ? std::ftell(f) : -1;
    ok = ok && size >= 0 && std::fseek(f, 0, SEEK_SET) == 0;
    if (ok) {
        out.assign(static_cast<size_t>(size), 0);
        ++g_readStats.reads;
        g_readStats.bytes += out.size();
        ok = size == 0 || std::fread(out.data(), 1, out.size(), f) == out.size();
    }
    std::fclose(f);
    return ok;
}

WriteResult safeWriteFile(const std::string& path, const std::vector<uint8_t>& data) {
    const std::string tmp = path + ".tmp";
    if (!writeWhole(tmp, data)) {
        std::remove(tmp.c_str());
        return WriteResult::TempWriteFailed;
    }
    if (!fileEquals(tmp, data.data(), data.size())) {
        std::remove(tmp.c_str());
        return WriteResult::VerifyFailed;
    }
    return replaceWith(tmp, path);
}

bool makeDirectories(const std::string& path) {
    if (path.empty() || isDirectory(path)) {
        return true;
    }
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos && slash > 0 && !makeDirectories(path.substr(0, slash))) {
        return false;
    }
    if (slash != std::string::npos && slash + 1 == path.size()) {
        return true;  // trailing slash: parent already created
    }
    return mkdir(path.c_str(), 0777) == 0 || isDirectory(path);
}

namespace {

// "<backupDir>/<file name>.<YYYYMMDD-HHMMSS>[_n].bak", not existing yet.
std::string newBackupName(const std::string& savePath, const std::string& backupDir) {
    std::string target = joinPath(backupDir, fileName(savePath) + "." + timestamp() + ".bak");
    for (int n = 1; fileExists(target) && n < 100; ++n) {
        target = joinPath(backupDir, fileName(savePath) + "." + timestamp() + "_" + std::to_string(n) + ".bak");
    }
    return target;
}

}  // namespace

std::string createBackup(const std::string& savePath, const std::string& backupDir) {
    if (!fileExists(savePath) || !makeDirectories(backupDir)) {
        return std::string();
    }
    // Two backups within the same second get a counter instead of overwriting.
    const std::string target = newBackupName(savePath, backupDir);
    // Streamed copy, verified against the original, then renamed into place.
    const std::string tmp = target + ".tmp";
    if (!copyWhole(savePath, tmp) || !filesEqual(savePath, tmp) || replaceWith(tmp, target) != WriteResult::Ok) {
        std::remove(tmp.c_str());
        flushVolume(savePath);
        return std::string();
    }
    return target;
}


WriteResult safeWriteWithBackup(const std::string& path, const std::vector<uint8_t>& data,
                                const std::string& backupDir, std::string& backupPath) {
    backupPath.clear();
    if (!fileExists(path)) return safeWriteFile(path, data);
    const std::string tmp = path + ".tmp";
    if (!writeWhole(tmp, data)) {
        std::remove(tmp.c_str());
        return WriteResult::TempWriteFailed;
    }
    if (!fileEquals(tmp, data.data(), data.size())) {
        std::remove(tmp.c_str());
        return WriteResult::VerifyFailed;
    }
    // The current file itself becomes the backup: a rename moves no data, so
    // saving reads and writes the save once instead of copying and comparing it.
    const std::string target = makeDirectories(backupDir) ? newBackupName(path, backupDir) : std::string();
    if (target.empty() || std::rename(path.c_str(), target.c_str()) != 0) {
        // Cannot move it (e.g. another volume): copy it, as before.
        backupPath = createBackup(path, backupDir);
        if (backupPath.empty()) {
            std::remove(tmp.c_str());
            return WriteResult::BackupFailed;
        }
        return replaceWith(tmp, path);
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::rename(target.c_str(), path.c_str());
        std::remove(tmp.c_str());
        flushVolume(path);
        return WriteResult::ReplaceFailed;
    }
    flushVolume(path);
    backupPath = target;
    return WriteResult::Ok;
}

std::vector<std::string> listBackups(const std::string& savePath, const std::string& backupDir) {
    std::vector<std::string> result;
    const std::string prefix = fileName(savePath) + ".";
    DIR* d = opendir(backupDir.c_str());
    if (d == nullptr) {
        return result;
    }
    while (dirent* entry = readdir(d)) {
        std::string name = entry->d_name;
        if (name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0 &&
            endsWithIgnoreCase(name, ".bak") && !endsWithIgnoreCase(name, ".bak.bak")) {
            result.push_back(joinPath(backupDir, name));
        }
    }
    closedir(d);
    // Timestamps sort lexicographically; newest first.
    std::sort(result.rbegin(), result.rend());
    return result;
}

WriteResult restoreBackup(const std::string& backupPath, const std::string& savePath,
                          const std::string& backupDir) {
    std::vector<uint8_t> data;
    if (!readFile(backupPath, data)) {
        return WriteResult::TempWriteFailed;
    }
    if (fileExists(savePath) && createBackup(savePath, backupDir).empty()) {
        return WriteResult::TempWriteFailed;
    }
    return safeWriteFile(savePath, data);
}

std::vector<std::string> findSaveFiles(const std::string& root, int maxDepth) {
    std::vector<std::string> out;
    scan(root, maxDepth, out);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> listFiles(const std::string& dir, const std::string& extension) {
    std::vector<std::string> out;
    DIR* d = opendir(dir.c_str());
    if (d == nullptr) {
        return out;
    }
    while (dirent* entry = readdir(d)) {
        std::string name = entry->d_name;
        if (!name.empty() && name[0] != '.' && endsWithIgnoreCase(name, extension.c_str())) {
            std::string path = joinPath(dir, name);
            if (!isDirectory(path)) out.push_back(path);
        }
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

std::string fileName(const std::string& path) {
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string joinPath(const std::string& dir, const std::string& name) {
    if (dir.empty()) {
        return name;
    }
    if (dir.back() == '/') {
        return dir + name;
    }
    return dir + "/" + name;
}

}  // namespace pktool::io
