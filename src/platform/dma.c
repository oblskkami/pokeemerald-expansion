#ifdef PORTABLE

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "gba/types.h"
#include "gba/gba.h"
#include "platform/dma.h"

struct DMATransfer {
    union {
        const void *src;
        const u16 *src16;
        const u32 *src32;
    };
    union {
        void *dst;
        vu16 *dst16;
        vu32 *dst32;
    };
    void *dstReload;
    u32 size;
    u16 control;
} DMAList[DMA_COUNT];

static u32 GetDmaTransferCount(int dmaNum, u32 control)
{
    u32 mask = dmaNum == 3 ? 0xFFFF : 0x3FFF;
    u32 count = control & mask;

    return count != 0 ? count : mask + 1;
}

void RunDMAs(u32 type)
{
    for (int dmaNum = 0; dmaNum < DMA_COUNT; dmaNum++)
    {
        struct DMATransfer *dma = &DMAList[dmaNum];
        u32 dmaCntReg = (&REG_DMA0CNT)[dmaNum * 3];
        if (!((dmaCntReg >> 16) & DMA_ENABLE))
        {
            dma->control &= ~DMA_ENABLE;
        }

        if ((dma->control & DMA_ENABLE) &&
            (u32)((dma->control & DMA_START_MASK) >> 12) == type)
        {
            //printf("DMA%d src=%p, dest=%p, control=%d\n", dmaNum, dma->src, dma->dest, dma->control);
            if (dma->src == NULL || dma->dst == NULL)
            {
                if (getenv("PE_TRACE") != NULL)
                    fprintf(stderr, "[dma] DMA%d with NULL %s: src=%p dst=%p control=%08x type=%u\n",
                            dmaNum,
                            dma->src == NULL ? (dma->dst == NULL ? "src and dst" : "src") : "dst",
                            dma->src, dma->dst, dma->control, type);
                dma->control &= ~DMA_ENABLE;
                (&REG_DMA0CNT_H)[dmaNum * 6] &= ~DMA_ENABLE;
                continue;
            }

            for (u32 i = 0; i < dma->size; i++)
            {
                if ((dma->control) & DMA_32BIT)
                     *dma->dst32 = *dma->src32;
                else *dma->dst16 = *dma->src16;

                // process destination pointer changes
                if (((dma->control) & DMA_DEST_MASK) == DMA_DEST_INC)
                {
                    if ((dma->control) & DMA_32BIT)
                            dma->dst32++;
                    else    dma->dst16++;
                }
                else if (((dma->control) & DMA_DEST_MASK) == DMA_DEST_DEC)
                {
                    if ((dma->control) & DMA_32BIT)
                            dma->dst32--;
                    else    dma->dst16--;
                }
                else if (((dma->control) & DMA_DEST_MASK) == DMA_DEST_RELOAD) // TODO
                {
                    if ((dma->control) & DMA_32BIT)
                            dma->dst32++;
                    else    dma->dst16++;
                }

                // process source pointer changes
                if (((dma->control) & DMA_SRC_MASK) == DMA_SRC_INC)
                {
                    if ((dma->control) & DMA_32BIT)
                            dma->src32++;
                    else    dma->src16++;
                }
                else if (((dma->control) & DMA_SRC_MASK) == DMA_SRC_DEC)
                {
                    if ((dma->control) & DMA_32BIT)
                            dma->src32--;
                    else    dma->src16--;
                }
            }

            if ((dma->control & DMA_REPEAT) && type != DMA_NOW)
            {
                dma->size = GetDmaTransferCount(dmaNum, (&REG_DMA0CNT)[dmaNum * 3]);
                if (((dma->control) & DMA_DEST_MASK) == DMA_DEST_RELOAD)
                    dma->dst = dma->dstReload;
            }
            else
            {
                dma->control &= ~DMA_ENABLE;
                // A one-shot GBA DMA clears its enable bit when the transfer
                // completes. Keep the host register image in sync so code
                // polling CNT_H observes the completed transfer.
                (&REG_DMA0CNT_H)[dmaNum * 6] &= ~DMA_ENABLE;
            }
        }
    }
}

void DmaSet(int dmaNum, const void *src, void *dest, u32 control)
{
    if (dmaNum < 0 || dmaNum >= DMA_COUNT)
    {
        fprintf(stderr, "DmaSet with invalid DMA number: dmaNum=%d, src=%p, dest=%p, control=%u\n", dmaNum, src, dest, control);
        return;
    }

    (&REG_DMA0SAD)[dmaNum * 3] = (vu32)(uintptr_t)src;
    (&REG_DMA0DAD)[dmaNum * 3] = (vu32)(uintptr_t)dest;
    (&REG_DMA0CNT)[dmaNum * 3] = control;

    struct DMATransfer *dma = &DMAList[dmaNum];
    dma->src = src;
    dma->dst = dest;
    dma->dstReload = dest;
    dma->size = GetDmaTransferCount(dmaNum, control);
    dma->control = control >> 16;

    RunDMAs(DMA_NOW);
}

#endif // PORTABLE
