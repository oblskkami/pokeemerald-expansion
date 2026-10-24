#ifdef PORTABLE

#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 未设 PE_TRACE 时不要往终端喷调试信息（发给别人试玩的 exe 要干净）。
static int BiosTraceEnabled(void)
{
    static int enabled = -1;

    if (enabled < 0)
        enabled = (getenv("PE_TRACE") != NULL) ? 1 : 0;
    return enabled;
}

#include "gba/types.h"
#include "gba/gba.h"
#include "platform.h"
// BIOS function implementations are adapted from the old multiplatform port.

#ifdef _WIN32
#include <windows.h>
#endif

// GBA 没有 MMU：读到未映射地址只会拿到总线垃圾（按 0 处理），写未映射地址被忽略。
// 宿主上同样的访问会直接让进程崩溃，所以拷前先确认整段地址是否可访问。
// 用 VirtualQuery 检查，并缓存最近一次确认可读的区域，避免热路径反复查询。
int HostMemoryIsReadable(const void *ptr, u32 len)
{
#ifdef _WIN32
    static uintptr_t cachedBase[4];
    static uintptr_t cachedEnd[4];
    static int cachedNext;
    uintptr_t start = (uintptr_t)ptr;
    uintptr_t end;
    uintptr_t cursor;
    MEMORY_BASIC_INFORMATION mbi;

    if (len == 0)
        return 1;
    if (start == 0 || len > 0x40000000u)
        return 0;

    end = start + len;

    for (int c = 0; c < 4; c++)
    {
        if (cachedBase[c] != 0 && start >= cachedBase[c] && end <= cachedEnd[c])
            return 1;
    }

    cursor = start;
    while (cursor < end)
    {
        uintptr_t regionBase;
        uintptr_t regionEnd;
        DWORD protect;

        if (VirtualQuery((LPCVOID)cursor, &mbi, sizeof(mbi)) == 0)
            return 0;

        regionBase = (uintptr_t)mbi.BaseAddress;
        regionEnd = regionBase + mbi.RegionSize;
        protect = mbi.Protect & 0xFF;

        if (mbi.State != MEM_COMMIT || regionEnd <= cursor
         || protect == PAGE_NOACCESS || protect == PAGE_EXECUTE
         || (mbi.Protect & PAGE_GUARD) != 0)
            return 0;

        if (start >= regionBase && end <= regionEnd)
        {
            cachedBase[cachedNext] = regionBase;
            cachedEnd[cachedNext] = regionEnd;
            cachedNext = (cachedNext + 1) & 3;
        }

        cursor = regionEnd;
    }
    return 1;
#else
    (void)len;
    return ptr != NULL;
#endif
}

int HostMemoryIsWritable(void *ptr, u32 len)
{
#ifdef _WIN32
    static uintptr_t cachedBase[4];
    static uintptr_t cachedEnd[4];
    static int cachedNext;
    uintptr_t start = (uintptr_t)ptr;
    uintptr_t end;
    uintptr_t cursor;
    MEMORY_BASIC_INFORMATION mbi;

    if (len == 0)
        return 1;
    if (start == 0 || len > 0x40000000u)
        return 0;

    end = start + len;

    for (int c = 0; c < 4; c++)
    {
        if (cachedBase[c] != 0 && start >= cachedBase[c] && end <= cachedEnd[c])
            return 1;
    }

    cursor = start;
    while (cursor < end)
    {
        uintptr_t regionBase;
        uintptr_t regionEnd;
        DWORD protect;

        if (VirtualQuery((LPCVOID)cursor, &mbi, sizeof(mbi)) == 0)
            return 0;

        regionBase = (uintptr_t)mbi.BaseAddress;
        regionEnd = regionBase + mbi.RegionSize;
        protect = mbi.Protect & 0xFF;

        if (mbi.State != MEM_COMMIT || regionEnd <= cursor
         || protect == PAGE_NOACCESS || protect == PAGE_EXECUTE
         || protect == PAGE_READONLY || protect == PAGE_EXECUTE_READ
         || protect == PAGE_EXECUTE_WRITECOPY
         || (mbi.Protect & PAGE_GUARD) != 0)
            return 0;

        if (start >= regionBase && end <= regionEnd)
        {
            cachedBase[cachedNext] = regionBase;
            cachedEnd[cachedNext] = regionEnd;
            cachedNext = (cachedNext + 1) & 3;
        }

        cursor = regionEnd;
    }
    return 1;
#else
    (void)len;
    return ptr != NULL;
#endif
}

