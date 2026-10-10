// Settings screen of the PKTool UI (see app.h): reachable from the Home screen.
//
// Changes take effect at once; leaving the screen (B) writes them to
// <dataDir>/settings.ini through io::saveSettings (the safe write helper). If
// the write fails the settings stay active for this session and a message says so.

#include <string>

#include "core/data/language.h"
#include "core/data/sorted_names.h"
#include "core/io/file_io.h"
#include "ds/app/app.h"
#include "ds/app/ui_common.h"

namespace pktool::ui {
using namespace detail;

namespace {

enum SettingsRow { kSetOrder, kSetLevel, kSetLanguage, kSetAnimations, kSetAbout, kSetRowCount };

const char* const kSetLabels[kSetRowCount] = {"Option lists", "New level", "Names", "Animations", "About"};
const char* const kSetTitles[kSetRowCount] = {"Option lists", "New Pokémon level", "Language of the names", "Animations",
                                              "About PKTool"};

int languageIndex(uint8_t language) {
    for (int i = 0; i < data::kNameLanguageCount; ++i) {
        if (data::kNameLanguages[i] == language) return i;
    }
    return 0;
}

}  // namespace

std::string App::settingsPath() const {
    return io::joinPath(config_.dataDir, "settings.ini");
}

void App::openSettings() {
    setCursor_ = 0;
    setStatus_.clear();
    screen_ = Screen::Settings;
}

// Writes the settings file if something changed since it was last written.
void App::applySettings() {
    if (settings_ != setSaved_) {
        io::WriteResult result = io::saveSettings(settingsPath(), settings_);
        if (result == io::WriteResult::Ok) {
            setSaved_ = settings_;
        } else {
            showMessage("Settings not saved", std::string("Could not write settings.ini:\n") + io::describe(result) +
                                                  "\n\nThey stay active until you exit.");
        }
    }
    screen_ = Screen::Home;
}

void App::updateSettings(const Input& in) {
    int zone = in.touched ? touchedZone(in) : -1;
    int delta = 0;
    int bigDelta = 0;
    bool activate = in.pressed & kKeyA;
    if (zone >= kZoneItem && zone < kZoneItem + kSetRowCount) {
        setCursor_ = zone - kZoneItem;
    } else if (zone >= kZoneDecrease && zone < kZoneDecrease + kSetRowCount) {
        setCursor_ = zone - kZoneDecrease;
        delta = -1;
    } else if (zone >= kZoneIncrease && zone < kZoneIncrease + kSetRowCount) {
        setCursor_ = zone - kZoneIncrease;
        delta = 1;
    }
    if (in.repeated & kKeyUp) setCursor_ = wrap(setCursor_ - 1, kSetRowCount);
    if (in.repeated & kKeyDown) setCursor_ = wrap(setCursor_ + 1, kSetRowCount);
    if (in.repeated & kKeyRight) delta = 1;
    if (in.repeated & kKeyLeft) delta = -1;
    if (in.repeated & kKeyR) bigDelta = 1;
    if (in.repeated & kKeyL) bigDelta = -1;

    switch (setCursor_) {
        case kSetOrder:
            if (delta != 0 || bigDelta != 0 || activate) {
                settings_.sortById = !settings_.sortById;
                data::setOrder(settings_.sortById ? data::Order::ById : data::Order::Alphabetical);
            }
            break;
        case kSetLevel:
            if (delta != 0 || bigDelta != 0) {
                int level = settings_.newLevel + delta + bigDelta * 10;
                settings_.newLevel = static_cast<uint8_t>(clampInt(level, io::kMinNewLevel, io::kMaxNewLevel));
            }
            break;
        case kSetLanguage:
            if (delta != 0 || bigDelta != 0 || activate) {
                int step = delta + bigDelta + (activate ? 1 : 0);
                uint8_t next = data::kNameLanguages[wrap(languageIndex(settings_.language) + step, data::kNameLanguageCount)];
                if (data::setNameLanguage(next)) {
                    settings_.language = next;
                    badgeValid_ = false;  // the checker compares nicknames with the names of this language
                    setStatus_.clear();
                } else {
                    setStatus_ = std::string("No ") + data::languageName(next) + " names (lang/" + data::languageCode(next) +
                                 ".bin missing).";
                }
            }
            break;
        case kSetAnimations:
            if (delta != 0 || bigDelta != 0 || activate) settings_.animations = !settings_.animations;
            break;
        default:
            if (activate) {
                showMessage("About PKTool " + config_.version,
                            "Pok\xC3\xA9mon save editor for Nintendo DS.\nFree software (GPLv3).\n"
                            "Save formats based on PKHeX.\n\nNot affiliated with Nintendo,\n"
                            "Game Freak or The Pok\xC3\xA9mon Company." +
                                (config_.freeMemory ? "\n\nFree memory: " + std::to_string(config_.freeMemory() / 1024) + " KB"
                                                    : std::string()));
            }
            break;
    }
    if (in.pressed & kKeyB) applySettings();
}

void App::drawSettings(Canvas& top, Canvas& bottom) {
    drawChrome(top, "Settings", settings_ != setSaved_ ? "Edited" : "", kYellow);
    top.roundRect(4, kBodyTop + 3, 248, 150, kBorder, kPanel);
    top.text(kFontLarge, 12, 40, 232, Align::Left, kSetTitles[setCursor_], kText);

    std::string lines[5];
    switch (setCursor_) {
        case kSetOrder:
            lines[0] = "Order of the lists of species, moves,";
            lines[1] = "items, natures and balls in the editor";
            lines[2] = "and in the Add list.";
            lines[3] = "By name: A-Z, L/R jump by letter.";
            lines[4] = "By ID: Dex / game order, L/R jump 10.";
            break;
        case kSetLevel:
            lines[0] = "Level of a Pok\xC3\xA9mon made with";
            lines[1] = "Add > New Pok\xC3\xA9mon, 1 to 100 (a species";
            lines[2] = "that appears later is raised to the";
            lines[3] = "lowest level it can be at).";
            break;
        case kSetLanguage:
            lines[0] = "Language of the species, move, item,";
            lines[1] = "ability and nature names, and of the";
            lines[2] = "default nickname of new Pok\xC3\xA9mon.";
            lines[3] = "Needs the language packs in NitroFS.";
            lines[4] = setStatus_;
            break;
        case kSetAnimations:
            lines[0] = "Small visual effects: the selection";
            lines[1] = "bar glides between rows, the selected";
            lines[2] = "Pok\xC3\xA9mon bobs and screens fade in.";
            lines[3] = "Off: everything appears at once.";
            break;
        default:
            lines[0] = "Version " + config_.version;
            lines[1] = "A: show the About box.";
            break;
    }
    int y = 62;
    for (const std::string& line : lines) {
        top.text(kFontSmall, 12, y, 232, Align::Left, line, kMuted);
        y += 13;
    }
    top.hline(12, 134, 232, kBorder);
    top.text(kFontSmall, 12, 140, 232, Align::Left, "Backups: always made before writing.", kDim);
    top.text(kFontSmall, 12, 153, 232, Align::Left, "Saved in settings.ini when you leave.", kDim);

    bottom.grid(0, 0, gfx::kScreenWidth, kHintTop, kBackground, kGridLine, 12);
    bottom.roundRect(4, 3, 248, 170, kBorder, kPanel);
    const int rowH = 16;
    const int x = 8;
    const int w = 236;
    const int vx = x + 100;
    const int vw = w - 104;
    for (int row = 0; row < kSetRowCount; ++row) {
        int ry = 8 + row * (rowH + 3);
        bool selected = row == setCursor_;
        if (selected) {
            bottom.roundRect(x, ry, w, rowH, kAccent, kSelectedRow);
        } else {
            bottom.fillRect(x + 1, ry, w - 2, rowH, kPanelDark);
        }
        bottom.text(kFontSmall, x + 6, ry + 1, 94, Align::Left, kSetLabels[row], selected ? kText : kMuted);
        zones_.push_back({x, ry, 100, rowH, kZoneItem + row});
        std::string value;
        bool changed = false;
        switch (row) {
            case kSetOrder:
                value = settings_.sortById ? "By ID" : "By name (A-Z)";
                changed = settings_.sortById != setSaved_.sortById;
                break;
            case kSetLevel:
                value = "Lv. " + std::to_string(settings_.newLevel);
                changed = settings_.newLevel != setSaved_.newLevel;
                break;
            case kSetLanguage:
                value = data::languageNativeName(settings_.language);
                if (settings_.language != data::kLangEnglish) {
                    value += std::string(" (") + data::languageName(settings_.language) + ")";
                }
                changed = settings_.language != setSaved_.language;
                break;
            case kSetAnimations:
                value = settings_.animations ? "On" : "Off";
                changed = settings_.animations != setSaved_.animations;
                break;
            default:
                value = "v" + config_.version;
                break;
        }
        if (selected && row != kSetAbout) {
            bottom.arrow(vx + 2, ry + 4, 7, -1, kAccent);
            bottom.arrow(vx + vw - 6, ry + 4, 7, 1, kAccent);
        }
        bottom.text(kFontSmall, vx + 12, ry + 1, vw - 24, Align::Center, value, changed ? kYellow : kText);
        zones_.push_back({vx, ry, vw / 2, rowH, kZoneDecrease + row});
        zones_.push_back({vx + vw / 2, ry, vw - vw / 2, rowH, kZoneIncrease + row});
    }
    drawHints(bottom, {{"+", "Change"}, {"LR", "Big step"}, {"B", "Back"}}, {0, 0, kKeyB});
}

}  // namespace pktool::ui
