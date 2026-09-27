#include "common.slang"

struct Params
{
    uint inputTexture;
    // TODO(caio): Can I just use float2 here?
    float halfPixelWidth;
    float halfPixelHeight;
    float blurRadius;
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
    float2 halfTexelSize = float2(params.halfPixelWidth, params.halfPixelHeight) * params.blurRadius;

#if BLUR_DOWNSAMPLE
    float4 sum = 
          input.SampleLevel(samplerLinear, psIn.uv, 0.0) * 4.0
        + input.SampleLevel(samplerLinear, psIn.uv + float2( halfTexelSize.x,   halfTexelSize.y), 0.0)
        + input.SampleLevel(samplerLinear, psIn.uv + float2(-halfTexelSize.x,   halfTexelSize.y), 0.0)
        + input.SampleLevel(samplerLinear, psIn.uv + float2( halfTexelSize.x,  -halfTexelSize.y), 0.0)
        + input.SampleLevel(samplerLinear, psIn.uv + float2(-halfTexelSize.x,  -halfTexelSize.y), 0.0);
    return sum / 8.0;
#else
    float4 sum = 
          input.SampleLevel(samplerLinear, psIn.uv + float2(-halfTexelSize.x * 2.0, 0.0), 0.0)
        + input.SampleLevel(samplerLinear, psIn.uv + float2(-halfTexelSize.x, halfTexelSize.y), 0.0) * 2.0
        + input.SampleLevel(samplerLinear, psIn.uv + float2(0.0, halfTexelSize.y * 2.0), 0.0)
        + input.SampleLevel(samplerLinear, psIn.uv + float2(halfTexelSize.x, halfTexelSize.y), 0.0) * 2.0
        + input.SampleLevel(samplerLinear, psIn.uv + float2(halfTexelSize.x * 2.0, 0.0), 0.0)
        + input.SampleLevel(samplerLinear, psIn.uv + float2(halfTexelSize.x, -halfTexelSize.y), 0.0) * 2.0
        + input.SampleLevel(samplerLinear, psIn.uv + float2(0.0, -halfTexelSize.y * 2.0), 0.0)
        + input.SampleLevel(samplerLinear, psIn.uv + float2(-halfTexelSize.x, -halfTexelSize.y), 0.0) * 2.0;
    return sum / 12.0;
#endif
}
