#include "global.h"
#include "malloc.h"
#if TESTING
#include "test/test.h"
#endif
#ifdef PORTABLE
#include "platform.h"
// malloc.h 在 PORTABLE 下把 Free() 定义成了带调用点的宏；本文件要实现真正的 Free()。
#undef Free
#endif

static void *sHeapStart;
static u32 sHeapSize;

#ifdef PORTABLE
#define HEAP_OP_LOG_COUNT 96

struct HeapOpLogEntry
{
    u8 isFree;
    u32 size;
    const void *ptr;
    const char *location;
};

static struct HeapOpLogEntry sHeapOpLog[HEAP_OP_LOG_COUNT];
static u32 sHeapOpCount;
static u32 sHeapReportCount;

static struct HeapOpLogEntry *HeapOpSlot(void)
{
    return &sHeapOpLog[sHeapOpCount % HEAP_OP_LOG_COUNT];
}

static void HeapOpRecord(u8 isFree, u32 size, const void *ptr, const char *location)
{
    struct HeapOpLogEntry *entry = HeapOpSlot();

    entry->isFree = isFree;
    entry->size = size;
    entry->ptr = ptr;
    entry->location = location;
    sHeapOpCount++;
}

static void HeapOpSetPtr(const void *ptr)
{
    if (sHeapOpCount != 0)
        HeapOpSlot()->ptr = ptr;
}

// 宿主堆没有 MMU：块链表一旦被写坏，GBA 上只是拿着野指针继续跑（读总线垃圾），
// 而 Windows 上会直接读地址 0 崩溃（实测崩在 AllocInternal 读 pos->allocated）。
// 这里先做范围与魔数校验；坏掉时把最近 96 次 Alloc/Free 与整张块表写进 crash.log，
// 然后安全返回 NULL，避免直接闪退。
static bool8 HeapBlockSane(const struct MemBlock *block)
{
    if (sHeapStart == NULL || sHeapSize == 0)
        return FALSE;
    if (block == NULL)
        return FALSE;
    if ((const u8 *)block < (const u8 *)sHeapStart)
        return FALSE;
    if ((const u8 *)block + sizeof(struct MemBlock) > (const u8 *)sHeapStart + sHeapSize)
        return FALSE;
    if (block->magic != MALLOC_SYSTEM_ID)
        return FALSE;
    if (block->next == NULL)
        return FALSE;
    if ((const u8 *)block->next < (const u8 *)sHeapStart)
        return FALSE;
    if ((const u8 *)block->next + sizeof(struct MemBlock) > (const u8 *)sHeapStart + sHeapSize)
        return FALSE;
    return TRUE;
}

static void HeapDumpOps(void)
{
    u32 count = sHeapOpCount < HEAP_OP_LOG_COUNT ? sHeapOpCount : HEAP_OP_LOG_COUNT;
    u32 i;

    HostLogf("heap: last %u heap ops (total %u):", count, sHeapOpCount);
    for (i = 0; i < count; i++)
    {
        const struct HeapOpLogEntry *entry = &sHeapOpLog[(sHeapOpCount - count + i) % HEAP_OP_LOG_COUNT];

        HostLogf("heap op #%u %s ptr=%p size=%u loc=%s",
                 sHeapOpCount - count + i, entry->isFree ? "free" : "alloc",
                 entry->ptr, entry->size, entry->location ? entry->location : "?");
    }
}

static void HeapDumpBlocks(void)
{
    const struct MemBlock *head = (const struct MemBlock *)sHeapStart;
    const struct MemBlock *block = head;
    u32 i;

    if (head == NULL)
    {
        HostLogf("heap: list not initialized (start=%p size=%u)", sHeapStart, sHeapSize);
        return;
    }
    for (i = 0; i < 64; i++)
    {
        HostLogf("heap blk %u ptr=%p alloc=%u magic=%04X size=%u prev=%p next=%p",
                 i, block, block->allocated, block->magic, block->size, block->prev, block->next);
        if (block->next == NULL || block->next == head)
            break;
        if (!HeapBlockSane(block->next))
        {
            HostLogf("heap: blk %u next=%p is not sane, stop dump", i, block->next);
            break;
        }
        block = block->next;
    }
}

