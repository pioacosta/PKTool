// PKTool — Nintendo DS entry point.
//
// Hardware glue only: sets up two 16-bit bitmap backgrounds, feeds buttons
// and touch input to ui::App and copies its canvases to VRAM. Everything
// else lives in ds/app (portable, also rendered on PC by tools/preview).

#include <fat.h>
#include <filesystem.h>
#include <malloc.h>
#include <nds.h>
#include <unistd.h>

#include <cstdlib>
#include <new>
#include <vector>

#include "ds/app/app.h"
#include "ds/app/ui_common.h"
#include "ds/gfx/canvas.h"

using namespace pktool;

namespace {

constexpr size_t kScreenBytes = gfx::kScreenWidth * gfx::kScreenHeight * sizeof(uint16_t);

// Canvases render into RAM and are copied to VRAM in one go, so the screen
// never shows a half-drawn frame.
uint16_t g_topBuffer[gfx::kScreenWidth * gfx::kScreenHeight] __attribute__((aligned(32)));
uint16_t g_bottomBuffer[gfx::kScreenWidth * gfx::kScreenHeight] __attribute__((aligned(32)));

// The screen fade-in is blended by the 2D engines (free) instead of the CPU:
// BG3 (the bitmap) over the backdrop, which holds the background colour.
void applyFade(int alpha) {  // alpha 0-32: weight of the background colour
    if (alpha <= 0) {
        REG_BLDCNT = 0;
        REG_BLDCNT_SUB = 0;
        return;
    }
    const int evb = alpha / 2;  // the hardware weights go 0-16
    const uint16_t weights = static_cast<uint16_t>((16 - evb) | (evb << 8));
    REG_BLDCNT = BLEND_ALPHA | BLEND_SRC_BG3 | BLEND_DST_BACKDROP;
    REG_BLDCNT_SUB = BLEND_ALPHA | BLEND_SRC_BG3 | BLEND_DST_BACKDROP;
    REG_BLDALPHA = weights;
    REG_BLDALPHA_SUB = weights;
}

void present(uint16_t* topVram, uint16_t* bottomVram) {
    DC_FlushRange(g_topBuffer, kScreenBytes);
    DC_FlushRange(g_bottomBuffer, kScreenBytes);
    dmaCopy(g_topBuffer, topVram, kScreenBytes);
    dmaCopy(g_bottomBuffer, bottomVram, kScreenBytes);
}

// Free heap: what malloc has handed back plus what it can still grow into.
size_t freeMemory() {
    struct mallinfo info = mallinfo();
    return static_cast<size_t>(info.fordblks) + static_cast<size_t>(getHeapLimit() - getHeapEnd());
}

// Out of memory: a failed allocation used to abort the program in the middle
// of whatever it was doing, possibly a write, with the SD card's FAT cache not
// flushed. Flush it (unmounting writes the cache back), tell the user and
// leave once a button is pressed.
void onOutOfMemory() {
    fatUnmount("sd");
    fatUnmount("fat");
    consoleDemoInit();
    iprintf("PKTool ran out of memory.\n\nNothing more was written.\nPress any button to exit.\n");
    for (;;) {
        swiWaitForVBlank();
        scanKeys();
        if (keysDown()) break;
    }
    std::exit(1);
}

}  // namespace

int main() {
    std::set_new_handler(onOutOfMemory);
    // The main engine drives the top screen, the sub engine the bottom one.
    videoSetMode(MODE_5_2D);
    videoSetModeSub(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);
    int topBg = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    int bottomBg = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    uint16_t* topVram = bgGetGfxPtr(topBg);
    uint16_t* bottomVram = bgGetGfxPtr(bottomBg);

    gfx::Canvas top(g_topBuffer);
    gfx::Canvas bottom(g_bottomBuffer);

    // Boot splash while the SD card is mounted.
    top.clear(gfx::rgb(12, 16, 30));
    bottom.clear(gfx::rgb(12, 16, 30));
    top.text(gfx::kFontLarge, 0, 80, gfx::kScreenWidth, gfx::Align::Center, "PKTool", gfx::rgb(238, 244, 255));
    top.text(gfx::kFontSmall, 0, 100, gfx::kScreenWidth, gfx::Align::Center, "Mounting SD card...",
             gfx::rgb(140, 160, 206));
    present(topVram, bottomVram);

    ui::Config config;
    config.storageReady = fatInitDefault();
    config.freeMemory = freeMemory;
    // NitroFS gives read access to the sprite pack packed into PKTool.nds.
    // It may change the current device, so restore the SD card afterwards:
    // "/PKTOOL/..." paths (backups, saves) must keep pointing at the SD.
    char sdRoot[64] = "/";
    if (getcwd(sdRoot, sizeof(sdRoot)) == nullptr) {
        sdRoot[0] = '/';
        sdRoot[1] = '\0';
    }
    bool nitroReady = nitroFSInit(nullptr);
    chdir(sdRoot);
    if (nitroReady) {
        config.spriteDirs.push_back("nitro:/sprites");
        config.assetDirs.push_back("nitro:/");  // fonts/cjk.bin, lang/*.bin
    }
    config.spriteDirs.push_back("/PKTOOL/sprites");  // fallback: loaders without NitroFS
    config.assetDirs.push_back("/PKTOOL");
    config.hardwareFade = true;
    BG_PALETTE[0] = ui::detail::kBackground & 0x7FFF;  // backdrop the fade blends towards
    BG_PALETTE_SUB[0] = ui::detail::kBackground & 0x7FFF;
    ui::App app(config);

    keysSetRepeat(15, 3);  // held D-pad: first repeat after 1/4 s, then 20 per second
    while (pmMainLoop()) {
        if (app.needsRedraw()) {
            app.draw(top, bottom);
            swiWaitForVBlank();
            present(topVram, bottomVram);
        } else {
            swiWaitForVBlank();
        }
        applyFade(app.fadeAlpha());

        scanKeys();
        ui::Input input;
        input.pressed = keysDown();
        input.repeated = keysDownRepeat();
        if (input.pressed & KEY_TOUCH) {
            touchPosition touch;
            touchRead(&touch);
            input.touched = true;
            input.touchX = touch.px;
            input.touchY = touch.py;
        }
        input.pressed &= 0x0FFF;  // buttons only (KEY_TOUCH and lid bits are handled above)
        input.repeated &= 0x0FFF;
        if (!app.update(input)) {
            break;
        }
    }
    return 0;
}
