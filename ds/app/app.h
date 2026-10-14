#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/bank/bank.h"
#include "core/io/settings.h"
#include "core/legality/legality.h"
#include "core/pokemon/entity.h"
#include "core/pokemon/pokemon.h"
#include "core/pokemon/card_file.h"
#include "core/pokemon/create4.h"
#include <memory>

#include "core/saves/gba_cart.h"
#include "core/saves/save_file.h"
#include "core/saves/save_probe.h"
#include "core/showdown/showdown.h"
#include "ds/app/keyboard_cjk.h"
#include "ds/gfx/canvas.h"
#include "ds/gfx/sprites.h"

// PKTool's user interface: state, input handling and drawing of both screens.
//
// This layer is portable (no libnds): ds/main.cpp feeds it DS input and shows
// its canvases, and tools/preview drives it on a PC to render screenshots.
namespace pktool::ui {

// Same bit layout as libnds' KEY_* constants, so the DS glue passes them through.
enum Key : uint32_t {
    kKeyA = 1u << 0,
    kKeyB = 1u << 1,
    kKeySelect = 1u << 2,
    kKeyStart = 1u << 3,
    kKeyRight = 1u << 4,
    kKeyLeft = 1u << 5,
    kKeyUp = 1u << 6,
    kKeyDown = 1u << 7,
    kKeyR = 1u << 8,
    kKeyL = 1u << 9,
    kKeyX = 1u << 10,
    kKeyY = 1u << 11,
};

struct Input {
    uint32_t pressed = 0;   // keys that went down this frame
    uint32_t repeated = 0;  // pressed + auto-repeat while held
    bool touched = false;   // stylus went down this frame (bottom screen)
    int touchX = 0;
    int touchY = 0;
};

struct Config {
    std::string scanRoot = "/";          // where .sav files are searched
    std::string dataDir = "/PKTOOL";     // backups live here
    // Sprite pack locations, searched in order (default: <dataDir>/sprites).
    std::vector<std::string> spriteDirs;
    // Where the NitroFS assets live ("fonts/cjk.bin", "lang/<code>.bin"),
    // searched in order (default: <dataDir>).
    std::vector<std::string> assetDirs;
    bool storageReady = true;            // FAT/SD initialised
    // The GBA slot (DS and DS Lite in DS mode), nullptr when there is none. The
    // browser then offers SELECT: back up the cartridge's save (beta, read-only).
    saves::gba::Bus* gbaSlot = nullptr;
    // The platform blends the screen fade itself (DS: BLDCNT against the backdrop,
    // see fadeAlpha()); draw() then skips the software blend and fade frames do
    // not force a redraw.
    bool hardwareFade = false;
    // Free heap in bytes (DS only; null elsewhere). Shown in the About box.
    size_t (*freeMemory)() = nullptr;
    std::string version = "1.0.1";
};

class App {
public:
    explicit App(Config config);

    // Processes one frame of input. Returns false when the user exits.
    // Animations advance by one step per call (the DS calls it every frame),
    // so with the same inputs the screens are the same on every run.
    bool update(const Input& input);

    // True when the screens changed since the last draw().
    bool needsRedraw() const { return redraw_; }
    // Weight (0-32) of the background colour over the screens for the fade-in.
    int fadeAlpha() const;
    void draw(gfx::Canvas& top, gfx::Canvas& bottom);

    // Opens a save directly (used by the PC preview tool).
    bool openSave(const std::string& path);

    // Read-only views of the UI state, for tests.
    // Name of the current screen ("SaveMenu", "Box", "Trainer", ...).
    const char* screenName() const;
    const pokemon::Pokemon& editedPokemon() const { return edit_.current; }
    const std::vector<std::string>& pickerRows() const { return pickerRows_; }
    int pickerCursor() const { return pickerCursor_; }
    // Animations (Settings > Animations): frames update() has run, and whether
    // a fade or a selection glide is still going (the screens keep redrawing).
    uint32_t frameCount() const { return frame_; }
    bool animating() const { return fadeLeft_ > 0 || easing_; }
    bool animationsEnabled() const { return settings_.animations; }
    void setAnimations(bool on) { settings_.animations = on; }

private:
    enum class Screen : uint8_t { Home, Browser, SaveMenu, Box, Edit, Backups, Picker, Bank, Keyboard, Trainer, Bag, ItemPicker, Ribbons, Settings };
    enum class KeyboardTarget : uint8_t { Nickname, BoxName, TrainerName, BankBoxName, BankSearch, OtName };
    // What the Bank screen shows on the bottom screen (bank_ui.cpp).
    enum class BankMode : uint8_t { Browse, Menu, SortKey, Filters, Results };
    enum class Dialog : uint8_t { None, Message, Legality, ConfirmWrite, ConfirmClose, ConfirmDiscard, ConfirmRestore, ConfirmRelease };
    enum class Pending : uint8_t { None, Scan, Write, Restore, Legalize, Create, GbaBackup };
    // Lists shown by the picker screen when adding a Pokémon to an empty slot.
    enum class PickerKind : uint8_t { AddMenu, Species, Showdown, Event };

