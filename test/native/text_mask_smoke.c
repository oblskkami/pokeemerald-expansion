#include <stdio.h>

#include "global.h"
#include "text.h"

static int sFailures;

#define CHECK(expression)                                                     \
    do                                                                        \
    {                                                                         \
        if (!(expression))                                                    \
        {                                                                     \
            fprintf(stderr, "text mask smoke failed: %s:%d: %s\n",         \
                    __FILE__, __LINE__, #expression);                        \
            sFailures++;                                                      \
        }                                                                     \
    } while (0)

int main(void)
{
    CHECK(GetTextGlyphWidthMask(-1) == 0);
    CHECK(GetTextGlyphWidthMask(0) == 0);
    CHECK(GetTextGlyphWidthMask(1) == 0xFu);
    CHECK(GetTextGlyphWidthMask(7) == 0x0FFFFFFFu);
    CHECK(GetTextGlyphWidthMask(8) == 0xFFFFFFFFu);
    CHECK(GetTextGlyphWidthMask(10) == 0xFFFFFFFFu);
    CHECK(GetTextGlyphWidthMask(12) == 0xFFFFFFFFu);

    if (sFailures != 0)
        return 1;

    puts("native text mask smoke checks passed");
    return 0;
}