// Host-backed storage for memory-mapped GBA regions.
u16 INTR_CHECK;
void *INTR_VECTOR;
unsigned char REG_BASE[0x400] __attribute__ ((aligned (4)));
unsigned char PLTT[PLTT_SIZE] __attribute__ ((aligned (4)));
unsigned char VRAM_[VRAM_SIZE] __attribute__ ((aligned (4)));
unsigned char OAM[OAM_SIZE] __attribute__ ((aligned (4)));
unsigned char FLASH_BASE[131072] __attribute__ ((aligned (4)));
struct SoundInfo *SOUND_INFO_PTR;

void IntrMain(void)
{}

void ReInitializeEWRAM(void)
{
    // PORTABLE has no contiguous EWRAM image: EWRAM_DATA/IWRAM_DATA are
    // ordinary host globals, so the ARM ROM-LMA copy has no safe equivalent.
}

void RegisterRamReset(u32 resetFlags)
{
    // Only process-backed GBA MMIO regions are modeled here.  EWRAM/IWRAM
    // reset flags remain intentionally untouched; a blanket host memset would
    // erase unrelated save blocks and platform state.
    if (resetFlags & RESET_PALETTE)
        memset(PLTT, 0, PLTT_SIZE);
    if (resetFlags & RESET_VRAM)
        memset(VRAM_, 0, VRAM_SIZE);
    if (resetFlags & RESET_OAM)
        memset(OAM, 0, OAM_SIZE);
    if (resetFlags & RESET_REGS)
        memset(REG_BASE, 0, sizeof(REG_BASE));
}

int MultiBoot(struct MultiBootParam *mp)
{
    (void)mp;
    return MULTIBOOT_ERROR_BOOT_FAILURE;
}

s32 Div(s32 num, s32 denom)
{
    if (denom == 0)
        return num < 0 ? INT32_MIN : INT32_MAX;
    if (num == INT32_MIN && denom == -1)
        return INT32_MIN;
    return num / denom;
}

static uint32_t CPUReadMemory(const void *src)
{
    uint32_t value;

    // GBA 没有 MMU，读地址 0 只会拿到 BIOS 数据；宿主上 NULL 解引用会直接崩溃。
    if (src == NULL)
        return 0;
    memcpy(&value, src, sizeof(value));
    return value;
}

static void CPUWriteMemory(void *dest, uint32_t val)
{
    memcpy(dest, &val, sizeof(val));
}

static uint16_t CPUReadHalfWord(const void *src)
{
    uint16_t value;

    if (src == NULL)
        return 0;
    memcpy(&value, src, sizeof(value));
    return value;
}

static void CPUWriteHalfWord(void *dest, uint16_t val)
{
    memcpy(dest, &val, sizeof(val));
}

static uint8_t CPUReadByte(const void *src)
{
    return *(uint8_t *)src;
}

static void CPUWriteByte(void *dest, uint8_t val)
{
    *(uint8_t *)dest = val;
}

