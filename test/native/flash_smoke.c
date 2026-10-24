#include <stdio.h>
#include <string.h>

#include "gba/types.h"
#include "gba/gba.h"
#include "gba/flash_internal.h"
#include "platform.h"

const struct FlashType *gFlash;

static int sFailures;

#define CHECK(expression)                                                     \
    do                                                                        \
    {                                                                         \
        if (!(expression))                                                    \
        {                                                                     \
            fprintf(stderr, "flash smoke check failed: %s:%d: %s\n",         \
                    __FILE__, __LINE__, #expression);                         \
            sFailures++;                                                      \
        }                                                                     \
    } while (0)

int main(void)
{
    const char *path = "build/native/flash-smoke.sav";
    u8 writeBuffer[FLASH_ROM_SIZE_1M > 4096 ? 4096 : FLASH_ROM_SIZE_1M];
    u8 readBuffer[sizeof(writeBuffer)];

    memset(writeBuffer, 0xA5, sizeof(writeBuffer));
    memset(readBuffer, 0, sizeof(readBuffer));
    remove(path);

    CHECK(Platform_InitSave(path));
    gFlash = &gPortableFlash.type;
    CHECK(ProgramFlashSector_Portable(3, writeBuffer) == 0);
    Platform_ReadFlash(3, 0, readBuffer, sizeof(readBuffer));
    CHECK(memcmp(writeBuffer, readBuffer, sizeof(writeBuffer)) == 0);

    CHECK(ProgramFlashByte_Portable(3, 17, 0x3C) == 0);
    memset(readBuffer, 0, sizeof(readBuffer));
    Platform_ReadFlash(3, 0, readBuffer, sizeof(readBuffer));
    CHECK(readBuffer[17] == 0x3C);

    CHECK(EraseFlashSector_Portable(3) == 0);
    memset(readBuffer, 0, sizeof(readBuffer));
    Platform_ReadFlash(3, 0, readBuffer, sizeof(readBuffer));
    for (size_t i = 0; i < sizeof(readBuffer); i++)
        CHECK(readBuffer[i] == 0xFF);

    remove(path);
    if (sFailures != 0)
        return 1;

    puts("native flash smoke checks passed");
    return 0;
}
