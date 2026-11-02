// Mystery Gift album of the PKTool UI (see app.h).
//
// The album holds the Wonder Cards the game's delivery man hands out (Gen IV:
// 3 cards, by the Poké Mart counter; Gen V: 12, in the Pokémon Centers). Adding
// a card puts it there as if it had been downloaded: after SELECT writes the
// save, the game gives its Pokémon at the delivery man, like the real event.

#include <algorithm>
#include <cstdint>
#include <string>

#include "core/data/names.h"
#include "core/pokemon/card_file.h"
#include "core/pokemon/gift4.h"
#include "core/pokemon/gift5.h"
#include "ds/app/app.h"
#include "ds/app/ui_common.h"

namespace pktool::ui {
using namespace detail;

namespace {

constexpr int kRowH = 15;
constexpr int kVisible = 10;

}  // namespace

void App::refreshGifts() {
    const int count = save_->giftSlotCount();
    gifts_.assign(static_cast<size_t>(count), GiftView());
    giftRows_.clear();
    giftEmpty_.clear();
    const pokemon::create4::Trainer trainer = saveTrainer();
    for (int i = 0; i < count; ++i) {
        GiftView& g = gifts_[static_cast<size_t>(i)];
        std::vector<uint8_t> card(save_->giftCardSize());
        if (save_->readGift(i, card.data())) {
            g.card = std::move(card);
            g.pickedUp = !save_->giftWaiting(i);
            auto parsed = pokemon::cardfile::parse(g.card.data(), g.card.size(),
                                                   save_->format() >= 5 ? ".pgf" : ".pcd", "");
            g.pokemon = parsed->status == pokemon::cardfile::Status::Ok;
            g.title = pokemon::cardfile::cardTitle(g.card.data(), g.card.size(),
                                                   g.pokemon ? data::speciesName(parsed->species) : "Wonder Card");
            if (g.pokemon) {
                uint8_t raw[pokemon::entity::kStoredSize];
                g.preview = parsed->generation == 5
                                ? pokemon::gift5::receive(parsed->event5, trainer, 1, raw)
                                : pokemon::gift4::receive(parsed->event4, trainer.today, 1, raw);
            }
        }
        giftRows_.push_back(twoDigits(i + 1) + "  " + (g.card.empty() ? "(empty)" : g.title));
        giftEmpty_.push_back(g.card.empty() ? "empty" : "");
    }
    giftCursor_ = clampInt(giftCursor_, 0, std::max(0, count - 1));
}

void App::openGifts() {
    giftCursor_ = 0;
    giftScroll_ = 0;
    giftPicking_ = false;
    refreshGifts();
    screen_ = Screen::Gifts;
}

void App::updateGifts(const Input& in) {
    const int count = static_cast<int>(gifts_.size());
    const int zone = in.touched ? touchedZone(in) : -1;
    bool activate = in.pressed & kKeyA;
    if (zone >= kZoneItem && zone < kZoneItem + count) {
        activate = zone - kZoneItem == giftCursor_;
        giftCursor_ = zone - kZoneItem;
    }
    if (in.repeated & kKeyUp) giftCursor_ = wrap(giftCursor_ - 1, count);
    if (in.repeated & kKeyDown) giftCursor_ = wrap(giftCursor_ + 1, count);
    if (in.pressed & kKeyB) {
        screen_ = Screen::SaveMenu;
        return;
    }
    if (in.pressed & kKeySelect) {  // write the save, as in the save menu
        if (unsaved_) {
            dialog_ = Dialog::ConfirmWrite;
        } else {
            showMessage("Nothing to save", "There are no changes to write.");
        }
        return;
    }
    if ((in.pressed & kKeyX) && count > 0 && !gifts_[static_cast<size_t>(giftCursor_)].card.empty()) {
        save_->removeGift(giftCursor_);
        unsaved_ = true;
        refreshGifts();
        return;
    }
    if (activate) {
        // The album fills from the top: any row adds a card to the first free slot.
        if (std::none_of(gifts_.begin(), gifts_.end(), [](const GiftView& g) { return g.card.empty(); })) {
            showMessage("Album full", "The Mystery Gift album holds " + std::to_string(count) +
                                          " cards.\nRemove one with X first.");
            return;
        }
        giftPicking_ = true;  // before the list is built: it leaves out what cannot go in the album
        openPicker(PickerKind::Event);
    }
}

void App::drawGifts(Canvas& top, Canvas& bottom) {
    const int count = static_cast<int>(gifts_.size());
    const int used = static_cast<int>(
        std::count_if(gifts_.begin(), gifts_.end(), [](const GiftView& g) { return !g.card.empty(); }));
    const GiftView* g = count > 0 ? &gifts_[static_cast<size_t>(giftCursor_)] : nullptr;
    // Status bar: whether the game still has to hand the selected card out.
    std::string status = "Mystery Gift  \xE2\x80\xA2  " + std::to_string(used) + "/" + std::to_string(count);
    if (g != nullptr && !g->card.empty()) status += g->pickedUp ? "  \xE2\x80\xA2  Picked up" : "  \xE2\x80\xA2  Waiting";
    drawChrome(top, status, unsaved_ ? "Unsaved" : "", kYellow);

    if (g != nullptr && g->pokemon) {
        drawSummary(top, &g->preview, false, true);
    } else {
        top.roundRect(4, kBodyTop + 3, 248, 150, kBorder, kPanel);
        if (g == nullptr || g->card.empty()) {
            top.text(kFontLarge, 12, 44, 232, Align::Left, "Mystery Gift album", kText);
            static const char* const kHelp[] = {
                "Wonder Cards waiting in the save.",
                "Add one with A and write the save",
                "with SELECT. Then, in the game, talk",
                "to the delivery man (in green, by the",
                "Pok\xC3\xA9 Mart counter):",
                "he gives the event Pok\xC3\xA9mon.",
            };
            for (int i = 0; i < 6; ++i) {
                std::string line = kHelp[i];
                if (i == 4) line = save_->format() >= 5 ? "Pok\xC3\xA9 Mart of any Pok\xC3\xA9mon Center):"
                                                        : "counter of any Pok\xC3\xA9 Mart):";
                top.text(kFontSmall, 12, 66 + i * 12, 232, Align::Left, line, kMuted);
            }
        } else {
            top.text(kFontLarge, 12, 44, 232, Align::Left, g->title, kText);
            top.text(kFontSmall, 12, 66, 232, Align::Left, "Not a Pok\xC3\xA9mon card (item or power)", kMuted);
        }
    }
    bottom.grid(0, 0, gfx::kScreenWidth, kHintTop, kBackground, kGridLine, 12);
    bottom.roundRect(4, 4, 248, 168, kBorder, kPanel);
    bottom.text(kFontSmall, 10, 5, "MYSTERY GIFT ALBUM", kMuted);
    drawList(bottom, giftRows_, giftCursor_, giftScroll_, 8, 19, 236, kRowH, kVisible, &giftEmpty_);
    drawHints(bottom, {{"A", "Add card"}, {"X", "Remove"}, {"SEL", "Save"}, {"B", "Back"}},
              {kKeyA, kKeyX, kKeySelect, kKeyB});
}

}  // namespace pktool::ui
