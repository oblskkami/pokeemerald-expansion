#ifdef PORTABLE

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define NOGDI
#include <windows.h>
#ifdef TRANSPARENT
#undef TRANSPARENT
#endif
#endif

#include "gba/types.h"
#include "gba/gba.h"
#include "gba/flash_internal.h"
#include "platform.h"

#define PORTABLE_SECTOR_SIZE 4096
#define PORTABLE_SECTOR_SHIFT 12
#define PORTABLE_SECTOR_COUNT 32

static char sSavePath[1024];

static bool ReplaceSaveFile(const char *tempPath, const char *savePath)
{
#ifdef _WIN32
    return MoveFileExA(tempPath, savePath,
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(tempPath, savePath) == 0;
#endif
}

static const u16 sPortableMaxTime[] =
{
    0, 0, 0,
    0, 0, 0,
    0, 0, 0,
    0, 0, 0,
};

const struct FlashSetupInfo gPortableFlash =
{
    ProgramFlashByte_Portable,
    ProgramFlashSector_Portable,
    EraseFlashChip_Portable,
    EraseFlashSector_Portable,
    WaitForFlashWrite_Portable,
    sPortableMaxTime,
    {
        FLASH_ROM_SIZE_1M,
        {
            PORTABLE_SECTOR_SIZE,
            PORTABLE_SECTOR_SHIFT,
            PORTABLE_SECTOR_COUNT,
            0,
        },
        {0, 0},
        {{0x50, 0x43}},
    },
};

static bool IsValidRange(u16 sectorNum, u32 offset, u32 size)
{
    return sectorNum < PORTABLE_SECTOR_COUNT
        && offset <= PORTABLE_SECTOR_SIZE
        && size <= PORTABLE_SECTOR_SIZE - offset;
}

bool Platform_InitSave(const char *path)
{
    FILE *file;
    size_t pathLength;

    if (path == NULL)
        return false;
    pathLength = strlen(path);
    if (pathLength == 0 || pathLength >= sizeof(sSavePath))
        return false;

    memcpy(sSavePath, path, pathLength + 1);
    memset(FLASH_BASE, 0xFF, FLASH_ROM_SIZE_1M);

    file = fopen(sSavePath, "rb");
    if (file == NULL)
        return errno == ENOENT;

    (void)fread(FLASH_BASE, 1, FLASH_ROM_SIZE_1M, file);
    if (ferror(file))
    {
        fclose(file);
        return false;
    }

    return fclose(file) == 0;
}

bool Platform_FlushSave(void)
{
    char tempPath[sizeof(sSavePath) + 5];
    FILE *file;
    bool success;

    if (sSavePath[0] == '\0')
        return false;
    if (snprintf(tempPath, sizeof(tempPath), "%s.tmp", sSavePath) < 0)
        return false;

    file = fopen(tempPath, "wb");
    if (file == NULL)
        return false;

    success = fwrite(FLASH_BASE, 1, FLASH_ROM_SIZE_1M, file) == FLASH_ROM_SIZE_1M;
    success = fflush(file) == 0 && success;
    success = fclose(file) == 0 && success;
    if (!success || !ReplaceSaveFile(tempPath, sSavePath))
    {
        remove(tempPath);
        return false;
    }
    return true;
}

void Platform_StoreSaveFile(void)
{
    (void)Platform_FlushSave();
}

void Platform_ReadFlash(u16 sectorNum, u32 offset, u8 *dest, u32 size)
{
    if (dest == NULL || !IsValidRange(sectorNum, offset, size))
        return;
    memcpy(dest, FLASH_BASE + (sectorNum << PORTABLE_SECTOR_SHIFT) + offset, size);
}

u16 WaitForFlashWrite_Portable(u8 phase, u8 *addr, u8 lastData)
{
    (void)phase;
    (void)addr;
    (void)lastData;
    return 0;
}

u16 EraseFlashChip_Portable(void)
{
    memset(FLASH_BASE, 0xFF, FLASH_ROM_SIZE_1M);
    return Platform_FlushSave() ? 0 : 1;
}

u16 EraseFlashSector_Portable(u16 sectorNum)
{
    if (!IsValidRange(sectorNum, 0, PORTABLE_SECTOR_SIZE))
        return 0x80FF;

    memset(FLASH_BASE + (sectorNum << PORTABLE_SECTOR_SHIFT),
           0xFF, PORTABLE_SECTOR_SIZE);
    return Platform_FlushSave() ? 0 : 1;
}

u16 ProgramFlashByte_Portable(u16 sectorNum, u32 offset, u8 data)
{
    if (!IsValidRange(sectorNum, offset, 1))
        return 0x8000;

    FLASH_BASE[(sectorNum << PORTABLE_SECTOR_SHIFT) + offset] = data;
    return Platform_FlushSave() ? 0 : 1;
}

u16 ProgramFlashSector_Portable(u16 sectorNum, u8 *src)
{
    if (src == NULL || !IsValidRange(sectorNum, 0, PORTABLE_SECTOR_SIZE))
        return 0x80FF;

    memcpy(FLASH_BASE + (sectorNum << PORTABLE_SECTOR_SHIFT),
           src, PORTABLE_SECTOR_SIZE);
    return Platform_FlushSave() ? 0 : 1;
}

#endif // PORTABLE
