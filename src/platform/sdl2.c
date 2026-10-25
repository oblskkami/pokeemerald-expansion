#ifdef PLATFORM_SDL2

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL2/SDL.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#include <windows.h>
#undef TRANSPARENT
#endif

#include "gba/types.h"
#include "gba/gba.h"
#include "gba/m4a_internal.h"
#include "constants/characters.h"
#include "fonts.h"
#include "platform.h"
#include "platform/dma.h"
#include "platform/framedraw.h"
#include "palette.h"
#include "global.h"
#include "fieldmap.h"
#include "bg.h"
#include "scanline_effect.h"
#include "sprite.h"
#include "field_camera.h"
#include "malloc.h"

#ifdef _WIN32
// winuser.h 也声明了一个 ScrollWindow（HWND 版），和 window.h 的
// void ScrollWindow(u32,u8,u8,u8) 撞名。这里在包含 window.h 前后临时改名，
// 我们只在 dump 里读 gWindows[]，并不调用它。
#define ScrollWindow PokeemeraldHostScrollWindow
#endif
#include "window.h"
#ifdef _WIN32
#undef ScrollWindow
#endif

extern void (*gIntrTable[])(void);

// 见 src/overworld.c：overworld 自管的 1/2/3 层 tilemap 缓冲，用于判断指针是否悬空。
extern u16 *gOverworldTilemapBuffer_Bg1;
extern u16 *gOverworldTilemapBuffer_Bg2;
extern u16 *gOverworldTilemapBuffer_Bg3;

// 见 src/platform/gba_easy_draw.c：按行记录每个像素由哪一层、窗口掩码与窗口寄存器。
extern u8 gHostProbeWinners[DISPLAY_WIDTH];
extern u8 gHostProbeWinMask[DISPLAY_WIDTH];
extern int gHostProbeRow;
extern int gHostProbeValid;
extern u16 gHostProbeRegs[8];
// 全帧层归属映射（0..3 = BG，0x10|prnum = 精灵，0xFF = 背景色）。
extern u8 *gHostWinnerMap;

static volatile int sFrameDumpRequest;
static volatile int sFrameDumpImmediate;
static const char *sFrameDumpReason = "unknown";
static u32 sPresentFrameCount;
static u32 sFrameDumpRequestFrame;
static SDL_Window *sWindow;
static SDL_Renderer *sRenderer;
static SDL_Texture *sTexture;
static SDL_GameController *sController;
static SDL_sem *sVBlankSemaphore;
static SDL_mutex *sInputMutex;
static SDL_AudioDeviceID sAudioDevice;
static int sAudioRate;
static bool sAudioUnavailable;
static SDL_atomic_t sFramePending;
static u64 sCycleCountStart;
static SDL_atomic_t sQuitRequested;
static u16 sKeyboardKeys;
static u16 sControllerKeys;
static u16 sControllerAxisKeys;
static bool sKeyboardFocused;
static bool sInitialized;
static u32 sSpeedMultiplier = 1;
static bool sFastForwardHeld;

// Host frame diagnostics. The Windows build keeps a console attached, so these
// lines are the cheapest way to tell apart a stalled presenter from a stalled
// game thread when a report only says "it stutters".
static Uint64 sDiagLastPresentEnd;
static Uint64 sDiagLogicTicks;
static Uint64 sDiagWaitTicks;
static Uint64 sDiagPresentTicks;
static Uint32 sDiagFrameCount;
static Uint32 sDiagMaxWaitMs;
static Uint32 sDiagMaxPresentMs;
static Uint32 sDiagMaxLogicMs;

// Every diagnostic line below costs a console write, which is far from free on
// Windows. Keep them off unless PE_TRACE is set so a distributed build stays
// quiet; crash.log still gets written by the unhandled-exception filter.
static int sHostTrace = -1;
// 自动帧快照（地图初始化 / 野外异常空带 / Navigator）默认关闭，
// 只保留 P / F 键的手动快照；需要抓自动快照时设 PE_AUTO_DUMP=1（PE_DUMP_POKENAV=1 亦可）。
static int sAutoDump = -1;
static int AutoDumpEnabled(void)
{
    if (sAutoDump < 0)
        sAutoDump = (getenv("PE_AUTO_DUMP") != NULL || getenv("PE_DUMP_POKENAV") != NULL);
    return sAutoDump;
}
static u32 sSlowPumpLogs;

static bool HostTraceEnabled(void)
{
    if (sHostTrace < 0)
        sHostTrace = getenv("PE_TRACE") != NULL;
    return sHostTrace != 0;
}

// 进程累计 CPU 时间（微秒）。配合 dtUs 一起看：dt 尖峰时 cpuUs 几乎不涨，
// 说明那一刻进程没被调度（被系统/别的线程饿了），不是我们的代码在算。
static Uint64 DiagProcessCpuUs(void)
{
#ifdef _WIN32
    FILETIME creation;
    FILETIME exitTime;
    FILETIME kernelTime;
    FILETIME userTime;
    ULARGE_INTEGER kernel;
    ULARGE_INTEGER user;

    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exitTime, &kernelTime, &userTime))
        return 0;
    kernel.LowPart = kernelTime.dwLowDateTime;
    kernel.HighPart = (LONG)kernelTime.dwHighDateTime;
    user.LowPart = userTime.dwLowDateTime;
    user.HighPart = (LONG)userTime.dwHighDateTime;
    return (Uint64)((kernel.QuadPart + user.QuadPart) / 10);
#else
    return 0;
#endif
}

static Uint32 DiagTicksToMs(Uint64 ticks)
{
    Uint64 frequency = SDL_GetPerformanceFrequency();

    if (frequency == 0)
        return 0;
    return (Uint32)((ticks * 1000) / frequency);
}

#define CONTROLLER_DEAD_ZONE 16000
#define HOST_SPEED_MAX_MULTIPLIER 9

static void LockInputState(void)
{
    if (sInputMutex != NULL)
        SDL_LockMutex(sInputMutex);
}

static void UnlockInputState(void)
{
    if (sInputMutex != NULL)
        SDL_UnlockMutex(sInputMutex);
}

static u16 ScancodeToButton(SDL_Scancode scancode)
{
    switch (scancode)
    {
    case SDL_SCANCODE_Z:
        return A_BUTTON;
    case SDL_SCANCODE_X:
        return B_BUTTON;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        return START_BUTTON;
    case SDL_SCANCODE_BACKSLASH:
        return SELECT_BUTTON;
    case SDL_SCANCODE_A:
        return L_BUTTON;
    case SDL_SCANCODE_S:
        return R_BUTTON;
    case SDL_SCANCODE_UP:
        return DPAD_UP;
    case SDL_SCANCODE_DOWN:
        return DPAD_DOWN;
    case SDL_SCANCODE_LEFT:
        return DPAD_LEFT;
    case SDL_SCANCODE_RIGHT:
        return DPAD_RIGHT;
    default:
        return 0;
    }
}

static u16 KeycodeToButton(SDL_Keycode keycode)
{
    switch (keycode)
    {
    case SDLK_z:         return A_BUTTON;
    case SDLK_x:         return B_BUTTON;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:  return START_BUTTON;
    case SDLK_BACKSLASH: return SELECT_BUTTON;
    case SDLK_a:         return L_BUTTON;
    case SDLK_s:         return R_BUTTON;
    case SDLK_UP:        return DPAD_UP;
    case SDLK_DOWN:      return DPAD_DOWN;
    case SDLK_LEFT:      return DPAD_LEFT;
    case SDLK_RIGHT:     return DPAD_RIGHT;
    default:             return 0;
    }
}

static bool IsSpeedCycleKey(const SDL_KeyboardEvent *event)
{
    return event->keysym.sym == SDLK_PLUS
        || event->keysym.sym == SDLK_KP_PLUS
        || event->keysym.scancode == SDL_SCANCODE_KP_PLUS
        || (event->keysym.sym == SDLK_EQUALS
         && (event->keysym.mod & KMOD_SHIFT) != 0);
}

static void CycleSpeedMultiplier(void)
{
    if (++sSpeedMultiplier > HOST_SPEED_MAX_MULTIPLIER)
        sSpeedMultiplier = 1;
}

static bool KeyboardFastForwardState(void)
{
    const Uint8 *state = SDL_GetKeyboardState(NULL);
    bool held = state != NULL && state[SDL_SCANCODE_SPACE] != 0;

#ifdef _WIN32
    held = held || (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
#endif

    return held;
}

static u16 KeyboardStateToButtons(void)
{
    const Uint8 *state = SDL_GetKeyboardState(NULL);
    u16 keys = 0;

    if (state[SDL_SCANCODE_Z])         keys |= A_BUTTON;
    if (state[SDL_SCANCODE_X])         keys |= B_BUTTON;
    if (state[SDL_SCANCODE_RETURN]
     || state[SDL_SCANCODE_KP_ENTER])  keys |= START_BUTTON;
    if (state[SDL_SCANCODE_BACKSLASH]) keys |= SELECT_BUTTON;
    if (state[SDL_SCANCODE_A])         keys |= L_BUTTON;
    if (state[SDL_SCANCODE_S])         keys |= R_BUTTON;
    if (state[SDL_SCANCODE_UP])        keys |= DPAD_UP;
    if (state[SDL_SCANCODE_DOWN])      keys |= DPAD_DOWN;
    if (state[SDL_SCANCODE_LEFT])      keys |= DPAD_LEFT;
    if (state[SDL_SCANCODE_RIGHT])     keys |= DPAD_RIGHT;

#ifdef _WIN32
    // Some Windows keyboard layouts do not provide a useful SDL scancode.
    // Poll the virtual-key state as a final fallback for the game controls.
    if (GetAsyncKeyState('Z') & 0x8000)      keys |= A_BUTTON;
    if (GetAsyncKeyState('X') & 0x8000)      keys |= B_BUTTON;
    if (GetAsyncKeyState('A') & 0x8000)      keys |= L_BUTTON;
    if (GetAsyncKeyState('S') & 0x8000)      keys |= R_BUTTON;
    if (GetAsyncKeyState(VK_RETURN) & 0x8000) keys |= START_BUTTON;
    if (GetAsyncKeyState(VK_BACK) & 0x8000)   keys |= B_BUTTON;
    if (GetAsyncKeyState(VK_UP) & 0x8000)     keys |= DPAD_UP;
    if (GetAsyncKeyState(VK_DOWN) & 0x8000)   keys |= DPAD_DOWN;
    if (GetAsyncKeyState(VK_LEFT) & 0x8000)   keys |= DPAD_LEFT;
    if (GetAsyncKeyState(VK_RIGHT) & 0x8000)  keys |= DPAD_RIGHT;
#endif

    return keys;
}

static u16 ControllerButtonToGba(Uint8 button)
{
    switch (button)
    {
    case SDL_CONTROLLER_BUTTON_A:
        return A_BUTTON;
    case SDL_CONTROLLER_BUTTON_B:
        return B_BUTTON;
    case SDL_CONTROLLER_BUTTON_BACK:
        return SELECT_BUTTON;
    case SDL_CONTROLLER_BUTTON_START:
        return START_BUTTON;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
        return L_BUTTON;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
        return R_BUTTON;
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
        return DPAD_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        return DPAD_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        return DPAD_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        return DPAD_RIGHT;
    default:
        return 0;
    }
}

static void OpenController(int deviceIndex)
{
    if (sController == NULL && SDL_IsGameController(deviceIndex))
        sController = SDL_GameControllerOpen(deviceIndex);
}

static void OpenFirstController(void)
{
    for (int i = 0; i < SDL_NumJoysticks() && sController == NULL; i++)
        OpenController(i);
}

static bool ControllerEventMatches(SDL_JoystickID instanceId)
{
    if (sController == NULL)
        return true;
    return SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(sController)) == instanceId;
}

