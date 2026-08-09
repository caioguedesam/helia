#include "common.hlsl"

PUSH_CONSTANTS_BEGIN()
uint baseMip;
uint mipCount;
PUSH_CONSTANTS_END()

groupshared float samples[8][8][3];

int2 GenerateMip(int i, int2 gID, int2 sampleCoord)
{
    int mip = int(pushConstants.baseMip) + i;
    if(mip >= HIZ_MAX || i > pushConstants.mipCount) return int2(0,0);
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
    GetStorageTextureR32F(perFrame.mHandleHiZ[mip])[mipCoord] = z;

    if(i < 3)
    {
        samples[sampleCoord.x][sampleCoord.y][i] = z;
    }

    return sampleCoord;
}

[numthreads(8,8,1)]
void CSMain(uint3 dispatchId : SV_DispatchThreadID,
            uint3 groupId : SV_GroupThreadID)
{
    // Read 8x8 block, one texel per thread
    int2 gID = int2(dispatchId.xy);
    float z = GetStorageTextureR32F(perFrame.mHandleHiZ[pushConstants.baseMip])[gID].r;

    int2 tID = int2(groupId.xy);
    samples[tID.x][tID.y][0] = z;

    GroupMemoryBarrierWithGroupSync();

    // Fill block for mip base + 1 (4x4)
    int2 sampleCoord = tID;
    if(tID.x % 2 == 0 && tID.y % 2 == 0)
        sampleCoord = GenerateMip(1, gID, sampleCoord); 
    GroupMemoryBarrierWithGroupSync();

    // Fill block for mip base + 2 (2x2)
    if(tID.x % 4 == 0 && tID.y % 4 == 0)
        sampleCoord = GenerateMip(2, gID, sampleCoord); 
    GroupMemoryBarrierWithGroupSync();

    // Fill block for mip base + 3 (1x1)
    if(tID.x % 8 == 0 && tID.y % 8 == 0)
        sampleCoord = GenerateMip(3, gID, sampleCoord); 

}
