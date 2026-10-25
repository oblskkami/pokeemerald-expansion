#ifndef GUARD_ALLOC_H
#define GUARD_ALLOC_H


#define FREE_AND_SET_NULL(ptr)          \
do {                                    \
    Free(ptr);                          \
    ptr = NULL;                         \
} while (0)

#define TRY_FREE_AND_SET_NULL(ptr) if (ptr != NULL) FREE_AND_SET_NULL(ptr)

#define MALLOC_SYSTEM_ID 0xA3A3

struct MemBlock
{
    // Whether this block is currently allocated.
    u16 allocated:1;

    u16 unused_00:4;

    // High 11 bits of location pointer.
    u16 locationHi:11;

    // Magic number used for error checking. Should equal MALLOC_SYSTEM_ID.
    u16 magic;

    // Size of the block (not including this header struct).
    u32 size:18;

    // Low 14 bits of location pointer.
    u32 locationLo:14;

    // Previous block pointer. Equals sHeapStart if this is the first block.
    struct MemBlock *prev;

    // Next block pointer. Equals sHeapStart if this is the last block.
    struct MemBlock *next;

#ifdef PORTABLE
    // Host pointers cannot be reconstructed from the GBA ROM address bits.
    const char *locationHost;
#endif

    // Data in the memory block. (Arrays of length 0 are a GNU extension.)
    u8 data[0];
};

#define HEAP_SIZE 0x1C500
extern u8 gHeap[HEAP_SIZE];

#if TESTING || !defined(NDEBUG)

#define Alloc(size) Alloc_(size, __FILE__ ":" STR(__LINE__))
#define AllocUnchecked(size) AllocUnchecked_(size, __FILE__ ":" STR(__LINE__))

#define AllocZeroed(size) AllocZeroed_(size, __FILE__ ":" STR(__LINE__))
#define AllocZeroedUnchecked(size) AllocZeroedUnchecked_(size, __FILE__ ":" STR(__LINE__))

#else

#define Alloc(size) Alloc_(size, NULL)
#define AllocUnchecked(size) AllocUnchecked_(size, NULL)
#define AllocZeroed(size) AllocZeroed_(size, NULL)
#define AllocZeroedUnchecked(size) AllocZeroedUnchecked_(size, NULL)

#endif

void *Alloc_(u32 size, const char *location);
void *AllocUnchecked_(u32 size, const char *location);
void *AllocZeroed_(u32 size, const char *location);
void *AllocZeroedUnchecked_(u32 size, const char *location);
#ifdef PORTABLE
// 宿主版：Free 也带上调用点（文件:行），用来指认"哪次释放把堆链表弄坏了"。
// 见 src/malloc.c 的 FreeInternal/HeapReport。GBA 构建不走这条路径。
void Free_Debug(void *pointer, const char *location);
#define Free(pointer) Free_Debug((pointer), __FILE__ ":" STR(__LINE__))
#else
void Free(void *pointer);
#endif
void InitHeap(void *heapStart, u32 heapSize);
void PrintHeap(void);

const struct MemBlock *HeapHead(void);
const char *MemBlockLocation(const struct MemBlock *block);
#ifdef PORTABLE
void HeapLogSummary(void);
void HeapLogCrashState(void);
#endif

#endif // GUARD_ALLOC_H