void CpuSet(const void *src, void *dst, u32 cnt)
{
    int count = cnt & 0x1FFFFF;
    u32 unit = ((cnt >> 26) & 1) ? 4 : 2;
    u32 total = (u32)count * unit;
    const u8 *source = src;
    u8 *dest = dst;

    if (dst == NULL)
    {
        if (BiosTraceEnabled())
            puts("Attempted to CpuSet to NULL\n");
        return;
    }

    // GBA 上写未映射地址会被忽略
    if (!HostMemoryIsWritable(dst, total))
        return;

    // GBA 上读未映射地址得到总线垃圾；这里按 0 处理，避免宿主崩溃。
    // 注意：fill 模式的 src 只指向一个 2 字节的临时变量（见 CPU_FILL 宏），
    // 绝不能按目标长度 total 去检查它，否则会被误判成不可读 → 填充值变成 0。
    if (!HostMemoryIsReadable(src, ((cnt >> 24) & 1) ? unit : total))
        source = NULL;

    // 32-bit ?
    if ((cnt >> 26) & 1) {

        // fill ?
        if ((cnt >> 24) & 1) {
            uint32_t value = CPUReadMemory(source);
            while (count) {
                CPUWriteMemory(dest, value);
                dest += 4;
                count--;
            }
        } else {
            // copy
            while (count) {
                CPUWriteMemory(dest, CPUReadMemory(source));
                if (source != NULL)
                    source += 4;
                dest += 4;
                count--;
            }
        }
    } else {
        // 16-bit fill?
        if ((cnt >> 24) & 1) {
            uint16_t value = CPUReadHalfWord(source);
            while (count) {
                CPUWriteHalfWord(dest, value);
                dest += 2;
                count--;
            }
        } else {
            // copy
            while (count) {
                CPUWriteHalfWord(dest, CPUReadHalfWord(source));
                if (source != NULL)
                    source += 2;
                dest += 2;
                count--;
            }
        }
    }
}

void CpuFastSet(const void *src, void *dst, u32 cnt)
{
    int count = cnt & 0x1FFFFF;
    u32 total = (u32)count * 4;
    u32 fill = (cnt >> 24) & 1;
    const u8 *source = src;
    u8 *dest = dst;

    if (dst == NULL)
    {
        if (BiosTraceEnabled())
            puts("Attempted to CpuFastSet to NULL\n");
        return;
    }

    if (!HostMemoryIsWritable(dst, total))
        return;

    // fill 模式的 src 只指向一个 4 字节的临时变量（见 CpuFastFill 宏），
    // 不能按目标长度 total 检查，否则会被误判成不可读 → 填充值变成 0。
    // 这正是「地图边界余量被清成 0 → 周围一圈黑带」的根因。
    if (!HostMemoryIsReadable(src, fill ? 4 : total))
        source = NULL;

    // fill?
    if (fill) {
        uint32_t value = CPUReadMemory(source);
        while (count > 0) {
            int n = (count >= 8) ? 8 : count;
            // BIOS always transfers 32 bytes at a time
            for (int i = 0; i < n; i++) {
                CPUWriteMemory(dest, value);
                dest += 4;
            }
            count -= n;
        }
    } else {
        // copy
        while (count > 0) {
            int n = (count >= 8) ? 8 : count;
            // BIOS always transfers 32 bytes at a time
            for (int i = 0; i < n; i++) {
                uint32_t value = CPUReadMemory(source);
                CPUWriteMemory(dest, value);
                if (source != NULL)
                    source += 4;
                dest += 4;
            }
            count -= n;
        }
    }
}

