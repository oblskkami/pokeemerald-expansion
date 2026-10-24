#include <stdio.h>
#include <string.h>

#include "gba/defines.h"
#include "gba/types.h"

void FastLZ77UnCompWram(const u32 *src, void *dest);

int main(void)
{
    static const u8 sCompressed[] ALIGNED(4) = {
        0x10, 0x04, 0x00, 0x00,
        0x00, 't', 'e', 's', 't',
    };
    u8 output[4] = {0};

    FastLZ77UnCompWram((const u32 *)sCompressed, output);
    if (memcmp(output, "test", sizeof(output)) != 0)
    {
        fprintf(stderr, "portable LZ77 fallback failed\n");
        return 1;
    }

    puts("native decompression smoke checks passed");
    return 0;
}
