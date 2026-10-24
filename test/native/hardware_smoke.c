#include <stdio.h>
#include <string.h>

#include "crt0.h"
#include "gba/types.h"
#include "gba/gba.h"
#include "gba/flash_internal.h"
#include "libgcnmultiboot.h"
#include "multiboot.h"
#include "platform/dma.h"

#define ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))

static int sFailures;

#define CHECK(expression)                                                     \
    do                                                                        \
    {                                                                         \
        if (!(expression))                                                    \
        {                                                                     \
            fprintf(stderr, "hardware smoke check failed: %s:%d: %s\n",     \
                    __FILE__, __LINE__, #expression);                         \
            sFailures++;                                                      \
        }                                                                     \
    } while (0)

static void TestMemoryMap(void)
{
    CHECK((uintptr_t)BG_PLTT == (uintptr_t)PLTT);
    CHECK((uintptr_t)OBJ_PLTT == (uintptr_t)PLTT + BG_PLTT_SIZE);
    CHECK((uintptr_t)BG_VRAM == (uintptr_t)VRAM_);
    CHECK((uintptr_t)OBJ_VRAM0 == (uintptr_t)VRAM_ + 0x10000);

    REG_DISPCNT = 0x1234;
    CHECK(*(u16 *)&REG_BASE[REG_OFFSET_DISPCNT] == 0x1234);

    FLASH_BASE[0] = 0x5A;
    FLASH_BASE[FLASH_ROM_SIZE_1M - 1] = 0xA5;
    CHECK(FLASH_BASE[0] == 0x5A);
    CHECK(FLASH_BASE[FLASH_ROM_SIZE_1M - 1] == 0xA5);
}

static bool32 IsZeroed(const void *data, size_t size)
{
    const u8 *bytes = data;

    for (size_t i = 0; i < size; i++)
    {
        if (bytes[i] != 0)
            return FALSE;
    }
    return TRUE;
}

static void TestMultiBootBoundary(void)
{
    struct MultiBootParam multiboot;
    u8 program[MULTIBOOT_SEND_SIZE_MIN] = {0};
    u8 registers[0x400];

    memset(&multiboot, 0xA5, sizeof(multiboot));
    multiboot.masterp = program;
    multiboot.server_type = MULTIBOOT_SERVER_TYPE_QUICK;
    memset(REG_BASE, 0x5A, sizeof(registers));
    memcpy(registers, REG_BASE, sizeof(registers));

    MultiBootInit(&multiboot);
    CHECK(multiboot.client_bit == 0);
    CHECK(multiboot.probe_count == 0);
    CHECK(multiboot.response_bit == 0);
    CHECK(multiboot.check_wait == MULTIBOOT_CONNECTION_CHECK_WAIT);
    CHECK(multiboot.sendflag == 0);
    CHECK(multiboot.handshake_timeout == 0);
    CHECK(multiboot.masterp == program);
    CHECK(multiboot.server_type == MULTIBOOT_SERVER_TYPE_QUICK);
    CHECK(memcmp(REG_BASE, registers, sizeof(registers)) == 0);

    CHECK(MultiBootMain(&multiboot) == MULTIBOOT_ERROR_BOOT_FAILURE);
    MultiBootStartProbe(&multiboot);
    CHECK(multiboot.probe_count == 0);
    MultiBootStartMaster(&multiboot, program, sizeof(program), 4, 1);
    CHECK(multiboot.probe_count == 0);
    CHECK(!MultiBootCheckComplete(&multiboot));
    CHECK(memcmp(REG_BASE, registers, sizeof(registers)) == 0);

    MultiBootInit(NULL);
    CHECK(MultiBootMain(NULL) == MULTIBOOT_ERROR_BOOT_FAILURE);
    MultiBootStartProbe(NULL);
    MultiBootStartMaster(NULL, NULL, 0, 0, 0);
    CHECK(!MultiBootCheckComplete(NULL));
}

static void TestResetAndBootBoundaries(void)
{
    struct GcmbStruct gcmb;
    struct GcmbStruct snapshot;
    u8 registers[0x400];

    memset(REG_BASE, 0x11, 0x400);
    memset(PLTT, 0x22, PLTT_SIZE);
    memset(VRAM_, 0x33, VRAM_SIZE);
    memset(OAM, 0x44, OAM_SIZE);
    FLASH_BASE[0] = 0x5A;

    RegisterRamReset(RESET_PALETTE | RESET_VRAM | RESET_OAM | RESET_REGS);
    CHECK(IsZeroed(REG_BASE, 0x400));
    CHECK(IsZeroed(PLTT, PLTT_SIZE));
    CHECK(IsZeroed(VRAM_, VRAM_SIZE));
    CHECK(IsZeroed(OAM, OAM_SIZE));
    CHECK(FLASH_BASE[0] == 0x5A);
    CHECK(MultiBoot(NULL) == MULTIBOOT_ERROR_BOOT_FAILURE);

    memset(&gcmb, 0xFF, sizeof(gcmb));
    GameCubeMultiBoot_Init(&gcmb);
    CHECK(IsZeroed(&gcmb, sizeof(gcmb)));
    snapshot = gcmb;
    for (int i = 0; i < 160; i++)
        GameCubeMultiBoot_Main(&gcmb);
    GameCubeMultiBoot_HandleSerialInterrupt(&gcmb);
    GameCubeMultiBoot_ExecuteProgram(&gcmb);
    GameCubeMultiBoot_Quit();
    CHECK(memcmp(&gcmb, &snapshot, sizeof(gcmb)) == 0);

    gcmb.gcmb_field_2 = 2;
    memset(REG_BASE, 0xA5, 0x400);
    memcpy(registers, REG_BASE, sizeof(registers));
    GameCubeMultiBoot_Main(&gcmb);
    CHECK(gcmb.gcmb_field_2 == 0);
    gcmb.gcmb_field_2 = 2;
    GameCubeMultiBoot_HandleSerialInterrupt(&gcmb);
    CHECK(gcmb.gcmb_field_2 == 0);
    gcmb.gcmb_field_2 = 2;
    snapshot = gcmb;
    GameCubeMultiBoot_ExecuteProgram(&gcmb);
    CHECK(memcmp(&gcmb, &snapshot, sizeof(gcmb)) == 0);
    CHECK(memcmp(REG_BASE, registers, sizeof(registers)) == 0);

    GameCubeMultiBoot_Init(NULL);
    GameCubeMultiBoot_Main(NULL);
    GameCubeMultiBoot_HandleSerialInterrupt(NULL);
    GameCubeMultiBoot_ExecuteProgram(NULL);

    IntrMain();
    ReInitializeEWRAM();
}

static void TestCpuSet(void)
{
    ALIGNED(4) const u16 source16[] = {1, 2, 3, 4};
    ALIGNED(4) u16 dest16[ARRAY_COUNT(source16)] = {0};
    ALIGNED(4) const u32 source32[] = {
        0x01234567, 0x89ABCDEF, 0x10203040, 0x50607080,
        0x11111111, 0x22222222, 0x33333333, 0x44444444,
    };
    ALIGNED(4) u32 dest32[ARRAY_COUNT(source32)] = {0};
    ALIGNED(4) const u32 fill = 0xDEADBEEF;

    CpuSet(source16, dest16, ARRAY_COUNT(source16));
    CHECK(memcmp(source16, dest16, sizeof(source16)) == 0);

    CpuSet(&fill, dest32,
           CPU_SET_SRC_FIXED | CPU_SET_32BIT | ARRAY_COUNT(dest32));
    for (u32 i = 0; i < ARRAY_COUNT(dest32); i++)
        CHECK(dest32[i] == fill);

    memset(dest32, 0, sizeof(dest32));
    CpuFastSet(source32, dest32, ARRAY_COUNT(source32));
    CHECK(memcmp(source32, dest32, sizeof(source32)) == 0);
    CHECK(Sqrt(81) == 9);
}

static void TestDecompression(void)
{
    ALIGNED(4) const u8 lzData[] = {
        0x10, 8, 0, 0,
        0x00, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
    };
    ALIGNED(4) const u8 rlData[] = {
        0x30, 6, 0, 0,
        0x81, 'A', 0x01, 'B', 'C',
    };
    u8 output[12];

    memset(output, 0xCD, sizeof(output));
    LZ77UnCompWram((const u32 *)lzData, output);
    CHECK(memcmp(output, "ABCDEFGH", 8) == 0);

    memset(output, 0xCD, sizeof(output));
    LZ77UnCompVram((const u32 *)lzData, output);
    CHECK(memcmp(output, "ABCDEFGH", 8) == 0);

    memset(output, 0xCD, sizeof(output));
    RLUnCompWram((const u32 *)rlData, output);
    CHECK(memcmp(output, "AAAABC", 6) == 0);
    CHECK(output[6] == 0 && output[7] == 0);

    memset(output, 0xCD, sizeof(output));
    RLUnCompVram((const u32 *)rlData, output);
    CHECK(memcmp(output, "AAAABC", 6) == 0);
    CHECK(output[6] == 0xCD);
}

static void TestBitUnPackAndDiv(void)
{
    const u8 source1Bpp[] = {0xB2};
    const u8 source2Bpp[] = {0xE4};
    u32 output = 0;
    struct BitUnPackArgs args = {
        .compressedSize = sizeof(source1Bpp),
        .compressedBits = 1,
        .decompressedBits = 4,
        .dataOffset = 3,
        .offsetZeros = FALSE,
    };

    BitUnPack(source1Bpp, &output, &args);
    CHECK(output == 0x40440040);

    output = 0;
    args.offsetZeros = TRUE;
    BitUnPack(source1Bpp, &output, &args);
    CHECK(output == 0x43443343);

    output = 0;
    args.compressedBits = 2;
    args.decompressedBits = 8;
    args.dataOffset = 0;
    args.offsetZeros = FALSE;
    BitUnPack(source2Bpp, &output, &args);
    CHECK(output == 0x03020100);

    CHECK(Div(7, 3) == 2);
    CHECK(Div(-7, 3) == -2);
    CHECK(Div(7, -3) == -2);
    CHECK(Div(INT32_MIN, -1) == INT32_MIN);
    CHECK(Div(1, 0) == INT32_MAX);
    CHECK(Div(-1, 0) == INT32_MIN);
}

static void TestDma(void)
{
    static u16 zeroCountDma0[0x4000];
    static u16 zeroCountDma3[0x10000];
    static u16 maskedCountSource[0x4001];
    ALIGNED(4) const u16 source[] = {10, 20, 30, 40};
    ALIGNED(4) u16 immediate[ARRAY_COUNT(source)] = {0};
    ALIGNED(4) u16 onVBlank[ARRAY_COUNT(source)] = {0};
    ALIGNED(4) u16 repeatedSource[] = {50, 60, 70};
    ALIGNED(4) u16 repeatedDest[] = {0, 0};
    u16 fill = 0xA55A;
    u16 maskedDest = 0;

    DmaSet(0, source, immediate,
           ((u32)(DMA_ENABLE | DMA_START_NOW | DMA_SRC_INC | DMA_DEST_INC) << 16)
           | ARRAY_COUNT(source));
    CHECK(memcmp(source, immediate, sizeof(source)) == 0);
    CHECK((REG_DMA0CNT_H & DMA_ENABLE) == 0);

    DmaSet(1, source, onVBlank,
           ((u32)(DMA_ENABLE | DMA_START_VBLANK | DMA_SRC_INC | DMA_DEST_INC) << 16)
           | ARRAY_COUNT(source));
    CHECK(onVBlank[0] == 0);
    CHECK((REG_DMA1CNT_H & DMA_ENABLE) != 0);
    RunDMAs(DMA_VBLANK);
    CHECK(memcmp(source, onVBlank, sizeof(source)) == 0);
    CHECK((REG_DMA1CNT_H & DMA_ENABLE) == 0);

    memset(zeroCountDma0, 0, sizeof(zeroCountDma0));
    DmaSet(0, &fill, zeroCountDma0,
           (u32)(DMA_ENABLE | DMA_START_NOW | DMA_SRC_FIXED | DMA_DEST_INC) << 16);
    CHECK(zeroCountDma0[0] == fill);
    CHECK(zeroCountDma0[ARRAY_COUNT(zeroCountDma0) - 1] == fill);

    memset(zeroCountDma3, 0, sizeof(zeroCountDma3));
    DmaSet(3, &fill, zeroCountDma3,
           (u32)(DMA_ENABLE | DMA_START_NOW | DMA_SRC_FIXED | DMA_DEST_INC) << 16);
    CHECK(zeroCountDma3[0] == fill);
    CHECK(zeroCountDma3[ARRAY_COUNT(zeroCountDma3) - 1] == fill);

    memset(maskedCountSource, 0, sizeof(maskedCountSource));
    maskedCountSource[0] = 0x1111;
    maskedCountSource[ARRAY_COUNT(maskedCountSource) - 1] = 0x2222;
    DmaSet(0, maskedCountSource, &maskedDest,
           ((u32)(DMA_ENABLE | DMA_START_NOW | DMA_SRC_INC | DMA_DEST_FIXED) << 16)
           | 0x4001);
    CHECK(maskedDest == 0x1111);

    immediate[0] = 0;
    DmaSet(0, &fill, immediate,
           ((u32)(DMA_ENABLE | DMA_START_NOW | DMA_REPEAT | DMA_SRC_FIXED | DMA_DEST_INC) << 16)
           | 1);
    CHECK(immediate[0] == fill);
    CHECK((REG_DMA0CNT_H & DMA_ENABLE) == 0);
    immediate[0] = 0;
    RunDMAs(DMA_NOW);
    CHECK(immediate[0] == 0);

    DmaSet(2, repeatedSource, repeatedDest,
           ((u32)(DMA_ENABLE | DMA_START_HBLANK | DMA_REPEAT | DMA_SRC_INC | DMA_DEST_RELOAD) << 16)
           | 1);
    RunDMAs(DMA_HBLANK);
    CHECK(repeatedDest[0] == 50 && repeatedDest[1] == 0);
    repeatedDest[0] = 0;
    RunDMAs(DMA_HBLANK);
    CHECK(repeatedDest[0] == 60 && repeatedDest[1] == 0);
    CHECK((REG_DMA2CNT_H & DMA_ENABLE) != 0);
    DmaStop(2);
    repeatedDest[0] = 0;
    RunDMAs(DMA_HBLANK);
    CHECK(repeatedDest[0] == 0);
}

int main(void)
{
    TestMemoryMap();
    TestMultiBootBoundary();
    TestResetAndBootBoundaries();
    TestCpuSet();
    TestDecompression();
    TestBitUnPackAndDiv();
    TestDma();

    if (sFailures != 0)
        return 1;

    puts("native hardware smoke checks passed");
    return 0;
}
