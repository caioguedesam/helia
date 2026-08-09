#include "common.hlsl"

struct VSIn
{
    float3 pos : POSITION;
    float3 color : TEXCOORD0;
};

struct PSIn
{
    float4 pos : SV_Position;
    float3 color : COLOR0;
};

PSIn VSMain(VSIn vsIn)
{
    PSIn result;

    result.pos = mul(perFrame.mViewProj, float4(vsIn.pos, 1.0f));
    result.color = vsIn.color;

    return result;
}

float4 PSMain(PSIn psIn) : SV_Target
{
    return float4(psIn.color, 0);
}
