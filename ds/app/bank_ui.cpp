// Bank screen of the PKTool UI (see app.h): browsing, deposit/withdraw, moving
// and the Bank menu (rename box, release, search, sort).
//
// Changes made inside the Bank (move, rename, release, sort) are written to
// the SD card right away, so they are refused while deposits or withdrawals
// of the open save are pending (those are written together with the save by
// SELECT) or while a Pokemon is picked up with X.

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "core/data/names.h"
#include "core/io/file_io.h"
#include "core/pokemon/pk1.h"
#include "core/pokemon/pk2.h"
#include "core/pokemon/time_capsule.h"
#include "core/pokemon/transfer34.h"
#include "core/pokemon/transfer45.h"
#include "ds/app/app.h"
#include "ds/app/ui_common.h"

namespace pktool::ui {
using namespace detail;

namespace {

enum BankMenuItem { kBmRename, kBmRelease, kBmSearch, kBmSortBox, kBmSortAll, kBmCount };
const char* const kBankMenuRows[kBmCount] = {"Rename this box", "Release this Pok\xC3\xA9mon", "Search the Bank",
                                             "Sort this box...", "Sort the whole Bank..."};
const char* const kBankSortRows[3] = {"By Dex number", "By name", "By level (highest first)"};
constexpr size_t kMaxResults = 200;

// Rows of the Search screen (BankMode::Filters).
enum BankFilterRow { kBfText, kBfShiny, kBfFormat, kBfGo, kBfCount };
// The Game filter cycles All, Gen III, Gen IV, Gen V, then the Game Boy generations.
const char* const kBankFormatNames[6] = {"All", "Gen III", "Gen IV", "Gen V", "Gen I", "Gen II"};
int formatToRow(uint8_t format) {
    return format == 0 ? 0 : format >= 3 ? format - 2 : format + 3;
}
uint8_t rowToFormat(int row) {
    return row == 0 ? 0 : row <= 3 ? static_cast<uint8_t>(row + 2) : static_cast<uint8_t>(row - 3);
}

// Japanese, Korean and international Game Boy games cannot trade with each
// other: the language byte of the blob and the save must be in the same class.
bool sameLanguageClass(uint8_t blobLanguage, uint8_t saveLanguage) {
    return (blobLanguage == 1) == (saveLanguage == 1) && (blobLanguage == 8) == (saveLanguage == 8);
}

pokemon::Pokemon decodeBank(const bank::Bank& b, int box, int slot) {
    return pokemon::entity::decode(b.format(box, slot), b.entity(box, slot), pokemon::entity::kStoredSize);
}

}  // namespace

std::string App::bankPath() const {
    return io::joinPath(io::joinPath(config_.dataDir, "bank"), "bank.pkb");
}

bool App::loadBank() {
    if (bankLoaded_) return !bank_.readOnly();
    bankLoaded_ = true;
    if (bank_.load(bankPath()) == bank::Bank::LoadResult::Corrupt) {
        showMessage("Bank", "The Bank file is damaged and\nwas not changed:\n" + bankPath());
        return false;
    }
    return true;
}

void App::openBank(bool withSave, Screen returnTo) {
    if (!config_.storageReady) {
        showMessage("No SD card", "The SD card could not be\ninitialised (DLDI/FAT).");
        return;
    }
    loadBank();
    bankWithSave_ = withSave;
    bankReturn_ = returnTo;
    bankMode_ = BankMode::Browse;
    screen_ = Screen::Bank;
}

bool App::bankWithdrawn(int box, int slot) const {
    for (const auto& w : bankWithdrawn_) {
        if (w.first == box && w.second == slot) return true;
    }
    return false;
}

// True (after telling the user why) when the Bank cannot be changed and
// written right now.
bool App::bankBlocked() {
    if (bank_.readOnly()) {
        showMessage("Bank", "The Bank file is damaged:\nit cannot be changed.");
        return true;
    }
    if (bankDirty_ || !bankWithdrawn_.empty()) {
        showMessage("Bank", "Write the pending deposits and\nwithdrawals first (SELECT).");
        return true;
    }
    if (bankHeld_ >= 0) {
        showMessage("Bank", "Drop the picked-up Pok\xC3\xA9mon\nfirst (X).");
        return true;
    }
    return false;
}

// Writes the Bank after a change; on failure the Bank is reloaded from the SD card.
bool App::bankCommit() {
    if (bank_.save() == io::WriteResult::Ok) return true;
    showMessage("Bank", "The Bank could not be written.");
    bank_.load(bankPath());
    return false;
}

void App::openBankList(BankMode mode) {
    bankMode_ = mode;
    bankListCursor_ = 0;
    bankListScroll_ = 0;
    bankRows_.clear();
    if (mode == BankMode::Menu) {
        bankRows_.assign(kBankMenuRows, kBankMenuRows + kBmCount);
    } else if (mode == BankMode::SortKey) {
        bankRows_.assign(kBankSortRows, kBankSortRows + 3);
    } else if (mode == BankMode::Filters) {
        refreshBankFilterRows();
    }
}

void App::refreshBankFilterRows() {
    const int format = formatToRow(bankFilter_.format);
    bankRows_.clear();
    bankRows_.push_back("Name: " + (bankFilter_.text.empty() ? std::string("(any)") : bankFilter_.text));
    bankRows_.push_back(std::string("Shiny: ") + (bankFilter_.shinyOnly ? "Shiny only" : "Any"));
    bankRows_.push_back(std::string("Game: ") + kBankFormatNames[format]);
    bankRows_.push_back("Search");
}

// Left/Right (or A) on a filter row.
void App::changeBankFilter(int row, int delta) {
    if (row == kBfShiny) {
        bankFilter_.shinyOnly = !bankFilter_.shinyOnly;
    } else if (row == kBfFormat) {
        bankFilter_.format = rowToFormat(wrap(formatToRow(bankFilter_.format) + delta, 6));
    } else if (row == kBfText && delta == 0) {
        openKeyboard(KeyboardTarget::BankSearch, bankFilter_.text, bank::Bank::kBoxNameMaxChars);
        return;
    }
    refreshBankFilterRows();
}

void App::updateBank(const Input& in) {
    if (bankMode_ != BankMode::Browse) {
        updateBankList(in);
        return;
    }
    constexpr int kSlots = bank::Bank::kSlotsPerBox;
    int zone = in.touched ? touchedZone(in) : -1;
    if (in.repeated & kKeyR || zone == kZoneNextBox) {
        bankBox_ = wrap(bankBox_ + 1, bank::Bank::kBoxCount);
        return;
    }
    if (in.repeated & kKeyL || zone == kZonePrevBox) {
        bankBox_ = wrap(bankBox_ - 1, bank::Bank::kBoxCount);
        return;
    }
    if (zone == kZoneBoxTitle) {
        runBankMenu(kBmRename);
        return;
    }
    bool activate = in.pressed & kKeyA;
    if (zone >= kZoneItem && zone < kZoneItem + kSlots) {
        int slot = zone - kZoneItem;
        activate = slot == bankCursor_;
        bankCursor_ = slot;
    }
    if (in.repeated & kKeyLeft) bankCursor_ = wrap(bankCursor_ - 1, kSlots);
    if (in.repeated & kKeyRight) bankCursor_ = wrap(bankCursor_ + 1, kSlots);
    if (in.repeated & kKeyUp) bankCursor_ = wrap(bankCursor_ - 6, kSlots);
    if (in.repeated & kKeyDown) bankCursor_ = wrap(bankCursor_ + 6, kSlots);
    if (in.pressed & kKeyB) screen_ = bankReturn_;
    if (in.pressed & kKeyStart && bank_.entity(bankBox_, bankCursor_)) {
        showLegality(decodeBank(bank_, bankBox_, bankCursor_));
    }
    if (in.pressed & kKeyY) {
        openBankList(BankMode::Menu);
        return;
    }
    if (activate) bankAction();
    if (in.pressed & kKeyX) bankMove();
}

// Menu, sort-criterion and search-result lists.
void App::updateBankList(const Input& in) {
    int zone = in.touched ? touchedZone(in) : -1;
    int count = static_cast<int>(bankRows_.size());
    bool activate = in.pressed & kKeyA;
    if (zone >= kZoneItem && zone < kZoneItem + count) {
        activate = zone - kZoneItem == bankListCursor_;
        bankListCursor_ = zone - kZoneItem;
    }
    if (in.repeated & kKeyUp) bankListCursor_ = wrap(bankListCursor_ - 1, count);
    if (in.repeated & kKeyDown) bankListCursor_ = wrap(bankListCursor_ + 1, count);
    if (in.repeated & kKeyL) bankListCursor_ = clampInt(bankListCursor_ - 8, 0, std::max(0, count - 1));
    if (in.repeated & kKeyR) bankListCursor_ = clampInt(bankListCursor_ + 8, 0, std::max(0, count - 1));
    if (bankMode_ == BankMode::Filters && count > 0) {
        if (in.repeated & kKeyLeft) changeBankFilter(bankListCursor_, -1);
        if (in.repeated & kKeyRight) changeBankFilter(bankListCursor_, 1);
    }
    if (in.pressed & kKeyB) {
        if (bankMode_ == BankMode::SortKey || bankMode_ == BankMode::Filters) {
            openBankList(BankMode::Menu);
        } else if (bankMode_ == BankMode::Results) {
            bankMode_ = BankMode::Filters;  // back to the filters for another try
            refreshBankFilterRows();
            bankListCursor_ = kBfGo;
        } else {
            bankMode_ = BankMode::Browse;
        }
        return;
    }
    if (!activate || count == 0) return;
    int index = bankListCursor_;
    switch (bankMode_) {
        case BankMode::Menu:
            runBankMenu(index);
            break;
        case BankMode::SortKey:
            runBankSort(index);
            break;
        case BankMode::Filters:
            if (index == kBfGo) {
                runBankSearch();
            } else {
                changeBankFilter(index, index == kBfText ? 0 : 1);
            }
            break;
        case BankMode::Results:
            if (index < static_cast<int>(bankMatches_.size())) {
                bankBox_ = bankMatches_[index].box;
                bankCursor_ = bankMatches_[index].slot;
            }
            bankMode_ = BankMode::Browse;
            break;
        case BankMode::Browse:
            break;
    }
}

void App::runBankMenu(int item) {
    switch (item) {
        case kBmRename:
            if (bankBlocked()) return;
            bankMode_ = BankMode::Browse;
            openKeyboard(KeyboardTarget::BankBoxName, bank_.hasCustomBoxName(bankBox_) ? bank_.boxName(bankBox_) : "",
                         bank::Bank::kBoxNameMaxChars);
            break;
        case kBmRelease:
            bankMode_ = BankMode::Browse;
            if (bank_.entity(bankBox_, bankCursor_) == nullptr) {
                showMessage("Release", "There is no Pok\xC3\xA9mon in this\nBank slot.");
            } else if (!bankBlocked()) {
                dialog_ = Dialog::ConfirmRelease;
            }
            break;
        case kBmSearch:
            openBankList(BankMode::Filters);
            break;
        case kBmSortBox:
        case kBmSortAll:
            if (bankBlocked()) return;
            bankSortAll_ = item == kBmSortAll;
            openBankList(BankMode::SortKey);
            break;
        default:
            break;
    }
}

void App::runBankSort(int index) {
    static const bank::Bank::SortKey kKeys[3] = {bank::Bank::SortKey::Dex, bank::Bank::SortKey::Name,
                                                 bank::Bank::SortKey::Level};
    bankMode_ = BankMode::Browse;
    if (index < 0 || index > 2 || bankBlocked()) return;
    if (!bank_.sort(kKeys[index], bankSortAll_ ? -1 : bankBox_)) return;
    if (bankSortAll_) bankBox_ = 0;
    bankCursor_ = 0;
    if (bankCommit()) {
        showMessage("Sorted", std::string(bankSortAll_ ? "The whole Bank" : "Bank box " + twoDigits(bankBox_ + 1)) +
                                  " is sorted\n" + std::string(kBankSortRows[index]) + ".");
    }
}

// Confirmed release of the Bank Pokemon under the cursor.
void App::bankRelease() {
    if (bankBlocked()) return;
    if (bank_.release(bankBox_, bankCursor_)) bankCommit();
}

// Text from the keyboard: the new box name or the search text.
void App::bankKeyboardDone(const std::string& text) {
    if (kbTarget_ == KeyboardTarget::BankBoxName) {
        if (bankBlocked()) return;
        std::string before = bank_.boxName(bankBox_);
        bank_.setBoxName(bankBox_, text);
        if (bank_.boxName(bankBox_) != before) bankCommit();
        return;
    }
    // The search text was typed on the Search screen: back to its filters.
    bankFilter_.text = text;
    bankMode_ = BankMode::Filters;
    refreshBankFilterRows();
}

// Runs the Search screen's filters (an empty name is fine when a filter is set).
void App::runBankSearch() {
    if (bankFilter_.text.empty() && !bankFilter_.shinyOnly && bankFilter_.format == 0) {
        showMessage("Search", "Type a name or choose a filter.");
        return;
    }
    bankMatches_ = bank_.search(bankFilter_);
    if (bankMatches_.empty()) {
        std::string what = bankFilter_.text.empty() ? "the filters" : "\"" + bankFilter_.text + "\"";
        showMessage("Search", "No Pok\xC3\xA9mon in the Bank\nmatch " + what + ".");
        return;
    }
    openBankList(BankMode::Results);
    if (bankMatches_.size() > kMaxResults) bankMatches_.resize(kMaxResults);
    for (const bank::Bank::Match& m : bankMatches_) {
        pokemon::Pokemon p = decodeBank(bank_, m.box, m.slot);
        std::string row = twoDigits(m.box + 1) + "-" + twoDigits(m.slot + 1) + "  " +
                          (p.isEgg ? std::string("Egg") : std::string(data::speciesName(p.species))) + "  Lv " +
                          std::to_string(p.level);
        if (p.shiny) row += "  *";
        bankRows_.push_back(row);
    }
}

// X picks a Bank Pokémon up and drops it on another slot (swapping if taken).
// Moves inside the Bank are written right away.
void App::bankMove() {
    int here = bankBox_ * bank::Bank::kSlotsPerBox + bankCursor_;
    if (bank_.readOnly()) {
        showMessage("Bank", "The Bank file is damaged:\nit cannot be changed.");
        return;
    }
    if (bankDirty_ || !bankWithdrawn_.empty()) {
        showMessage("Bank", "Write the pending deposits and\nwithdrawals first (SELECT).");
        return;
    }
    if (bankHeld_ < 0) {
        if (bank_.entity(bankBox_, bankCursor_)) bankHeld_ = here;
        return;
    }
    int fromBox = bankHeld_ / bank::Bank::kSlotsPerBox;
    int fromSlot = bankHeld_ % bank::Bank::kSlotsPerBox;
    bankHeld_ = -1;
    if (here == fromBox * bank::Bank::kSlotsPerBox + fromSlot) return;
    uint8_t a[bank::Bank::kEntitySize];
    uint8_t formatA = bank_.format(fromBox, fromSlot);
    std::memcpy(a, bank_.entity(fromBox, fromSlot), sizeof a);
    const uint8_t* b = bank_.entity(bankBox_, bankCursor_);
    if (b) {
        bank_.set(fromBox, fromSlot, bank_.format(bankBox_, bankCursor_), b);
    } else {
        bank_.clear(fromBox, fromSlot);
    }
    bank_.set(bankBox_, bankCursor_, formatA, a);
    bankCommit();
}

void App::bankAction() {
    if (!bankWithSave_) {
        showMessage("Bank", "Press Y on a slot in the Boxes\nview to deposit or withdraw.");
        return;
    }
    if (bank_.readOnly()) {
        showMessage("Bank", "The Bank file is damaged:\nit cannot be changed.");
        return;
    }
    // Party slots hold party data; the Bank keeps the stored part only.
    uint8_t raw[pokemon::entity::kMaxPartySize] = {};
    auto state = isParty() ? save_->readPartySlot(slotCursor_, raw) : save_->readBoxSlot(box_, slotCursor_, raw);
    if (state == saves::SlotState::BadChecksum) {
        showMessage("Bank", "The save slot holds bad data.");
        return;
    }
    const uint8_t* stored = bank_.entity(bankBox_, bankCursor_);
    bool withdrawn = bankWithdrawn(bankBox_, bankCursor_);
    std::string where = isParty() ? std::string("the party")
                                  : "Box " + twoDigits(box_ + 1) + " slot " + std::to_string(slotCursor_ + 1);

    if (state == saves::SlotState::Valid) {  // deposit
        if (stored != nullptr) {
            showMessage("Bank", "This Bank slot is taken:\npick an empty one.");
            return;
        }
        if (isParty() && save_->partyCount() <= 1) {
            showMessage("Bank", "The party needs at least\none Pok\xC3\xA9mon.");
            return;
        }
        bank_.set(bankBox_, bankCursor_, save_->format(), raw);
        if (isParty()) {
            save_->removePartySlot(slotCursor_);
            slotCursor_ = std::min(slotCursor_, save_->partyCount() - 1);
        } else {
            save_->clearBoxSlot(box_, slotCursor_);
        }
        unsaved_ = true;
        bankDirty_ = true;
        screen_ = Screen::Box;
        showMessage("Deposited", "Moved to Bank box " + twoDigits(bankBox_ + 1) +
                                     ".\n\nSELECT writes the save and the\nBank to the SD card.");
        return;
    }

    // Withdraw into the empty save slot.
    if (stored == nullptr || withdrawn) {
        showMessage("Bank", "Pick a Pok\xC3\xA9mon to withdraw into\n" + where + ".");
        return;
    }
    uint8_t format = bank_.format(bankBox_, bankCursor_);
    uint8_t data[pokemon::entity::kStoredSize] = {};
    std::vector<std::string> notes;
    const uint8_t blobLanguage = format == 1 ? stored[pokemon::pk1::kLanguageOffset]
                                 : format == 2 ? stored[pokemon::pk2::kLanguageOffset]
                                               : 0;
    if (format <= 2 && save_->format() <= 2 && !sameLanguageClass(blobLanguage, save_->language())) {
        showMessage("Bank", "Japanese, Korean and international\ngames cannot trade Pok\xC3\xA9mon.");
        return;
    }
    if (format == save_->format()) {
        std::memcpy(data, stored, sizeof data);
    } else if (format <= 2 && save_->format() <= 2) {
        // Time Capsule: Gen I <-> Gen II, with the games' own restrictions.
        pokemon::timecapsule::Result trip = format == 1 ? pokemon::timecapsule::toGen2(stored, data, &notes)
                                                        : pokemon::timecapsule::toGen1(stored, data, &notes);
        if (!trip.ok) {
            showMessage("Time Capsule", "The Time Capsule refuses it:\n" + trip.reason);
            return;
        }
        notes.insert(notes.begin(), "Converted by the Time Capsule");
    } else if (format <= 2) {
        showMessage("Bank", "Gen I and II Pok\xC3\xA9mon cannot move to\nGen III-V: there is no official path.");
        return;
    } else if (save_->format() <= 2) {
        showMessage("Bank", "Pok\xC3\xA9mon cannot go back to\nan older generation.");
        return;
    } else if (format < save_->format()) {
        // Gen III -> IV with Pal Park rules, IV -> V with Poke Transfer rules
        // (Gen III -> V goes through both, as in the games).
        if (pokemon::entity::decode(format, stored, pokemon::entity::kStoredSize).isEgg) {
            showMessage("Bank", "Eggs cannot be moved to a newer\ngame: hatch it first.");
            return;
        }
        uint8_t step[pokemon::entity::kStoredSize] = {};
        std::memcpy(step, stored, sizeof step);
        if (format == 3) {
            pokemon::transfer34::convert(stored, step, saveTrainer().today, &notes);
            notes.insert(notes.begin(), "Converted with Pal Park rules");
        }
        if (save_->format() == 5) {
            pokemon::transfer45::convert(step, data, &notes);
            notes.insert(notes.begin() + (format == 3 ? 1 : 0), "Converted with Poke Transfer rules");
        } else {
            std::memcpy(data, step, sizeof data);
        }
    } else {
        showMessage("Bank", "Pok\xC3\xA9mon cannot go back to\nan older generation.");
        return;
    }
    if (isParty()) {
        // Party data = stored data + battle stats computed from it.
        uint8_t party[pokemon::entity::kMaxPartySize] = {};
        std::memcpy(party, data, sizeof data);
        pokemon::entity::refreshParty(save_->format(), party);
        if (!save_->appendParty(party)) {
            showMessage("Bank", "The party is full.");
            return;
        }
        slotCursor_ = save_->partyCount() - 1;
    } else {
        save_->writeBoxSlot(box_, slotCursor_, data);
    }
    bankWithdrawn_.push_back({bankBox_, bankCursor_});
    unsaved_ = true;
    screen_ = Screen::Box;
    std::string text = "Placed in " + where + ".";
    for (const std::string& n : notes) text += "\n" + n;
    text += "\n\nSELECT writes the save; then it\nleaves the Bank.";
    showMessage("Withdrawn", text);
}

void App::drawBank(Canvas& top, Canvas& bottom) {
    std::string status = "Bank  \xE2\x80\xA2  " + std::to_string(bank_.count()) + " Pok\xC3\xA9mon";
    if (bankWithSave_) status += "  \xE2\x80\xA2  " + saveTitle();
    drawChrome(top, status, bankDirty_ || !bankWithdrawn_.empty() ? "Unsaved" : "", kYellow);
    const bool listing = bankMode_ != BankMode::Browse;
    // The Pokemon shown on the top screen: the highlighted search result or the cursor.
    int viewBox = bankBox_;
    int viewSlot = bankCursor_;
    if (bankMode_ == BankMode::Results && bankListCursor_ < static_cast<int>(bankMatches_.size())) {
        viewBox = bankMatches_[bankListCursor_].box;
        viewSlot = bankMatches_[bankListCursor_].slot;
    }
    pokemon::Pokemon selected;
    const uint8_t* stored = bank_.entity(viewBox, viewSlot);
    if (stored) selected = decodeBank(bank_, viewBox, viewSlot);
    drawSummary(top, stored ? &selected : nullptr, false);

    bottom.grid(0, 0, gfx::kScreenWidth, kHintTop, kBackground, kGridLine, 12);
    bottom.roundRect(4, 3, 248, 17, kBorder, kPanelDark);
    bottom.fillRect(6, 5, 10, 13, kGreen);
    bottom.fillRect(240, 5, 10, 13, kGreen);
    bottom.arrow(9, 8, 7, -1, kTitleBar);
    bottom.arrow(243, 8, 7, 1, kTitleBar);
    std::string title = "BANK " + twoDigits(bankBox_ + 1);
    if (bank_.hasCustomBoxName(bankBox_)) title += "  \xE2\x80\xA2  " + bank_.boxName(bankBox_);
    bottom.text(kFontSmall, 18, 4, 220, Align::Center, title, kText);
    bottom.roundRect(4, 23, 248, 151, kGreen, kBoxArea);

    if (listing) {
        const char* heading = bankMode_ == BankMode::Menu       ? "BANK MENU"
                              : bankMode_ == BankMode::SortKey  ? (bankSortAll_ ? "SORT THE WHOLE BANK" : "SORT THIS BOX")
                              : bankMode_ == BankMode::Filters  ? "SEARCH THE BANK"
                                                                : "SEARCH RESULTS";
        std::string headingText = heading;
        if (bankMode_ == BankMode::Results) {
            headingText += "  \xE2\x80\xA2  " + std::to_string(bankMatches_.size()) +
                           (bankMatches_.size() >= kMaxResults ? "+" : "");
        }
        bottom.text(kFontSmall, 8, 26, 240, Align::Center, headingText, kMuted);
        drawList(bottom, bankRows_, bankListCursor_, bankListScroll_, 8, 42, 236, 15, 8);
        if (bankMode_ == BankMode::Filters) {
            const char* verb = bankListCursor_ == kBfText ? "Type" : bankListCursor_ == kBfGo ? "Search" : "Change";
            drawHints(bottom, {{"A", verb}, {"LR", "Change"}, {"B", "Back"}}, {kKeyA, 0, kKeyB});
            return;
        }
        const char* verb = bankMode_ == BankMode::Menu ? "Choose" : bankMode_ == BankMode::SortKey ? "Sort" : "Go to";
        drawHints(bottom, {{"A", verb}, {"B", "Back"}}, {kKeyA, kKeyB});
        return;
    }

    zones_.push_back({4, 2, 30, 20, kZonePrevBox});
    zones_.push_back({222, 2, 30, 20, kZoneNextBox});
    zones_.push_back({36, 2, 184, 20, kZoneBoxTitle});
    for (int slot = 0; slot < bank::Bank::kSlotsPerBox; ++slot) {
        int x = 8 + (slot % 6) * 40;
        int y = 25 + (slot / 6) * 29;
        const uint8_t* data = bank_.entity(bankBox_, slot);
        if (data) {
            pokemon::Pokemon p = decodeBank(bank_, bankBox_, slot);
            drawIcon(bottom, x, y, 40, 29, p, true);
            if (p.shiny) drawStar(bottom, x + 32, y + 1, kYellow);
            if (bank_.format(bankBox_, slot) == 5) bottom.text(kFontSmall, x + 1, y + 15, "V", kMuted);
            if (bank_.format(bankBox_, slot) == 3) bottom.text(kFontSmall, x + 1, y + 15, "III", kMuted);
            if (bank_.format(bankBox_, slot) == 2) bottom.text(kFontSmall, x + 1, y + 15, "II", kMuted);
            if (bank_.format(bankBox_, slot) == 1) bottom.text(kFontSmall, x + 1, y + 15, "I", kMuted);
            if (bankWithdrawn(bankBox_, slot)) bottom.blendRect(x + 1, y, 38, 29, kBoxArea, 22);
            if (bankHeld_ == bankBox_ * bank::Bank::kSlotsPerBox + slot) bottom.brackets(x + 3, y + 2, 34, 25, kGreen, 4);
        } else {
            bottom.pixel(x + 20, y + 14, kBoxGrid);
            bottom.pixel(x + 19, y + 14, kBoxGrid);
        }
        if (slot == bankCursor_) bottom.brackets(x + 1, y, 38, 29, kCursor, 5);
        zones_.push_back({x, y, 40, 29, kZoneItem + slot});
    }
    const char* action = "View";
    if (bankWithSave_) {
        pokemon::Pokemon p;
        action = readSlot(box_, slotCursor_, p) == saves::SlotState::Valid ? "Deposit" : "Withdraw";
    }
    // START (legality check) is not hinted: five hints do not fit the bar.
    drawHints(bottom, {{"A", action}, {"X", bankHeld_ >= 0 ? "Drop" : "Move"}, {"Y", "Menu"}, {"B", "Back"}},
              {kKeyA, kKeyX, kKeyY, kKeyB});
}

}  // namespace pktool::ui
