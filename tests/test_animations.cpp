// UI animations: screen fade-in, gliding selection bar and the Settings toggle.
// They advance by update() calls only, so a run is reproducible, and they can
// be switched off (Settings > Animations) to get the settled screen at once.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/io/file_io.h"
#include "core/io/settings.h"
#include "ds/app/app.h"
#include "ds/gfx/canvas.h"
#include "tests/test.h"

using namespace pktool;

namespace {

struct Shot {
    std::vector<uint16_t> top = std::vector<uint16_t>(gfx::kScreenWidth * gfx::kScreenHeight);
    std::vector<uint16_t> bottom = std::vector<uint16_t>(gfx::kScreenWidth * gfx::kScreenHeight);
    bool operator==(const Shot& o) const { return top == o.top && bottom == o.bottom; }
};

void render(ui::App& app, Shot& shot) {
    gfx::Canvas top(shot.top.data());
    gfx::Canvas bottom(shot.bottom.data());
    app.draw(top, bottom);
}

// One frame with `keys` pressed (0 = idle), then a draw when the app asks for one.
void frame(ui::App& app, Shot& shot, uint32_t keys = 0) {
    ui::Input in;
    in.pressed = keys;
    in.repeated = keys;
    app.update(in);
    if (app.needsRedraw()) render(app, shot);
}

void settle(ui::App& app, Shot& shot, int frames = 12) {
    for (int i = 0; i < frames; ++i) frame(app, shot);
}

ui::Config makeConfig(bool animations, const std::string& dir) {
    ui::Config config;
    config.scanRoot = dir;
    config.dataDir = io::joinPath(dir, "PKTOOL");
    if (!animations) {
        // Settings come from the file the app loads at start-up.
        io::makeDirectories(config.dataDir);
        std::string text = "animations=off\n";
        CHECK(io::safeWriteFile(io::joinPath(config.dataDir, "settings.ini"),
                                std::vector<uint8_t>(text.begin(), text.end())) == io::WriteResult::Ok);
    }
    return config;
}

std::string tempDir() {
    char pattern[] = "/tmp/pktool-anim-XXXXXX";
    return mkdtemp(pattern);
}

int differingPixels(const Shot& a, const Shot& b) {
    int count = 0;
    for (size_t i = 0; i < a.bottom.size(); ++i) count += a.bottom[i] != b.bottom[i] ? 1 : 0;
    return count;
}

}  // namespace

TEST(animations_setting_parses_prints_and_defaults_to_on) {
    io::Settings s;
    CHECK(s.animations);
    CHECK(io::settingsToText(s).find("animations=on\n") != std::string::npos);
    io::parseSettings("animations=off", s);
    CHECK(!s.animations);
    CHECK(io::settingsToText(s).find("animations=off\n") != std::string::npos);
    io::parseSettings("ANIMATIONS = On", s);
    CHECK(s.animations);
    io::parseSettings("animations=0", s);
    CHECK(!s.animations);
    io::parseSettings("animations=maybe", s);  // unknown value: unchanged
    CHECK(!s.animations);
    io::Settings other = s;
    CHECK(other == s);
    other.animations = true;
    CHECK(other != s);
}

TEST(animations_settings_row_toggles_and_is_saved) {
    std::string dir = tempDir();
    ui::Config config = makeConfig(true, dir);
    std::string file = io::joinPath(config.dataDir, "settings.ini");
    Shot shot;
    {
        ui::App app(config);
        CHECK(app.animationsEnabled());
        frame(app, shot, ui::kKeyDown);
        frame(app, shot, ui::kKeyRight);
        frame(app, shot, ui::kKeyA);  // Settings
        CHECK(std::string(app.screenName()) == "Settings");
        frame(app, shot, ui::kKeyDown);
        frame(app, shot, ui::kKeyDown);   // Names
        frame(app, shot, ui::kKeyDown);   // Animations
        frame(app, shot, ui::kKeyRight);  // Off
        CHECK(!app.animationsEnabled());
        frame(app, shot, ui::kKeyA);      // A toggles too: On
        CHECK(app.animationsEnabled());
        frame(app, shot, ui::kKeyLeft);   // Off
        CHECK(!io::fileExists(file));
        frame(app, shot, ui::kKeyB);
        std::vector<uint8_t> bytes;
        CHECK(io::readFile(file, bytes));
        CHECK(std::string(bytes.begin(), bytes.end()).find("animations=off\n") != std::string::npos);
    }
    ui::App again(config);  // loaded at the next start
    CHECK(!again.animationsEnabled());
}

TEST(animations_screen_fades_in_and_settles_on_the_normal_screen) {
    std::string dir = tempDir();
    Shot onShot, offShot, settled;
    ui::App on(makeConfig(true, dir));
    ui::App off(makeConfig(false, dir));
    CHECK(on.animationsEnabled());
    CHECK(!off.animationsEnabled());

    frame(on, onShot, ui::kKeyDown);
    frame(on, onShot, ui::kKeyA);   // Bank
    frame(off, offShot, ui::kKeyDown);
    frame(off, offShot, ui::kKeyA);
    CHECK(std::string(on.screenName()) == "Bank");
    CHECK(on.animating());
    CHECK(!off.animating());
    CHECK(differingPixels(onShot, offShot) > 10000);  // the first frame is mostly faded
    // The fade lasts a few frames, then the screen is exactly the normal one.
    int frames = 0;
    while (on.animating() && frames < 20) {
        frame(on, onShot);
        ++frames;
    }
    CHECK(frames >= 3 && frames <= 8);
    CHECK(!on.animating());
    frame(on, onShot);
    settle(off, offShot);
    render(on, onShot);
    render(off, offShot);
    CHECK(onShot == offShot);
}