static void SetControllerAxis(Uint8 axis, Sint16 value)
{
    u16 negative;
    u16 positive;

    if (axis == SDL_CONTROLLER_AXIS_LEFTX)
    {
        negative = DPAD_LEFT;
        positive = DPAD_RIGHT;
    }
    else if (axis == SDL_CONTROLLER_AXIS_LEFTY)
    {
        negative = DPAD_UP;
        positive = DPAD_DOWN;
    }
    else
    {
        return;
    }

    sControllerAxisKeys &= ~(negative | positive);
    if (value < -CONTROLLER_DEAD_ZONE)
        sControllerAxisKeys |= negative;
    else if (value > CONTROLLER_DEAD_ZONE)
        sControllerAxisKeys |= positive;
}

static uint32_t GbaColorToArgb(u16 color)
{
    uint32_t red = (color & 0x1F) * 255 / 31;
    uint32_t green = ((color >> 5) & 0x1F) * 255 / 31;
    uint32_t blue = ((color >> 10) & 0x1F) * 255 / 31;

    return 0xFF000000 | (red << 16) | (green << 8) | blue;
}

// The speed indicator is a host overlay. It reuses the game's generated
// latin_small font asset, but is blitted after DrawFrame so it never consumes
// GBA windows, sprites, palettes, or VRAM.
static void DrawSpeedGlyph(uint32_t *pixels, u16 glyphId, int x, int y,
                           uint32_t color)
{
    const u8 *glyph = (const u8 *)(gFontSmallLatinGlyphs + 0x20 * glyphId);
    u32 width = gFontSmallLatinGlyphWidths[glyphId];

    if (width > 16)
        width = 16;

    for (u32 row = 0; row < 13; row++)
    {
        for (u32 column = 0; column < width; column++)
        {
            u32 tileOffset = (column >= 8 ? 0x10 : 0)
                           + (row >= 8 ? 0x20 : 0);
            const u8 *rowData = glyph + tileOffset + ((row & 7) * 2);
            u8 packed = rowData[(column & 7) / 4];
            u8 value = (packed >> ((column & 3) * 2)) & 3;
            int pixelX = x + (int)column;
            int pixelY = y + (int)row;

            if (value != 0
             && pixelX >= 0 && pixelX < DISPLAY_WIDTH
             && pixelY >= 0 && pixelY < DISPLAY_HEIGHT)
                pixels[pixelY * DISPLAY_WIDTH + pixelX] = color;
        }
    }
}

static u32 SpeedTextGlyphs(u16 *glyphs)
{
    u32 count = 0;

    if (sFastForwardHeld)
    {
        glyphs[count++] = CHAR_M;
        glyphs[count++] = CHAR_A;
        glyphs[count++] = CHAR_X;
    }
    else
    {
        u32 multiplier = sSpeedMultiplier;

        if (multiplier >= 10)
            glyphs[count++] = CHAR_0 + (multiplier / 10);
        glyphs[count++] = CHAR_0 + (multiplier % 10);
        glyphs[count++] = CHAR_x;
    }

    return count;
}

static void DrawSpeedOverlay(uint32_t *pixels)
{
    u16 glyphs[4];
    u32 count;
    u32 width = 0;
    int x;

    // Keep the normal 1x screen unobstructed. The indicator appears whenever
    // a user-selected multiplier or hold-to-accelerate mode is active.
    if (!sFastForwardHeld && sSpeedMultiplier == 1)
        return;

    count = SpeedTextGlyphs(glyphs);
    for (u32 i = 0; i < count; i++)
        width += gFontSmallLatinGlyphWidths[glyphs[i]] + (i + 1 < count);

    x = DISPLAY_WIDTH - (int)width - 3;
    // A one-pixel black shadow keeps the white asset legible over bright maps.
    for (u32 i = 0; i < count; i++)
    {
        DrawSpeedGlyph(pixels, glyphs[i], x + 1, 2 + 1, 0xFF000000);
        x += gFontSmallLatinGlyphWidths[glyphs[i]] + 1;
    }

    x = DISPLAY_WIDTH - (int)width - 3;
    for (u32 i = 0; i < count; i++)
    {
        DrawSpeedGlyph(pixels, glyphs[i], x, 2, 0xFFFFFFFF);
        x += gFontSmallLatinGlyphWidths[glyphs[i]] + 1;
    }
}

static bool CreateRenderer(void)
{
    // native_main.c owns the 59.7275 Hz GBA frame clock.  By default SDL does
    // not add a second (usually 60 Hz) present clock: the two clocks drift and
    // make queued audio periodically lead or lag the rendered frame.
    // PE_VSYNC=1 opts into letting the display refresh pace presentation, which
    // removes tearing/judder but trades that drift back in.
    Uint32 flags = SDL_RENDERER_ACCELERATED;

    if (getenv("PE_VSYNC") != NULL)
    {
        flags |= SDL_RENDERER_PRESENTVSYNC;
        fprintf(stderr, "[host] vsync present requested\n");
    }

    sRenderer = SDL_CreateRenderer(sWindow, -1, flags);
    if (sRenderer == NULL)
        sRenderer = SDL_CreateRenderer(sWindow, -1, SDL_RENDERER_SOFTWARE);
    if (sRenderer == NULL)
        return false;

    if (SDL_RenderSetLogicalSize(sRenderer, DISPLAY_WIDTH, DISPLAY_HEIGHT) != 0)
        return false;
    if (SDL_RenderSetIntegerScale(sRenderer, SDL_TRUE) != 0)
        return false;

    sTexture = SDL_CreateTexture(sRenderer, SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_STREAMING,
                                 DISPLAY_WIDTH, DISPLAY_HEIGHT);
    return sTexture != NULL;
}

static void CloseAudio(void)
{
    if (sAudioDevice != 0)
    {
        SDL_ClearQueuedAudio(sAudioDevice);
        SDL_CloseAudioDevice(sAudioDevice);
    }
    sAudioDevice = 0;
    sAudioRate = 0;
    sAudioUnavailable = false;
}

static bool EnsureAudio(u32 sampleRate)
{
    SDL_AudioSpec want;
    SDL_AudioSpec have;

    if (sampleRate == 0)
        return false;
    if (sAudioDevice != 0)
    {
        if (sAudioRate == (int)sampleRate)
            return true;
        CloseAudio();
    }
    if (sAudioUnavailable)
        return false;

    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0
     && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
    {
        fprintf(stderr, "SDL audio initialization failed: %s\n", SDL_GetError());
        sAudioUnavailable = true;
        return false;
    }

    SDL_zero(want);
    want.freq = (int)sampleRate;
    want.format = AUDIO_S8;
    want.channels = 2;
    // A 512-sample device buffer is only ~38 ms at the GBA's 13379 Hz. The
    // game queues one VBlank of audio at a time (~446 bytes), so the FIFO sits
    // at zero and every late frame is audible as a crackle.
    {
        const char *samplesEnv = getenv("PE_AUDIO_SAMPLES");
        long requestedSamples = samplesEnv != NULL ? strtol(samplesEnv, NULL, 0) : 1024;

        if (requestedSamples < 128)
            requestedSamples = 128;
        want.samples = (int)requestedSamples;
    }
    sAudioDevice = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (sAudioDevice == 0)
    {
        fprintf(stderr, "SDL audio device unavailable: %s\n", SDL_GetError());
        sAudioUnavailable = true;
        return false;
    }
    if (have.format != AUDIO_S8 || have.channels != 2 || have.freq != (int)sampleRate)
    {
        fprintf(stderr, "SDL audio format mismatch (got %d Hz, 0x%04X, %u channels)\n",
                have.freq, have.format, have.channels);
        SDL_CloseAudioDevice(sAudioDevice);
        sAudioDevice = 0;
        sAudioUnavailable = true;
        return false;
    }

    sAudioRate = have.freq;
    SDL_PauseAudioDevice(sAudioDevice, 0);

    {
        // Start the FIFO with ~150 ms of silence so scheduling jitter drains
        // slack instead of producing gaps. Production and consumption rates are
        // both 13379 Hz, so this head start is preserved, not accumulated.
        // PE_AUDIO_PREFILL=0 disables it when comparing against the old setup.
        static u8 silence[8192];
        const char *prefillEnv = getenv("PE_AUDIO_PREFILL");
        long prefillBytes = prefillEnv != NULL ? strtol(prefillEnv, NULL, 0) : 4096;

        if (prefillBytes > (long)sizeof(silence))
            prefillBytes = (long)sizeof(silence);
        if (prefillBytes > 0)
        {
            memset(silence, 0, (size_t)prefillBytes);
            if (SDL_QueueAudio(sAudioDevice, silence, (Uint32)prefillBytes) < 0)
                fprintf(stderr, "SDL audio prefill failed: %s\n", SDL_GetError());
        }
    }
    return true;
}

