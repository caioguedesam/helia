#include "common.slang"

struct Params
{
    uint pFrame;
};

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

VS_ENTRY
PSIn VSMain(VSIn vsIn, uniform Params params)
{
    PSIn result;

    PerFrame frame = GetBufferConstants(PerFrame, params.pFrame);

    result.pos = mul(frame.mViewProj, float4(vsIn.pos, 1.0f));
    result.color = vsIn.color;

    return result;
}

PS_ENTRY
float4 PSMain(PSIn psIn) : SV_Target
{
    return float4(psIn.color, 0);
}
