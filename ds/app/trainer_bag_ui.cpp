// Trainer card and bag editors of the PKTool UI (see app.h).
//
// Both work on a copy of the save's data: nothing reaches the save until
// START (Apply), which goes through the validating setters of SaveFile. The
// bytes are then written to the SD card by the usual SELECT save flow.

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "core/data/sorted_names.h"
#include "ds/app/app.h"
#include "ds/app/ui_common.h"

namespace pktool::ui {
using namespace detail;

namespace {

// ----- Trainer card -----------------------------------------------------------

enum TrainerField {
    kTrName,
    kTrGender,
    kTrTid,
    kTrSid,
    kTrMoney,
    kTrHours,
    kTrMinutes,
    kTrSeconds,
    kTrBadges,
    kTrFieldCount,
};

const char* const kTrLabels[kTrFieldCount] = {"Name",  "Gender",  "Trainer ID", "Secret ID", "Money",
                                              "Hours", "Minutes", "Seconds",    "Badges"};

std::string idText(uint16_t id) {
    char buffer[8];
    std::snprintf(buffer, sizeof buffer, "%05u", id);
    return buffer;
}

std::string playTime(const saves::TrainerData& t) {
    return std::to_string(t.hours) + ":" + twoDigits(t.minutes) + ":" + twoDigits(t.seconds);
}

int popcount16(uint16_t v) {
    int n = 0;
    for (; v; v = static_cast<uint16_t>(v & (v - 1))) ++n;
    return n;
}

const Color kBadgeColors[8] = {rgb(190, 190, 200), rgb(80, 150, 255), rgb(255, 170, 60),  rgb(110, 210, 110),
                               rgb(255, 120, 150), rgb(190, 130, 255), rgb(255, 90, 80), rgb(255, 215, 80)};

}  // namespace

bool App::trainerChanged() const {
    const saves::TrainerData& a = trOriginal_;
    const saves::TrainerData& b = trCurrent_;
    return a.name != b.name || a.gender != b.gender || a.tid != b.tid || a.sid != b.sid || a.money != b.money ||
           a.hours != b.hours || a.minutes != b.minutes || a.seconds != b.seconds || a.badges != b.badges;
}

void App::openTrainer() {
    trOriginal_ = save_->trainerData();
    trCurrent_ = trOriginal_;
    trField_ = 0;
    trBadge_ = 0;
    trStatus_.clear();
    screen_ = Screen::Trainer;
}

void App::applyTrainer() {
    if (!trainerChanged()) {
        screen_ = Screen::SaveMenu;
        return;
    }
    std::string error;
    if (!save_->setTrainerData(trCurrent_, &error)) {
        showMessage("Cannot apply", error.empty() ? "The trainer data is not valid." : error);
        return;
    }
    unsaved_ = true;
    screen_ = Screen::SaveMenu;
}

void App::updateTrainer(const Input& in) {
    saves::TrainerData& t = trCurrent_;
    int zone = in.touched ? touchedZone(in) : -1;
    int delta = 0;      // -1 / +1, small step
    int bigDelta = 0;   // shoulder buttons, large step
    bool activate = in.pressed & kKeyA;
    const int badges = save_->badgeCount();
    bool badgeTapped = false;

    if (zone >= kZoneBadge && zone < kZoneBadge + badges) {
        trField_ = kTrBadges;
        trBadge_ = zone - kZoneBadge;
        badgeTapped = true;
    } else if (zone >= kZoneItem && zone < kZoneItem + kTrFieldCount) {
        trField_ = zone - kZoneItem;
    } else if (zone >= kZoneDecrease && zone < kZoneDecrease + kTrFieldCount) {
        trField_ = zone - kZoneDecrease;
        delta = -1;
        activate = trField_ == kTrName;  // tapping the name opens the keyboard
    } else if (zone >= kZoneIncrease && zone < kZoneIncrease + kTrFieldCount) {
        trField_ = zone - kZoneIncrease;
        delta = 1;
        activate = trField_ == kTrName;
    }
    // Game Boy saves have no secret ID, and only Crystal has a trainer gender.
    auto hidden = [&](int field) {
        return (field == kTrSid && !save_->hasSecretId()) || (field == kTrGender && !save_->hasTrainerGender());
    };
    if (in.repeated & kKeyUp) {
        do trField_ = wrap(trField_ - 1, kTrFieldCount); while (hidden(trField_));
    }
    if (in.repeated & kKeyDown) {
        do trField_ = wrap(trField_ + 1, kTrFieldCount); while (hidden(trField_));
    }
    if (in.repeated & kKeyRight) delta = 1;
    if (in.repeated & kKeyLeft) delta = -1;
    if (in.repeated & kKeyR) bigDelta = 1;
    if (in.repeated & kKeyL) bigDelta = -1;

    if (delta != 0 || bigDelta != 0 || activate || badgeTapped || (in.pressed & (kKeyX | kKeyY))) trStatus_.clear();

    auto step = [&](int value, int unit, int big, int low, int high) {
        return clampInt(value + delta * unit + bigDelta * big, low, high);
    };
    switch (trField_) {
        case kTrName:
            if (activate) {
                openKeyboard(KeyboardTarget::TrainerName, t.name, save_->trainerNameMaxLength());
                return;
            }
            break;
        case kTrGender:
            if (delta != 0 || bigDelta != 0 || activate) t.gender = t.gender == 0 ? 1 : 0;
            break;
        case kTrTid:
        case kTrSid: {
            uint16_t& id = trField_ == kTrTid ? t.tid : t.sid;
            if (delta != 0 || bigDelta != 0) id = static_cast<uint16_t>(step(id, 1, 1000, 0, 65535));
            if (activate) {
                showMessage("Trainer ID", "Changing the Trainer ID or Secret ID\nmakes every Pok\xC3\xA9mon already in the\n"
                                          "save count as traded: their OT no\nlonger matches.");
            }
            break;
        }
        case kTrMoney:
            if (delta != 0 || bigDelta != 0) {
                const int64_t high = save_->maxMoney();
                int64_t next = static_cast<int64_t>(t.money) + delta * 100 + bigDelta * 10000;
                t.money = static_cast<uint32_t>(std::min<int64_t>(std::max<int64_t>(next, 0), high));
            }
            break;
        case kTrHours:
            if (delta != 0 || bigDelta != 0) t.hours = static_cast<uint16_t>(step(t.hours, 1, 10, 0, save_->maxPlayHours()));
            break;
        case kTrMinutes:
            if (delta != 0 || bigDelta != 0) t.minutes = static_cast<uint8_t>(step(t.minutes, 1, 10, 0, 59));
            break;
        case kTrSeconds:
            if (delta != 0 || bigDelta != 0) t.seconds = static_cast<uint8_t>(step(t.seconds, 1, 10, 0, 59));
            break;
        case kTrBadges:
            if (!badgeTapped) {
                if (delta != 0) trBadge_ = wrap(trBadge_ + delta, badges);
            }
            if (badgeTapped || activate) t.badges = static_cast<uint16_t>(t.badges ^ (1u << trBadge_));
            break;
    }
    // Y: maximum (money, all badges); X: minimum (money 0, no badges).
    if (in.pressed & kKeyY) {
        if (trField_ == kTrMoney) t.money = save_->maxMoney();
        if (trField_ == kTrBadges) t.badges = static_cast<uint16_t>((1u << badges) - 1);
    }
    if (in.pressed & kKeyX) {
        if (trField_ == kTrMoney) t.money = 0;
        if (trField_ == kTrBadges) t.badges = 0;
    }

    if (in.pressed & kKeyStart) applyTrainer();
    if (in.pressed & kKeyB) {
        if (trainerChanged()) {
            dialog_ = Dialog::ConfirmDiscard;
        } else {
            screen_ = Screen::SaveMenu;
        }
    }
}

void App::drawTrainer(Canvas& top, Canvas& bottom) {
    const saves::TrainerData& t = trCurrent_;
    const saves::TrainerData& o = trOriginal_;
    const int badges = save_->badgeCount();

    drawChrome(top, "Trainer card  \xE2\x80\xA2  " + t.name, trainerChanged() ? "Edited" : "", kYellow);
    top.roundRect(4, kBodyTop + 3, 248, 150, kBorder, kPanel);
    top.text(kFontSmall, 12, 40, "TRAINER CARD", kMuted);
    int nameWidth = top.text(kFontLarge, 12, 53, 190, Align::Left, t.name, t.name != o.name ? kYellow : kText);
    if (t.gender <= 1 && save_->hasTrainerGender()) {
        drawGenderGlyph(top, 12 + std::min(nameWidth, 190) + 6, 57, t.gender == 0 ? pokemon::Gender::Male : pokemon::Gender::Female);
    }
    top.hline(12, 74, 232, kBorder);

    struct Row {
        const char* label;
        std::string value;
        bool changed;
        bool bad;
    };
    const Row rows[] = {
        {"ID No.", idText(t.tid), t.tid != o.tid, false},
        {"Secret ID", idText(t.sid), t.sid != o.sid, false},
        {"Money", "\xC2\xA5" + groupDigits(t.money), t.money != o.money, t.money > save_->maxMoney()},
        {"Play time", playTime(t), t.hours != o.hours || t.minutes != o.minutes || t.seconds != o.seconds,
         t.hours > save_->maxPlayHours() || t.minutes > 59 || t.seconds > 59},
    };
    int y = 80;
    for (const Row& r : rows) {
        if (!save_->hasSecretId() && std::string(r.label) == "Secret ID") continue;
        top.text(kFontSmall, 12, y, 90, Align::Left, r.label, kMuted);
        top.text(kFontSmall, 90, y, 150, Align::Left, r.value, r.bad ? kRed : r.changed ? kYellow : kText);
        y += 14;
    }

    top.hline(12, 138, 232, kBorder);
    top.text(kFontSmall, 12, 142, "Badges", kMuted);
    top.text(kFontSmall, 90, 142, std::to_string(popcount16(t.badges)) + " / " + std::to_string(badges),
             t.badges != o.badges ? kYellow : kText);
    const int perRow = 8;
    for (int i = 0; i < badges; ++i) {
        int cx = 28 + (i % perRow) * 28;
        int cy = 164 + (i / perRow) * 14 - (badges > perRow ? 6 : 0);
        Color color = kBadgeColors[i % 8];
        if (t.badges & (1u << i)) {
            top.fillCircle(cx, cy, 5, color);
            top.circle(cx, cy, 5, kText);
        } else {
            top.circle(cx, cy, 5, kDim);
        }
    }

    // Bottom: field list.
    bottom.grid(0, 0, gfx::kScreenWidth, kHintTop, kBackground, kGridLine, 12);
    bottom.roundRect(4, 3, 248, 170, kBorder, kPanel);
    const int rowH = 14;
    const int x = 8;
    const int w = 236;
    const int vx = x + 92;
    const int vw = w - 96;
    int shownRows = 0;
    for (int field = 0; field < kTrFieldCount; ++field) {
        if ((field == kTrSid && !save_->hasSecretId()) || (field == kTrGender && !save_->hasTrainerGender())) continue;
        int ry = 7 + shownRows++ * (rowH + 1);
        bool selected = field == trField_;
        if (selected) {
            bottom.roundRect(x, ry, w, rowH, kAccent, kSelectedRow);
        } else {
            bottom.fillRect(x + 1, ry, w - 2, rowH, kPanelDark);
        }
        bottom.text(kFontSmall, x + 6, ry, 80, Align::Left, kTrLabels[field], selected ? kText : kMuted);
        zones_.push_back({x, ry, 92, rowH, kZoneItem + field});
        if (field == kTrBadges) {
            int cell = (vw - 8) / badges;
            for (int i = 0; i < badges; ++i) {
                int cx = vx + 4 + i * cell + cell / 2;
                int cy = ry + rowH / 2;
                int r = std::min(cell / 2 - 1, 4);
                if (t.badges & (1u << i)) {
                    bottom.fillCircle(cx, cy, r, kBadgeColors[i % 8]);
                } else {
                    bottom.circle(cx, cy, r, kDim);
                }
                if (selected && i == trBadge_) bottom.rect(cx - cell / 2, ry + 1, cell, rowH - 2, kAccent);
                zones_.insert(zones_.begin(), {cx - cell / 2, ry, cell, rowH, kZoneBadge + i});
            }
            continue;
        }
        std::string value;
        bool changed = false;
        bool bad = false;
        switch (field) {
            case kTrName:
                value = t.name;
                changed = t.name != o.name;
                break;
            case kTrGender:
                value = t.gender == 0 ? "Male" : t.gender == 1 ? "Female" : "?";
                changed = t.gender != o.gender;
                break;
            case kTrTid:
                value = std::to_string(t.tid);
                changed = t.tid != o.tid;
                break;
            case kTrSid:
                value = std::to_string(t.sid);
                changed = t.sid != o.sid;
                break;
            case kTrMoney:
                value = groupDigits(t.money);
                changed = t.money != o.money;
                bad = t.money > save_->maxMoney();
                break;
            case kTrHours:
                value = std::to_string(t.hours);
                changed = t.hours != o.hours;
                bad = t.hours > save_->maxPlayHours();
                break;
            case kTrMinutes:
                value = std::to_string(t.minutes);
                changed = t.minutes != o.minutes;
                bad = t.minutes > 59;
                break;
            case kTrSeconds:
                value = std::to_string(t.seconds);
                changed = t.seconds != o.seconds;
                bad = t.seconds > 59;
                break;
        }
        if (selected && field != kTrName) {
            bottom.arrow(vx + 2, ry + 3, 7, -1, kAccent);
            bottom.arrow(vx + vw - 6, ry + 3, 7, 1, kAccent);
        }
        bottom.text(kFontSmall, vx + 12, ry, vw - 24, Align::Center, value, bad ? kRed : changed ? kYellow : kText);
        zones_.push_back({vx, ry, vw / 2, rowH, kZoneDecrease + field});
        zones_.push_back({vx + vw / 2, ry, vw - vw / 2, rowH, kZoneIncrease + field});
    }

    // Help for the selected field (or the last message).
    std::string line1;
    std::string line2;
    switch (trField_) {
        case kTrName:
            line1 = "A: type a name (up to " + std::to_string(save_->trainerNameMaxLength()) + " characters)";
            break;
        case kTrGender:
            line1 = "Left/Right or A: switch";
            break;
        case kTrTid:
        case kTrSid:
            line1 = "Changing an ID makes your Pok\xC3\xA9mon";
            line2 = "count as traded (their OT differs).";
            break;
        case kTrMoney:
            line1 = "0 to " + groupDigits(save_->maxMoney()) + ".  Y: maximum  X: zero";
            break;
        case kTrHours:
            line1 = "Play time, 0 to " + std::to_string(save_->maxPlayHours()) + " hours";
            break;
        case kTrMinutes:
        case kTrSeconds:
            line1 = "0 to 59";
            break;
        case kTrBadges:
            line1 = "Left/Right: pick  A or tap: toggle";
            line2 = "Y: all badges  X: none";
            break;
    }
    bool warn = trField_ == kTrTid || trField_ == kTrSid;
    bottom.text(kFontSmall, 12, 147, 232, Align::Left, line1, warn ? kYellow : kMuted);
    bottom.text(kFontSmall, 12, 159, 232, Align::Left, line2, warn ? kYellow : kMuted);

    drawHints(bottom, {{"+", "Change"}, {"LR", "Big step"}, {"ST", "Apply"}, {"B", "Back"}}, {0, 0, kKeyStart, kKeyB});
}

// ----- Bag --------------------------------------------------------------------

namespace {

constexpr int kBagRowH = 14;
constexpr int kBagVisible = 10;

}  // namespace

bool App::bagPocketChanged(int pocket) const {
    const auto& a = bagOriginal_[static_cast<size_t>(pocket)];
    const auto& b = bagCurrent_[static_cast<size_t>(pocket)];
    if (a.size() != b.size()) return true;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].item != b[i].item || a[i].count != b[i].count) return true;
    }
    return false;
}