bool Platform_Init(const char *title, int scale, bool hidden)
{
    Uint32 windowFlags = hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN;

    // 每次启动把 BIOS 填充自检写进 crash.log（见 src/platform/bios.c）：
    // 填充坏掉会让备份地图的边界余量变成 0，地图周围就会出现黑带。
    HostBiosSelfTest();

    // 常开 y=80 行探针：渲染器每帧记录该行每个像素由哪一层提供，
    // 这样「左半边被某层盖住」的坏帧不必等按键也能自动抓快照。
    gHostProbeRow = 80;

    // 全帧层归属映射（38400 字节）。用 malloc 而不是静态数组：
    // 静态数据会落到 IWRAM（只有 32KB），必然溢出。
    if (gHostWinnerMap == NULL)
        gHostWinnerMap = (u8 *)malloc((size_t)DISPLAY_WIDTH * (size_t)DISPLAY_HEIGHT);

    if (sInitialized)
        return true;
    if (scale < 1)
        scale = 1;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER
               | SDL_INIT_GAMECONTROLLER) != 0)
    {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return false;
    }

    sInputMutex = SDL_CreateMutex();
    if (sInputMutex == NULL)
    {
        fprintf(stderr, "SDL input mutex creation failed: %s\n", SDL_GetError());
        Platform_Shutdown();
        return false;
    }

    // Keep the GBA's discrete pixels crisp when SDL scales the logical frame.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    // 不用输入法：不要弹出候选词窗口。
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "0");

    sWindow = SDL_CreateWindow(title,
                               SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               DISPLAY_WIDTH * scale, DISPLAY_HEIGHT * scale,
                               windowFlags | SDL_WINDOW_RESIZABLE);
    if (sWindow == NULL || !CreateRenderer())
    {
        fprintf(stderr, "SDL video initialization failed: %s\n", SDL_GetError());
        Platform_Shutdown();
        return false;
    }

    // 关掉 SDL 的文本输入（= 让 Windows 把我们这个窗口从中文输入法上摘下来）。
    // 游戏完全不需要输入文字，但只要 IME 还挂在这个窗口上，按住方向键时
    // 输入法会把按键当成"候选词光标移动"去处理，产生大量 SDL_TEXTEDITING
    // 事件；那些事件的处理发生在 PeekMessage 内部（Windows/IME 代码），
    // 一次能让主循环卡 50~270ms，表现为画面和音乐一起顿一下。
    SDL_StopTextInput();

    sVBlankSemaphore = SDL_CreateSemaphore(0);
    if (sVBlankSemaphore == NULL)
    {
        fprintf(stderr, "SDL semaphore creation failed: %s\n", SDL_GetError());
        Platform_Shutdown();
        return false;
    }

    SDL_AtomicSet(&sFramePending, 0);
    sKeyboardKeys = 0;
    sControllerKeys = 0;
    sControllerAxisKeys = 0;
    sKeyboardFocused = true;
    sSpeedMultiplier = 1;
    sFastForwardHeld = false;
    OpenFirstController();
    SDL_AtomicSet(&sQuitRequested, 0);
    // Start outside the visible scanlines so the first GPU-register writes are
    // committed immediately, matching the GBA's post-VBlank state.
    REG_VCOUNT = DISPLAY_HEIGHT + 1;
    REG_DISPSTAT = 0;
    sInitialized = true;
    return true;
}

void Platform_RequestQuit(void)
{
    SDL_AtomicSet(&sQuitRequested, 1);
    if (sVBlankSemaphore != NULL && SDL_AtomicCAS(&sFramePending, 1, 0))
        SDL_SemPost(sVBlankSemaphore);
}

void Platform_Shutdown(void)
{
    Platform_RequestQuit();

    LockInputState();

    CloseAudio();

    if (sController != NULL)
        SDL_GameControllerClose(sController);

    if (sVBlankSemaphore != NULL)
        SDL_DestroySemaphore(sVBlankSemaphore);
    if (sTexture != NULL)
        SDL_DestroyTexture(sTexture);
    if (sRenderer != NULL)
        SDL_DestroyRenderer(sRenderer);
    if (sWindow != NULL)
        SDL_DestroyWindow(sWindow);

    sVBlankSemaphore = NULL;
    sController = NULL;
    sTexture = NULL;
    sRenderer = NULL;
    sWindow = NULL;
    sKeyboardKeys = 0;
    sControllerKeys = 0;
    sControllerAxisKeys = 0;
    sKeyboardFocused = false;
    sSpeedMultiplier = 1;
    sFastForwardHeld = false;
    UnlockInputState();
    if (sInputMutex != NULL)
        SDL_DestroyMutex(sInputMutex);
    sInputMutex = NULL;
    SDL_AtomicSet(&sQuitRequested, 0);
    sInitialized = false;
    SDL_Quit();
}

void Platform_QueueAudio(const s8 *right, const s8 *left, u32 sampleCount,
                         u32 sampleRate)
{
    Sint8 *interleaved;
    size_t byteCount;
    const Uint32 maxQueuedBytes = (Uint32)PCM_DMA_BUF_SIZE * 2 * 4;

    if (!sInitialized || right == NULL || left == NULL || sampleCount == 0
     || sampleCount > PCM_DMA_BUF_SIZE || !EnsureAudio(sampleRate))
        return;

    byteCount = (size_t)sampleCount * 2;
    // A stalled/slow audio device must not let the game allocate an
    // unbounded SDL queue.  The next VBlank will retry once the backlog has
    // drained below four target blocks.
    if (SDL_GetQueuedAudioSize(sAudioDevice) > maxQueuedBytes)
        return;
    interleaved = SDL_malloc(byteCount);
    if (interleaved == NULL)
        return;

    // SDL's stereo order is left/right, while the target buffer is FIFO A
    // (right) followed by FIFO B (left).
    for (u32 i = 0; i < sampleCount; i++)
    {
        interleaved[i * 2] = left[i];
        interleaved[i * 2 + 1] = right[i];
    }

    if (SDL_QueueAudio(sAudioDevice, interleaved, (Uint32)byteCount) < 0)
        fprintf(stderr, "SDL audio queue failed: %s\n", SDL_GetError());
    SDL_free(interleaved);
}

u64 Platform_GetGbaCycleCount(void)
{
    u64 counter = SDL_GetPerformanceCounter();
    u64 frequency = SDL_GetPerformanceFrequency();

    if (frequency == 0)
        return 0;
    return (counter / frequency) * 16777216u
         + ((counter % frequency) * 16777216u) / frequency;
}

void Platform_CycleCountStart(void)
{
    sCycleCountStart = Platform_GetGbaCycleCount();
}

u32 Platform_CycleCountEnd(void)
{
    return (u32)(Platform_GetGbaCycleCount() - sCycleCountStart);
}

bool Platform_PumpEvents(void)
{
    SDL_Event event;
    bool result = true;
    Uint64 pumpStart;
    Uint64 lockEnd;
    Uint64 pollEnd;
    Uint64 stateEnd;
    Uint64 handlerTicks = 0;
    Uint32 eventCount = 0;
    Uint32 eventTypes[8] = {0};
    Uint32 eventSubs[8] = {0};
    Uint32 eventMs[8] = {0};

    pumpStart = SDL_GetPerformanceCounter();
    LockInputState();
    lockEnd = SDL_GetPerformanceCounter();

    while (SDL_PollEvent(&event))
    {
        Uint64 handlerStart = SDL_GetPerformanceCounter();
        Uint32 slot = eventCount;

        eventCount++;
        if (slot < 8)
        {
            eventTypes[slot] = (Uint32)event.type;
            // 窗口事件记子类型（FOCUS_LOST=1, EXPOSED=3, MOVED=4, SIZE_CHANGED=5…），
            // 按键事件记 scancode。慢 pump 里到底是哪个事件，一看就知道。
            if (event.type == SDL_WINDOWEVENT)
                eventSubs[slot] = (Uint32)event.window.event;
            else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)
                eventSubs[slot] = (Uint32)event.key.keysym.scancode;
        }
        if (event.type == SDL_QUIT)
        {
            Platform_RequestQuit();
            result = false;
            break;
        }
        if (event.type == SDL_WINDOWEVENT)
        {
            if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
            {
                sKeyboardKeys = 0;
                sFastForwardHeld = false;
                sKeyboardFocused = false;
            }
            else if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
            {
                sKeyboardFocused = true;
                // 切回窗口时输入法可能又被挂上来，再摘一次。
                SDL_StopTextInput();
            }
        }
        if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)
        {
            u16 button = ScancodeToButton(event.key.keysym.scancode);
            if (button == 0)
                button = KeycodeToButton(event.key.keysym.sym);
            if (event.type == SDL_KEYDOWN)
                sKeyboardKeys |= button;
            else
                sKeyboardKeys &= ~button;

            if (event.key.keysym.sym == SDLK_SPACE
             || event.key.keysym.scancode == SDL_SCANCODE_SPACE)
                sFastForwardHeld = event.type == SDL_KEYDOWN;

            if (event.type == SDL_KEYDOWN
             && event.key.repeat == 0
             && event.key.keysym.scancode >= SDL_SCANCODE_F1
             && event.key.keysym.scancode <= SDL_SCANCODE_F12)
            {
                sFrameDumpImmediate = 1;
                Platform_RequestFrameDump("F-key");
            }

            // 笔记本的 F1 常被系统当亮度/音量键吃掉，游戏收不到。
            // P 键不参与游戏操作，用它做手动快照（立即抓当前这一帧）。
            if (event.type == SDL_KEYDOWN
             && event.key.repeat == 0
             && event.key.keysym.scancode == SDL_SCANCODE_P)
            {
                sFrameDumpImmediate = 1;
                Platform_RequestFrameDump("key P");
            }

            if (event.type == SDL_KEYDOWN
             && event.key.repeat == 0
             && IsSpeedCycleKey(&event.key))
                CycleSpeedMultiplier();
        }
        if (event.type == SDL_CONTROLLERDEVICEADDED)
        {
            OpenController(event.cdevice.which);
        }
        else if (event.type == SDL_CONTROLLERDEVICEREMOVED
              && sController != NULL && ControllerEventMatches(event.cdevice.which))
        {
            SDL_GameControllerClose(sController);
            sController = NULL;
            sControllerKeys = 0;
            sControllerAxisKeys = 0;
            OpenFirstController();
        }
        else if ((event.type == SDL_CONTROLLERBUTTONDOWN
               || event.type == SDL_CONTROLLERBUTTONUP)
              && ControllerEventMatches(event.cbutton.which))
        {
            u16 button = ControllerButtonToGba(event.cbutton.button);
            if (event.type == SDL_CONTROLLERBUTTONDOWN)
                sControllerKeys |= button;
            else
                sControllerKeys &= ~button;
        }
        else if (event.type == SDL_CONTROLLERAXISMOTION
              && ControllerEventMatches(event.caxis.which))
        {
            SetControllerAxis(event.caxis.axis, event.caxis.value);
        }
        handlerTicks += SDL_GetPerformanceCounter() - handlerStart;
        if (slot < 8)
            eventMs[slot] = DiagTicksToMs(SDL_GetPerformanceCounter() - handlerStart);
    }
    pollEnd = SDL_GetPerformanceCounter();

    // The event queue can lose a transition while a Windows window is being
    // focused or moved. In a focused real window, the physical state is the
    // authoritative state (which also prevents a missed KEYUP from sticking
    // a button). Hidden/headless smoke windows keep their synthetic events.
    if (sKeyboardFocused)
    {
        u16 stateKeys = KeyboardStateToButtons();
        if (sWindow != NULL
         && (SDL_GetWindowFlags(sWindow) & SDL_WINDOW_INPUT_FOCUS) != 0)
            sKeyboardKeys = stateKeys;
        else
            sKeyboardKeys |= stateKeys;

        if (sWindow != NULL
         && (SDL_GetWindowFlags(sWindow) & SDL_WINDOW_INPUT_FOCUS) != 0)
            sFastForwardHeld = KeyboardFastForwardState();
    }
    stateEnd = SDL_GetPerformanceCounter();
    UnlockInputState();

    {
        Uint32 slowMs = DiagTicksToMs(stateEnd - pumpStart);

        if (slowMs >= 40 && sSlowPumpLogs < 400)
        {
            // 卡顿现场：慢 pump 的子阶段明细（排查左右行走卡顿用）。
            sSlowPumpLogs++;
            HostLogf("pump total=%ums lock=%ums poll=%ums ev=%u state=%ums handler=%ums frame=%u keys=%03X",
                     slowMs,
                     DiagTicksToMs(lockEnd - pumpStart),
                     DiagTicksToMs(pollEnd - lockEnd),
                     eventCount,
                     DiagTicksToMs(stateEnd - pollEnd),
                     DiagTicksToMs(handlerTicks),
                     sPresentFrameCount,
                     (unsigned)sKeyboardKeys);
            HostLogf("pump ev0=%02x/%02x:%ums ev1=%02x/%02x:%ums ev2=%02x/%02x:%ums ev3=%02x/%02x:%ums",
                     eventTypes[0], eventSubs[0], eventMs[0],
                     eventTypes[1], eventSubs[1], eventMs[1],
                     eventTypes[2], eventSubs[2], eventMs[2],
                     eventTypes[3], eventSubs[3], eventMs[3]);
        }
        if (slowMs >= 30 && HostTraceEnabled())
        {
            fprintf(stderr,
                    "[host] slow pump total=%ums lock=%ums poll=%ums(%u ev) state=%ums handler=%ums types=%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x\n",
                    slowMs,
                    DiagTicksToMs(lockEnd - pumpStart),
                    DiagTicksToMs(pollEnd - lockEnd),
                    eventCount,
                    DiagTicksToMs(stateEnd - pollEnd),
                    DiagTicksToMs(handlerTicks),
                    eventTypes[0], eventTypes[1], eventTypes[2], eventTypes[3],
                    eventTypes[4], eventTypes[5], eventTypes[6], eventTypes[7]);
        }
    }
    return result;
}