// 宿主侧 BIOS 自检：把「填充是否真的写入了给的值」记进 crash.log。
// GBA 的 CpuFill16/CpuFastFill16 会把「值」放进一个临时变量再按指针传给 BIOS，
// 移植层如果对这个指针做长度检查，就会悄悄把值变成 0（地图边界黑带那种症状）。
void HostBiosSelfTest(void)
{
    static u16 fastFillBig[10240];
    u16 fastFillSmall[16];
    u16 fillSmall[16];
    u32 fillBig32[8];
    int i;

    for (i = 0; i < 16; i++)
    {
        fastFillSmall[i] = 0xAAAA;
        fillSmall[i] = 0xAAAA;
    }
    for (i = 0; i < 8; i++)
        fillBig32[i] = 0xAAAAAAAA;

    // 0x03FF == MAPGRID_UNDEFINED：备份地图的边界余量就是用它填的。
    CpuFastFill16(0x03FF, fastFillSmall, sizeof(fastFillSmall));
    CpuFastFill16(0x03FF, fastFillBig, sizeof(fastFillBig));
    CpuFill16(0x1234, fillSmall, sizeof(fillSmall));
    CpuFill32(0xDEADBEEF, fillBig32, sizeof(fillBig32));

    HostLogf("selftest fill: fastfill16 %04X/%04X big %04X/%04X (want 03FF) fill16 %04X (want 1234) fill32 %08X (want DEADBEEF)",
             fastFillSmall[0], fastFillSmall[15], fastFillBig[0], fastFillBig[10239],
             fillSmall[0], (unsigned)fillBig32[0]);
}
void LZ77UnCompVram(const u32 *src_, void *dest_)
{
    const u8 *src = (const u8 *)src_;
    u8 *dest = (u8 *)dest_;
    int destSize = (src[3] << 16) | (src[2] << 8) | src[1];
    int srcPos = 4;
    int destPos = 0;

    for (;;) {
        unsigned char flags = src[srcPos++];

        for (int i = 0; i < 8; i++) {
            if (flags & 0x80) {
                int blockSize = (src[srcPos] >> 4) + 3;
                int blockDistance = (((src[srcPos] & 0xF) << 8) | src[srcPos + 1]) + 1;

                srcPos += 2;

                int blockPos = destPos - blockDistance;

                // Some Ruby/Sapphire tilesets overflow.
                if (destPos + blockSize > destSize) {
                    blockSize = destSize - destPos;
                    fputs("Destination buffer overflow.\n", stderr);
                }

                if (blockPos < 0)
                    goto fail;

                for (int j = 0; j < blockSize; j++)
                    dest[destPos++] = dest[blockPos + j];
            } else {
                if (destPos >= destSize)
                    goto fail;

                dest[destPos++] = src[srcPos++];
            }

            if (destPos == destSize) {
                return;
            }

            flags <<= 1;
        }
    }

fail:
    puts("Fatal error while decompressing LZ file.\n");
}

void LZ77UnCompWram(const u32 *src, void *dst)
{
    const uint8_t *source = (const uint8_t *)src;
    uint8_t *dest = (uint8_t *)dst;

    uint32_t header = CPUReadMemory(source);
    source += 4;

    int len = header >> 8;

    while (len > 0) {
        uint8_t d = CPUReadByte(source++);

        if (d) {
            for (int i = 0; i < 8; i++) {
                if (d & 0x80) {
                    uint16_t data = CPUReadByte(source++) << 8;
                    data |= CPUReadByte(source++);
                    int length = (data >> 12) + 3;
                    int offset = (data & 0x0FFF);
                    uint8_t *windowOffset = dest - offset - 1;
                    for (int i2 = 0; i2 < length; i2++) {
                        CPUWriteByte(dest++, CPUReadByte(windowOffset++));
                        len--;
                        if (len == 0)
                            return;
                    }
                } else {
                    CPUWriteByte(dest++, CPUReadByte(source++));
                    len--;
                    if (len == 0)
                        return;
                }
                d <<= 1;
            }
        } else {
            for (int i = 0; i < 8; i++) {
                CPUWriteByte(dest++, CPUReadByte(source++));
                len--;
                if (len == 0)
                    return;
            }
        }
    }
}

static void RLUnComp(const u32 *src, void *dest, u32 alignment)
{
    const u8 *source = (const u8 *)src;
    u8 *output = dest;
    u32 remaining = source[1] | (source[2] << 8) | (source[3] << 16);
    u32 outputSize = remaining;

    source += 4;
    while (remaining != 0)
    {
        u8 header = *source++;
        u32 length = (header & 0x7F) + ((header & 0x80) ? 3 : 1);
        if (length > remaining)
            length = remaining;

        if (header & 0x80)
        {
            u8 value = *source++;
            memset(output, value, length);
            output += length;
        }
        else
        {
            memcpy(output, source, length);
            output += length;
            source += length;
        }
        remaining -= length;
    }

    u32 padding = (alignment - outputSize) & (alignment - 1);
    memset(output, 0, padding);
}

void RLUnCompWram(const u32 *src, void *dest)
{
    RLUnComp(src, dest, 4);
}

void RLUnCompVram(const u32 *src, void *dest)
{
    RLUnComp(src, dest, 2);
}

