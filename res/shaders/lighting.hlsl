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

float3 WorldPositionFromDepth(float z, float2 uv, float4x4 mInvView, float4x4 mInvProj)
{
    // From depth [0, 1] to clip space
    float4 clipPos = float4(uv * 2.0f - 1.0f, z, 1.0f);

    // From clip space to view space (doing perspective divide)
    float4 viewPos = mul(mInvProj, clipPos);
    viewPos /= viewPos.w;

    // From view to world space
    float4 worldPos = mul(mInvView, viewPos);

    return worldPos.xyz;
}

// =================================
// Shadows
uint GetCascadeIndex(float3 worldPos)
{
    float4 viewPos = mul(perFrame.mView, float4(worldPos, 1.0));
    float viewZ = abs(viewPos.z);
    uint cascade = MAX_CASCADES - 1;
    for(uint i = 0; i < MAX_CASCADES; i++)
    {
        if(viewZ < perFrame.mShadowCascadeDistances[i])
        {
            cascade = i;
            break;
        }
    }

    return cascade;
}

float MSMGetShadowVisibility(float4 moments, float fragmentDepth, float depthBias, float momentBias)
{
    // Based on MJP's Moment Shadow Mapping implementation:
    // https://github.com/TheRealMJP/Shadows/blob/master/Shadows/MSM.hlsl

    // Bias input data to avoid artifacts
    float4 b = lerp(moments, float4(0.5f, 0.5f, 0.5f, 0.5f), momentBias);
    float3 z;
    z[0] = fragmentDepth - depthBias;

    // Compute a Cholesky factorization of the Hankel matrix B storing only non-
    // trivial entries or related products
    float L32D22 = (-b[0] * b[1]) + b[2];
    float D22 = (-b[0] * b[0]) + b[1];
    float squaredDepthVariance = (-b[1] * b[1]) + b[3];
    float D33D22 = dot(float2(squaredDepthVariance, -L32D22), float2(D22, L32D22));
    float InvD22 = 1.0f / D22;
    float L32 = L32D22 * InvD22;

    // Obtain a scaled inverse image of bz = (1,z[0],z[0]*z[0])^T
    float3 c = float3(1.0f, z[0], z[0] * z[0]);

    // Forward substitution to solve L*c1=bz
    c[1] -= b.x;
    c[2] -= b.y + L32 * c[1];

    // Scaling to solve D*c2=c1
    c[1] *= InvD22;
    c[2] *= D22 / D33D22;

    // Backward substitution to solve L^T*c3=c2
    c[1] -= L32 * c[2];
    c[0] -= dot(c.yz, b.xy);

    // Solve the quadratic equation c[0]+c[1]*z+c[2]*z^2 to obtain solutions
    // z[1] and z[2]
    float p = c[1] / c[2];
    float q = c[0] / c[2];
    float D = (p * p * 0.25f) - q;
    float r = sqrt(D);
    z[1] =- p * 0.5f - r;
    z[2] =- p * 0.5f + r;

    // Compute the shadow intensity by summing the appropriate weights
    // Taking reverse Z into account as well.
    float4 switchVal = (z[2] >= z[0]) ? float4(z[1], z[0], 1.0f, 1.0f) :
                      ((z[1] >= z[0]) ? float4(z[0], z[1], 0.0f, 1.0f) :
                      float4(0.0f,0.0f,0.0f,0.0f));
    float quotient = (switchVal[0] * z[2] - b[0] * (switchVal[0] + z[2]) + b[1])/((z[2] - switchVal[1]) * (z[0] - z[1]));
    float shadowIntensity = switchVal[2] + switchVal[3] * quotient;
    return 1.0f - saturate(shadowIntensity);
}

float ReduceLightBleeding(float v)
{
    float f = shadowConstants.mBleedingReduction;
    return saturate((v - f) / (1.0 - f));
}

float GetShadowVisibility(float3 worldPos, uint cascade)
{
    // This is multiplied to the final color. 1 is fully lit, 0 is fully shadowed.

    // Getting fragment depth
    float4 lightPos = mul(perFrame.mShadowCascadesViewProj[cascade], float4(worldPos, 1.0));
    lightPos /= lightPos.w;
    float zf = lightPos.z;

    // Sample shadow map
    float2 uv = lightPos.xy * 0.5 + 0.5;
    if(uv.x < 0.0 || uv.x > 1.0
    || uv.y < 0.0 || uv.y > 1.0)
    {
        return 1.0;
    }

    float4 moments = GetSampledTexture(perFrame.mHandleShadowMaps[cascade]).Sample(samplerLinear, uv);

    float result = MSMGetShadowVisibility(moments, zf, shadowConstants.mDepthBias * 0.001f, shadowConstants.mMomentBias * 0.001f);

    result = ReduceLightBleeding(result);

    return result;
}

