#include "common.slang"

struct Params
{
    uint inputTexture;
    // TODO(caio): Can I just use float2 here?
    float texelWidth;
    float texelHeight;
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

    float3 result = input.SampleLevel(samplerLinear, psIn.uv, 0.0).rgb * 0.125; // e
    result += (
        input.SampleLevel(samplerLinear, psIn.uv + float2(-2.0 * params.texelWidth, 2.0 *  params.texelHeight), 0.0).rgb + // a
        input.SampleLevel(samplerLinear, psIn.uv + float2( 2.0 * params.texelWidth, 2.0 *  params.texelHeight), 0.0).rgb + // c
        input.SampleLevel(samplerLinear, psIn.uv + float2(-2.0 * params.texelWidth, 2.0 * -params.texelHeight), 0.0).rgb + // g
        input.SampleLevel(samplerLinear, psIn.uv + float2( 2.0 * params.texelWidth, 2.0 * -params.texelHeight), 0.0).rgb   // i
    ) * 0.03125;

    result += (
        input.SampleLevel(samplerLinear, psIn.uv + float2(0.0,  2.0 * params.texelHeight), 0.0).rgb + // b
        input.SampleLevel(samplerLinear, psIn.uv + float2(-2.0 * params.texelWidth,  0.0), 0.0).rgb +  // d
        input.SampleLevel(samplerLinear, psIn.uv + float2( 2.0 * params.texelWidth,  0.0), 0.0).rgb +  // f
        input.SampleLevel(samplerLinear, psIn.uv + float2(0.0, -2.0 * params.texelHeight), 0.0).rgb   // b
    ) * 0.0625;

    result += (
        input.SampleLevel(samplerLinear, psIn.uv + float2(-params.texelWidth,  params.texelHeight), 0.0).rgb + // j
        input.SampleLevel(samplerLinear, psIn.uv + float2( params.texelWidth,  params.texelHeight), 0.0).rgb + // k
        input.SampleLevel(samplerLinear, psIn.uv + float2(-params.texelWidth, -params.texelHeight), 0.0).rgb + // l
        input.SampleLevel(samplerLinear, psIn.uv + float2( params.texelWidth, -params.texelHeight), 0.0).rgb   // m
    ) * 0.125;

    return float4(result.rgb, 1.0);
}
