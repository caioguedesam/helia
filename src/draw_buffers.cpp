#include "../src/draw_buffers.hpp"
#include "dw/src/render/buffer.hpp"
#include "dw/src/render/render.hpp"

void initDrawBuffers(Renderer* pRenderer, DrawBuffers* pBuffers)
{
    ASSERT(pRenderer && pBuffers);

    *pBuffers = {};
    pBuffers->mCount = DB_COUNT;

    BufferDesc desc = {};
    desc.mType = BUFFER_TYPE_INDIRECT;
    desc.mSize = sizeof(IndirectDraw) * MAX_DRAWS * MAX_DRAW_BUFFERS;
    desc.mCount = MAX_DRAWS * MAX_DRAW_BUFFERS;
    desc.mStride = sizeof(IndirectDraw);
    for(int32 f = 0; f < CONCURRENT_FRAMES; f++)
    {
        addBuffer(pRenderer, desc, &pBuffers->pDrawBuffers[f]);
    }

    desc.mType = BUFFER_TYPE_INDIRECT;
    desc.mSize = sizeof(uint32) * MAX_DRAW_BUFFERS;
    desc.mCount = MAX_DRAW_BUFFERS;
    desc.mStride = sizeof(uint32);
    for(int32 f = 0; f < CONCURRENT_FRAMES; f++)
    {
        addBuffer(pRenderer, desc, &pBuffers->pDrawCountBuffers[f]);
    }
}

void destroyDrawBuffers(Renderer* pRenderer, DrawBuffers* pBuffers)
{
    ASSERT(pRenderer && pBuffers);

    for(int32 f = 0; f < CONCURRENT_FRAMES; f++)
    {
        removeBuffer(pRenderer, &pBuffers->pDrawBuffers[f]);
        removeBuffer(pRenderer, &pBuffers->pDrawCountBuffers[f]);
    }

    *pBuffers = {};
}

void cmdPrepareDrawBuffers(CommandBuffer* pCmd, DrawBuffers* pBuffers, uint32 activeFrame)
{
    ASSERT(activeFrame < CONCURRENT_FRAMES);

    cmdFillBuffer(pCmd, pBuffers->pDrawBuffers[activeFrame], 0);
    cmdFillBuffer(pCmd, pBuffers->pDrawCountBuffers[activeFrame], 0);

    Barrier barrier = {};
    barrier.mSrcStage = PIPELINE_STAGE_TRANSFER;
    barrier.mDstStage = PIPELINE_STAGE_DRAW_INDIRECT;
    barrier.mSrcAccess = MEMORY_ACCESS_TRANSFER_WRITE;
    barrier.mDstAccess = MEMORY_ACCESS_INDIRECT_READ;
    cmdBarrier(pCmd, 1, &barrier);
}

void cmdDrawIndirectBuffer(CommandBuffer* pCmd, DrawBuffers* pBuffers, uint32 drawBuffer, uint32 activeFrame)
{
    ASSERT(pBuffers);
    ASSERT(drawBuffer < pBuffers->mCount);
    ASSERT(activeFrame < CONCURRENT_FRAMES);

    uint64 bufferOffset = drawBuffer * MAX_DRAWS * sizeof(IndirectDraw);
    uint64 countOffset = drawBuffer * sizeof(uint32);

    cmdDrawIndexedIndirect(pCmd,
            pBuffers->pDrawBuffers[activeFrame],
            bufferOffset,
            pBuffers->pDrawCountBuffers[activeFrame],
            countOffset,
            MAX_DRAWS);
}