void HeapLogSummary(void)
{
    const struct MemBlock *head = (const struct MemBlock *)sHeapStart;
    const struct MemBlock *block = head;
    u32 blocks = 0, allocated = 0, freeBytes = 0, largestFree = 0;

    if (head == NULL)
    {
        HostLogf("heap: summary unavailable (start=%p)", sHeapStart);
        return;
    }
    do
    {
        blocks++;
        if (block->allocated)
        {
            allocated++;
        }
        else
        {
            freeBytes += block->size;
            if (block->size > largestFree)
                largestFree = block->size;
        }
        if (block->next == NULL || block->next == head)
            break;
        if (!HeapBlockSane(block->next))
        {
            HostLogf("heap: summary stopped at bad next=%p (blk %u)", block->next, blocks);
            break;
        }
        block = block->next;
    } while (blocks < 4096);

    HostLogf("heap: blocks=%u allocated=%u free=%u largestFree=%u start=%p size=%u ops=%u",
             blocks, allocated, freeBytes, largestFree, sHeapStart, sHeapSize, sHeapOpCount);
}

static void HeapReport(const char *who, const void *block)
{
    if (sHeapReportCount >= 6)
        return;
    sHeapReportCount++;
    HostLogf("heap: %s saw bad block %p (start=%p size=%u)", who, block, sHeapStart, sHeapSize);
    HeapDumpOps();
    HeapDumpBlocks();
}

// 每次 Alloc/Free 前后整表校验：这样"链表被写坏"的那个操作会被当场抓到，
// 配合 96 条操作环形日志，就能指认是哪次分配（loc=文件:行号）/释放出的问题。
static bool8 HeapValidateList(const char *when)
{
    const struct MemBlock *head = (const struct MemBlock *)sHeapStart;
    const struct MemBlock *block = head;
    u32 n = 0;

    if (head == NULL)
    {
        HeapReport(when, NULL);
        return FALSE;
    }
    for (;;)
    {
        if (!HeapBlockSane(block))
        {
            HeapReport(when, block);
            return FALSE;
        }
        block = block->next;
        if (block == head)
            break;
        if (++n > 4096)
        {
            HeapReport(when, block);
            return FALSE;
        }
    }
    return TRUE;
}

// 崩溃时调用（宿主异常过滤器里）：即使这次不是崩在 Alloc/Free 里，
// 也能看出堆在这之前是不是已经坏了，并把最近 96 次分配/释放打出来。
void HeapLogCrashState(void)
{
    HostLogf("heap: crash snapshot list=%s", HeapValidateList("crash") ? "ok" : "BROKEN");
    HeapDumpOps();
    HeapDumpBlocks();
    HeapLogSummary();
}
#endif

ALIGNED(4) EWRAM_DATA u8 gHeap[HEAP_SIZE] = {0};

void PutMemBlockHeader(void *block, struct MemBlock *prev, struct MemBlock *next, u32 size)
{
    struct MemBlock *header = (struct MemBlock *)block;

    header->allocated = FALSE;
    header->locationHi = 0;
    header->magic = MALLOC_SYSTEM_ID;
    header->size = size;
    header->locationLo = 0;
    header->prev = prev;
    header->next = next;
#ifdef PORTABLE
    header->locationHost = NULL;
#endif
}

void PutFirstMemBlockHeader(void *block, u32 size)
{
    PutMemBlockHeader(block, (struct MemBlock *)block, (struct MemBlock *)block, size - sizeof(struct MemBlock));
}

