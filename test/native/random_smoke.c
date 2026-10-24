#include <stdio.h>

#include "gba/types.h"
#include "random.h"

int main(void)
{
    gRngValue = (struct Sfc32State){ .a = 1, .b = 2, .c = 3, .ctr = 4 };

    if (Random32() != 7
     || gRngValue.a != 2
     || gRngValue.b != 27
     || gRngValue.c != 6291463
     || gRngValue.ctr != 5)
    {
        fprintf(stderr, "portable Random32 state transition failed\n");
        return 1;
    }

    puts("native random smoke checks passed");
    return 0;
}