void BitUnPack(const void *src, void *dest, const struct BitUnPackArgs *args)
{
    const u8 *source = src;
    u8 *output = dest;
    u64 packed = 0;
    u32 packedBits = 0;
    u32 sourceMask;
    u32 outputMask;

    if (source == NULL || output == NULL || args == NULL
     || (args->compressedBits != 1 && args->compressedBits != 2
      && args->compressedBits != 4 && args->compressedBits != 8)
     || (args->decompressedBits != 1 && args->decompressedBits != 2
      && args->decompressedBits != 4 && args->decompressedBits != 8
      && args->decompressedBits != 16 && args->decompressedBits != 32))
    {
        return;
    }

    sourceMask = (1u << args->compressedBits) - 1;
    outputMask = args->decompressedBits == 32
        ? UINT32_MAX
        : (1u << args->decompressedBits) - 1;

    for (u32 byte = 0; byte < args->compressedSize; byte++)
    {
        for (u32 bit = 0; bit < 8; bit += args->compressedBits)
        {
            u32 value = (source[byte] >> bit) & sourceMask;

            if (value != 0 || args->offsetZeros)
                value += args->dataOffset;
            packed |= (u64)(value & outputMask) << packedBits;
            packedBits += args->decompressedBits;

            if (packedBits >= 32)
            {
                u32 word = packed;
                memcpy(output, &word, sizeof(word));
                output += sizeof(word);
                packed >>= 32;
                packedBits -= 32;
            }
        }
    }

    if (packedBits != 0)
    {
        u32 word = packed;
        memcpy(output, &word, sizeof(word));
    }
}