bool Platform_IsQuitRequested(void)
{
    return SDL_AtomicGet(&sQuitRequested) != 0;
}

void Platform_RequestFrameDump(const char *reason)
{
    // 手动快照（P/F 键会先置 sFrameDumpImmediate）总是生效；自动快照默认关闭。
    if (!sFrameDumpImmediate && !AutoDumpEnabled())
        return;
    sFrameDumpReason = (reason != NULL) ? reason : "unknown";
    sFrameDumpRequestFrame = sPresentFrameCount;
    sFrameDumpRequest = 1;
    gHostProbeRow = 80;
}

static const char *BgLayerName(u32 bg)
{
    static const char *const names[] = {"BG0", "BG1", "BG2", "BG3"};
    return names[bg & 3];
}

// 把当前帧写成 PPM（240x160 RGB888），方便和 mGBA 原生截图逐像素对照。
static void DumpFramePPM(const u16 *pixels, u32 seq)
{
    char name[64];
    FILE *f;
    u32 i;

    sprintf(name, "frame_%u.ppm", (unsigned)seq);
    f = fopen(name, "wb");
    if (f == NULL)
    {
        HostLogf("frame ppm: cannot open %s", name);
        return;
    }
    fprintf(f, "P6\n%u %u\n255\n", (unsigned)DISPLAY_WIDTH, (unsigned)DISPLAY_HEIGHT);
    for (i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++)
    {
        u16 c = pixels[i];
        unsigned char rgb[3];
        rgb[0] = (unsigned char)(((c & 0x1F) * 255 + 15) / 31);
        rgb[1] = (unsigned char)((((c >> 5) & 0x1F) * 255 + 15) / 31);
        rgb[2] = (unsigned char)((((c >> 10) & 0x1F) * 255 + 15) / 31);
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    HostLogf("frame ppm: %s", name);
}

// 导出这一帧的原始数据，便于离线逐像素还原：winners（层归属）、bgmaps（四层 tilemap）、pltt（调色板）。
static void DumpFrameBins(const u16 *pixels, u32 seq)
{
    char name[64];
    FILE *f;
    u32 bg;

    (void)pixels;

    sprintf(name, "winners_%u.bin", (unsigned)seq);
    f = fopen(name, "wb");
    if (f != NULL)
    {
        if (gHostWinnerMap != NULL)
            fwrite(gHostWinnerMap, 1, DISPLAY_WIDTH * DISPLAY_HEIGHT, f);
        fclose(f);
    }

    sprintf(name, "bgmaps_%u.bin", (unsigned)seq);
    f = fopen(name, "wb");
    if (f != NULL)
    {
        for (bg = 0; bg < NUM_BACKGROUNDS; bg++)
        {
            u32 cnt = (bg == 0) ? REG_BG0CNT : (bg == 1) ? REG_BG1CNT : (bg == 2) ? REG_BG2CNT : REG_BG3CNT;
            const u8 *map = (const u8 *)VRAM + (((cnt >> 8) & 0x1F) * 0x800);
            fwrite(map, 1, 0x800, f);
        }
        fclose(f);
    }

    sprintf(name, "vram_%u.bin", (unsigned)seq);
    f = fopen(name, "wb");
    if (f != NULL)
    {
        fwrite((const void *)VRAM, 1, 0x18000, f);
        fclose(f);
    }

    sprintf(name, "oam_%u.bin", (unsigned)seq);
    f = fopen(name, "wb");
    if (f != NULL)
    {
        fwrite((const void *)(uintptr_t)OAM, 1, 0x400, f);
        fclose(f);
    }

    sprintf(name, "unfaded_%u.bin", (unsigned)seq);
    f = fopen(name, "wb");
    if (f != NULL)
    {
        fwrite((const void *)gPlttBufferUnfaded, 1, 0x400, f);
        fclose(f);
    }

    sprintf(name, "pltt_%u.bin", (unsigned)seq);
    f = fopen(name, "wb");
    if (f != NULL)
    {
        fwrite((const void *)PLTT, 1, 0x400, f);
        fclose(f);
    }

    HostLogf("frame bins: winners_%u.bin bgmaps_%u.bin pltt_%u.bin vram_%u.bin oam_%u.bin", (unsigned)seq, (unsigned)seq, (unsigned)seq, (unsigned)seq, (unsigned)seq);
}

// F1 dumps the graphics state that can leave a band of the screen wrong:
// window registers, BG control/offset registers, palette slots, the final
// framebuffer samples of one scanline and the tilemap entries the map layers
// are actually reading for those pixels.
u32 Platform_ObjTileChecksum(u32 tileNum)
{
    const u8 *p = (const u8 *)(uintptr_t)(VRAM + 0x10000 + tileNum * 32);
    u32 sum = 0;
    u32 i;

    // 128 个 4bpp 图块 = 0x1000 字节，必须整个落在 OBJ VRAM 内。
    if (tileNum >= 1024 || tileNum + 128 > 1024)
        return 0;
    for (i = 0; i < 0x1000; i++)
        sum = sum * 31u + p[i];
    return sum;
}

static void DumpFrameDiagnostics(const u16 *pixels)
{
    #define DUMP_SAMPLE_COUNT 11u
    static const u32 sampleXs[] = {0, 40, 80, 111, 115, 116, 117, 120, 160, 200, 239};
    const u16 *vram = (const u16 *)VRAM;
    u16 pltt[PLTT_BUFFER_SIZE];
    u32 bg, i;
    const u32 y = 80;

    for (i = 0; i < PLTT_BUFFER_SIZE; i++)
        pltt[i] = ((const u16 *)PLTT)[i];

    static u32 sDumpSequence;
    HostLogf("---- frame dump #%u (%s) ----", ++sDumpSequence, sFrameDumpReason);
    DumpFramePPM(pixels, sDumpSequence);
    // 那堆 .bin（vram/winners/bgmaps/oam/pltt，加起来约 150KB）只在需要逐像素
    // 复盘时用 PE_DUMP_BINS=1 打开。默认关掉：一是写这么多文件本身会让
    // 下一帧卡半秒到一秒（杀毒/磁盘），会污染我们正在测的卡顿数据。
    if (getenv("PE_DUMP_BINS") != NULL)
        DumpFrameBins(pixels, sDumpSequence);
    HostLogf("DISPCNT=%04X DISPSTAT=%04X BLDCNT=%04X BLDALPHA=%04X BLDY=%02X MOSAIC=%04X",
             REG_DISPCNT, REG_DISPSTAT, REG_BLDCNT, REG_BLDALPHA, REG_BLDY, REG_MOSAIC);
    HostLogf("WININ=%04X WINOUT=%04X W0H=%04X W0V=%04X W1H=%04X W1V=%04X",
             REG_WININ, REG_WINOUT, REG_WIN0H, REG_WIN0V, REG_WIN1H, REG_WIN1V);
    HostLogf("BGCNT=%04X %04X %04X %04X", REG_BG0CNT, REG_BG1CNT, REG_BG2CNT, REG_BG3CNT);
    HostLogf("fade active=%d y=%d targetY=%d mode=%d blend=%06X",
             (int)gPaletteFade.active, (int)gPaletteFade.y, (int)gPaletteFade.targetY,
             (int)gPaletteFade.mode, gPaletteFade.blendColor);
    HostLogf("BGOFS=%u,%u %u,%u %u,%u %u,%u",
             REG_BG0HOFS, REG_BG0VOFS, REG_BG1HOFS, REG_BG1VOFS,
             REG_BG2HOFS, REG_BG2VOFS, REG_BG3HOFS, REG_BG3VOFS);
    HostLogf("PLTT[0..15]=%04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X",
             pltt[0], pltt[1], pltt[2], pltt[3], pltt[4], pltt[5], pltt[6], pltt[7],
             pltt[8], pltt[9], pltt[10], pltt[11], pltt[12], pltt[13], pltt[14], pltt[15]);
    {
        u32 pl, k;
        char line[256];
        for (pl = 0; pl < 32; pl++)
        {
            int n = 0;
            for (k = 0; k < 16; k++)
                n += (int)snprintf(line + n, sizeof(line) - (size_t)n, "%04X ", pltt[pl * 16 + k]);
            HostLogf("PLTT+%03X: %s", (unsigned)(pl * 16), line);
        }
    }
    {
        char line[256];
        int n = 0;
        u32 k;
        for (k = 0; k < 16; k++)
            n += (int)snprintf(line + n, sizeof(line) - (size_t)n, "%02X ", GetSpritePaletteTagByPaletteNum((u8)k));
        HostLogf("sprpals reserved=%u tags=%s", (unsigned)gReservedSpritePaletteCount, line);
        HostLogf("sprpal idx: default=%d blue=%d pink=%d beige=%d red=%d (want 4..slot)",
                 (int)IndexOfSpritePaletteTag(4), (int)IndexOfSpritePaletteTag(5),
                 (int)IndexOfSpritePaletteTag(6), (int)IndexOfSpritePaletteTag(7),
                 (int)IndexOfSpritePaletteTag(8));
    }
    {
        static const u32 ry[] = {33, 40, 48, 56, 64, 72, 88, 96, 104, 112};
        static const u32 rx[] = {120, 140, 160, 180, 200, 220, 236};
        u32 a, b;
        for (a = 0; a < sizeof(ry) / sizeof(ry[0]); a++)
        {
            char line[512];
            int n = 0;
            for (b = 0; b < sizeof(rx) / sizeof(rx[0]); b++)
            {
                u32 off = ry[a] * DISPLAY_WIDTH + rx[b];
                unsigned win = (gHostWinnerMap != NULL) ? (unsigned)gHostWinnerMap[off] : 0xFFu;
                n += (int)snprintf(line + n, sizeof(line) - (size_t)n, "%u:%02X/%04X ",
                                   (unsigned)rx[b], win, (unsigned)pixels[off]);
            }
            HostLogf("region y=%02u %s", (unsigned)ry[a], line);
        }
    }
    {
        const struct OamData *oam = (const struct OamData *)(uintptr_t)OAM;
        u32 i2;
        u32 shown = 0;
        for (i2 = 0; i2 < 128 && shown < 24; i2++)
        {
            const struct OamData *o = &oam[i2];
            if (o->objMode == 3)
                continue;
            if (o->y < 24 || o->y > 100 || o->x < 96)
                continue;
            HostLogf("oam[%u] x=%d y=%d tile=%u pal=%u bpp=%u shape=%u size=%u aff=%u prio=%u mode=%u mnum=%u",
                     (unsigned)i2, (int)o->x, (int)o->y, (unsigned)o->tileNum, (unsigned)o->paletteNum,
                     (unsigned)o->bpp, (unsigned)o->shape, (unsigned)o->size, (unsigned)o->affineMode,
                     (unsigned)o->priority, (unsigned)o->objMode, (unsigned)o->matrixNum);
            shown++;
        }
        HostLogf("oam listed=%u", (unsigned)shown);
        // 右上角旋转小图标（pokenav 主菜单）单独列出来：它在 y<24，上面的过滤条件看不到它。
        for (i2 = 0; i2 < 128; i2++)
        {
            const struct OamData *o = &oam[i2];
            if (o->objMode == 3 || o->y >= 24 || o->x < 200)
                continue;
            HostLogf("oam-icon[%u] x=%d y=%d tile=%u pal=%u bpp=%u shape=%u size=%u prio=%u",
                     (unsigned)i2, (int)o->x, (int)o->y, (unsigned)o->tileNum,
                     (unsigned)o->paletteNum, (unsigned)o->bpp, (unsigned)o->shape,
                     (unsigned)o->size, (unsigned)o->priority);
            if (o->bpp == 0)
            {
                u16 sheetStart = GetSpriteTileStartByTag(0);
                HostLogf("oam-icon[%u] tilesum=%08X sheetsum=%08X",
                         (unsigned)i2, Platform_ObjTileChecksum((u32)o->tileNum),
                         (sheetStart == TAG_NONE) ? 0 : Platform_ObjTileChecksum((u32)sheetStart));
            }
        }
        HostLogf("tag0 tileStart=%u palSlot=%d",
                 (unsigned)GetSpriteTileStartByTag(0), (int)IndexOfSpritePaletteTag(0));
    }
    HostLogf("PLTT[0x100..0x10F]=%04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X",
             pltt[0x100], pltt[0x101], pltt[0x102], pltt[0x103],
             pltt[0x104], pltt[0x105], pltt[0x106], pltt[0x107],
             pltt[0x108], pltt[0x109], pltt[0x10A], pltt[0x10B],
             pltt[0x10C], pltt[0x10D], pltt[0x10E], pltt[0x10F]);
    HostLogf("PLTT[0xE0..0xEF]=%04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X",
             pltt[0xE0], pltt[0xE1], pltt[0xE2], pltt[0xE3],
             pltt[0xE4], pltt[0xE5], pltt[0xE6], pltt[0xE7],
             pltt[0xE8], pltt[0xE9], pltt[0xEA], pltt[0xEB],
             pltt[0xEC], pltt[0xED], pltt[0xEE], pltt[0xEF]);
    HostLogf("PLTT[0xF0..0xFF]=%04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X",
             pltt[0xF0], pltt[0xF1], pltt[0xF2], pltt[0xF3],
             pltt[0xF4], pltt[0xF5], pltt[0xF6], pltt[0xF7],
             pltt[0xF8], pltt[0xF9], pltt[0xFA], pltt[0xFB],
             pltt[0xFC], pltt[0xFD], pltt[0xFE], pltt[0xFF]);
    HostLogf("PLTT pal1=%04X %04X %04X %04X %04X %04X %04X %04X | pal3=%04X %04X %04X %04X %04X %04X %04X %04X",
             pltt[0x10], pltt[0x11], pltt[0x12], pltt[0x13], pltt[0x14], pltt[0x15], pltt[0x16], pltt[0x17],
             pltt[0x30], pltt[0x31], pltt[0x32], pltt[0x33], pltt[0x34], pltt[0x35], pltt[0x36], pltt[0x37]);
    {
        u32 dFaded = 0, dUnfaded = 0, k;
        for (k = 0; k < PLTT_BUFFER_SIZE; k++)
        {
            if (pltt[k] != gPlttBufferFaded[k])
                dFaded++;
            if (pltt[k] != gPlttBufferUnfaded[k])
                dUnfaded++;
        }
        HostLogf("pltt buf diff: PLTTvsFaded=%u PLTTvsUnfaded=%u/%u faded1=%04X unfaded1=%04X",
                 dFaded, dUnfaded, (u32)PLTT_BUFFER_SIZE,
                 gPlttBufferFaded[BG_PLTT_ID(1)], gPlttBufferUnfaded[BG_PLTT_ID(1)]);
        // 把 Faded 与 Unfaded 不一致的条目逐条打出来（最多 8 条），
        // 用来判断"某个调色板槽只进了 Unfaded、从没同步到显存"。
        {
            u32 printed = 0;
            for (i = 0; i < PLTT_BUFFER_SIZE && printed < 8; i++)
            {
                if (gPlttBufferFaded[i] != gPlttBufferUnfaded[i])
                {
                    HostLogf("pltt diff[%03X] faded=%04X unfaded=%04X",
                             i, gPlttBufferFaded[i], gPlttBufferUnfaded[i]);
                    printed++;
                }
            }
        }
    }
    HostLogf("scanline state=%u src=%u dmaDest=%08X dmaControl=%08X win0h[0]=%04X win0h[10]=%04X win0h[80]=%04X win0h[159]=%04X",
             (u32)gScanlineEffect.state, (u32)gScanlineEffect.srcBuffer,
             (u32)(uintptr_t)gScanlineEffect.dmaDest, gScanlineEffect.dmaControl,
             gScanlineEffectRegBuffers[0][0], gScanlineEffectRegBuffers[0][10],
             gScanlineEffectRegBuffers[0][80], gScanlineEffectRegBuffers[0][159]);

    for (i = 0; i < DUMP_SAMPLE_COUNT; i++)
        HostLogf("px y%u x%u = %04X", y, sampleXs[i], pixels[y * DISPLAY_WIDTH + sampleXs[i]]);

    for (bg = 0; bg < NUM_BACKGROUNDS; bg++)
    {
        u16 cnt = (bg == 0) ? REG_BG0CNT : (bg == 1) ? REG_BG1CNT : (bg == 2) ? REG_BG2CNT : REG_BG3CNT;
        u32 hofs = (bg == 0) ? REG_BG0HOFS : (bg == 1) ? REG_BG1HOFS : (bg == 2) ? REG_BG2HOFS : REG_BG3HOFS;
        u32 vofs = (bg == 0) ? REG_BG0VOFS : (bg == 1) ? REG_BG1VOFS : (bg == 2) ? REG_BG2VOFS : REG_BG3VOFS;
        u32 screenBase = (cnt >> 8) & 0x1F;
        u32 charBase = (cnt >> 2) & 0xF;
        u32 size = (cnt >> 14) & 3;
        const u16 *map = vram + (screenBase * (BG_SCREEN_SIZE / 2));
        u32 row = (((y + vofs) & 0xFF) >> 3);

        HostLogf("%s prio=%u char=%u screen=%u size=%u color256=%u row=%u",
                 BgLayerName(bg), (u32)(cnt & 3), charBase, screenBase, size,
                 (u32)((cnt & BGCNT_256COLOR) != 0), row);

        for (i = 0; i < DUMP_SAMPLE_COUNT; i++)
        {
            u32 col = (((sampleXs[i] + hofs) & 0xFF) >> 3);
            u16 entry = map[row * 32 + col];
            HostLogf("  %s x%u col%u entry=%04X tile=%03X pal=%u",
                     BgLayerName(bg), sampleXs[i], col, entry, (u32)(entry & 0x3FF), (u32)((entry >> 12) & 0xF));
        }
    }

    HostLogf("screenblk31=%04X %04X %04X %04X %04X %04X %04X %04X",
             vram[(31 * BG_SCREEN_SIZE) / 2 + 0], vram[(31 * BG_SCREEN_SIZE) / 2 + 1],
             vram[(31 * BG_SCREEN_SIZE) / 2 + 2], vram[(31 * BG_SCREEN_SIZE) / 2 + 3],
             vram[(31 * BG_SCREEN_SIZE) / 2 + 4], vram[(31 * BG_SCREEN_SIZE) / 2 + 5],
             vram[(31 * BG_SCREEN_SIZE) / 2 + 6], vram[(31 * BG_SCREEN_SIZE) / 2 + 7]);
    {
        char line[256];
        u32 n = 0;
        s32 dx;

        // 过场/标题画面时 gSaveBlock1Ptr 可能还指向未初始化区域（悬空指针），
        // 只判 NULL 不够：再用宿主地址探测确认整块可读，避免诊断代码自己崩溃。
        if (gSaveBlock1Ptr != NULL && HostMemoryIsReadable(gSaveBlock1Ptr, 0x200)
         && gMapHeader.mapLayout != NULL && HostMemoryIsReadable(gMapHeader.mapLayout, 0x40))
        {

        HostLogf("map group=%d num=%d pos=%d,%d layout=%p size=%dx%d border=%04X,%04X,%04X,%04X",
                 (int)gSaveBlock1Ptr->location.mapGroup, (int)gSaveBlock1Ptr->location.mapNum,
                 gSaveBlock1Ptr->pos.x, gSaveBlock1Ptr->pos.y,
                 (const void *)gMapHeader.mapLayout,
                 gMapHeader.mapLayout->width, gMapHeader.mapLayout->height,
                 gMapHeader.mapLayout->border[0], gMapHeader.mapLayout->border[1],
                 gMapHeader.mapLayout->border[2], gMapHeader.mapLayout->border[3]);

        for (dx = -18; dx <= 18; dx += 2)
            n += (u32)snprintf(line + n, sizeof(line) - n, "%04X ",
                               (unsigned)MapGridGetMetatileIdAt(gSaveBlock1Ptr->pos.x + dx,
                                                                gSaveBlock1Ptr->pos.y));
        HostLogf("grid row  y=%d: %s", gSaveBlock1Ptr->pos.y, line);

        n = 0;
        for (dx = -18; dx <= 18; dx += 2)
            n += (u32)snprintf(line + n, sizeof(line) - n, "%04X ",
                               (unsigned)MapGridGetMetatileIdAt(gSaveBlock1Ptr->pos.x + dx,
                                                                gSaveBlock1Ptr->pos.y - 10));
        HostLogf("grid row y-10: %s", line);

        // 备份地图（sBackupMapData）的边界余量必须是 MAPGRID_UNDEFINED(0x03FF)。
        // 如果这里是 0000，说明 CpuFastFill16 没把值写进去 → 地图外圈会画成
        // metatile 0（透明/黑），也就是玩家看到的整条黑带。
        {
            s32 bw = (s32)gMapHeader.mapLayout->width + MAP_OFFSET_W;
            s32 bh = (s32)gMapHeader.mapLayout->height + MAP_OFFSET_H;
            s32 rows = bw * bh;
            s32 corner = MAP_OFFSET + MAP_OFFSET * bw;
            s32 px = gSaveBlock1Ptr->pos.x;
            s32 py = gSaveBlock1Ptr->pos.y;

            if (rows <= 0 || rows > MAX_MAP_DATA_SIZE)
                rows = MAX_MAP_DATA_SIZE;

            HostLogf("backup w=%d h=%d undef=%04X [0]=%04X [1]=%04X corner(0,0)=%04X player=%04X last=%04X",
                     bw, bh, (unsigned)MAPGRID_UNDEFINED,
                     sBackupMapData[0], sBackupMapData[1], sBackupMapData[corner],
                     sBackupMapData[(py + MAP_OFFSET) * bw + (px + MAP_OFFSET)],
                     sBackupMapData[rows - 1]);

            n = 0;
            for (dx = -10; dx <= 10; dx += 2)
            {
                s32 bx = px + dx + MAP_OFFSET;
                s32 by = py + MAP_OFFSET;

                if (bx >= 0 && bx < bw && by >= 0 && by < bh)
                    n += (u32)snprintf(line + n, sizeof(line) - n, "%04X ", sBackupMapData[by * bw + bx]);
                else
                    n += (u32)snprintf(line + n, sizeof(line) - n, "---- ");
            }
            HostLogf("backup row blocks x-10..x+10: %s", line);
        }
        }
        else
        {
            // 标题画面/过场动画阶段还没有存档块，跳过地图相关的探针。
            HostLogf("map: saveblock/mapLayout not ready (skip map probes)");
        }
    }

    {
        u32 blackPixels = 0;
        u32 firstBlack = DISPLAY_WIDTH;
        u32 lastBlack = 0;

        for (i = 0; i < DISPLAY_WIDTH; i++)
        {
            if (pixels[y * DISPLAY_WIDTH + i] == 0)
            {
                if (blackPixels == 0)
                    firstBlack = i;
                lastBlack = i;
                blackPixels++;
            }
        }
        HostLogf("row%u black=%u first=%u last=%u", y, blackPixels, firstBlack, lastBlack);
    }

    {
        // 整帧每 16 像素一列统计纯黑像素数（满值 = DISPLAY_HEIGHT*16），
        // 用来精确量出黑区在屏幕上的位置与宽度。
        char line[256];
        u32 n = 0;
        u32 bandCounts[DISPLAY_WIDTH / 16];
        u32 band, x2, y2;

        for (band = 0; band < DISPLAY_WIDTH / 16; band++)
            bandCounts[band] = 0;
        for (y2 = 0; y2 < DISPLAY_HEIGHT; y2++)
            for (x2 = 0; x2 < DISPLAY_WIDTH; x2++)
                if (pixels[y2 * DISPLAY_WIDTH + x2] == 0)
                    bandCounts[x2 / 16]++;
        for (band = 0; band < DISPLAY_WIDTH / 16; band++)
            n += (u32)snprintf(line + n, sizeof(line) - n, "%u ", bandCounts[band]);
        HostLogf("black per 16px band (max %u): %s", DISPLAY_HEIGHT * 16, line);
    }

    {
        // 黑区所在行上，每 16 像素取一列，列出该列 BG3 / BG2 真正读到的 tilemap entry。
        // 若黑区的 entry 全为 0000 而相邻列非零，说明该处 tilemap 根本没被画上。
        char line[512];
        u32 n = 0;
        u32 band;

        for (band = 0; band < DISPLAY_WIDTH / 16; band++)
        {
            u32 cx = band * 16 + 8;
            u32 row3 = ((y + (REG_BG3VOFS & 0x1FF)) / 8) & 31;
            u32 col3 = ((cx + (REG_BG3HOFS & 0x1FF)) / 8) & 31;
            u32 row2 = ((y + (REG_BG2VOFS & 0x1FF)) / 8) & 31;
            u32 col2 = ((cx + (REG_BG2HOFS & 0x1FF)) / 8) & 31;
            u16 *map3 = (u16 *)(VRAM + (((REG_BG3CNT >> 8) & 0x1F) * BG_SCREEN_SIZE));
            u16 *map2 = (u16 *)(VRAM + (((REG_BG2CNT >> 8) & 0x1F) * BG_SCREEN_SIZE));

            n += (u32)snprintf(line + n, sizeof(line) - n, "%u:%04X/%04X ",
                               band, map3[row3 * 32 + col3], map2[row2 * 32 + col2]);
        }
        HostLogf("band BG3/BG2 entries: %s", line);

        // BG0 是窗口/文字框层：黑区若由它盖住，这里会看到非零 entry。
        n = 0;
        {
            u32 row0 = ((y + (REG_BG0VOFS & 0x1FF)) / 8) & 31;
            u16 *map0 = (u16 *)(VRAM + (((REG_BG0CNT >> 8) & 0x1F) * BG_SCREEN_SIZE));

            for (band = 0; band < DISPLAY_WIDTH / 16; band++)
            {
                u32 col0 = (((band * 16 + 8) + (REG_BG0HOFS & 0x1FF)) / 8) & 31;
                n += (u32)snprintf(line + n, sizeof(line) - n, "%u:%04X ", band, map0[row0 * 32 + col0]);
            }
        }
        HostLogf("band BG0 entries: %s", line);

        n = 0;
        {
            u32 row1 = ((y + (REG_BG1VOFS & 0x1FF)) / 8) & 31;
            u16 *map1 = (u16 *)(VRAM + (((REG_BG1CNT >> 8) & 0x1F) * BG_SCREEN_SIZE));

            for (band = 0; band < DISPLAY_WIDTH / 16; band++)
            {
                u32 col1 = (((band * 16 + 8) + (REG_BG1HOFS & 0x1FF)) / 8) & 31;
                n += (u32)snprintf(line + n, sizeof(line) - n, "%u:%04X ", band, map1[row1 * 32 + col1]);
            }
        }
        HostLogf("band BG1 entries: %s", line);
    }

    {
        // 关键判据：EWRAM 里的地图 tilemap 缓冲（DrawMetatile 画进去的内容）
        // 与 VRAM 里的实际内容是否一致。
        //   两者不一致  -> VRAM 拷贝（DMA3）丢了；
        //   两者一致且都是 0000 -> 画图时那几列本来就没写内容。
        u16 *ew[NUM_BACKGROUNDS];
        u16 *vr[NUM_BACKGROUNDS];
        u32 bgId;

        ew[1] = gOverworldTilemapBuffer_Bg1;
        ew[2] = gOverworldTilemapBuffer_Bg2;
        ew[3] = gOverworldTilemapBuffer_Bg3;
        vr[1] = (u16 *)(VRAM + (((REG_BG1CNT >> 8) & 0x1F) * BG_SCREEN_SIZE));
        vr[2] = (u16 *)(VRAM + (((REG_BG2CNT >> 8) & 0x1F) * BG_SCREEN_SIZE));
        vr[3] = (u16 *)(VRAM + (((REG_BG3CNT >> 8) & 0x1F) * BG_SCREEN_SIZE));

        for (bgId = 1; bgId < NUM_BACKGROUNDS; bgId++)
        {
            u32 diff = 0, firstIdx = 0, ewNz = 0, vrNz = 0, k;
            u16 firstEw = 0, firstVr = 0;

            if (ew[bgId] == NULL)
            {
                HostLogf("BG%u buffer: EWRAM NULL (vr nonzero=%u)", bgId, 0);
                continue;
            }

            for (k = 0; k < 1024; k++)
            {
                if (ew[bgId][k] != 0)
                    ewNz++;
                if (vr[bgId][k] != 0)
                    vrNz++;
                if (ew[bgId][k] != vr[bgId][k])
                {
                    if (diff == 0)
                    {
                        firstIdx = k;
                        firstEw = ew[bgId][k];
                        firstVr = vr[bgId][k];
                    }
                    diff++;
                }
            }

            HostLogf("BG%u ew-vs-vr diff=%u/%u ewNz=%u vrNz=%u first@%u ew=%04X vr=%04X",
                     bgId, diff, 1024, ewNz, vrNz, firstIdx, firstEw, firstVr);
        }
    }

    {
        // 同一行上每 16 像素一列，列出 EWRAM 缓冲里对应位置的 entry，
        // 与上面的 VRAM 采样逐列对照。
        char line[512];
        u32 band;
        u32 bgId;

        for (bgId = 2; bgId <= 3; bgId++)
        {
            u16 *ew = (bgId == 2) ? gOverworldTilemapBuffer_Bg2 : gOverworldTilemapBuffer_Bg3;
            u16 hofs = (bgId == 2) ? REG_BG2HOFS : REG_BG3HOFS;
            u16 vofs = (bgId == 2) ? REG_BG2VOFS : REG_BG3VOFS;
            u32 row = ((y + (vofs & 0x1FF)) / 8) & 31;
            u32 n = 0;

            if (ew == NULL)
            {
                HostLogf("band BG%u EWRAM entries: (buffer freed)", bgId);
                continue;
            }

            for (band = 0; band < DISPLAY_WIDTH / 16; band++)
            {
                u32 col = (((band * 16 + 8) + (hofs & 0x1FF)) / 8) & 31;

                n += (u32)snprintf(line + n, sizeof(line) - n, "%u:%04X ",
                                   band, ew[row * 32 + col]);
            }
            HostLogf("band BG%u EWRAM entries: %s", bgId, line);
        }
    }

    for (bg = 1; bg < NUM_BACKGROUNDS; bg++)
    {
        u16 cnt = (bg == 1) ? REG_BG1CNT : (bg == 2) ? REG_BG2CNT : REG_BG3CNT;
        const u16 *map = (const u16 *)(VRAM + ((cnt >> 8) & 0x1F) * BG_SCREEN_SIZE);
        u32 nonzero = 0;
        u32 r, c;

        for (r = 0; r < 32; r++)
            for (c = 0; c < 32; c++)
                if (map[r * 32 + c] != 0)
                    nonzero++;
        HostLogf("%s nonzero entries=%u/1024", BgLayerName(bg), nonzero);
    }

    if (gHostProbeValid)
    {
        // 探针：上一帧 DrawScanline 在 y=gHostProbeRow 上实际生效的窗口状态与各像素归属。
        char line[DISPLAY_WIDTH + 64];
        u32 n = 0;
        u32 wins[DISPLAY_WIDTH];
        u32 backdrop = 0;

        memset(wins, 0, sizeof(wins));
        for (i = 0; i < DISPLAY_WIDTH; i++)
        {
            wins[gHostProbeWinners[i] & 0x1F]++;
            if (gHostProbeWinners[i] == 0xFF)
                backdrop++;
        }
        HostLogf("probe row=%d valid=%d windowsEnabled=%d", gHostProbeRow, gHostProbeValid, gHostProbeValid == 2);
        HostLogf("probe regs W0H=%04X W0V=%04X W1H=%04X W1V=%04X WININ=%04X WINOUT=%04X DISPCNT=%04X BLDCNT=%04X",
                 gHostProbeRegs[0], gHostProbeRegs[1], gHostProbeRegs[2], gHostProbeRegs[3],
                 gHostProbeRegs[4], gHostProbeRegs[5], gHostProbeRegs[6], gHostProbeRegs[7]);
        HostLogf("probe winners bg0=%u bg1=%u bg2=%u bg3=%u obj=%u backdrop=%u",
                 wins[0], wins[1], wins[2], wins[3], wins[0x10] + wins[0x11] + wins[0x12] + wins[0x13],
                 backdrop);

        n = 0;
        for (i = 0; i < DISPLAY_WIDTH; i += 8)
            n += (u32)snprintf(line + n, sizeof(line) - n, "%02X ", gHostProbeWinners[i]);
        HostLogf("probe winners per 8px: %s", line);

        n = 0;
        for (i = 0; i < DISPLAY_WIDTH; i += 8)
            n += (u32)snprintf(line + n, sizeof(line) - n, "%02X ", gHostProbeWinMask[i]);
        HostLogf("probe winmask per 8px: %s", line);

        n = 0;
        for (i = 0; i < DISPLAY_WIDTH; i += 8)
            n += (u32)snprintf(line + n, sizeof(line) - n, "%04X ", pixels[gHostProbeRow * DISPLAY_WIDTH + i]);
        HostLogf("probe pixels per 8px: %s", line);
    }

    {
        // BG0（窗口层）在 VRAM 里的实际分布：定位黑区/白区对应的行与列范围。
        const u16 *map0v = (const u16 *)(VRAM + (((REG_BG0CNT >> 8) & 0x1F) * BG_SCREEN_SIZE));
        char rows[DISPLAY_WIDTH + 64];
        u32 r, c, nb = 0, minx = 32, maxx = 0, miny = 32, maxy = 0;
        char *p = rows;

        for (r = 0; r < 32; r++)
        {
            u32 cnt = 0;

            for (c = 0; c < 32; c++)
            {
                if (map0v[r * 32 + c] != 0)
                {
                    cnt++;
                    if (c < minx) minx = c;
                    if (c > maxx) maxx = c;
                    if (r < miny) miny = r;
                    if (r > maxy) maxy = r;
                }
            }
            nb += cnt;
            p += snprintf(p, 5, "%u,", cnt);
        }
        *p = '\0';
        HostLogf("BG0 vram nonzero=%u rows=%s", nb, rows);
        if (nb != 0)
            HostLogf("BG0 vram bbox cols %u..%u rows %u..%u", minx, maxx, miny, maxy);
    }

    {
        // 窗口层诊断：BG0 的 tilemap 缓冲指针与窗口表，定位「左侧被窗口覆盖」的来源。
        static const char *const wnames[] = {"BG0", "BG1", "BG2", "BG3"};
        u32 w;

        HostLogf("bgmeta=1:%u 2:%u 3:%u", GetBgAttribute(1, BG_ATTR_METRIC), GetBgAttribute(2, BG_ATTR_METRIC), GetBgAttribute(3, BG_ATTR_METRIC));
        HostLogf("ovbuf=%p/%p/%p bgbuf=%p/%p/%p",
                 (void *)gOverworldTilemapBuffer_Bg1, (void *)gOverworldTilemapBuffer_Bg2, (void *)gOverworldTilemapBuffer_Bg3,
                 GetBgTilemapBuffer(1), GetBgTilemapBuffer(2), GetBgTilemapBuffer(3));
        HostLogf("bgbuf0=%p winbuf0=%p bg0metric=%u",
                 GetBgTilemapBuffer(0), gWindowBgTilemapBuffers[0], GetBgAttribute(0, BG_ATTR_METRIC));

        for (w = 0; w < 16; w++)
        {
            u8 wbg = gWindows[w].window.bg;

            if (wbg != 0xFF)
                HostLogf("win%02u bg=%s(%u) left=%u top=%u w=%u h=%u pal=%u base=%u data=%p",
                         w, (wbg < 4) ? wnames[wbg] : "?", wbg,
                         gWindows[w].window.tilemapLeft, gWindows[w].window.tilemapTop,
                         gWindows[w].window.width, gWindows[w].window.height,
                         gWindows[w].window.paletteNum, gWindows[w].window.baseBlock,
                         (void *)gWindows[w].tileData);
        }
    }

    HeapLogSummary();

    HostLogf("---- end frame dump ----");
}

bool Platform_PresentFrame(void)
{
    static u16 gbaPixels[DISPLAY_WIDTH * DISPLAY_HEIGHT];
    static uint32_t argbPixels[DISPLAY_WIDTH * DISPLAY_HEIGHT];
    Uint64 presentStart;

    if (!sInitialized)
        return false;

    presentStart = SDL_GetPerformanceCounter();

    DrawFrame(gbaPixels);
    // 地图初始化那一刻画面上还什么都没有（调色板/tilemap 尚未装入），
    // 所以自动快照要等淡入结束、画面稳定之后再抓。
    if (sFrameDumpRequest
     && (sFrameDumpImmediate != 0
      || (sPresentFrameCount >= sFrameDumpRequestFrame + 45 && gPaletteFade.active == 0)))
    {
        sFrameDumpRequest = 0;
        sFrameDumpImmediate = 0;
        DumpFrameDiagnostics(gbaPixels);
    }
    else if (gHostProbeValid == 2)
    {
        // 野外正常情况下 y=80 那一行不该被 BG0（窗口/文字框层）覆盖：
        // 对话框在第 15~18 行、地名弹窗在顶部。一旦这一行有较多像素由 BG0
        // 提供，就是「某块窗口被画到了屏幕中间」的坏帧，自动抓一份快照。
        static u32 lastAutoDumpFrame;
        static u32 autoDumps;
        static u32 emptyStreak;
        u32 bg0Cover = 0;
        u32 emptyRun = 0;
        u32 maxEmptyRun = 0;
        u32 tileDiff = 0;
        u32 i;
        const u16 *pltt = (const u16 *)PLTT;

        for (i = 0; i < DISPLAY_WIDTH; i++)
        {
            if (gHostProbeWinners[i] == 0)
                bg0Cover++;

            // 0xFF = 这一列没有任何层（BG0..BG3 / OBJ）画过，露出背景色。
            // 野外正常画面上地图三层铺满全屏，不该出现这种连续空带。
            if (gHostProbeWinners[i] == 0xFF)
            {
                emptyRun++;
                if (emptyRun > maxEmptyRun)
                    maxEmptyRun = emptyRun;
            }
            else
            {
                emptyRun = 0;
            }
        }

        // 地图载入过程中 EWRAM 里的 tilemap 已经画好、VRAM 还是全 0，
        // 这时整屏黑是正常过渡，不能当坏帧抓。只有两边一致才算「画面已稳定」。
        if (gOverworldTilemapBuffer_Bg2 != NULL)
        {
            const u16 *ew2 = gOverworldTilemapBuffer_Bg2;
            const u16 *vr2 = (const u16 *)(VRAM + (((REG_BG2CNT >> 8) & 0x1F) * BG_SCREEN_SIZE));

            for (i = 0; i < 1024; i++)
            {
                if (ew2[i] != vr2[i])
                    tileDiff++;
            }
        }

        // 稳定的野外画面 = 地图 tilemap 已进 VRAM + 地图调色板已装入 + 没有淡入淡出
        if (tileDiff == 0 && pltt[0] != 0 && gPaletteFade.active == 0
         && gOverworldTilemapBuffer_Bg2 != NULL
         && (maxEmptyRun >= 40 || bg0Cover >= 24))
            emptyStreak++;
        else
            emptyStreak = 0;

        // 连续 24 帧都成立才抓，彻底排除载入/淡入那几帧的假报警。
        if (emptyStreak >= 24
         && autoDumps < 8
         && AutoDumpEnabled()
         && (lastAutoDumpFrame == 0 || sPresentFrameCount - lastAutoDumpFrame >= 600))
        {
            lastAutoDumpFrame = sPresentFrameCount;
            autoDumps++;
            sFrameDumpReason = (maxEmptyRun >= 40)
                             ? "auto: empty band on row80 (no layer)"
                             : "auto: bg0 covers row80";
            HostLogf("auto dump gate: maxEmptyRun=%u bg0Cover=%u tileDiff=%u pltt0=%04X streak=%u",
                     maxEmptyRun, bg0Cover, tileDiff, pltt[0], emptyStreak);
            DumpFrameDiagnostics(gbaPixels);
        }
    }

    // Navigator（pokenav 主菜单）一进来就同时具备：BLDCNT 只对 OBJ 做亮度增高、
    // WIN0 覆盖整屏、扫描线 HBlank DMA 在逐行改 WIN0H。这个组合只出现在这个菜单。
    // 默认关闭；设 PE_AUTO_DUMP=1（或 PE_DUMP_POKENAV=1）时才抓，
    // 每次运行最多 3 份、间隔 600 帧，用来复现 Navigator 的调色板/图块问题。
    // （只写文本 + frame_N.ppm；那堆 .bin 由 PE_DUMP_BINS 单独控制）
    if (REG_BLDCNT == (BLDCNT_TGT1_OBJ | BLDCNT_EFFECT_LIGHTEN) && AutoDumpEnabled())
    {
        static u32 pokenavDumps;
        static u32 lastPokenavDumpFrame;

        if (pokenavDumps < 3
         && (lastPokenavDumpFrame == 0 || sPresentFrameCount - lastPokenavDumpFrame >= 600))
        {
            lastPokenavDumpFrame = sPresentFrameCount;
            pokenavDumps++;
            sFrameDumpReason = "auto: pokenav glow";
            HostLogf("auto dump gate: pokenav bldcnt=%04X bldy=%02X scanline=%u win0h=%04X win0v=%04X",
                     REG_BLDCNT, REG_BLDY, (u32)gScanlineEffect.state, REG_WIN0H, REG_WIN0V);
            DumpFrameDiagnostics(gbaPixels);
        }
    }
    sPresentFrameCount++;
    for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++)
        argbPixels[i] = GbaColorToArgb(gbaPixels[i]);
    DrawSpeedOverlay(argbPixels);

    if (SDL_UpdateTexture(sTexture, NULL, argbPixels,
                          DISPLAY_WIDTH * sizeof(argbPixels[0])) != 0)
        return false;
    if (SDL_RenderClear(sRenderer) != 0)
        return false;
    if (SDL_RenderCopy(sRenderer, sTexture, NULL, NULL) != 0)
        return false;

    SDL_RenderPresent(sRenderer);

    REG_VCOUNT = DISPLAY_HEIGHT;
    REG_DISPSTAT |= DISPSTAT_VBLANK;
    RunDMAs(DMA_VBLANK);
    if (REG_IME != 0
     && (REG_IE & INTR_FLAG_VBLANK)
     && (REG_DISPSTAT & DISPSTAT_VBLANK_INTR))
        gIntrTable[4]();
    REG_DISPSTAT &= ~DISPSTAT_VBLANK;
    REG_VCOUNT = DISPLAY_HEIGHT + 1;

    {
        Uint64 now = SDL_GetPerformanceCounter();
        Uint32 presentMs = DiagTicksToMs(now - presentStart);

        sDiagPresentTicks += now - presentStart;
        if (presentMs > sDiagMaxPresentMs)
            sDiagMaxPresentMs = presentMs;

        if (++sDiagFrameCount >= 60)
        {
            Uint32 audioKB = sAudioDevice != 0
                           ? (Uint32)(SDL_GetQueuedAudioSize(sAudioDevice) / 1024) : 0;

            // 每 60 帧一行性能汇总：logicMax 是游戏线程一帧逻辑的最长耗时，
            // presentMax 是呈现路径的最长耗时，audioQ 是音频队列深度。
            // 掉帧时看这两个最大值是谁造成的，比猜快得多。
            HostLogf("perf n=%u logicAvg=%.1fms logicMax=%ums waitAvg=%.1fms waitMax=%ums presentAvg=%.1fms presentMax=%ums audioQ=%uKB",
                    sDiagFrameCount,
                    (double)DiagTicksToMs(sDiagLogicTicks) / (double)sDiagFrameCount,
                    sDiagMaxLogicMs,
                    (double)DiagTicksToMs(sDiagWaitTicks) / (double)sDiagFrameCount,
                    sDiagMaxWaitMs,
                    (double)DiagTicksToMs(sDiagPresentTicks) / (double)sDiagFrameCount,
                    sDiagMaxPresentMs,
                    audioKB);
            sDiagFrameCount = 0;
            sDiagLogicTicks = 0;
            sDiagWaitTicks = 0;
            sDiagPresentTicks = 0;
            sDiagMaxWaitMs = 0;
            sDiagMaxPresentMs = 0;
            sDiagMaxLogicMs = 0;
        }
        sDiagLastPresentEnd = now;
    }
    return true;
}

