#include <stdio.h>
#include <setjmp.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "gba/types.h"
#include "gba/gba.h"
#include "gpu_regs.h"
#include "platform.h"

static int sFailures;
static int sVBlankInterrupts;

#define CHECK(expression)                                                     \
    do                                                                        \
    {                                                                         \
        if (!(expression))                                                    \
        {                                                                     \
            fprintf(stderr, "SDL smoke check failed: %s:%d: %s\n",          \
                    __FILE__, __LINE__, #expression);                         \
            sFailures++;                                                      \
        }                                                                     \
    } while (0)

static void NoopInterrupt(void)
{}

static void CountVBlankInterrupt(void)
{
    sVBlankInterrupts++;
}

void (*gIntrTable[])(void) =
{
    NoopInterrupt,
    NoopInterrupt,
    NoopInterrupt,
    NoopInterrupt,
    CountVBlankInterrupt,
};

static void ResetVideoState(u16 backdrop)
{
    memset(REG_BASE, 0, 0x400);
    memset(PLTT, 0, PLTT_SIZE);
    memset(VRAM_, 0, VRAM_SIZE);
    memset(OAM, 0, OAM_SIZE);
    *(u16 *)PLTT = backdrop;
    sVBlankInterrupts = 0;
}

static void PushKeyEvent(Uint32 type, SDL_Keycode keycode)
{
    SDL_Event event;

    SDL_zero(event);
    event.type = type;
    event.key.type = type;
    event.key.state = type == SDL_KEYDOWN ? SDL_PRESSED : SDL_RELEASED;
    event.key.keysym.sym = keycode;
    event.key.keysym.scancode = SDL_GetScancodeFromKey(keycode);
    CHECK(SDL_PushEvent(&event) == 1);
}

static void PushUnknownScancodeKeyEvent(Uint32 type, SDL_Keycode keycode)
{
    SDL_Event event;

    SDL_zero(event);
    event.type = type;
    event.key.type = type;
    event.key.state = type == SDL_KEYDOWN ? SDL_PRESSED : SDL_RELEASED;
    event.key.keysym.sym = keycode;
    event.key.keysym.scancode = SDL_SCANCODE_UNKNOWN;
    CHECK(SDL_PushEvent(&event) == 1);
}

static void PushWindowFocusEvent(Uint8 windowEvent)
{
    SDL_Event event;

    SDL_zero(event);
    event.type = SDL_WINDOWEVENT;
    event.window.type = SDL_WINDOWEVENT;
    event.window.event = windowEvent;
    CHECK(SDL_PushEvent(&event) == 1);
}

static void PushControllerButtonEvent(Uint32 type, Uint8 button)
{
    SDL_Event event;

    SDL_zero(event);
    event.type = type;
    event.cbutton.type = type;
    event.cbutton.state = type == SDL_CONTROLLERBUTTONDOWN
        ? SDL_PRESSED
        : SDL_RELEASED;
    event.cbutton.button = button;
    CHECK(SDL_PushEvent(&event) == 1);
}

static void PushControllerAxisEvent(Uint8 axis, Sint16 value)
{
    SDL_Event event;

    SDL_zero(event);
    event.type = SDL_CONTROLLERAXISMOTION;
    event.caxis.type = SDL_CONTROLLERAXISMOTION;
    event.caxis.axis = axis;
    event.caxis.value = value;
    CHECK(SDL_PushEvent(&event) == 1);
}

static int WaitForVBlank(void *unused)
{
    (void)unused;
    VBlankIntrWait();
    return 17;
}

static SDL_atomic_t sSoftResetBefore;
static SDL_atomic_t sSoftResetAfter;
static jmp_buf sSoftResetExit;

void Platform_ExitGameThread(void)
{
    longjmp(sSoftResetExit, 1);
}

static int TriggerSoftReset(void *unused)
{
    (void)unused;
    if (setjmp(sSoftResetExit) == 0)
    {
        SDL_AtomicSet(&sSoftResetBefore, 1);
        SoftReset(RESET_ALL);
        SDL_AtomicSet(&sSoftResetAfter, 1);
        return 29;
    }
    return 0;
}

static void TestInput(void)
{
    PushKeyEvent(SDL_KEYDOWN, SDLK_z);
    PushKeyEvent(SDL_KEYDOWN, SDLK_RIGHT);
    CHECK(Platform_PumpEvents());
    CHECK((Platform_GetKeyInput() & A_BUTTON) != 0);
    CHECK((Platform_GetKeyInput() & DPAD_RIGHT) != 0);

    PushKeyEvent(SDL_KEYUP, SDLK_z);
    PushKeyEvent(SDL_KEYUP, SDLK_RIGHT);
    CHECK(Platform_PumpEvents());
    CHECK(Platform_GetKeyInput() == 0);

    PushKeyEvent(SDL_KEYDOWN, SDLK_KP_ENTER);
    CHECK(Platform_PumpEvents());
    CHECK((Platform_GetKeyInput() & START_BUTTON) != 0);
    PushKeyEvent(SDL_KEYUP, SDLK_KP_ENTER);
    CHECK(Platform_PumpEvents());
    CHECK(Platform_GetKeyInput() == 0);

    PushUnknownScancodeKeyEvent(SDL_KEYDOWN, SDLK_z);
    CHECK(Platform_PumpEvents());
    CHECK((Platform_GetKeyInput() & A_BUTTON) != 0);
    PushUnknownScancodeKeyEvent(SDL_KEYUP, SDLK_z);
    CHECK(Platform_PumpEvents());
    CHECK(Platform_GetKeyInput() == 0);

    PushKeyEvent(SDL_KEYDOWN, SDLK_z);
    PushWindowFocusEvent(SDL_WINDOWEVENT_FOCUS_LOST);
    CHECK(Platform_PumpEvents());
    CHECK(Platform_GetKeyInput() == 0);

    PushKeyEvent(SDL_KEYDOWN, SDLK_x);
    PushControllerButtonEvent(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_A);
    PushControllerButtonEvent(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_START);
    PushControllerAxisEvent(SDL_CONTROLLER_AXIS_LEFTX, -20000);
    PushControllerAxisEvent(SDL_CONTROLLER_AXIS_LEFTY, 20000);
    CHECK(Platform_PumpEvents());
    CHECK((Platform_GetKeyInput() & B_BUTTON) != 0);
    CHECK((Platform_GetKeyInput() & A_BUTTON) != 0);
    CHECK((Platform_GetKeyInput() & START_BUTTON) != 0);
    CHECK((Platform_GetKeyInput() & DPAD_LEFT) != 0);
    CHECK((Platform_GetKeyInput() & DPAD_DOWN) != 0);

    PushKeyEvent(SDL_KEYUP, SDLK_x);
    PushControllerButtonEvent(SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_A);
    PushControllerButtonEvent(SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_START);
    PushControllerAxisEvent(SDL_CONTROLLER_AXIS_LEFTX, 0);
    PushControllerAxisEvent(SDL_CONTROLLER_AXIS_LEFTY, 0);
    CHECK(Platform_PumpEvents());
    CHECK(Platform_GetKeyInput() == 0);
}

static bool HasSpeedOverlayPixels(void)
{
    uint32_t pixel;

    for (int y = 0; y < 18; y++)
    {
        for (int x = DISPLAY_WIDTH - 40; x < DISPLAY_WIDTH; x++)
        {
            if (Platform_ReadPixel(x, y, &pixel) && pixel == 0xFFFFFFFF)
                return true;
        }
    }

    return false;
}

static void TestHostSpeedControls(void)
{
    CHECK(Platform_GetSpeedMultiplier() == 1);
    CHECK(!Platform_IsFastForward());

    PushKeyEvent(SDL_KEYDOWN, SDLK_PLUS);
    CHECK(Platform_PumpEvents());
    CHECK(Platform_GetSpeedMultiplier() == 2);
    CHECK(!Platform_IsFastForward());
    CHECK(Platform_PresentFrame());
    CHECK(HasSpeedOverlayPixels());

    PushKeyEvent(SDL_KEYUP, SDLK_PLUS);
    CHECK(Platform_PumpEvents());
    PushKeyEvent(SDL_KEYDOWN, SDLK_PLUS);
    CHECK(Platform_PumpEvents());
    CHECK(Platform_GetSpeedMultiplier() == 3);

    PushKeyEvent(SDL_KEYUP, SDLK_PLUS);
    PushKeyEvent(SDL_KEYDOWN, SDLK_SPACE);
    CHECK(Platform_PumpEvents());
    CHECK(Platform_IsFastForward());
    CHECK(Platform_PresentFrame());
    CHECK(HasSpeedOverlayPixels());

    PushKeyEvent(SDL_KEYUP, SDLK_SPACE);
    CHECK(Platform_PumpEvents());
    CHECK(!Platform_IsFastForward());
    CHECK(Platform_GetSpeedMultiplier() == 3);
}

static void TestAudioQueue(void)
{
    s8 right[32];
    s8 left[32];
    const u32 count = (u32)(sizeof(right) / sizeof(right[0]));

    for (u32 i = 0; i < count; i++)
    {
        right[i] = (s8)(i - 16);
        left[i] = (s8)(16 - i);
    }
    // The dummy driver may decline an audio device, but the queue boundary
    // must remain a no-op instead of affecting video/event startup.
    Platform_QueueAudio(right, left, count, 13379);
}

static void TestMonotonicCycleCounter(void)
{
    u64 before = Platform_GetGbaCycleCount();

    SDL_Delay(2);
    CHECK(Platform_GetGbaCycleCount() > before);
}

static void TestVBlankInterruptGating(void)
{
    REG_IME = 0;
    CHECK(Platform_PresentFrame());
    CHECK(sVBlankInterrupts == 1);

    REG_IME = 1;
    REG_IE = 0;
    CHECK(Platform_PresentFrame());
    CHECK(sVBlankInterrupts == 1);

    REG_IE = INTR_FLAG_VBLANK;
    CHECK(Platform_PresentFrame());
    CHECK(sVBlankInterrupts == 2);
}

static void TestVBlankHandshake(void)
{
    SDL_Thread *thread = SDL_CreateThread(WaitForVBlank, "vblank-smoke", NULL);
    int threadResult = 0;
    int timeout = 1000;

    CHECK(thread != NULL);
    if (thread == NULL)
        return;

    while (!Platform_IsFramePending() && timeout-- > 0)
        SDL_Delay(1);

    CHECK(Platform_IsFramePending());
    CHECK(Platform_PresentPendingFrame());
    SDL_WaitThread(thread, &threadResult);
    CHECK(threadResult == 17);
    CHECK(!Platform_IsFramePending());
}

static void TestQuitEvent(void)
{
    SDL_Event event;

    SDL_zero(event);
    event.type = SDL_QUIT;
    CHECK(SDL_PushEvent(&event) == 1);
    CHECK(!Platform_PumpEvents());
}

static void TestSoftReset(void)
{
    SDL_Thread *vblankThread;
    SDL_Thread *resetThread;
    int vblankResult = 0;
    int resetResult = -1;
    int timeout = 1000;

    CHECK(!Platform_IsQuitRequested());
    SDL_AtomicSet(&sSoftResetBefore, 0);
    SDL_AtomicSet(&sSoftResetAfter, 0);

    vblankThread = SDL_CreateThread(WaitForVBlank, "soft-reset-vblank", NULL);
    CHECK(vblankThread != NULL);
    if (vblankThread == NULL)
        return;
    while (!Platform_IsFramePending() && timeout-- > 0)
        SDL_Delay(1);
    CHECK(Platform_IsFramePending());

    resetThread = SDL_CreateThread(TriggerSoftReset, "soft-reset", NULL);
    CHECK(resetThread != NULL);
    if (resetThread == NULL)
    {
        Platform_RequestQuit();
        SDL_WaitThread(vblankThread, &vblankResult);
        return;
    }

    SDL_WaitThread(resetThread, &resetResult);
    SDL_WaitThread(vblankThread, &vblankResult);
    CHECK(Platform_IsQuitRequested());
    CHECK(SDL_AtomicGet(&sSoftResetBefore) == 1);
    CHECK(SDL_AtomicGet(&sSoftResetAfter) == 0);
    CHECK(resetResult == 0);
    CHECK(vblankResult == 17);
    CHECK(!Platform_IsFramePending());
}

int main(void)
{
    const char *scaleQuality;
    uint32_t pixel = 0;

    ResetVideoState(0x001F);
    CHECK(Platform_Init("pokeemerald-expansion SDL smoke", 1, true));
    scaleQuality = SDL_GetHint(SDL_HINT_RENDER_SCALE_QUALITY);
    CHECK(scaleQuality != NULL && strcmp(scaleQuality, "0") == 0);
    CHECK(REG_VCOUNT == DISPLAY_HEIGHT + 1);
    InitGpuRegManager();
    REG_IME = 1;
    EnableInterrupts(INTR_FLAG_VBLANK);
    CHECK((REG_DISPSTAT & DISPSTAT_VBLANK_INTR) != 0);
    if (sFailures != 0)
        return 1;

    CHECK(Platform_PresentFrame());
    CHECK(sVBlankInterrupts == 1);
    CHECK(Platform_ReadPixel(0, 0, &pixel));
    CHECK(pixel == 0xFFFF0000);
    TestVBlankInterruptGating();
    TestInput();
    TestHostSpeedControls();
    TestAudioQueue();
    TestMonotonicCycleCounter();
    TestVBlankHandshake();
    // TestHostSpeedControls presents two additional frames while checking
    // the host overlay (one selected multiplier and one fast-forward frame).
    CHECK(sVBlankInterrupts == 5);
    TestSoftReset();
    Platform_Shutdown();

    CHECK(Platform_Init("pokeemerald-expansion SDL quit smoke", 1, true));
    CHECK(!Platform_IsQuitRequested());
    TestQuitEvent();
    Platform_Shutdown();

    if (sFailures != 0)
        return 1;

    puts("native SDL smoke checks passed");
    return 0;
}
