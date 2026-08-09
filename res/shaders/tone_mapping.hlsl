#include "common.hlsl"

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

float3 ToneMapping_ACES(float3 x)
{
    // Approximated ACES fit
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float3 ToneMapping_HablePartial(float3 x)
{
    float A = 0.15f;
    float B = 0.50f;
    float C = 0.10f;
    float D = 0.20f;
    float E = 0.02f;
    float F = 0.30f;
    return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}

float3 ToneMapping_Hable(float3 v)
{
    float exposure_bias = 2.0f;
    float3 curr = ToneMapping_HablePartial(v * exposure_bias);

    float3 W = 11.2f;
    float3 white_scale = float3(1.0f, 1.0f, 1.0f) / ToneMapping_HablePartial(W);
    return curr * white_scale;
}

PSIn VSMain(VSIn vsIn)
{
    PSIn output;
    output.pos = float4(vsIn.pos, 0, 1);
    output.uv = vsIn.uv;

    return output;
}

float4 PSMain(PSIn psIn) : SV_Target
{
    float4 result = GetSampledTexture(perFrame.mHandleLightingAccum).Sample(samplerPoint, psIn.uv);

    result.rgb = ToneMapping_ACES(result.rgb);
    //result.rgb = ToneMapping_Hable(result.rgb);
    
    // Gamma correction
    float gammaFactor = 1.0 / 2.2;
    result.rgb = pow(result.rgb, float3(gammaFactor, gammaFactor, gammaFactor));

    return float4(result.rgb, 1.0);
}