bool App::bagChanged() const {
    for (size_t p = 0; p < bagCurrent_.size(); ++p) {
        if (bagPocketChanged(static_cast<int>(p))) return true;
    }
    return false;
}

void App::openBag() {
    if (save_->pocketCount() == 0) {
        showMessage("Bag", "This save has no bag\nthat can be edited.");
        return;
    }
    const size_t n = static_cast<size_t>(save_->pocketCount());
    bagOriginal_.assign(n, {});
    for (size_t p = 0; p < n; ++p) bagOriginal_[p] = save_->readPocket(static_cast<int>(p));
    bagCurrent_ = bagOriginal_;
    bagCursors_.assign(n, 0);
    bagScrolls_.assign(n, 0);
    bagPocket_ = 0;
    bagStatus_.clear();
    screen_ = Screen::Bag;
}

// Writes every changed pocket. A pocket that fails validation stays as edited
// (with the reason shown); pockets applied before it are kept.
void App::applyBag() {
    bool applied = false;
    for (int p = 0; p < static_cast<int>(bagCurrent_.size()); ++p) {
        if (!bagPocketChanged(p)) continue;
        std::string error;
        if (!save_->setPocket(p, bagCurrent_[static_cast<size_t>(p)], &error)) {
            bagPocket_ = p;
            std::string text = std::string(save_->pocketInfo(p).name) + ": " + (error.empty() ? "invalid data" : error);
            if (applied) text += "\n\nThe pockets before it were applied.";
            showMessage("Cannot apply", text);
            if (applied) unsaved_ = true;
            return;
        }
        bagOriginal_[static_cast<size_t>(p)] = save_->readPocket(p);
        bagCurrent_[static_cast<size_t>(p)] = bagOriginal_[static_cast<size_t>(p)];
        applied = true;
    }
    if (applied) unsaved_ = true;
    screen_ = Screen::SaveMenu;
}

