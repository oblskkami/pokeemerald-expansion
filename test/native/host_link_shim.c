#include "gba/types.h"
#include "main.h"
#include "random.h"

#ifdef _WIN32
// MinGW's PE linker does not resolve GCC's weak alias declarations in the
// same way as the ELF linker.  Keep the normal overrideable aliases in the
// game sources, but provide strong Windows defaults for the final executable.
u32 RandomUniform(enum RandomTag tag, u32 lo, u32 hi)
{
    return RandomUniformDefault(tag, lo, hi);
}

u32 RandomUniformExcept(enum RandomTag tag, u32 lo, u32 hi, bool32 (*reject)(u32))
{
    return RandomUniformExceptDefault(tag, lo, hi, reject);
}

u32 RandomWeightedArray(enum RandomTag tag, u32 sum, u32 n, const u16 *weights)
{
    return RandomWeightedArrayDefault(tag, sum, n, weights);
}

const void *RandomElementArray(enum RandomTag tag, const void *array, size_t size, size_t count)
{
    return RandomElementArrayDefault(tag, array, size, count);
}

const bool8 gTestRunnerEnabled = FALSE;
#endif

// These names are supplied by the GBA linker script/ROM header.  Keep the
// host definitions in the link harness so portable object counts remain a
// direct audit of game sources.
char __iwram_end[1];
char __rom_end[1];

const u8 RomHeaderGameCode[GAME_CODE_LENGTH] = {'B', 'P', 'E', 'E'};
const u8 RomHeaderSoftwareVersion = 0;
