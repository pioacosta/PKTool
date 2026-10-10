// Ribbon editor of the PKTool UI (see app.h).
//
// It works on a copy of the Pokémon's decrypted bytes: nothing reaches the
// save until START (Apply), which writes the slot back. The bytes are then
// written to the SD card by the usual SELECT save flow.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

#include "core/data/names.h"
#include "core/pokemon/ribbons.h"
#include "ds/app/app.h"
#include "ds/app/ui_common.h"

namespace pktool::ui {
using namespace detail;
namespace rb = pokemon::ribbons;

namespace {

constexpr int kRowH = 14;
constexpr int kRowStep = 15;
constexpr int kVisible = 11;

}  // namespace

bool App::ribbonsChanged() const {
    const size_t size = rib_.party ? save_->partySize() : pokemon::entity::kStoredSize;
    return std::memcmp(rib_.raw, rib_.original, size) != 0;
}

void App::openRibbons() {
    RibbonState& r = rib_;
    r = RibbonState();
    r.party = isParty();
    r.box = box_;
    r.slot = slotCursor_;
    r.format = save_->format();
    auto state = r.party ? save_->readPartySlot(r.slot, r.raw) : save_->readBoxSlot(r.box, r.slot, r.raw);
    if (state != saves::SlotState::Valid) return;
    std::memcpy(r.original, r.raw, sizeof r.raw);
    r.view = pokemon::entity::decode(r.format, r.raw, r.party ? save_->partySize() : pokemon::entity::kStoredSize);
    screen_ = Screen::Ribbons;
}

void App::applyRibbons() {
    RibbonState& r = rib_;
    if (ribbonsChanged()) {
        // Only ribbon bits (and the checksum) differ from what the slot held.
        if (r.party) {
            save_->writePartySlot(r.slot, r.raw);
        } else {
            save_->writeBoxSlot(r.box, r.slot, r.raw);
        }
        unsaved_ = true;
    }
    screen_ = Screen::Box;
}

void App::updateRibbons(const Input& in) {
    RibbonState& r = rib_;
    const int count = rb::count(r.format);
    const int zone = in.touched ? touchedZone(in) : -1;

    auto change = [&](int index, int delta) {  // delta: -1, +1 (ranks) or 0 = toggle
        const int value = rb::get(r.format, r.raw, index);
        const int high = rb::maxValue(r.format, index);
        int next;
        if (delta == 0) {
            next = rb::isRank(r.format, index) ? (value >= high ? 0 : value + 1) : (value ? 0 : 1);
        } else {
            next = std::min(std::max(std::min(value, high) + delta, 0), high);
        }
        rb::set(r.format, r.raw, index, next);
    };

    bool toggle = in.pressed & kKeyA;
    int delta = 0;
    if (zone >= kZoneItem && zone < kZoneItem + count) {
        r.cursor = zone - kZoneItem;
        toggle = !rb::isRank(r.format, r.cursor);  // a tap flips a flag; ranks use their arrows
    } else if (zone >= kZoneDecrease && zone < kZoneDecrease + count) {
        r.cursor = zone - kZoneDecrease;
        delta = -1;
    } else if (zone >= kZoneIncrease && zone < kZoneIncrease + count) {
        r.cursor = zone - kZoneIncrease;
        delta = 1;
    }
    if (in.repeated & kKeyUp) r.cursor = wrap(r.cursor - 1, count);
    if (in.repeated & kKeyDown) r.cursor = wrap(r.cursor + 1, count);
    if (in.repeated & kKeyL) r.cursor = clampInt(r.cursor - kVisible, 0, count - 1);
    if (in.repeated & kKeyR) r.cursor = clampInt(r.cursor + kVisible, 0, count - 1);
    if (in.repeated & kKeyRight) delta = 1;
    if (in.repeated & kKeyLeft) delta = -1;

    if (toggle) {
        change(r.cursor, 0);
    } else if (delta != 0) {
        change(r.cursor, delta);
    }
    // Y: every ribbon (ranks at Master); X: none. Only the ribbon bits change.
    if (in.pressed & (kKeyY | kKeyX)) {
        const int value = (in.pressed & kKeyY) ? 1 : 0;
        for (int i = 0; i < count; ++i) rb::set(r.format, r.raw, i, value * rb::maxValue(r.format, i));
    }

    if (in.pressed & kKeyStart) applyRibbons();
    if (in.pressed & kKeyB) {
        if (ribbonsChanged()) {
            dialog_ = Dialog::ConfirmDiscard;
        } else {
            screen_ = Screen::Box;
        }
    }
}

void App::drawRibbons(Canvas& top, Canvas& bottom) {
    RibbonState& r = rib_;
    const int count = rb::count(r.format);
    const int cursor = clampInt(r.cursor, 0, std::max(0, count - 1));
    const bool changed = ribbonsChanged();
    const int held = rb::total(r.format, r.raw);
    const int heldBefore = rb::total(r.format, r.original);

    // ---- Top: who, how many, and the selected ribbon.
    drawChrome(top, std::string("Ribbons  \xE2\x80\xA2  ") + (r.party ? "Party" : "Box " + twoDigits(r.box + 1)) +
                        " slot " + std::to_string(r.slot + 1),
               changed ? "Edited" : "", kYellow);
    top.roundRect(4, kBodyTop + 3, 248, 150, kBorder, kPanel);
    drawIcon(top, 10, 38, 40, 30, r.view, false);
    std::string name = r.view.isNicknamed ? r.view.nickname : data::speciesName(r.view.species);
    top.text(kFontLarge, 54, 38, 194, Align::Left, name, kText);
    top.text(kFontSmall, 54, 56, 194, Align::Left,
             std::to_string(held) + (held == 1 ? " ribbon" : " ribbons"), held != heldBefore ? kYellow : kMuted);
    top.hline(12, 74, 232, kBorder);

    if (count > 0) {
        const int value = rb::get(r.format, r.raw, cursor);
        const int before = rb::get(r.format, r.original, cursor);
        top.text(kFontLarge, 12, 80, 232, Align::Left, rb::name(r.format, cursor), value != before ? kYellow : kText);
        std::string state;
        if (rb::isRank(r.format, cursor)) {
            state = std::string("Rank: ") + rb::rankName(value);
        } else {
            state = value ? "Obtained" : "Not obtained";
        }
        top.text(kFontSmall, 12, 99, 232, Align::Left, state, value ? kGreen : kMuted);

        // One dot per ribbon: filled when owned, ring on the selected one.
        const int perRow = count > 20 ? 20 : count;
        const int pitch = 11;
        const int x0 = 128 - perRow * pitch / 2 + pitch / 2;
        for (int i = 0; i < count; ++i) {
            const int cx = x0 + (i % perRow) * pitch;
            const int cy = 126 + (i / perRow) * pitch;
            const int v = rb::get(r.format, r.raw, i);
            if (v) {
                top.fillCircle(cx, cy, 3, v != rb::get(r.format, r.original, i) ? kYellow : kGreen);
            } else {
                top.circle(cx, cy, 3, kDim);
            }
            if (i == cursor) top.circle(cx, cy, 5, kAccent);
        }
        top.text(kFontSmall, 12, 168, 232, Align::Center, "L/R: page   Left/Right: set or rank", kDim);
    }

    // ---- Bottom: the list.
    bottom.grid(0, 0, gfx::kScreenWidth, kHintTop, kBackground, kGridLine, 12);
    bottom.roundRect(4, 3, 248, 171, kBorder, kBoxArea);
    int scroll = clampInt(r.scroll, 0, std::max(0, count - kVisible));
    if (cursor < scroll) scroll = cursor;
    if (cursor >= scroll + kVisible) scroll = cursor - kVisible + 1;
    scroll = clampInt(scroll, 0, std::max(0, count - kVisible));
    r.scroll = scroll;

    const int x = 8;
    const int w = 232;
    const int y0 = 6;
    for (int i = 0; i < kVisible; ++i) {
        const int index = scroll + i;
        if (index >= count) break;
        const int ry = y0 + i * kRowStep;
        const bool sel = index == cursor;
        const int value = rb::get(r.format, r.raw, index);
        const bool edited = value != rb::get(r.format, r.original, index);
        if (sel) {
            bottom.roundRect(x, ry, w, kRowH, kAccent, kSelectedRow);
        } else {
            bottom.fillRect(x + 1, ry, w - 2, kRowH, kPanelDark);
        }
        const Color ink = edited ? kYellow : sel ? kText : kMuted;
        if (rb::isRank(r.format, index)) {
            bottom.text(kFontSmall, x + 6, ry, 110, Align::Left, rb::name(r.format, index), ink);
            const int qx = x + w - 96;
            const int qw = 96;
            if (sel) {
                bottom.arrow(qx + 2, ry + 3, 7, -1, kAccent);
                bottom.arrow(qx + qw - 8, ry + 3, 7, 1, kAccent);
            }
            bottom.text(kFontSmall, qx + 10, ry, qw - 20, Align::Center, rb::rankName(value),
                        value ? (edited ? kYellow : kGreen) : ink);
            zones_.push_back({x, ry, w - qw, kRowH, kZoneItem + index});
            zones_.push_back({qx, ry, qw / 2, kRowH, kZoneDecrease + index});
            zones_.push_back({qx + qw / 2, ry, qw - qw / 2, kRowH, kZoneIncrease + index});
        } else {
            // Checkbox.
            const int bx = x + 6;
            const int by = ry + 2;
            bottom.roundRect(bx, by, 10, 10, value ? kGreen : kDim, value ? kGreen : kPanelDark);
            if (value) {
                static const int8_t kTick[][2] = {{2, 5}, {3, 6}, {4, 7}, {5, 6}, {6, 5}, {7, 4}, {8, 3},
                                                  {2, 4}, {3, 5}, {4, 6}, {5, 5}, {6, 4}, {7, 3}};
                for (const auto& p : kTick) bottom.pixel(bx + p[0] - 1, by + p[1] - 1, kTitleBar);
            }
            bottom.text(kFontSmall, x + 24, ry, w - 30, Align::Left, rb::name(r.format, index), ink);
            zones_.push_back({x, ry, w, kRowH, kZoneItem + index});
        }
    }
    if (count > kVisible) {
        const int trackH = kVisible * kRowStep;
        const int thumbH = std::max(8, trackH * kVisible / count);
        const int thumbY = y0 + (trackH - thumbH) * scroll / std::max(1, count - kVisible);
        bottom.fillRect(x + w + 3, y0, 2, trackH, kPanelDark);
        bottom.fillRect(x + w + 3, thumbY, 2, thumbH, kAccent);
    }
    drawHints(bottom, {{"A", "Toggle"}, {"Y", "All"}, {"X", "None"}, {"ST", "Apply"}, {"B", "Back"}},
              {kKeyA, kKeyY, kKeyX, kKeyStart, kKeyB});
}

}  // namespace pktool::ui
