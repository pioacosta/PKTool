#include "core/io/settings.h"

#include "core/data/language.h"

#include <vector>

namespace pktool::io {
namespace {

std::string trim(const std::string& s) {
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && (s[begin] == ' ' || s[begin] == '\t' || s[begin] == '\r')) ++begin;
    while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r')) --end;
    return s.substr(begin, end - begin);
}

std::string lower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

}  // namespace

std::string settingsToText(const Settings& settings) {
    std::string out = "# PKTool settings\n";
    out += settings.sortById ? "sort_order=id\n" : "sort_order=alphabetical\n";
    out += "new_level=" + std::to_string(settings.newLevel) + "\n";
    out += settings.animations ? "animations=on\n" : "animations=off\n";
    if (settings.language != data::kLangEnglish) {
        out += std::string("language=") + data::languageCode(settings.language) + "\n";
    }
    return out;
}

void parseSettings(const std::string& text, Settings& settings) {
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = trim(text.substr(pos, end - pos));
        pos = end + 1;
        size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || line[0] == ';' || eq == std::string::npos) continue;
        std::string key = lower(trim(line.substr(0, eq)));
        std::string value = lower(trim(line.substr(eq + 1)));
        if (key == "sort_order") {
            if (value == "id") settings.sortById = true;
            if (value == "alphabetical") settings.sortById = false;
        } else if (key == "animations") {
            if (value == "on" || value == "1" || value == "true") settings.animations = true;
            if (value == "off" || value == "0" || value == "false") settings.animations = false;
        } else if (key == "language") {
            if (uint8_t id = data::languageFromCode(value)) settings.language = id;
        } else if (key == "new_level") {
            if (value.empty() || value.size() > 3) continue;
            int level = 0;
            bool digits = true;
            for (char c : value) {
                if (c < '0' || c > '9') digits = false;
                level = level * 10 + (c - '0');
            }
            if (digits && level >= kMinNewLevel && level <= kMaxNewLevel) {
                settings.newLevel = static_cast<uint8_t>(level);
            }
        }
    }
}

bool loadSettings(const std::string& path, Settings& settings) {
    std::vector<uint8_t> bytes;
    if (!readFile(path, bytes)) return false;
    parseSettings(std::string(bytes.begin(), bytes.end()), settings);
    return true;
}

WriteResult saveSettings(const std::string& path, const Settings& settings) {
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos && slash > 0) makeDirectories(path.substr(0, slash));
    std::string text = settingsToText(settings);
    return safeWriteFile(path, std::vector<uint8_t>(text.begin(), text.end()));
}

}  // namespace pktool::io