void App::updateBag(const Input& in) {
    const int pockets = static_cast<int>(bagCurrent_.size());
    if (pockets == 0) {
        if (in.pressed & kKeyB) screen_ = Screen::SaveMenu;
        return;
    }
    int zone = in.touched ? touchedZone(in) : -1;
    int pocketDelta = 0;
    if ((in.repeated & kKeyR) || zone == kZoneNextBox) pocketDelta = 1;
    if ((in.repeated & kKeyL) || zone == kZonePrevBox) pocketDelta = -1;
    if (pocketDelta != 0) {
        bagPocket_ = wrap(bagPocket_ + pocketDelta, pockets);
        bagStatus_.clear();
        return;
    }

    const size_t pk = static_cast<size_t>(bagPocket_);
    std::vector<saves::BagSlot>& slots = bagCurrent_[pk];
    const int count = static_cast<int>(slots.size());
    const bool canAdd = count < save_->pocketInfo(bagPocket_).capacity;
    const int rows = count + (canAdd ? 1 : 0);
    int& cursor = bagCursors_[pk];
    cursor = clampInt(cursor, 0, std::max(0, rows - 1));

    int delta = 0;
    bool activate = in.pressed & kKeyA;
    if (zone >= kZoneItem && zone < kZoneItem + rows) {
        int index = zone - kZoneItem;
        activate = index == cursor && index == count;  // a second tap on "Add item"
        cursor = index;
    } else if (zone >= kZoneDecrease && zone < kZoneDecrease + rows) {
        cursor = zone - kZoneDecrease;
        delta = -1;
    } else if (zone >= kZoneIncrease && zone < kZoneIncrease + rows) {
        cursor = zone - kZoneIncrease;
        delta = 1;
    }
    if (rows > 0) {
        if (in.repeated & kKeyUp) cursor = wrap(cursor - 1, rows);
        if (in.repeated & kKeyDown) cursor = wrap(cursor + 1, rows);
    }
    if (in.repeated & kKeyRight) delta = 1;
    if (in.repeated & kKeyLeft) delta = -1;

    const bool onItem = cursor < count;
    if (delta != 0 || activate || (in.pressed & (kKeyX | kKeyY))) bagStatus_.clear();
    if (onItem) {
        saves::BagSlot& slot = slots[static_cast<size_t>(cursor)];
        const int maxCount = std::max(1, save_->itemMaxCount(bagPocket_, slot.item));
        int change = delta;
        if (activate) change = 10;  // A on an item: +10
        if (change != 0) {
            if (slot.count > maxCount) {
                // Stacks above the game's limit come down to it first.
                slot.count = static_cast<uint16_t>(maxCount);
                bagStatus_ = "Reduced to the limit (" + std::to_string(maxCount) + ")";
            } else {
                slot.count = static_cast<uint16_t>(clampInt(slot.count + change, 1, maxCount));
            }
        }
        if (in.pressed & kKeyY) slot.count = static_cast<uint16_t>(maxCount);
        if (in.pressed & kKeyX) {
            slots.erase(slots.begin() + cursor);
            cursor = std::max(0, std::min(cursor, static_cast<int>(slots.size())));
        }
    } else if (activate && canAdd) {
        openItemPicker();
        return;
    }

    if (in.pressed & kKeyStart) applyBag();
    if (in.pressed & kKeyB) {
        if (bagChanged()) {
            dialog_ = Dialog::ConfirmDiscard;
        } else {
            screen_ = Screen::SaveMenu;
        }
    }
}

