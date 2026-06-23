#pragma once
#include "../dw/src/render/buffer.hpp"
#include "../dw/src/render/command_buffer.hpp"
#include "../src/shared_defines.hpp"

struct Renderer;

struct DrawBuffers
{
    // Contains draw commands for each indirect draw pass
    Buffer* pDrawBuffers[CONCURRENT_FRAMES];
    // Buffer with amount of draws per draw pass
    Buffer* pDrawCountBuffers[CONCURRENT_FRAMES];
    uint32 mCount = 0;
};

void initDrawBuffers(Renderer* pRenderer, DrawBuffers* pBuffers);
void destroyDrawBuffers(Renderer* pRenderer, DrawBuffers* pBuffers);

void cmdPrepareDrawBuffers(CommandBuffer* pCmd, DrawBuffers* pBuffers, uint32 activeFrame);
void cmdDrawIndirectBuffer(CommandBuffer* pCmd, DrawBuffers* pBuffers, uint32 drawBuffer, uint32 activeFrame);
