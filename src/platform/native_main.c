#ifdef PLATFORM_SDL2

#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#include <SDL2/SDL.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "platform.h"

void AgbMain(void);

static jmp_buf sGameThreadExit;

void HostLogf(const char *fmt, ...)
{
    va_list args;
    FILE *logFile = fopen("crash.log", "a");

    if (logFile == NULL)
        return;

    fprintf(logFile, "[log] ");
    va_start(args, fmt);
    vfprintf(logFile, fmt, args);
    va_end(args);
    fprintf(logFile, "\n");
    fflush(logFile);
    fclose(logFile);
}

#ifdef _WIN32
// The Windows build crashes without leaving any trace when the fault happens on
// the game thread, so log the exception code and faulting address before the
// process dies.
static LONG WINAPI HostCrashFilter(EXCEPTION_POINTERS *info)
{
    FILE *logFile = fopen("crash.log", "a");

    if (logFile != NULL)
    {
        fprintf(logFile, "[crash] code=0x%08lx addr=%p\n",
                (unsigned long)info->ExceptionRecord->ExceptionCode,
                info->ExceptionRecord->ExceptionAddress);

        // ExceptionInformation[0] 是访问类型（0 读 / 1 写 / 8 执行），
        // [1] 才是真正出问题的数据地址——定位野指针时这一项最关键。
        if (info->ExceptionRecord->NumberParameters >= 2)
        {
            const char *access = "unknown";

            switch (info->ExceptionRecord->ExceptionInformation[0])
            {
            case 0: access = "read";  break;
            case 1: access = "write"; break;
            case 8: access = "exec";  break;
            }

            fprintf(logFile, "[crash] fault=%p access=%s\n",
                    (void *)(uintptr_t)info->ExceptionRecord->ExceptionInformation[1],
                    access);
        }
#ifdef _M_IX86
        {
            DWORD *ebp = (DWORD *)(uintptr_t)info->ContextRecord->Ebp;
            DWORD *esp = (DWORD *)(uintptr_t)info->ContextRecord->Esp;
            int depth;

            fprintf(logFile, "[crash] eip=%08lx esp=%08lx ebp=%08lx\n",
                    (unsigned long)info->ContextRecord->Eip,
                    (unsigned long)info->ContextRecord->Esp,
                    (unsigned long)info->ContextRecord->Ebp);
            fprintf(logFile, "[crash] eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx\n",
                    (unsigned long)info->ContextRecord->Eax,
                    (unsigned long)info->ContextRecord->Ebx,
                    (unsigned long)info->ContextRecord->Ecx,
                    (unsigned long)info->ContextRecord->Edx,
                    (unsigned long)info->ContextRecord->Esi,
                    (unsigned long)info->ContextRecord->Edi);

            // 崩溃点附近的栈内容：通常是出错调用的参数（比如那个坏指针本身）
            {
                const DWORD *stack = (const DWORD *)(uintptr_t)info->ContextRecord->Esp;
                int i;

                fprintf(logFile, "[crash] stack:");
                for (i = 0; i < 8; i++)
                    fprintf(logFile, " %08lx", (unsigned long)stack[i]);
                fprintf(logFile, "\n");
            }

            // Walk the frame chain so the crash can be attributed to a caller
            // even when the faulting function is a generic helper.
            for (depth = 0; depth < 16 && ebp != NULL; depth++)
            {
                DWORD *next;

                if ((uintptr_t)ebp < (uintptr_t)esp
                 || (uintptr_t)ebp >= (uintptr_t)esp + 0x200000
                 || ((uintptr_t)ebp & 3) != 0)
                    break;

                fprintf(logFile, "[crash] frame %d ret=%08lx\n", depth,
                        (unsigned long)ebp[1]);
                next = (DWORD *)(uintptr_t)ebp[0];
                if (next <= ebp)
                    break;
                ebp = next;
            }
        }
#endif
        fflush(logFile);
        fclose(logFile);
    }

    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

static void PaceHostFrame(Uint64 *nextFrame, Uint64 framePeriod)
{
    Uint64 now;

    if (framePeriod == 0)
    {
        // Space-held fast-forward is intentionally uncapped. The game thread
        // still gates each frame through the VBlank semaphore, so this does
        // not create duplicate presentations.
        *nextFrame = SDL_GetPerformanceCounter();
        return;
    }

    now = SDL_GetPerformanceCounter();
    if (now < *nextFrame)
    {
        Uint64 remaining = *nextFrame - now;
        Uint32 milliseconds = (Uint32)((remaining * 1000) /
                                       SDL_GetPerformanceFrequency());

        if (milliseconds != 0)
            SDL_Delay(milliseconds);

        // SDL_Delay is intentionally coarse on Windows. Spin only for the
        // small remainder so each VBlank stays on the 59.7275 Hz cadence.
        while ((now = SDL_GetPerformanceCounter()) < *nextFrame)
            SDL_Delay(0);
    }
    else if (now - *nextFrame > framePeriod * 15)
    {
        // A long synchronous load should not make every subsequent frame
        // run back-to-back while the deadline catches up.
        *nextFrame = now;
    }

    *nextFrame += framePeriod;
}

static Uint32 LoopPhaseMs(Uint64 start, Uint64 end)
{
    Uint64 frequency = SDL_GetPerformanceFrequency();

    if (frequency == 0 || end <= start)
        return 0;
    return (Uint32)(((end - start) * 1000) / frequency);
}

static Uint64 GetHostFramePeriod(Uint64 baseFramePeriod)
{
    Uint64 period;
    u32 multiplier;

    if (Platform_IsFastForward())
        return 0;

    multiplier = Platform_GetSpeedMultiplier();
    if (multiplier == 0)
        multiplier = 1;

    period = baseFramePeriod / multiplier;
    return period == 0 ? 1 : period;
}

void Platform_ExitGameThread(void)
{
    longjmp(sGameThreadExit, 1);
}

static int GameThread(void *unused)
{
    (void)unused;
    if (setjmp(sGameThreadExit) == 0)
        AgbMain();
    return 0;
}

int main(int argc, char **argv)
{
    SDL_Thread *gameThread;
    int gameResult;
    Uint64 baseFramePeriod;
    Uint64 nextFrame;
    Uint64 framePeriod;

    (void)argc;
    (void)argv;

    // 追加写 crash.log：F1 快照需要跨多次运行保留，否则下一次启动会把它清掉。
    // 只在上一次日志过大时轮转成 crash.log.1，避免无限增长。
    {
        FILE *fresh = fopen("crash.log", "a");
        FILE *probe = fopen("crash.log", "rb");

        if (probe != NULL)
        {
            long size;

            fseek(probe, 0, SEEK_END);
            size = ftell(probe);
            fclose(probe);
            if (size > 2 * 1024 * 1024)
            {
                remove("crash.log.1");
                rename("crash.log", "crash.log.1");
                if (fresh != NULL)
                    fclose(fresh);
                fresh = fopen("crash.log", "a");
            }
        }

        if (fresh != NULL)
        {
            fprintf(fresh, "[run] start\n");
            fflush(fresh);
            fclose(fresh);
        }
    }

#ifdef _WIN32
    SetUnhandledExceptionFilter(HostCrashFilter);
#endif

    if (!Platform_Init("Pokemon Emerald", 3, false))
        return 1;
    if (!Platform_InitSave("pokeemerald.sav"))
    {
        Platform_Shutdown();
        return 1;
    }

    gameThread = SDL_CreateThread(GameThread, "pokeemerald-game", NULL);
    if (gameThread == NULL)
    {
        Platform_FlushSave();
        Platform_Shutdown();
        return 1;
    }

    // The GBA LCD refreshes at 59.7275 Hz (280896 CPU cycles per frame),
    // rather than the nominal 60 Hz. Keep the host frame clock aligned with
    // the audio driver's pcmFreq and VBlank cadence.
    baseFramePeriod = (SDL_GetPerformanceFrequency() * 10000 + 597275 / 2) / 597275;
    framePeriod = baseFramePeriod;
    nextFrame = SDL_GetPerformanceCounter();

    while (!Platform_IsQuitRequested())
    {
        Uint64 loopStart = SDL_GetPerformanceCounter();
        Uint64 pumpEnd;
        Uint64 paceEnd;
        Uint64 presentEnd;
        Uint64 loopEnd;

        if (!Platform_PumpEvents())
            break;
        pumpEnd = SDL_GetPerformanceCounter();

        {
            Uint64 requestedPeriod = GetHostFramePeriod(baseFramePeriod);

            if (requestedPeriod != framePeriod)
            {
                // Do not carry a 1x deadline into a newly selected speed (or
                // vice versa); resume from the next frame at the new cadence.
                framePeriod = requestedPeriod;
                nextFrame = SDL_GetPerformanceCounter();
            }
        }

        if (Platform_IsFramePending())
        {
            PaceHostFrame(&nextFrame, framePeriod);
            paceEnd = SDL_GetPerformanceCounter();
            Platform_PresentPendingFrame();
            presentEnd = SDL_GetPerformanceCounter();
        }
        else
        {
            paceEnd = pumpEnd;
            presentEnd = pumpEnd;
        }

        SDL_Delay(Platform_IsFastForward() ? 0 : 1);
        loopEnd = SDL_GetPerformanceCounter();

        // A long main-loop iteration is what the game thread sees as a missed
        // VBlank, so name the phase that ate the time instead of guessing.
        if (LoopPhaseMs(loopStart, loopEnd) >= 30 && getenv("PE_TRACE") != NULL)
        {
            fprintf(stderr,
                    "[host] slow loop total=%ums pump=%ums pace=%ums present=%ums tail=%ums\n",
                    LoopPhaseMs(loopStart, loopEnd),
                    LoopPhaseMs(loopStart, pumpEnd),
                    LoopPhaseMs(pumpEnd, paceEnd),
                    LoopPhaseMs(paceEnd, presentEnd),
                    LoopPhaseMs(presentEnd, loopEnd));
        }
    }

    SDL_WaitThread(gameThread, &gameResult);
    Platform_FlushSave();
    Platform_Shutdown();
    return gameResult;
}

#endif // PLATFORM_SDL2