void App::drawBag(Canvas& top, Canvas& bottom) {
    const int pockets = static_cast<int>(bagCurrent_.size());
    if (pockets == 0) {
        drawChrome(top, "Bag", "", kYellow);
        bottom.grid(0, 0, gfx::kScreenWidth, kHintTop, kBackground, kGridLine, 12);
        bottom.text(kFontSmall, 4, 80, 248, Align::Center, "This save has no bag", kMuted);
        drawHints(bottom, {{"B", "Back"}}, {kKeyB});
        return;
    }
    const size_t pk = static_cast<size_t>(bagPocket_);
    const std::vector<saves::BagSlot>& slots = bagCurrent_[pk];
    const saves::BagPocket info = save_->pocketInfo(bagPocket_);
    const int count = static_cast<int>(slots.size());
    const bool canAdd = count < info.capacity;
    const int rows = count + (canAdd ? 1 : 0);
    const int cursor = clampInt(bagCursors_[pk], 0, std::max(0, rows - 1));
    const bool onItem = cursor < count;
    const saves::BagSlot* selected = onItem ? &slots[static_cast<size_t>(cursor)] : nullptr;

    // ---- Top: the selected item and an overview of the pockets.
    // Messages (e.g. "Reduced to the limit") replace the status text, as in the Pokemon editor.
    drawChrome(top, bagStatus_.empty() ? std::string("Bag  \xE2\x80\xA2  ") + info.name : bagStatus_,
               bagChanged() ? "Edited" : "", kYellow, bagStatus_.empty() ? 0 : kYellow);
    top.roundRect(4, kBodyTop + 3, 248, 150, kBorder, kPanel);
    if (selected != nullptr) {
        const int maxCount = std::max(1, save_->itemMaxCount(bagPocket_, selected->item));
        top.text(kFontLarge, 12, 42, 232, Align::Left, save_->itemName(selected->item), kText);
        const bool over = selected->count > maxCount;
        top.text(kFontSmall, 12, 62, 232, Align::Left,
                 "Quantity " + std::to_string(selected->count) + "  (limit " + std::to_string(maxCount) + ")",
                 over ? kRed : kMuted);
        if (over) {
            top.text(kFontSmall, 12, 75, 232, Align::Left, "Above the game's limit: press Left/Right to lower it", kRed);
        } else {
            top.text(kFontSmall, 12, 75, 232, Align::Left, "Item " + std::to_string(cursor + 1) + " of " + std::to_string(count), kDim);
        }
    } else {
        top.text(kFontLarge, 12, 42, 232, Align::Left, canAdd ? "Add an item" : "Pocket full", kText);
        top.text(kFontSmall, 12, 62, 232, Align::Left,
                 canAdd ? "Press A to choose an item for this pocket" : "Delete an item to make room", kMuted);
    }
    top.hline(12, 92, 232, kBorder);
    for (int p = 0; p < pockets; ++p) {
        const saves::BagPocket pi = save_->pocketInfo(p);
        const int n = static_cast<int>(bagCurrent_[static_cast<size_t>(p)].size());
        int col = p % 2;
        int row = p / 2;
        int x = 12 + col * 118;
        int y = 97 + row * 13;
        bool current = p == bagPocket_;
        if (current) top.roundRect(x - 2, y - 1, 116, 13, kAccent, kSelectedRow);
        Color color = n > pi.capacity ? kRed : bagPocketChanged(p) ? kYellow : current ? kText : kMuted;
        top.text(kFontSmall, x + 2, y - 1, 70, Align::Left, pi.name, color);
        top.text(kFontSmall, x + 2, y - 1, 108, Align::Right, std::to_string(n) + "/" + std::to_string(pi.capacity), color);
    }
    // Usage bar of the current pocket.
    {
        const int barY = 158;
        top.text(kFontSmall, 12, barY, "Slots used", kMuted);
        top.text(kFontSmall, 12, barY, 232, Align::Right, std::to_string(count) + " / " + std::to_string(info.capacity),
                 count > info.capacity ? kRed : kText);
        top.roundRect(12, barY + 14, 232, 8, kBorder, kPanelDark);
        int fill = info.capacity > 0 ? std::min(228, 228 * count / info.capacity) : 0;
        if (fill > 0) top.fillRect(14, barY + 16, fill, 4, count > info.capacity ? kRed : kAccent);
    }

    // ---- Bottom: pocket header and slot list.
    bottom.grid(0, 0, gfx::kScreenWidth, kHintTop, kBackground, kGridLine, 12);
    bottom.roundRect(4, 3, 248, 17, kBorder, kPanelDark);
    bottom.fillRect(6, 5, 10, 13, kAccent);
    bottom.fillRect(240, 5, 10, 13, kAccent);
    bottom.arrow(9, 8, 7, -1, kTitleBar);
    bottom.arrow(243, 8, 7, 1, kTitleBar);
    bottom.text(kFontSmall, 18, 4, 220, Align::Center,
                std::string(info.name) + "  \xE2\x80\xA2  " + std::to_string(count) + "/" + std::to_string(info.capacity),
                count > info.capacity ? kRed : kText);
    zones_.push_back({4, 2, 30, 20, kZonePrevBox});
    zones_.push_back({222, 2, 30, 20, kZoneNextBox});

    bottom.roundRect(4, 23, 248, 151, kBorder, kBoxArea);
    int& scroll = bagScrolls_[pk];
    if (cursor < scroll) scroll = cursor;
    if (cursor >= scroll + kBagVisible) scroll = cursor - kBagVisible + 1;
    scroll = clampInt(scroll, 0, std::max(0, rows - kBagVisible));
    const int x = 8;
    const int w = 236;
    const int y0 = 26;
    for (int i = 0; i < kBagVisible; ++i) {
        const int index = scroll + i;
        if (index >= rows) break;
        const int ry = y0 + i * (kBagRowH + 1);
        const bool sel = index == cursor;
        if (sel) {
            bottom.roundRect(x, ry, w, kBagRowH, kAccent, kSelectedRow);
        } else {
            bottom.fillRect(x + 1, ry, w - 2, kBagRowH, kPanelDark);
        }
        zones_.push_back({x, ry, w - 76, kBagRowH, kZoneItem + index});
        if (index == count) {
            bottom.text(kFontSmall, x + 6, ry, w - 12, Align::Left, "+ Add item", sel ? kGreen : rgb(60, 150, 110));
            zones_.back().w = w;
            continue;
        }
        const saves::BagSlot& slot = slots[static_cast<size_t>(index)];
        const int maxCount = std::max(1, save_->itemMaxCount(bagPocket_, slot.item));
        const bool bad = slot.count > maxCount;
        bottom.text(kFontSmall, x + 6, ry, w - 84, Align::Left, save_->itemName(slot.item),
                    bad ? kRed : sel ? kText : kMuted);
        const int qx = x + w - 72;
        const int qw = 72;
        if (sel) {
            bottom.arrow(qx + 2, ry + 3, 7, -1, kAccent);
            bottom.arrow(qx + qw - 8, ry + 3, 7, 1, kAccent);
        }
        bool changed = true;  // yellow unless the pocket had this very stack when the editor opened
        for (const saves::BagSlot& before : bagOriginal_[pk]) {
            if (before.item == slot.item && before.count == slot.count) changed = false;
        }
        bottom.text(kFontSmall, qx + 10, ry, qw - 20, Align::Center, "x" + std::to_string(slot.count),
                    bad ? kRed : changed ? kYellow : kText);
        zones_.push_back({qx, ry, qw / 2, kBagRowH, kZoneDecrease + index});
        zones_.push_back({qx + qw / 2, ry, qw - qw / 2, kBagRowH, kZoneIncrease + index});
    }
    if (rows > kBagVisible) {
        int trackH = kBagVisible * (kBagRowH + 1);
        int thumbH = std::max(8, trackH * kBagVisible / rows);
        int thumbY = y0 + (trackH - thumbH) * scroll / std::max(1, rows - kBagVisible);
        bottom.fillRect(x + w + 2, y0, 2, trackH, kPanelDark);
        bottom.fillRect(x + w + 2, thumbY, 2, thumbH, kAccent);
    }
    drawHints(bottom, {{"+", "Qty"}, {"LR", "Pocket"}, {"X", "Delete"}, {"ST", "Apply"}}, {0, 0, kKeyX, kKeyStart});
}

