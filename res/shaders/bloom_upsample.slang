#include "common.slang"

struct Params
{
    uint inputTexture;
    float blurRadius;   // in texture coordinates
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
    Texture2D<float4> input = GetTexture2DFloat4(params.inputTexture);
    SamplerState samplerLinear = SAMPLER_BILINEAR_CLAMP;

    float3 result = input.SampleLevel(samplerLinear, psIn.uv, 0.0).rgb * 4.0; // e

    result += (
        input.SampleLevel(samplerLinear, psIn.uv + float2(0.0, -params.blurRadius), 0.0).rgb +
        input.SampleLevel(samplerLinear, psIn.uv + float2(0.0,  params.blurRadius), 0.0).rgb +
        input.SampleLevel(samplerLinear, psIn.uv + float2( params.blurRadius, 0.0), 0.0).rgb +
        input.SampleLevel(samplerLinear, psIn.uv + float2(-params.blurRadius, 0.0), 0.0).rgb
    ) * 2.0;

    result += (
        input.SampleLevel(samplerLinear, psIn.uv + float2(-params.blurRadius, -params.blurRadius), 0.0).rgb +
        input.SampleLevel(samplerLinear, psIn.uv + float2( params.blurRadius, -params.blurRadius), 0.0).rgb +
        input.SampleLevel(samplerLinear, psIn.uv + float2(-params.blurRadius,  params.blurRadius), 0.0).rgb +
        input.SampleLevel(samplerLinear, psIn.uv + float2( params.blurRadius,  params.blurRadius), 0.0).rgb
    );

    result *= 1.0 / 16.0;

    return float4(result.rgb, 1.0);
}
