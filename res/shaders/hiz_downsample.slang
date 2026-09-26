#include "common.slang"

struct Params
{
    uint prevDepth;
    uint prevWidth;
    uint prevHeight;
};

struct VSIn
{
    float2 pos : POSITION;
    float2 uv : TEXCOORD0;
};

struct PSIn
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

struct PSOut
{
    float depth : SV_Depth;
};

VS_ENTRY
PSIn VSMain(VSIn vsIn)
{
    PSIn output;
    output.pos = float4(vsIn.pos, 0, 1);
    output.uv = vsIn.uv;

    return output;
}

#define USE_GATHER 0

PS_ENTRY
PSOut PSMain(PSIn psIn, uniform Params params)
{
    Texture2D<float4> prevDepth = GetTexture2DFloat4(params.prevDepth);
#if USE_GATHER
    float2 uv;
    uv.x = psIn.pos.x * 2.0 / (float)params.prevWidth;
    uv.y = psIn.pos.y * 2.0 / (float)params.prevHeight;

    SamplerState samplerPoint = GetSamplerPoint();
    float4 prevDepthSamples = prevDepth.Gather(samplerPoint, uv);
#else
    int3 currentCoord = int3(int2(psIn.pos.xy), 0);
    int3 prevCoord = currentCoord * 2;
    float4 prevDepthSamples;
    prevDepthSamples.x = prevDepth.Load(prevCoord).r;
    prevDepthSamples.y = prevDepth.Load(prevCoord + int3(1, 0, 0)).r;
    prevDepthSamples.z = prevDepth.Load(prevCoord + int3(0, 1, 0)).r;
    prevDepthSamples.w = prevDepth.Load(prevCoord + int3(1, 1, 0)).r;
#endif

    float z = min(min(min(prevDepthSamples.x, prevDepthSamples.y), prevDepthSamples.z), prevDepthSamples.w);

    // TODO(cguedes): Handle odd widths/heights, currently forgoing just for testing purposes

    // For odd dimensions, extra samples are needed to maintain the correct correspondence
    // between mip levels when downsampling (texels from N to N-1 aren't mapped exactly 2-1 like in even dimensions).
    bool isWidthOdd = (params.prevWidth & 1) != 0;
    bool isHeightOdd = (params.prevHeight & 1) != 0;
    if(isWidthOdd)
    {
        prevDepthSamples.x = prevDepth.Load(prevCoord + int3(2, 0, 0)).r;
        prevDepthSamples.y = prevDepth.Load(prevCoord + int3(2, 1, 0)).r;
        z = min(min(prevDepthSamples.x, prevDepthSamples.y), z);
    }
    if(isHeightOdd)
    {
        prevDepthSamples.z = prevDepth.Load(prevCoord + int3(0, 2, 0)).r;
        prevDepthSamples.w = prevDepth.Load(prevCoord + int3(1, 2, 0)).r;
        z = min(min(prevDepthSamples.z, prevDepthSamples.w), z);
    }
    if(isWidthOdd && isHeightOdd)
    {
        z = min(z, prevDepth.Load(prevCoord + int3(2, 2, 0)).r);
    }

    PSOut result;
    result.depth = z;
    return result;
}

#if 0
struct Params
{
    uint pFrame;
    uint baseMip;
    uint mipCount;
};

groupshared float samples[8][8][3];

int2 GenerateMip(Params params, PerFrame frame, int i, int2 gID, int2 sampleCoord)
{
    int mip = int(params.baseMip) + i;
    if(mip >= HIZ_MAX || i > params.mipCount) return int2(0,0);
    int prevSample = i - 1;

    float z0 = samples[sampleCoord.x][sampleCoord.y][prevSample];
    float z1 = samples[sampleCoord.x + 1][sampleCoord.y][prevSample];
    float z2 = samples[sampleCoord.x][sampleCoord.y + 1][prevSample];
    float z3 = samples[sampleCoord.x + 1][sampleCoord.y + 1][prevSample];
    
    // Store the furthest depth in the mip chain (reverse Z = min)
    // which is the most conservative occluder
    float z = min(z0, min(z1, min(z2, z3)));

    sampleCoord = sampleCoord / 2;

    int denom = int(pow(2, i));
    int2 mipCoord = int2(gID.x / denom, gID.y / denom);
    RWTexture2D<float> hizMip = GetRWTexture2DFloat(frame.mHandleHiZ[mip]);
    hizMip[mipCoord] = z;

    if(i < 3)
    {
        samples[sampleCoord.x][sampleCoord.y][i] = z;
    }

    return sampleCoord;
}

CS_ENTRY
[numthreads(8,8,1)]
void CSMain(uint3 dispatchId : SV_DispatchThreadID,
            uint3 groupId : SV_GroupThreadID,
            uniform Params params)
{
    PerFrame frame = GetBufferConstants(PerFrame, params.pFrame);
    // Read 8x8 block, one texel per thread
    int2 gID = int2(dispatchId.xy);
    RWTexture2D<float> hizMip = GetRWTexture2DFloat(frame.mHandleHiZ[params.baseMip]);
    float z = hizMip[gID].r;

    int2 tID = int2(groupId.xy);
    samples[tID.x][tID.y][0] = z;

    GroupMemoryBarrierWithGroupSync();

    // Fill block for mip base + 1 (4x4)
    int2 sampleCoord = tID;
    if(tID.x % 2 == 0 && tID.y % 2 == 0)
        sampleCoord = GenerateMip(params, frame, 1, gID, sampleCoord); 
    GroupMemoryBarrierWithGroupSync();
    return;

    // Fill block for mip base + 2 (2x2)
    if(tID.x % 4 == 0 && tID.y % 4 == 0)
        sampleCoord = GenerateMip(params, frame, 2, gID, sampleCoord); 
    GroupMemoryBarrierWithGroupSync();

    // Fill block for mip base + 3 (1x1)
    if(tID.x % 8 == 0 && tID.y % 8 == 0)
        sampleCoord = GenerateMip(params, frame, 3, gID, sampleCoord); 

}
#endif