    // Actions of the slot menu (A on a Pokémon in the box view).
    enum class MenuAction : uint8_t { Edit, Move, Legality, Showdown, Bank, Clone, Release, Ribbons, Cancel };

    struct TouchZone {
        int x, y, w, h;
        int id;
    };

    struct EditState {
        pokemon::Pokemon original;
        pokemon::Pokemon current;
        uint8_t raw[pokemon::entity::kMaxPartySize] = {};
        bool party = false;
        int box = 0;
        int slot = 0;
        int field = 0;
        bool created = false;  // new Pokémon for an empty slot
        std::string status;
    };

    bool updateFrame(const Input& input);
    // Animation helpers (app.cpp). The fade covers both screens when the screen changes.
    void tickAnimations(Screen before);
    int bobOffset() const;
    // Input per screen.
    bool updateHome(const Input& in);
    void updateBrowser(const Input& in);
    void backUpGbaCartridge();
    void updateSaveMenu(const Input& in);
    void updateBox(const Input& in);
    void updateEdit(const Input& in);
    void updateBackups(const Input& in);
    void updatePicker(const Input& in);
    void toggleSpeciesOrder();
    void updateBank(const Input& in);
    void updateKeyboard(const Input& in);
    void keyboardDelete();
    void updateMenu(const Input& in);
    void updateTrainer(const Input& in);
    void updateBag(const Input& in);
    void updateItemPicker(const Input& in);
    void updateSettings(const Input& in);
    void updateDialog(const Input& in);
    int touchedZone(const Input& in) const;

    // Actions.
    void startBrowser();
    void openBackups(Screen returnTo);
    bool startEdit();
    void applyEdit();
    // Slot menu and the actions it offers.
    void openMenu(const pokemon::Pokemon& p);
    void runMenuAction(MenuAction action);
    void openSaveMenu();
    void closeSave();
    void runSaveMenuAction(int item);
    void backupNow();
    // Trainer card and bag editors (trainer_bag_ui.cpp).
    void openTrainer();
    void applyTrainer();
    bool trainerChanged() const;
    void openBag();
    void applyBag();
    bool bagChanged() const;
    bool bagPocketChanged(int pocket) const;
    void openItemPicker();
    void addBagItem(uint16_t item);
    // Ribbon editor of the selected Pokémon (ribbons_ui.cpp).
    void openRibbons();
    void applyRibbons();
    bool ribbonsChanged() const;
    void updateRibbons(const Input& in);
    void drawRibbons(gfx::Canvas& top, gfx::Canvas& bottom);
    // Settings screen (settings_ui.cpp): preferences kept in <dataDir>/settings.ini.
    void openSettings();
    void applySettings();
    std::string settingsPath() const;
    void dropHeld();
    void cloneSlot();
    void releaseSlot();
    std::string exportShowdown(const pokemon::Pokemon& p);
    bool editChanged() const;
    void runPending();
    void showMessage(const std::string& title, const std::string& text);
    void showLegality(const pokemon::Pokemon& p);
    void openPicker(PickerKind kind);
    void openKeyboard(KeyboardTarget target, const std::string& text, size_t maxLength);
    void keyboardPress(int key);
    void keyboardDone();
    // Bank. With a save open, the selected box slot is the deposit source
    // (if it holds a Pokémon) or the withdraw target (if empty).
    bool loadBank();
    void openBank(bool withSave, Screen returnTo);
    void bankAction();
    void bankMove();
    void updateBankList(const Input& in);
    void openBankList(BankMode mode);
    void runBankMenu(int item);
    void runBankSort(int index);
    void refreshBankFilterRows();
    void changeBankFilter(int row, int delta);
    void runBankSearch();
    void bankRelease();
    void bankKeyboardDone(const std::string& text);
    bool bankBlocked();
    bool bankCommit();
    bool bankWithdrawn(int box, int slot) const;
    void createPokemon();
    void startCreatedEdit(const pokemon::Pokemon& p, const uint8_t* raw, const std::string& title,
                          const std::vector<std::string>& changes, const std::vector<std::string>& notes);
    pokemon::create4::Trainer saveTrainer() const;
    uint32_t nextSeed();
    const pokemon::Pokemon& pickerPreview();
    // The Wonder Card on the selected Event picker row, received by the save's trainer.
    pokemon::Pokemon receiveEvent(uint32_t seed, uint8_t* raw = nullptr);
    const char* eventTitle() const;
    void addCardFiles();

