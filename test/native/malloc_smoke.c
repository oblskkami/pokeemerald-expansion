#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "malloc.h"

static int sFailures;

#define CHECK(expression)                                                     \
    do                                                                        \
    {                                                                         \
        if (!(expression))                                                    \
        {                                                                     \
            fprintf(stderr, "malloc smoke check failed: %s:%d: %s\n",      \
                    __FILE__, __LINE__, #expression);                         \
            sFailures++;                                                      \
        }                                                                     \
    } while (0)

void MgbaPrintf(s32 level, const char *format, ...)
{
    va_list args;

    (void)level;
    va_start(args, format);
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
    va_end(args);
}

void MgbaAssert(const char *file, s32 line, const char *expression, bool32 stopProgram)
{
    fprintf(stderr, "unexpected malloc assertion: %s:%d: %s (%u)\n",
            file, line, expression, stopProgram);
    sFailures++;
}

int main(void)
{
    static ALIGNED(4) u8 heap[256];
    static const char location[] = "test/native/malloc_smoke.c:portable";
    const struct MemBlock *head;
    void *allocation;

    InitHeap(heap, sizeof(heap));
    allocation = AllocUnchecked_(16, location);
    CHECK(allocation != NULL);

    head = HeapHead();
    CHECK(head != NULL);
    CHECK(MemBlockLocation(head) == location);
    CHECK(strcmp(MemBlockLocation(head), location) == 0);

    // This used to pass a reconstructed 0x08xxxxxx address to "%s".
    PrintHeap();

    Free(allocation);
    CHECK(MemBlockLocation(head) == NULL);

    allocation = AllocUnchecked_(16, NULL);
    CHECK(allocation != NULL);
    CHECK(MemBlockLocation(head) == NULL);
    Free(allocation);

    if (sFailures != 0)
        return 1;

    puts("native malloc metadata smoke checks passed");
    return 0;
}
