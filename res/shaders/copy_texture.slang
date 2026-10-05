#include "common.slang"

struct Params
{
    uint src;
    float srcLevel;
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

VS_ENTRY
PSIn VSMain(VSIn vsIn)
{
    PSIn output;
    output.pos = float4(vsIn.pos, 0, 1);
    output.uv = vsIn.uv;

    return output;
}

PS_ENTRY
float4 PSMain(PSIn psIn, uniform Params params) : SV_Target
{
    Texture2D<float4> input = GetTexture2DFloat4(params.src);
    SamplerState samplerNearest = SAMPLER_POINT_CLAMP;
    float4 result = input.SampleLevel(samplerNearest, psIn.uv, params.srcLevel);
    return result;
}