    // Save helpers.
    bool isParty() const { return box_ < 0; }
    int slotCount() const;
    saves::SlotState readSlot(int box, int slot, pokemon::Pokemon& out) const;

    // Drawing.
    // statusColor 0 = default text colour.
    void drawChrome(gfx::Canvas& c, const std::string& status, const std::string& badge, gfx::Color badgeColor,
                    gfx::Color statusColor = 0);
    void drawHints(gfx::Canvas& c, const std::vector<std::pair<std::string, std::string>>& hints,
                   const std::vector<int>& ids);
    void drawDialog(gfx::Canvas& c);
    void drawHome(gfx::Canvas& top, gfx::Canvas& bottom);
    void drawBrowser(gfx::Canvas& top, gfx::Canvas& bottom);
    void drawSaveMenu(gfx::Canvas& top, gfx::Canvas& bottom);
    void drawBox(gfx::Canvas& top, gfx::Canvas& bottom);
    void drawEdit(gfx::Canvas& top, gfx::Canvas& bottom);
    void drawBackups(gfx::Canvas& top, gfx::Canvas& bottom);
    void drawPicker(gfx::Canvas& top, gfx::Canvas& bottom);
    void drawBank(gfx::Canvas& top, gfx::Canvas& bottom);
    void drawKeyboard(gfx::Canvas& top, gfx::Canvas& bottom);
    void drawTrainer(gfx::Canvas& top, gfx::Canvas& bottom);
    void drawBag(gfx::Canvas& top, gfx::Canvas& bottom);
    void drawItemPicker(gfx::Canvas& top, gfx::Canvas& bottom);
    void drawSettings(gfx::Canvas& top, gfx::Canvas& bottom);
    void drawSummary(gfx::Canvas& c, const pokemon::Pokemon* p, bool badData, bool badge = true);
    // Overall legality of `p`, recomputed only when it differs from the last Pokémon asked
    // about: the checker costs milliseconds on the DS and the summary is drawn every frame.
    legality::Severity badgeSeverity(const pokemon::Pokemon& p);
    void drawIcon(gfx::Canvas& c, int x, int y, int w, int h, const pokemon::Pokemon& p, bool showName);
    // `dimmed` (optional, one entry per row): rows with a non-empty entry are drawn greyed.
    void drawList(gfx::Canvas& c, const std::vector<std::string>& rows, int cursor, int& scroll, int x, int y,
                  int w, int rowHeight, int visibleRows, const std::vector<std::string>* dimmed = nullptr);

    std::string backupDir() const;
    std::string showdownDir() const;
    std::string eventsDir() const;
    std::string bankPath() const;
    std::string saveTitle() const;

    Config config_;
    gfx::SpriteStore sprites_;
    Screen screen_ = Screen::Home;
    Dialog dialog_ = Dialog::None;
    Pending pending_ = Pending::None;
    bool redraw_ = true;
    uint32_t frame_ = 0;            // update() calls so far
    int fadeLeft_ = 0;              // frames of screen fade-in left
    bool easing_ = false;           // the selection bar is still gliding (set while drawing)
    const void* easeKey_ = nullptr; // list the glide belongs to (its scroll variable)
    uint32_t easeFrame_ = 0;        // frame_ the glide was last advanced at
    int easeY_ = 0;                 // gliding bar position, in 1/16 pixel
    std::string dialogTitle_;
    std::string dialogText_;
    legality::Report dialogReport_;
    pokemon::Pokemon badgeFor_;                                 // see badgeSeverity()
    legality::Severity badgeSeverity_ = legality::Severity::Valid;
    bool badgeValid_ = false;

    int homeCursor_ = 0;
    int saveMenuCursor_ = 0;

    io::Settings settings_;      // active preferences (loaded in the constructor)
    io::Settings setSaved_;      // what settings.ini holds
    int setCursor_ = 0;
    Screen setReturn_ = Screen::Home;
    std::string setStatus_;

    std::vector<saves::SaveSummary> saveFiles_;  // Pokémon saves found on the SD card
    bool scanned_ = false;
    int browserCursor_ = 0;
    int browserScroll_ = 0;

    std::string savePath_;
    std::unique_ptr<saves::SaveFile> save_;
    int box_ = -1;  // -1 = party
    int slotCursor_ = 0;
    bool unsaved_ = false;
    bool menuOpen_ = false;
    std::vector<MenuAction> menuItems_;
    int menuCursor_ = 0;
    bool moving_ = false;  // a Pokémon was picked up with Move
    int moveBox_ = 0;      // where it comes from (-1 = party)
    int moveSlot_ = 0;