bool Platform_PresentPendingFrame(void)
{
    bool result;

    if (!SDL_AtomicCAS(&sFramePending, 1, 0))
        return false;

    result = Platform_PresentFrame();
    SDL_SemPost(sVBlankSemaphore);
    return result;
}

bool Platform_IsFramePending(void)
{
    return SDL_AtomicGet(&sFramePending) != 0;
}

bool Platform_ReadPixel(int x, int y, uint32_t *pixel)
{
    SDL_Rect rect = {x, y, 1, 1};

    if (!sInitialized || pixel == NULL)
        return false;
    return SDL_RenderReadPixels(sRenderer, &rect, SDL_PIXELFORMAT_ARGB8888,
                                pixel, sizeof(*pixel)) == 0;
}

u16 Platform_GetKeyInput(void)
{
    u16 keys;

    LockInputState();
    keys = sKeyboardKeys | sControllerKeys | sControllerAxisKeys;
    UnlockInputState();
    return keys;
}

u32 Platform_GetSpeedMultiplier(void)
{
    u32 multiplier;

    LockInputState();
    multiplier = sSpeedMultiplier;
    UnlockInputState();
    return multiplier;
}

bool Platform_IsFastForward(void)
{
    bool fastForward;

    LockInputState();
    fastForward = sFastForwardHeld;
    UnlockInputState();
    return fastForward;
}

