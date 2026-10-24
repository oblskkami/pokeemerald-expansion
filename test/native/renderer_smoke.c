#include <stdio.h>
#include <string.h>

#include "constants/rgb.h"
#include "gba/types.h"
#include "gba/gba.h"
#include "platform/framedraw.h"

#define FRAME_PIXELS (DISPLAY_WIDTH * DISPLAY_HEIGHT)

static int sFailures;
static int sVCountInterrupts;
static int sHBlankInterrupts;

#define CHECK(expression)                                                     \
    do                                                                        \
    {                                                                         \
        if (!(expression))                                                    \
        {                                                                     \
            fprintf(stderr, "renderer smoke check failed: %s:%d: %s\n",     \
                    __FILE__, __LINE__, #expression);                         \
            sFailures++;                                                      \
        }                                                                     \
    } while (0)

static void CountVCountInterrupt(void)
{
    sVCountInterrupts++;
}

static void CountHBlankInterrupt(void)
{
    sHBlankInterrupts++;
}

static void NoopInterrupt(void)
{}

void (*gIntrTable[])(void) =
{
    CountVCountInterrupt,
    NoopInterrupt,
    NoopInterrupt,
    CountHBlankInterrupt,
    NoopInterrupt,
};

static void ResetVideoState(void)
{
    memset(REG_BASE, 0, 0x400);
    memset(PLTT, 0, PLTT_SIZE);
    memset(VRAM_, 0, VRAM_SIZE);
    memset(OAM, 0, OAM_SIZE);
    sVCountInterrupts = 0;
    sHBlankInterrupts = 0;
}

static void FillTextBackground(u16 color)
{
    u16 *palette = (u16 *)PLTT;
    u16 *tilemap = (u16 *)BG_SCREEN_ADDR(31);
    u8 *tile = (u8 *)BG_CHAR_ADDR(0) + TILE_SIZE_4BPP;

    palette[1] = color;
    memset(tile, 0x11, TILE_SIZE_4BPP);
    for (int i = 0; i < 32 * 32; i++)
        tilemap[i] = 1;

    REG_BG0CNT = BGCNT_CHARBASE(0) | BGCNT_SCREENBASE(31) | BGCNT_16COLOR;
    REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_BG0_ON;
}

static void TestBackdrop(void)
{
    static u16 frame[FRAME_PIXELS];
    const u16 backdrop = 0x001F;

    ResetVideoState();
    *(u16 *)PLTT = backdrop;
    DrawFrame(frame);

    for (int i = 0; i < FRAME_PIXELS; i++)
        CHECK(frame[i] == backdrop);
}

static void TestTextBackground(void)
{
    static u16 frame[FRAME_PIXELS];
    const u16 green = 0x03E0;

    ResetVideoState();
    FillTextBackground(green);
    DrawFrame(frame);

    CHECK((frame[0] & 0x7FFF) == green);
    CHECK((frame[80 * DISPLAY_WIDTH + 120] & 0x7FFF) == green);
    CHECK((frame[FRAME_PIXELS - 1] & 0x7FFF) == green);
}

static void TestEightBitTransparency(void)
{
    static u16 frame[FRAME_PIXELS];
    const u16 backdrop = 0x7C00;
    u16 *tilemap;

    ResetVideoState();
    *(u16 *)PLTT = backdrop;
    tilemap = (u16 *)BG_SCREEN_ADDR(31);
    for (int i = 0; i < 32 * 32; i++)
        tilemap[i] = 1;

    REG_BG0CNT = BGCNT_CHARBASE(0) | BGCNT_SCREENBASE(31) | BGCNT_256COLOR;
    REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_BG0_ON;
    DrawFrame(frame);

    CHECK(frame[0] == backdrop);
    CHECK(frame[FRAME_PIXELS - 1] == backdrop);
}

static void TestWindowOneRegisters(void)
{
    static u16 frame[FRAME_PIXELS];
    const u16 backdrop = 0x001F;
    const u16 green = 0x03E0;

    ResetVideoState();
    *(u16 *)PLTT = backdrop;
    FillTextBackground(green);
    REG_DISPCNT |= DISPCNT_WIN1_ON;
    REG_WIN1H = (10 << 8) | 20;
    REG_WIN1V = (5 << 8) | 15;
    REG_WININ = 0;
    REG_WINOUT = 1 << 0;
    DrawFrame(frame);

    CHECK((frame[0] & 0x7FFF) == green);
    CHECK((frame[8 * DISPLAY_WIDTH + 12] & 0x7FFF) == backdrop);
}

static void TestScanlineInterrupts(void)
{
    static u16 frame[FRAME_PIXELS];

    ResetVideoState();
    REG_DISPSTAT = (42 << 8) | DISPSTAT_VCOUNT_INTR | DISPSTAT_HBLANK_INTR;
    REG_IME = 1;
    REG_IE = INTR_FLAG_VCOUNT | INTR_FLAG_HBLANK;
    DrawFrame(frame);

    CHECK(sVCountInterrupts == 1);
    CHECK(sHBlankInterrupts == DISPLAY_HEIGHT);
}

static void TestForcedBlank(void)
{
    static u16 frame[FRAME_PIXELS];

    ResetVideoState();
    *(u16 *)PLTT = RGB_BLUE;
    FillTextBackground(RGB_GREEN);
    REG_DISPSTAT = (42 << 8) | DISPSTAT_VCOUNT_INTR | DISPSTAT_HBLANK_INTR;
    REG_IME = 1;
    REG_IE = INTR_FLAG_VCOUNT | INTR_FLAG_HBLANK;
    REG_DISPCNT |= DISPCNT_FORCED_BLANK;
    DrawFrame(frame);

    for (int i = 0; i < FRAME_PIXELS; i++)
        CHECK(frame[i] == RGB_WHITE);
    CHECK(sVCountInterrupts == 1);
    CHECK(sHBlankInterrupts == DISPLAY_HEIGHT);
}

static void TestInterruptGating(void)
{
    static u16 frame[FRAME_PIXELS];

    ResetVideoState();
    REG_DISPSTAT = (42 << 8) | DISPSTAT_VCOUNT_INTR | DISPSTAT_HBLANK_INTR;
    REG_IE = INTR_FLAG_VCOUNT | INTR_FLAG_HBLANK;
    REG_IME = 0;
    DrawFrame(frame);
    CHECK(sVCountInterrupts == 0);
    CHECK(sHBlankInterrupts == 0);

    REG_IME = 1;
    REG_IE = 0;
    DrawFrame(frame);
    CHECK(sVCountInterrupts == 0);
    CHECK(sHBlankInterrupts == 0);

    REG_IE = INTR_FLAG_VCOUNT | INTR_FLAG_HBLANK;
    DrawFrame(frame);
    CHECK(sVCountInterrupts == 1);
    CHECK(sHBlankInterrupts == DISPLAY_HEIGHT);
}

static void TestProhibitedObjectMode(void)
{
    static u16 frame[FRAME_PIXELS];
    struct OamData *oam = (struct OamData *)OAM;

    ResetVideoState();
    *(u16 *)PLTT = RGB_BLUE;
    ((u8 *)OBJ_VRAM0)[0] = 0x11;
    ((u16 *)PLTT)[0x101] = RGB_RED;
    for (int i = 0; i < 128; i++)
        oam[i].y = DISPLAY_HEIGHT;
    oam[0].objMode = 3;
    oam[0].x = 0;
    oam[0].y = 0;
    oam[0].shape = ST_OAM_SQUARE;
    oam[0].size = 0;
    oam[0].tileNum = 0;
    oam[0].paletteNum = 0;
    REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_OBJ_1D_MAP | DISPCNT_OBJ_ON;
    DrawFrame(frame);

    for (int i = 0; i < FRAME_PIXELS; i++)
        CHECK(frame[i] == RGB_BLUE);
}

static void TestVramTileBounds(void)
{
    static u16 frame[FRAME_PIXELS];
    u16 *tilemap;
    struct OamData *oam = (struct OamData *)OAM;

    ResetVideoState();
    *(u16 *)PLTT = RGB_BLUE;
    tilemap = (u16 *)BG_SCREEN_ADDR(31);
    tilemap[0] = 1023;
    REG_BG0CNT = BGCNT_CHARBASE(3) | BGCNT_SCREENBASE(31) | BGCNT_256COLOR;
    REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_BG0_ON;
    DrawFrame(frame);
    CHECK(frame[0] == RGB_BLUE);

    ResetVideoState();
    *(u16 *)PLTT = RGB_BLUE;
    for (int i = 0; i < 128; i++)
        oam[i].y = DISPLAY_HEIGHT;
    oam[0].x = 0;
    oam[0].y = 0;
    oam[0].bpp = ST_OAM_8BPP;
    oam[0].tileNum = 1023;
    oam[0].shape = ST_OAM_SQUARE;
    oam[0].size = 0;
    REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_OBJ_1D_MAP | DISPCNT_OBJ_ON;
    DrawFrame(frame);
    CHECK(frame[0] == RGB_BLUE);
}

static void TestAffineModeTwo(void)
{
    static u16 frame[FRAME_PIXELS];
    u8 *bg2Map;
    u8 *bg3Map;
    u8 *bg2Tile;
    u8 *bg3Tile;

    ResetVideoState();
    ((u16 *)PLTT)[1] = RGB_RED;
    ((u16 *)PLTT)[2] = RGB_GREEN;
    bg2Map = (u8 *)BG_SCREEN_ADDR(30);
    bg3Map = (u8 *)BG_SCREEN_ADDR(31);
    bg2Tile = (u8 *)BG_CHAR_ADDR(0) + TILE_SIZE_8BPP;
    bg3Tile = (u8 *)BG_CHAR_ADDR(1) + TILE_SIZE_8BPP;
    bg2Map[0] = 1;
    bg3Map[0] = 1;
    memset(bg2Tile, 1, TILE_SIZE_8BPP);
    memset(bg3Tile, 2, TILE_SIZE_8BPP);

    REG_BG2CNT = BGCNT_PRIORITY(1) | BGCNT_CHARBASE(0) | BGCNT_SCREENBASE(30);
    REG_BG3CNT = BGCNT_PRIORITY(0) | BGCNT_CHARBASE(1) | BGCNT_SCREENBASE(31);
    REG_BG2PA = 0x100;
    REG_BG2PD = 0x100;
    REG_BG3PA = 0x100;
    REG_BG3PD = 0x100;
    REG_DISPCNT = DISPCNT_MODE_2 | DISPCNT_BG2_ON | DISPCNT_BG3_ON;
    DrawFrame(frame);

    CHECK((frame[0] & 0x7FFF) == RGB_GREEN);
    CHECK((frame[7 * DISPLAY_WIDTH + 7] & 0x7FFF) == RGB_GREEN);
    CHECK(frame[8] == 0);

    REG_DISPCNT = DISPCNT_MODE_2 | DISPCNT_BG2_ON;
    DrawFrame(frame);
    CHECK((frame[0] & 0x7FFF) == RGB_RED);
}

int main(void)
{
    TestBackdrop();
    TestTextBackground();
    TestEightBitTransparency();
    TestWindowOneRegisters();
    TestScanlineInterrupts();
    TestForcedBlank();
    TestInterruptGating();
    TestProhibitedObjectMode();
    TestVramTileBounds();
    TestAffineModeTwo();

    if (sFailures != 0)
        return 1;

    puts("native renderer smoke checks passed");
    return 0;
}
