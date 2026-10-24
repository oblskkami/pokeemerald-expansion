#ifndef GUARD_PLATFORM_H
#define GUARD_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#include "gba/types.h"

struct SiiRtcInfo;

// Host 地址空间探测：GBA 没有 MMU，读到未映射地址只返回总线垃圾，
// 但宿主会段错误；移植层在拷贝/调色板加载前用它做保护。
int HostMemoryIsReadable(const void *ptr, u32 len);
int HostMemoryIsWritable(void *ptr, u32 len);
// 启动自检：验证 CpuFill*/CpuFastFill* 是否真的写入了给的值（结果写进 crash.log）。
void HostBiosSelfTest(void);
// 宿主诊断日志：追加写入 crash.log。只在异常路径调用，
// 正常游玩不会产生任何输出，也不影响性能。
// crash.log 每次启动都会被重写，只保留当次运行的内容。
void HostLogf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
// 请求在下一帧渲染完成后把画面/寄存器状态写进 crash.log（诊断用，
// F1 键与地图初始化路径会调用它）。
void Platform_RequestFrameDump(const char *reason);

bool Platform_Init(const char *title, int scale, bool hidden);
void Platform_Shutdown(void);
void Platform_RequestQuit(void);
void Platform_ExitGameThread(void) __attribute__((noreturn));
bool Platform_PumpEvents(void);
bool Platform_IsQuitRequested(void);
bool Platform_PresentFrame(void);
bool Platform_PresentPendingFrame(void);
bool Platform_IsFramePending(void);
bool Platform_ReadPixel(int x, int y, uint32_t *pixel);
u16 Platform_GetKeyInput(void);
// Host-only pacing controls. They are no-ops outside PLATFORM_SDL2 and are
// intentionally separate from the GBA key register state.
u32 Platform_GetSpeedMultiplier(void);
bool Platform_IsFastForward(void);
bool Platform_InitSave(const char *path);
bool Platform_FlushSave(void);
void Platform_StoreSaveFile(void);
void Platform_ReadFlash(u16 sectorNum, u32 offset, u8 *dest, u32 size);
bool Platform_RtcReset(void);
bool Platform_RtcGetStatus(struct SiiRtcInfo *rtc);
bool Platform_RtcSetStatus(const struct SiiRtcInfo *rtc);
bool Platform_RtcGetDateTime(struct SiiRtcInfo *rtc);
bool Platform_RtcSetDateTime(const struct SiiRtcInfo *rtc);
bool Platform_RtcGetTime(struct SiiRtcInfo *rtc);
bool Platform_RtcSetTime(const struct SiiRtcInfo *rtc);
u64 Platform_GetGbaCycleCount(void);
void Platform_CycleCountStart(void);
u32 Platform_CycleCountEnd(void);
// Queues one target-layout stereo DirectSound block. The GBA stores FIFO A
// (right) and FIFO B (left) in separate halves; SDL interleaves them.
void Platform_QueueAudio(const s8 *right, const s8 *left, u32 sampleCount,
                         u32 sampleRate);

#endif // GUARD_PLATFORM_H