static void *AllocInternal(void *heapStart, u32 size, const char *location)
{
    struct MemBlock *pos = (struct MemBlock *)heapStart;
    struct MemBlock *head = pos;
    struct MemBlock *splitBlock;
    u32 foundBlockSize;
#ifdef PORTABLE
    u32 guard = 0;
    HeapOpRecord(FALSE, size, NULL, location);
    if (!HeapValidateList("Alloc"))
        return NULL;
#endif

    // Alignment
    if (size & 3)
        size = 4 * ((size / 4) + 1);

    for (;;)
    {
        // Loop through the blocks looking for unused block that's big enough.
#ifdef PORTABLE
        if (!HeapBlockSane(pos))
        {
            HeapReport("Alloc", pos);
            return NULL;
        }
        if (++guard > 4096)
        {
            HeapReport("Alloc (looping list)", pos);
            return NULL;
        }
#endif

        if (!pos->allocated)
        {
            foundBlockSize = pos->size;

            if (foundBlockSize >= size)
            {
                if (foundBlockSize - size < 2 * sizeof(struct MemBlock))
                {
                    // The block isn't much bigger than the requested size,
                    // so just use it.
                    pos->allocated = TRUE;
                }
                else
                {
                    // The block is significantly bigger than the requested
                    // size, so split the rest into a separate block.
                    foundBlockSize -= sizeof(struct MemBlock);
                    foundBlockSize -= size;

                    splitBlock = (struct MemBlock *)(pos->data + size);

                    pos->allocated = TRUE;
                    pos->size = size;

                    PutMemBlockHeader(splitBlock, pos, pos->next, foundBlockSize);

                    pos->next = splitBlock;

                    if (splitBlock->next != head)
                        splitBlock->next->prev = splitBlock;
                }

#ifdef PORTABLE
                pos->locationHost = location;
                // 宿主堆复用会留下上一份数据的指针，未初始化的结构体字段
                // （例如储存系统的 displayMonPalette）会变成野指针并导致崩溃。
                // GBA 上读野地址只是读到总线垃圾，不会崩；这里按 0 填充保持一致。
                memset(pos->data, 0, size);
#else
                pos->locationHi = ((uintptr_t)location) >> 14;
                pos->locationLo = (uintptr_t)location;
#endif
#ifdef PORTABLE
                HeapOpSetPtr(pos->data);
#endif

                return pos->data;
            }
        }

        if (pos->next == head)
            return NULL;

        pos = pos->next;
    }
}

static void FreeInternal(void *heapStart, void *pointer, const char *location)
{
    if (pointer)
    {
        struct MemBlock *head = (struct MemBlock *)heapStart;
        struct MemBlock *block = (struct MemBlock *)((u8 *)pointer - sizeof(struct MemBlock));
#ifdef PORTABLE
        HeapOpRecord(TRUE, 0, pointer, location);
        if (!HeapValidateList("Free") || !HeapBlockSane(block))
        {
            if (HeapBlockSane(block))
                HeapReport("Free (bad pointer)", block);
            return;
        }
        // 双重释放：GBA 的 AGB_ASSERT 是空宏，会再合并一次邻居 → 链表被写坏。
        // 这里直接拦下来并记下调用点，绝不让它污染堆。
        if (!block->allocated)
        {
            HeapReport("Free (double free)", block);
            return;
        }
#endif
        AGB_ASSERT(block->magic == MALLOC_SYSTEM_ID);
        AGB_ASSERT(block->allocated == TRUE);
        block->allocated = FALSE;

        // If the freed block isn't the last one, merge with the next block
        // if it's not in use.
        if (block->next != head)
        {
            if (!block->next->allocated)
            {
                block->size += sizeof(struct MemBlock) + block->next->size;
                block->next->magic = 0;
                block->next = block->next->next;
                if (block->next != head)
                    block->next->prev = block;
            }
        }

        // If the freed block isn't the first one, merge with the previous block
        // if it's not in use.
        if (block != head)
        {
            if (!block->prev->allocated)
            {
                AGB_ASSERT(block->prev->magic == MALLOC_SYSTEM_ID);

                block->prev->next = block->next;

                if (block->next != head)
                    block->next->prev = block->prev;

                block->magic = 0;
                block->prev->size += sizeof(struct MemBlock) + block->size;
            }
        }
    }
}