// 逐帧记录「时间 + 滚动 + 相机」，用来分辨「卡顿」到底是掉帧（dt 尖峰）
// 还是画面步进不匀（scroll 每帧不是稳定地 +1）。单独一个文件、
// 句柄只开一次，避免每帧 fopen 的抖动本身影响测量。
static void HostFrameTrace(void)
{
    static FILE *traceFile;
    static Uint64 lastCounter;
    static Uint64 lastCpuUs;
    static u32 line;

    // 默认就记（不需要 PE_TRACE，省得作者为了一次取证再设环境变量），
    // 上限 20000 帧（约 5 分钟），超出后自动停写，文件不会无限长大。
    if (line >= 20000)
        return;

    if (traceFile == NULL)
    {
        // 每次运行单独一个文件（带进程号），这样连跑两次对比也不会互相覆盖。
        char name[64];

#ifdef _WIN32
        snprintf(name, sizeof(name), "frame_trace_%lu.log", (unsigned long)GetCurrentProcessId());
#else
        snprintf(name, sizeof(name), "frame_trace_%lu.log", (unsigned long)0);
#endif
        traceFile = fopen(name, "w");
        if (traceFile == NULL)
            return;
        fprintf(traceFile, "# n dtUs hofs1 vofs1 camX camY sprX sprY posX posY keys cpuUs\n");
        lastCounter = SDL_GetPerformanceCounter();
        lastCpuUs = DiagProcessCpuUs();
    }

    {
        Uint64 now = SDL_GetPerformanceCounter();
        Uint64 freq = SDL_GetPerformanceFrequency();
        Uint64 cpuNowUs;
        u32 dtUs = (freq != 0) ? (u32)(((now - lastCounter) * 1000000) / freq) : 0;
        int posX = (gSaveBlock1Ptr != NULL) ? (int)gSaveBlock1Ptr->pos.x : -1;
        int posY = (gSaveBlock1Ptr != NULL) ? (int)gSaveBlock1Ptr->pos.y : -1;

        lastCounter = now;
        // keys：当时按着的 GBA 按键（A=001 B=002 SELECT=004 START=008
        // RIGHT=010 LEFT=020 UP=040 DOWN=080 R=100 L=200）。把 dt 尖峰和
        // 按着的方向对上，才能判定卡顿是不是跟着某个方向走。
        // cpuUs：这一帧进程实际烧掉的 CPU 时间（微秒）。
        cpuNowUs = DiagProcessCpuUs();
        fprintf(traceFile, "%u %u %u %u %d %d %d %d %d %d %03X %u\n",
                line++, dtUs,
                (unsigned)REG_BG1HOFS, (unsigned)REG_BG1VOFS,
                (int)gTotalCameraPixelOffsetX, (int)gTotalCameraPixelOffsetY,
                (int)gSpriteCoordOffsetX, (int)gSpriteCoordOffsetY,
                posX, posY, (unsigned)Platform_GetKeyInput(),
                (unsigned)(cpuNowUs - lastCpuUs));
        lastCpuUs = cpuNowUs;
        if ((line % 30) == 0)
            fflush(traceFile);
    }
}