    EditState edit_;
    int editScroll_ = 0;

    std::vector<std::string> backups_;
    int backupCursor_ = 0;
    int backupScroll_ = 0;
    Screen backupsReturn_ = Screen::Home;

    PickerKind pickerKind_ = PickerKind::AddMenu;
    std::vector<std::string> pickerRows_;
    std::vector<uint16_t> pickerSpecies_;  // Species picker row -> species
    std::vector<uint8_t> pickerInitials_;  // alphabetical pickers: initial letter of each row (L/R jumps)
    int pickerCursor_ = 0;
    int pickerScroll_ = 0;
    int addMenuCursor_ = 0;
    std::vector<int> addMenuIds_;  // rows of the Add menu: 0 new, 1 Showdown, 2 event, 3 egg (by format)
    bool eggMode_ = false;  // the species list creates an egg
    std::vector<showdown::Set> showdownSets_;
    std::string eventError_;       // why a Colosseum/XD Pokemon could not be created
    std::vector<int> eventCards_;  // Event picker row -> card index (Gen IV or Gen V table, or a card file)
    std::vector<std::string> eventNotes_;  // Event picker row -> why it cannot be received ("" = it can)
    std::vector<std::unique_ptr<pokemon::cardfile::Card>> fileCards_;  // cards read from /PKTOOL/events/
    std::vector<std::string> showdownSources_;  // file name of each set
    pokemon::Pokemon preview_;
    int previewFor_ = -1;  // picker row the preview was built for
    uint32_t seedCounter_ = 0;

    bank::Bank bank_;
    bool bankLoaded_ = false;
    bool bankDirty_ = false;       // deposits not written to the SD card yet
    bool bankWithSave_ = false;    // opened from a save's box view
    Screen bankReturn_ = Screen::Home;
    int bankBox_ = 0;
    int bankCursor_ = 0;
    // Bank slots withdrawn into the save: removed from the Bank file once the
    // save has been written.
    std::vector<std::pair<int, int>> bankWithdrawn_;
    int bankHeld_ = -1;  // Bank slot picked up with X (box * 30 + slot), -1 if none
    BankMode bankMode_ = BankMode::Browse;
    std::vector<std::string> bankRows_;           // rows of the menu, sort and result lists
    std::vector<bank::Bank::Match> bankMatches_;  // search results (rows of Results)
    int bankListCursor_ = 0;
    int bankListScroll_ = 0;
    bool bankSortAll_ = false;
    bank::Bank::SearchFilter bankFilter_;  // the Search screen's text, shiny and format filters

    // On-screen keyboard.
    KeyboardTarget kbTarget_ = KeyboardTarget::Nickname;
    Screen kbReturn_ = Screen::Edit;
    std::string kbText_;
    size_t kbMax_ = 10;
    kbcjk::Kind kbKind_ = kbcjk::Kind::Latin;  // Japanese / Korean targets get extra pages
    int kbKey_ = 0;       // selected key (0-39 characters, 40+ special keys)
    int kbPage_ = 0;  // 0 upper case, 1 lower case, 2 symbols (incl. the male and female signs)

    // Trainer card editor.
    saves::TrainerData trOriginal_;
    saves::TrainerData trCurrent_;
    int trField_ = 0;
    int trBadge_ = 0;  // selected badge on the Badges row
    std::string trStatus_;

    // Bag editor: every pocket is read when the editor opens and written back
    // pocket by pocket on Apply.
    std::vector<std::vector<saves::BagSlot>> bagOriginal_;
    std::vector<std::vector<saves::BagSlot>> bagCurrent_;
    std::vector<int> bagCursors_;  // per pocket; index == slot count is the "Add item" row
    std::vector<int> bagScrolls_;
    int bagPocket_ = 0;
    std::string bagStatus_;
    std::vector<uint16_t> itemChoices_;  // item picker rows -> item IDs
    std::vector<std::string> itemRows_;
    int itemCursor_ = 0;
    int itemScroll_ = 0;

    // Ribbon editor: a copy of the slot's decrypted bytes, written back on Apply.
    struct RibbonState {
        uint8_t raw[pokemon::entity::kMaxPartySize] = {};
        uint8_t original[pokemon::entity::kMaxPartySize] = {};
        pokemon::Pokemon view;
        uint8_t format = 4;
        bool party = false;
        int box = 0;
        int slot = 0;
        int cursor = 0;
        int scroll = 0;
    };
    RibbonState rib_;

    std::vector<TouchZone> zones_;  // touch targets from the last draw
};

}  // namespace pktool::ui