const s16 sineTable[256] = {
  (s16)0x0000, (s16)0x0192, (s16)0x0323, (s16)0x04B5, (s16)0x0645, (s16)0x07D5, (s16)0x0964, (s16)0x0AF1,
  (s16)0x0C7C, (s16)0x0E05, (s16)0x0F8C, (s16)0x1111, (s16)0x1294, (s16)0x1413, (s16)0x158F, (s16)0x1708,
  (s16)0x187D, (s16)0x19EF, (s16)0x1B5D, (s16)0x1CC6, (s16)0x1E2B, (s16)0x1F8B, (s16)0x20E7, (s16)0x223D,
  (s16)0x238E, (s16)0x24DA, (s16)0x261F, (s16)0x275F, (s16)0x2899, (s16)0x29CD, (s16)0x2AFA, (s16)0x2C21,
  (s16)0x2D41, (s16)0x2E5A, (s16)0x2F6B, (s16)0x3076, (s16)0x3179, (s16)0x3274, (s16)0x3367, (s16)0x3453,
  (s16)0x3536, (s16)0x3612, (s16)0x36E5, (s16)0x37AF, (s16)0x3871, (s16)0x392A, (s16)0x39DA, (s16)0x3A82,
  (s16)0x3B20, (s16)0x3BB6, (s16)0x3C42, (s16)0x3CC5, (s16)0x3D3E, (s16)0x3DAE, (s16)0x3E14, (s16)0x3E71,
  (s16)0x3EC5, (s16)0x3F0E, (s16)0x3F4E, (s16)0x3F84, (s16)0x3FB1, (s16)0x3FD3, (s16)0x3FEC, (s16)0x3FFB,
  (s16)0x4000, (s16)0x3FFB, (s16)0x3FEC, (s16)0x3FD3, (s16)0x3FB1, (s16)0x3F84, (s16)0x3F4E, (s16)0x3F0E,
  (s16)0x3EC5, (s16)0x3E71, (s16)0x3E14, (s16)0x3DAE, (s16)0x3D3E, (s16)0x3CC5, (s16)0x3C42, (s16)0x3BB6,
  (s16)0x3B20, (s16)0x3A82, (s16)0x39DA, (s16)0x392A, (s16)0x3871, (s16)0x37AF, (s16)0x36E5, (s16)0x3612,
  (s16)0x3536, (s16)0x3453, (s16)0x3367, (s16)0x3274, (s16)0x3179, (s16)0x3076, (s16)0x2F6B, (s16)0x2E5A,
  (s16)0x2D41, (s16)0x2C21, (s16)0x2AFA, (s16)0x29CD, (s16)0x2899, (s16)0x275F, (s16)0x261F, (s16)0x24DA,
  (s16)0x238E, (s16)0x223D, (s16)0x20E7, (s16)0x1F8B, (s16)0x1E2B, (s16)0x1CC6, (s16)0x1B5D, (s16)0x19EF,
  (s16)0x187D, (s16)0x1708, (s16)0x158F, (s16)0x1413, (s16)0x1294, (s16)0x1111, (s16)0x0F8C, (s16)0x0E05,
  (s16)0x0C7C, (s16)0x0AF1, (s16)0x0964, (s16)0x07D5, (s16)0x0645, (s16)0x04B5, (s16)0x0323, (s16)0x0192,
  (s16)0x0000, (s16)0xFE6E, (s16)0xFCDD, (s16)0xFB4B, (s16)0xF9BB, (s16)0xF82B, (s16)0xF69C, (s16)0xF50F,
  (s16)0xF384, (s16)0xF1FB, (s16)0xF074, (s16)0xEEEF, (s16)0xED6C, (s16)0xEBED, (s16)0xEA71, (s16)0xE8F8,
  (s16)0xE783, (s16)0xE611, (s16)0xE4A3, (s16)0xE33A, (s16)0xE1D5, (s16)0xE075, (s16)0xDF19, (s16)0xDDC3,
  (s16)0xDC72, (s16)0xDB26, (s16)0xD9E1, (s16)0xD8A1, (s16)0xD767, (s16)0xD633, (s16)0xD506, (s16)0xD3DF,
  (s16)0xD2BF, (s16)0xD1A6, (s16)0xD095, (s16)0xCF8A, (s16)0xCE87, (s16)0xCD8C, (s16)0xCC99, (s16)0xCBAD,
  (s16)0xCACA, (s16)0xC9EE, (s16)0xC91B, (s16)0xC851, (s16)0xC78F, (s16)0xC6D6, (s16)0xC626, (s16)0xC57E,
  (s16)0xC4E0, (s16)0xC44A, (s16)0xC3BE, (s16)0xC33B, (s16)0xC2C2, (s16)0xC252, (s16)0xC1EC, (s16)0xC18F,
  (s16)0xC13B, (s16)0xC0F2, (s16)0xC0B2, (s16)0xC07C, (s16)0xC04F, (s16)0xC02D, (s16)0xC014, (s16)0xC005,
  (s16)0xC000, (s16)0xC005, (s16)0xC014, (s16)0xC02D, (s16)0xC04F, (s16)0xC07C, (s16)0xC0B2, (s16)0xC0F2,
  (s16)0xC13B, (s16)0xC18F, (s16)0xC1EC, (s16)0xC252, (s16)0xC2C2, (s16)0xC33B, (s16)0xC3BE, (s16)0xC44A,
  (s16)0xC4E0, (s16)0xC57E, (s16)0xC626, (s16)0xC6D6, (s16)0xC78F, (s16)0xC851, (s16)0xC91B, (s16)0xC9EE,
  (s16)0xCACA, (s16)0xCBAD, (s16)0xCC99, (s16)0xCD8C, (s16)0xCE87, (s16)0xCF8A, (s16)0xD095, (s16)0xD1A6,
  (s16)0xD2BF, (s16)0xD3DF, (s16)0xD506, (s16)0xD633, (s16)0xD767, (s16)0xD8A1, (s16)0xD9E1, (s16)0xDB26,
  (s16)0xDC72, (s16)0xDDC3, (s16)0xDF19, (s16)0xE075, (s16)0xE1D5, (s16)0xE33A, (s16)0xE4A3, (s16)0xE611,
  (s16)0xE783, (s16)0xE8F8, (s16)0xEA71, (s16)0xEBED, (s16)0xED6C, (s16)0xEEEF, (s16)0xF074, (s16)0xF1FB,
  (s16)0xF384, (s16)0xF50F, (s16)0xF69C, (s16)0xF82B, (s16)0xF9BB, (s16)0xFB4B, (s16)0xFCDD, (s16)0xFE6E
};

