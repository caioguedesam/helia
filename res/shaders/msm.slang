#include "common.slang"

struct Params
{
    uint shadowMap;
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
    Texture2D<float4> shadowMap = GetTexture2DFloat4(params.shadowMap);
    SamplerState samplerPoint = SAMPLER_POINT_CLAMP;
    float z = 1.0 - shadowMap.SampleLevel(samplerPoint, psIn.uv, 0.0).r;  // Converted from reverse Z for consistency

    // Moments 1 to 4
    return float4(z, z * z, z * z * z, z * z * z * z);
}
