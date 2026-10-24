#ifdef PLATFORM_SDL2

#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

            // 崩溃点附近的机器码（对齐到 32 字节，取 64 字节，覆盖 eip-31..eip+32）。
            // 宿主 exe 的符号表大部分被剥掉（见 AGENTS.md 8.6），有了这 64 字节
            // 即使没有符号也能手工反汇编认出 idiv/解引用等指令。
            {
                uintptr_t base = (uintptr_t)info->ContextRecord->Eip & ~(uintptr_t)0x1F;
                const unsigned char *code = (const unsigned char *)base;
                int i;

                if (HostMemoryIsReadable(code, 64))
                {
                    fprintf(logFile, "[crash] code@%08lx:", (unsigned long)base);
                    for (i = 0; i < 64; i++)
                        fprintf(logFile, " %02x", code[i]);
                    fprintf(logFile, "\n");
                }
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

static Uint32 LoopPhaseMs(Uint64 start, Uint64 end);

// 落后超过 N 帧就重新对齐 deadline（默认 2 帧）。
// 原来是 15 帧（约 250ms）：一次晚帧（Windows 的 SDL_Delay 偶尔睡过头）之后
// 会连跑十几帧「快放」把 deadline 追回来，玩家看到的就是「卡一下再快放」。
// 改成 2 帧后只丢一帧、下一帧立刻回到 59.7Hz，不再有快放段。
// 想恢复旧行为：set PE_PACE_SNAP=15
static Uint32 PaceSnapFrames(void)
{
    static int cached = -1;

    if (cached < 0)
    {
        const char *env = getenv("PE_PACE_SNAP");

        cached = (env != NULL) ? atoi(env) : 2;
        if (cached < 1)
            cached = 1;
    }

    return (Uint32)cached;
}

static u32 sPaceOversleepLogs;
static u32 sPaceLateLogs;
static u32 sSlowLoopLogs;

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
        {
            Uint64 sleepStart = SDL_GetPerformanceCounter();
            Uint64 deadline = *nextFrame;
            Uint64 after;
            Uint32 slept;

            SDL_Delay(milliseconds);
            after = SDL_GetPerformanceCounter();
            slept = LoopPhaseMs(sleepStart, after);

            // Windows 上 SDL_Delay 偶尔会睡过头（实测要求 10ms、实际近 100ms）。
            // 把「要求/实际/超期」记下来，才能区分「睡过头」和「呈现阻塞」。
            if ((slept > milliseconds + 20 || slept >= 60)
             && sPaceOversleepLogs < 200)
            {
                Uint64 freq = SDL_GetPerformanceFrequency();
                Uint32 late = 0;

                if (after > deadline && freq != 0)
                    late = (Uint32)(((after - deadline) * 1000) / freq);

                sPaceOversleepLogs++;
                HostLogf("pace want=%ums got=%ums late=%ums keys=%03X", milliseconds,
                         slept, late, Platform_GetKeyInput());
            }
        }

        // SDL_Delay is intentionally coarse on Windows. Spin only for the
        // small remainder so each VBlank stays on the 59.7275 Hz cadence.
        while ((now = SDL_GetPerformanceCounter()) < *nextFrame)
            SDL_Delay(0);
    }
    else
    {
        Uint64 lateTicks = now - *nextFrame;
        Uint64 freq = SDL_GetPerformanceFrequency();
        Uint32 lateMs = (freq != 0) ? (Uint32)((lateTicks * 1000) / freq) : 0;

        // 这一帧已经晚了：记下「晚了多少 + 当时按着哪些键」。
        // 卡顿到底跟方向键有没有关系，看这几行就能确认。
        if (lateMs >= 30 && sPaceLateLogs < 400)
        {
            sPaceLateLogs++;
            HostLogf("pace late=%ums keys=%03X snap=%d", lateMs,
                     Platform_GetKeyInput(),
                     (lateTicks > framePeriod * PaceSnapFrames()) ? 1 : 0);
        }

        if (lateTicks > framePeriod * PaceSnapFrames())
        {
            // A long synchronous load should not make every subsequent frame
            // run back-to-back while the deadline catches up.
            *nextFrame = now;
        }
    }

    *nextFrame += framePeriod;
}

// 进程累计 CPU 时间（微秒）。用来区分「真在被调度执行」和「被系统饿着」：
// 墙上时间 278ms 而 CPU 只涨了 2ms，就说明这一顿不是我们自己的代码在跑。
static Uint64 HostProcessCpuUs(void)
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

// 可选：把游戏线程和主线程提到高优先级（PE_PRIO=1）。
// 用于验证「左右行走卡顿」是不是被系统调度饿出来的——如果是，
// 提优先级后尖峰应显著减少；如果完全没变，就说明卡顿在代码里。
static void HostMaybeRaisePriority(void)
{
#ifdef _WIN32
    if (getenv("PE_PRIO") == NULL)
        return;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    HostLogf("host prio: thread raised to HIGHEST");
#endif
}

static int GameThread(void *unused)
{
    (void)unused;
    HostMaybeRaisePriority();
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
    HostLogf("host cpus=%d timerFreq=%llu pe_prio=%d", SDL_GetCPUCount(),
             (unsigned long long)SDL_GetPerformanceFrequency(),
             getenv("PE_PRIO") != NULL);
    HostMaybeRaisePriority();
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
        Uint64 loopCpuStart = HostProcessCpuUs();
        Uint64 pumpEnd;
        Uint64 paceEnd;
        Uint64 presentEnd;
        Uint64 loopEnd;
        bool hadFrame = false;

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
            hadFrame = true;
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
        {
            Uint32 total = LoopPhaseMs(loopStart, loopEnd);
            Uint32 pump = LoopPhaseMs(loopStart, pumpEnd);
            Uint32 pace = LoopPhaseMs(pumpEnd, paceEnd);
            Uint32 present = LoopPhaseMs(paceEnd, presentEnd);
            Uint32 tail = LoopPhaseMs(presentEnd, loopEnd);

            // 写进 crash.log 而不是 stderr：用户从 PowerShell 启动，stderr 抓不到。
            // 限流 400 条，避免一份日志被刷爆。
            if (total >= 40 && sSlowLoopLogs < 400)
            {
                Uint64 cpuUs = HostProcessCpuUs() - loopCpuStart;

                sSlowLoopLogs++;
                HostLogf("loop total=%ums cpu=%ums pump=%ums pace=%ums present=%ums tail=%ums keys=%03X speed=%u frame=%d",
                         total, (Uint32)(cpuUs / 1000), pump, pace, present, tail,
                         Platform_GetKeyInput(), Platform_GetSpeedMultiplier(),
                         hadFrame ? 1 : 0);
            }
            else if (total >= 30 && getenv("PE_TRACE") != NULL)
            {
                fprintf(stderr,
                        "[host] slow loop total=%ums pump=%ums pace=%ums present=%ums tail=%ums\n",
                        total, pump, pace, present, tail);
            }
        }
    }

    SDL_WaitThread(gameThread, &gameResult);
    Platform_FlushSave();
    Platform_Shutdown();
    return gameResult;
}

#endif // PLATFORM_SDL2