// ----- Item picker -------------------------------------------------------------

void App::openItemPicker() {
    itemChoices_.clear();
    itemRows_.clear();
    const std::vector<saves::BagSlot>& slots = bagCurrent_[static_cast<size_t>(bagPocket_)];
    for (uint16_t item : save_->pocketItems(bagPocket_)) {
        bool present = false;
        for (const saves::BagSlot& s : slots) present = present || s.item == item;
        if (present) continue;
        itemChoices_.push_back(item);
    }
    // Alphabetical by name, so an item is easy to find.
    data::sortIdsByName(itemChoices_, [this](uint16_t item) { return save_->itemName(item); });
    for (uint16_t item : itemChoices_) itemRows_.push_back(save_->itemName(item));
    if (itemChoices_.empty()) {
        showMessage("No items left", "Every item this pocket can hold\nis already in it.");
        return;
    }
    itemCursor_ = 0;
    itemScroll_ = 0;
    screen_ = Screen::ItemPicker;
}

void App::addBagItem(uint16_t item) {
    std::vector<saves::BagSlot>& slots = bagCurrent_[static_cast<size_t>(bagPocket_)];
    slots.push_back({item, 1});
    bagCursors_[static_cast<size_t>(bagPocket_)] = static_cast<int>(slots.size()) - 1;
    bagStatus_.clear();
    screen_ = Screen::Bag;
}