void BgAffineSet(struct BgAffineSrcData *src, struct BgAffineDstData *dest, s32 count)
{
    for(s32 i=0; i<count; i++)
    {
        s32 cx = src[i].texX;
        s32 cy = src[i].texY;
        s16 dispx = src[i].scrX;
        s16 dispy = src[i].scrY;
        s16 rx = src[i].sx;
        s16 ry = src[i].sy;
        u16 theta = src[i].alpha>>8;
        s32 a = sineTable[(theta+0x40)&255];
        s32 b = sineTable[theta];

        s16 dx =  (rx * a)>>14;
        s16 dmx = (rx * b)>>14;
        s16 dy =  (ry * b)>>14;
        s16 dmy = (ry * a)>>14;

        dest[i].pa = dx;
        dest[i].pb = -dmx;
        dest[i].pc = dy;
        dest[i].pd = dmy;

        s32 startx = cx - dx * dispx + dmx * dispy;
        s32 starty = cy - dy * dispx - dmy * dispy;

        dest[i].dx = startx;
        dest[i].dy = starty;
    }
}

void ObjAffineSet(struct ObjAffineSrcData *src, void *dest, s32 count, s32 offset)
{
    for(s32 i=0; i<count; i++)
    {
        s16 rx = src[i].xScale;
        s16 ry = src[i].yScale;
        u16 theta = src[i].rotation>>8;

        s32 a = (s32)sineTable[(theta + 64) & 255];
        s32 b = (s32)sineTable[theta];

        s16 dx =  ((s32)rx * a)>>14;
        s16 dmx = ((s32)rx * b)>>14;
        s16 dy =  ((s32)ry * b)>>14;
        s16 dmy = ((s32)ry * a)>>14;

        CPUWriteHalfWord(dest, dx);
        dest += offset;
        CPUWriteHalfWord(dest, -dmx);
        dest += offset;
        CPUWriteHalfWord(dest, dy);
        dest += offset;
        CPUWriteHalfWord(dest, dmy);
        dest += offset;
    }
}

// Following functions taken from mGBA's source
u16 ArcTan(s16 i)
{
    s32 a = -((i * i) >> 14);
    s32 b = ((0xA9 * a) >> 14) + 0x390;
    b = ((b * a) >> 14) + 0x91C;
    b = ((b * a) >> 14) + 0xFB6;
    b = ((b * a) >> 14) + 0x16AA;
    b = ((b * a) >> 14) + 0x2081;
    b = ((b * a) >> 14) + 0x3651;
    b = ((b * a) >> 14) + 0xA2F9;

    return (i * b) >> 16;
}

u16 ArcTan2(s16 x, s16 y)
{
    if (!y)
    {
        if (x >= 0)
            return 0;
        return 0x8000;
    }
    if (!x)
    {
        if (y >= 0)
            return 0x4000;
        return 0xC000;
    }
    if (y >= 0)
    {
        if (x >= 0)
        {
            if (x >= y)
                return ArcTan((y << 14) / x);
        }
        else if (-x >= y)
            return ArcTan((y << 14) / x) + 0x8000;
        return 0x4000 - ArcTan((x << 14) / y);
    }
    else
    {
        if (x <= 0)
        {
            if (-x > -y)
                return ArcTan((y << 14) / x) + 0x8000;
        }
        else if (x >= -y)
            return ArcTan((y << 14) / x) + 0x10000;
        return 0xC000 - ArcTan((x << 14) / y);
    }
}

u16 Sqrt(u32 num)
{
    if (!num)
        return 0;
    u32 lower;
    u32 upper = num;
    u32 bound = 1;
    while (bound < upper)
    {
        upper >>= 1;
        bound <<= 1;
    }
    while (1)
    {
        upper = num;
        u32 accum = 0;
        lower = bound;
        while (1)
        {
            u32 oldLower = lower;
            if (lower <= upper >> 1)
                lower <<= 1;
            if (oldLower >= upper >> 1)
                break;
        }
        while (1)
        {
            accum <<= 1;
            if (upper >= lower)
            {
                ++accum;
                upper -= lower;
            }
            if (lower == bound)
                break;
            lower >>= 1;
        }
        u32 oldBound = bound;
        bound += accum;
        bound >>= 1;
        if (bound >= oldBound)
        {
            bound = oldBound;
            break;
        }
    }
    return bound;
}

#endif // PORTABLE
