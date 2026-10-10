#include "common.slang"

struct Params
{
    uint pFrame;
    uint currBuffer;
    uint prevBuffer;
    uint velocityBuffer;
    uint depthBuffer;

    float texSizeX;
    float texSizeY;
    float texelSizeX;
    float texelSizeY;
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
    PerFrame frame = GetBufferConstants(PerFrame, params.pFrame);
    Texture2D<float4> currTarget    = GetTexture2DFloat4(params.currBuffer);
    Texture2D<float4> prevTarget    = GetTexture2DFloat4(params.prevBuffer);
    Texture2D<float4> depthBuffer   = GetTexture2DFloat4(params.depthBuffer);
    Texture2D<float4> velocityBuffer   = GetTexture2DFloat4(params.velocityBuffer);
    SamplerState samplerPoint = SAMPLER_POINT_CLAMP;
    SamplerState samplerLinear = SAMPLER_BILINEAR_CLAMP;

    float2 uv = psIn.uv;
    float3 currColor = currTarget.Sample(samplerLinear, uv).rgb;
    float2 velocity = velocityBuffer.Sample(samplerPoint, uv).rg;
    float2 prevUV = uv - velocity;
    float3 prevColor = SampleTextureCatmullRom5(prevTarget, samplerLinear, prevUV, float2(params.texSizeX, params.texSizeY)).rgb;
    // TODO(caio): Leaving this commented to deal with NaN/Inf problems in lighting pass later
    //-if(any(isnan(prevColor)) || any(isinf(prevColor)))
    //-    prevColor = currColor;

    // Color clamping
    float3 minColor = 9999.0; float3 maxColor = -9999.0;
    for(int x = -1; x <= 1; x++)
    {
        for(int y = -1; y <= 1; y++)
        {
            float3 color = currTarget.Sample(samplerPoint, uv + float2(x * params.texelSizeX, y * params.texelSizeY)).rgb;
            minColor = min(minColor, color);
            maxColor = max(maxColor, color);
        }
    }

    prevColor = clamp(prevColor, minColor, maxColor);

    float3 result = lerp(prevColor, currColor, 0.1);

    return float4(result, 1.0);
}