void VBlankIntrWait(void)
{
    Uint64 waitStart;
    Uint64 waitEnd;
    Uint32 waitMs;

    if (!sInitialized)
        return;

    waitStart = SDL_GetPerformanceCounter();
    if (sDiagLastPresentEnd != 0)
    {
        Uint32 logicMs = DiagTicksToMs(waitStart - sDiagLastPresentEnd);

        sDiagLogicTicks += waitStart - sDiagLastPresentEnd;
        if (logicMs > sDiagMaxLogicMs)
            sDiagMaxLogicMs = logicMs;
    }

    SDL_AtomicSet(&sFramePending, 1);
    if (Platform_IsQuitRequested())
        return;
    SDL_SemWait(sVBlankSemaphore);

    waitEnd = SDL_GetPerformanceCounter();
    waitMs = DiagTicksToMs(waitEnd - waitStart);
    sDiagWaitTicks += waitEnd - waitStart;
    if (waitMs > sDiagMaxWaitMs)
        sDiagMaxWaitMs = waitMs;
    if (waitMs > 25 && HostTraceEnabled())
        fprintf(stderr, "[host] long vblank wait: %ums\n", waitMs);

    HostFrameTrace();
}

void SoftReset(u32 resetFlags)
{
    (void)resetFlags;
    Platform_RequestQuit();
    Platform_ExitGameThread();
}

#endif // PLATFORM_SDL2