static void *AllocZeroedInternal(void *heapStart, u32 size, const char *location)
{
    void *mem = AllocInternal(heapStart, size, location);

    if (mem != NULL)
    {
        if (size & 3)
            size = 4 * ((size / 4) + 1);

        CpuFill32(0, mem, size);
    }

    return mem;
}

void InitHeap(void *heapStart, u32 heapSize)
{
#ifdef PORTABLE
    // 重新初始化堆会让所有已分配的指针变成野指针（之后 Free/Alloc 就会踩坏链表）。
    // 这里把"上一次堆里还有多少块在用"记下来，便于识别这种时序问题。
    {
        u32 outstanding = 0;

        if (sHeapStart != NULL)
        {
            const struct MemBlock *head = (const struct MemBlock *)sHeapStart;
            const struct MemBlock *block = head;
            u32 n = 0;

            while (HeapBlockSane(block))
            {
                if (block->allocated)
                    outstanding++;
                if (block->next == head)
                    break;
                block = block->next;
                if (++n >= 4096)
                    break;
            }
            HostLogf("heap: reinit start=%p size=%u outstanding=%u", heapStart, heapSize, outstanding);
        }
    }
    if (sHeapStart != NULL && !HeapValidateList("InitHeap"))
        HostLogf("heap: previous list was already broken at InitHeap");
#endif
    sHeapStart = heapStart;
    sHeapSize = heapSize;
    PutFirstMemBlockHeader(heapStart, heapSize);
#ifdef PORTABLE
    HostLogf("heap: init start=%p size=%u", heapStart, heapSize);
#endif
}

void PrintHeap(void)
{
    const struct MemBlock *head = HeapHead();
    const struct MemBlock *block = head;
    do
    {
        if (block->allocated)
        {
            const char *location = MemBlockLocation(block);
            if (location)
                DebugPrintf("%s: %d bytes allocated", location, block->size);
            else
                DebugPrintf("<unknown>: %d bytes allocated", block->size);
        }
        block = block->next;
    }
    while (block != head);
}

void *Alloc_(u32 size, const char *location)
{
    void *p = AllocInternal(sHeapStart, size, location);
    if (!p)
    {
        if (TESTING)
            PrintHeap();
        fatalf("%s: out of memory trying to allocate %d bytes", location, size);
    }
    return p;
}

void *AllocUnchecked_(u32 size, const char *location)
{
    return AllocInternal(sHeapStart, size, location);
}

void *AllocZeroed_(u32 size, const char *location)
{
    void *p = AllocZeroedInternal(sHeapStart, size, location);
    if (!p)
    {
        if (TESTING)
            PrintHeap();
        fatalf("%s: out of memory trying to allocate %d bytes", location, size);
    }
    return p;
}

void *AllocZeroedUnchecked_(u32 size, const char *location)
{
    return AllocZeroedInternal(sHeapStart, size, location);
}

void Free(void *pointer)
{
    FreeInternal(sHeapStart, pointer, NULL);
}

#ifdef PORTABLE
void Free_Debug(void *pointer, const char *location)
{
    FreeInternal(sHeapStart, pointer, location);
}
#endif

const struct MemBlock *HeapHead(void)
{
    return (const struct MemBlock *)sHeapStart;
}

const char *MemBlockLocation(const struct MemBlock *block)
{
    if (!block->allocated)
        return NULL;

#ifdef PORTABLE
    return block->locationHost;
#else
    return (const char *)(ROM_START | (block->locationHi << 14) | block->locationLo);
#endif
}