// =================================
// PBR

// PBR Lighting uses:
// - Cook-Torrance specular microfacet model
// - Lambertian diffuse model
// Reference: https://google.github.io/filament/Filament.md.html

// Specular BRDF Normal distribution function (GGX)
float D_GGX(float NoH, float a)
{
    float a2 = a * a;
    float f = (NoH * a2 - NoH) * NoH + 1.0f;
    return a2 / (PI * f * f);
}

// Specular BRDF Geometric shadowing visibility function (Smith-GGX)
float V_SmithGGXCorrelated(float NoV, float NoL, float a)
{
    float a2 = a * a;
    float GGXL = NoV * sqrt((-NoL * a2 + NoL) * NoL + a2);
    float GGXV = NoL * sqrt((-NoV * a2 + NoV) * NoV + a2);
    return 0.5f / (GGXV + GGXL);
}

// Specular BRDF Fresnel term (Schlick)
float3 F_F0(float3 color, float metallic)
{
    return lerp(float3(0.04, 0.04, 0.04), color, metallic);
}

float3 F_Schlick(float u, float3 f0)
{
    return f0 + (float3(1.0, 1.0, 1.0) - f0) * pow(1.0 - u, 5.0);
}

float3 SpecularBRDF(float NoH, float NoV, float NoL, float LoH, float3 surfaceColor,
        float roughness, float metallic)
{
    float D = D_GGX(NoH, roughness);
    float V = V_SmithGGXCorrelated(NoV, NoL, roughness);
    float3 F = F_Schlick(LoH, F_F0(surfaceColor, metallic));

    return (D * V) * F;
}

// Diffuse BRDF
float Fd_Lambert()
{
    return 1.0f / PI;
}

float3 DiffuseBRDF(float3 surfaceColor, float metallic)
{
    float3 diffuseColor = (1.0f - metallic) * surfaceColor;
    return Fd_Lambert() * diffuseColor;
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
    float2 uv = psIn.uv;
    float4 sampleA = GetSampledTexture(perFrame.mHandleGBufferA).Sample(samplerLinear, uv);   // rgb = Diffuse color, a = Metallic
    float4 sampleB = GetSampledTexture(perFrame.mHandleGBufferB).Sample(samplerLinear, uv);   // rg = Encoded normal, b = Roughness
    float depth = GetSampledTexture(perFrame.mHandleDepthBuffer).Sample(samplerLinear, uv).r;

    // BRDF inputs
    float3 L = normalize(-perFrame.mLight1.xyz);
    float3 N = DecodeNormal(sampleB.xy);

    float3 worldPos = WorldPositionFromDepth(depth, uv, perFrame.mInvView, perFrame.mInvProj);
    float3 camPos = perFrame.mCamWorldPos.xyz;

    float3 V = normalize(camPos - worldPos);
    float3 H = normalize(V + L);

    float NoV = abs(dot(N, V)) + 1e-5;
    float NoL = clamp(dot(N, L), 0.0f, 1.0f);
    float NoH = clamp(dot(N, H), 0.0f, 1.0f);
    float LoH = clamp(dot(L, H), 0.0f, 1.0f);

    float3 surfaceColor = sampleA.rgb;
    float roughness = sampleA.a;
    roughness *= roughness;     // Linear roughness -> Perceptual roughness
    float metallic = sampleB.b;

    // Specular BRDF
    float3 Fr = SpecularBRDF(NoH, NoV, NoL, LoH, surfaceColor, roughness, metallic);

    // Diffuse BRDF
    float3 Fd = DiffuseBRDF(surfaceColor.rgb, metallic);

    float intensity = perFrame.mLight1.w;
    float3 lightColor = perFrame.mLight2.rgb;
    float4 result = float4(0.0f, 0.0f, 0.0f, 1.0f);

    // Ambient lighting
    float ambient = perFrame.mLight2.w;
    result.rgb += ambient * surfaceColor * lightColor;

    // TODO(caio): Shadow cascades have sharp cutoff. Need to implement some sort of blending.
    // This should be more apparent with soft shadows.
    uint shadowCascade = GetCascadeIndex(worldPos);
    float shadowVisibility = GetShadowVisibility(worldPos, shadowCascade);

    // Directional lighting
    result.rgb += (Fr + Fd) * NoL * intensity * lightColor * shadowVisibility; 

    return result;
}