TEST(animations_are_deterministic_for_the_same_inputs) {
    std::string dir = tempDir();
    Shot a, b;
    ui::App one(makeConfig(true, dir));
    ui::App two(makeConfig(true, dir));
    const uint32_t keys[] = {ui::kKeyDown, ui::kKeyA, 0, 0, ui::kKeyY, ui::kKeyDown, 0, ui::kKeyDown, 0, 0, 0, 0, ui::kKeyB};
    for (uint32_t k : keys) {
        frame(one, a, k);
        frame(two, b, k);
        CHECK(a == b);
        CHECK_EQ(one.frameCount(), two.frameCount());
    }
}

TEST(animations_selection_bar_glides_to_the_new_row) {
    std::string dir = tempDir();
    Shot onShot, offShot;
    ui::App on(makeConfig(true, dir));
    ui::App off(makeConfig(false, dir));
    for (ui::App* app : {&on, &off}) {
        Shot& shot = app == &on ? onShot : offShot;
        frame(*app, shot, ui::kKeyDown);
        frame(*app, shot, ui::kKeyA);  // Bank
        settle(*app, shot);
        frame(*app, shot, ui::kKeyY);  // the Bank menu list
        settle(*app, shot);
    }
    CHECK(!on.animating());
    CHECK(onShot == offShot);

    frame(on, onShot, ui::kKeyDown);
    frame(off, offShot, ui::kKeyDown);
    CHECK(on.animating());           // the bar is on its way
    CHECK(!off.animating());         // off: it jumped
    CHECK(!(onShot == offShot));
    Shot previous = onShot;
    int moved = 0;
    for (int i = 0; i < 12 && on.animating(); ++i) {
        frame(on, onShot);
        if (!(previous == onShot)) ++moved;
        previous = onShot;
    }
    CHECK(moved >= 1);
    CHECK(!on.animating());
    frame(on, onShot);
    render(on, onShot);
    render(off, offShot);
    CHECK(onShot == offShot);  // it ends exactly where the unanimated bar is
}

TEST(animations_cost_of_a_fade_frame_is_small) {
    // Not a pass/fail on speed (the host is not a DS): prints the cost of the extra
    // blend per screen so regressions are visible, and checks it stays a small factor.
    Shot shot;
    std::string dir = tempDir();
    ui::App app(makeConfig(false, dir));
    gfx::Canvas top(shot.top.data());
    gfx::Canvas bottom(shot.bottom.data());
    app.draw(top, bottom);
    using Clock = std::chrono::steady_clock;
    const int runs = 200;
    auto t0 = Clock::now();
    for (int i = 0; i < runs; ++i) app.draw(top, bottom);
    auto t1 = Clock::now();
    for (int i = 0; i < runs; ++i) {
        app.draw(top, bottom);
        top.blendRect(0, 0, gfx::kScreenWidth, gfx::kScreenHeight, gfx::rgb(0, 0, 0), 16);
        bottom.blendRect(0, 0, gfx::kScreenWidth, gfx::kScreenHeight, gfx::rgb(0, 0, 0), 16);
    }
    auto t2 = Clock::now();
    double plain = std::chrono::duration<double, std::micro>(t1 - t0).count() / runs;
    double faded = std::chrono::duration<double, std::micro>(t2 - t1).count() / runs;
    std::printf("  draw %.0f us, draw + fade of both screens %.0f us (host)\n", plain, faded);
    CHECK(faded < plain * 8 + 2000);
}

TEST(animations_hardware_fade_skips_the_software_blend) {
    // On the DS the 2D engines blend the fade (Config::hardwareFade): the frames
    // are drawn settled, fadeAlpha() gives the weight, and fade frames do not
    // force a redraw.
    std::string dir = tempDir();
    ui::Config hwConfig = makeConfig(true, dir);
    hwConfig.hardwareFade = true;
    ui::App hw(hwConfig);
    ui::App off(makeConfig(false, dir));
    Shot hwShot, offShot;
    frame(hw, hwShot, ui::kKeyDown);
    frame(hw, hwShot, ui::kKeyA);  // Bank: the fade starts
    frame(off, offShot, ui::kKeyDown);
    frame(off, offShot, ui::kKeyA);
    CHECK(hw.fadeAlpha() > 0);
    CHECK(hwShot == offShot);  // no software blend in the drawn frame
    int last = hw.fadeAlpha();
    int redraws = 0;
    while (hw.fadeAlpha() > 0) {
        ui::Input idle;
        hw.update(idle);
        CHECK(hw.fadeAlpha() < last);
        last = hw.fadeAlpha();
        if (hw.needsRedraw() && hw.fadeAlpha() > 0) ++redraws;
        if (hw.needsRedraw()) render(hw, hwShot);
    }
    CHECK_EQ(redraws, 0);
}