void App::updateItemPicker(const Input& in) {
    const int count = static_cast<int>(itemRows_.size());
    int zone = in.touched ? touchedZone(in) : -1;
    bool activate = in.pressed & kKeyA;
    if (zone >= kZoneItem && zone < kZoneItem + count) {
        int index = zone - kZoneItem;
        activate = index == itemCursor_;
        itemCursor_ = index;
    }
    if (count > 0) {
        if (in.repeated & kKeyUp) itemCursor_ = wrap(itemCursor_ - 1, count);
        if (in.repeated & kKeyDown) itemCursor_ = wrap(itemCursor_ + 1, count);
        if (in.repeated & (kKeyL | kKeyLeft)) itemCursor_ = clampInt(itemCursor_ - 10, 0, count - 1);
        if (in.repeated & (kKeyR | kKeyRight)) itemCursor_ = clampInt(itemCursor_ + 10, 0, count - 1);
    }
    if (in.pressed & kKeyB) {
        screen_ = Screen::Bag;
        return;
    }
    if (activate && count > 0) addBagItem(itemChoices_[static_cast<size_t>(itemCursor_)]);
}

void App::drawItemPicker(Canvas& top, Canvas& bottom) {
    const saves::BagPocket info = save_->pocketInfo(bagPocket_);
    drawChrome(top, std::string("Add item  \xE2\x80\xA2  ") + info.name, std::to_string(itemRows_.size()), kAccent);
    top.roundRect(4, kBodyTop + 3, 248, 150, kBorder, kPanel);
    if (!itemChoices_.empty()) {
        const uint16_t item = itemChoices_[static_cast<size_t>(clampInt(itemCursor_, 0, static_cast<int>(itemChoices_.size()) - 1))];
        top.text(kFontLarge, 12, 42, 232, Align::Left, save_->itemName(item), kText);
        top.text(kFontSmall, 12, 62, 232, Align::Left,
                 "Up to " + std::to_string(save_->itemMaxCount(bagPocket_, item)) + " per slot", kMuted);
        top.text(kFontSmall, 12, 75, 232, Align::Left, "It is added with quantity 1.", kDim);
    }
    top.hline(12, 92, 232, kBorder);
    const int used = static_cast<int>(bagCurrent_[static_cast<size_t>(bagPocket_)].size());
    top.text(kFontSmall, 12, 98, 232, Align::Left, std::string(info.name) + ": " + std::to_string(used) + " of " +
                                                       std::to_string(info.capacity) + " slots used",
             kMuted);

    bottom.grid(0, 0, gfx::kScreenWidth, kHintTop, kBackground, kGridLine, 12);
    bottom.roundRect(4, 4, 248, 168, kBorder, kPanel);
    bottom.text(kFontSmall, 10, 5, "ADD AN ITEM", kMuted);
    drawList(bottom, itemRows_, itemCursor_, itemScroll_, 8, 19, 236, 15, 10);
    drawHints(bottom, {{"A", "Add"}, {"LR", "Page"}, {"B", "Back"}}, {kKeyA, 0, kKeyB});
}

}  // namespace pktool::ui
