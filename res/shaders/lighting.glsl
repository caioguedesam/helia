#version 460 core
#include "common.glsl"

DEFINE_CONSTANT_BLOCK
{
    uint frameId;
};

// ====================================================
#ifdef VERTEX_SHADER
VS_IN(0) vec2 inPosition;
VS_IN(1) vec2 inUV;

VS_OUT(0) vec2 outUV;

void main()
{
    gl_Position = vec4(inPosition, 0.0f, 1.0f);
    outUV = inUV;
}
#endif // VERTEX_SHADER

// ====================================================
#ifdef PIXEL_SHADER

PS_IN(0) vec2 inUV;

PS_OUT(0) vec4 outColor;

vec3 worldPositionFromDepth(float z, vec2 uv, mat4 mView, mat4 mProj)
{
    // From depth [0, 1] to clip space
    vec4 clipPos = vec4(uv * 2.0f - 1.0f, z, 1.0f);

    // From clip space to view space (doing perspective divide)
    vec4 viewPos = inverse(mProj) * clipPos;
    viewPos /= viewPos.w;

    // From view to world space
    vec4 worldPos = inverse(mView) * viewPos;

    return worldPos.xyz;
}

uint getCascadeIndex(vec3 worldPos)
{
    vec4 viewPos = perFrame.mView * vec4(worldPos, 1.0);
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

float msmGetShadowVisibility(vec4 moments, float fragmentDepth, float depthBias, float momentBias)
{
    // Based on MJP's Moment Shadow Mapping implementation:
    // https://github.com/TheRealMJP/Shadows/blob/master/Shadows/MSM.hlsl

    // Bias input data to avoid artifacts
    vec4 b = mix(moments, vec4(0.5f, 0.5f, 0.5f, 0.5f), momentBias);
    vec3 z;
    z[0] = fragmentDepth - depthBias;

    // Compute a Cholesky factorization of the Hankel matrix B storing only non-
    // trivial entries or related products
    float L32D22 = (-b[0] * b[1]) + b[2];
    float D22 = (-b[0] * b[0]) + b[1];
    float squaredDepthVariance = (-b[1] * b[1]) + b[3];
    float D33D22 = dot(vec2(squaredDepthVariance, -L32D22), vec2(D22, L32D22));
    float InvD22 = 1.0f / D22;
    float L32 = L32D22 * InvD22;

    // Obtain a scaled inverse image of bz = (1,z[0],z[0]*z[0])^T
    vec3 c = vec3(1.0f, z[0], z[0] * z[0]);

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
    vec4 switchVal = (z[2] >= z[0]) ? vec4(z[1], z[0], 1.0f, 1.0f) :
                      ((z[1] >= z[0]) ? vec4(z[0], z[1], 0.0f, 1.0f) :
                      vec4(0.0f,0.0f,0.0f,0.0f));
    float quotient = (switchVal[0] * z[2] - b[0] * (switchVal[0] + z[2]) + b[1])/((z[2] - switchVal[1]) * (z[0] - z[1]));
    float shadowIntensity = switchVal[2] + switchVal[3] * quotient;
    return 1.0f - saturate(shadowIntensity);
}

float reduceLightBleeding(float v)
{
    float f = shadowConstants.mBleedingReduction;
    return saturate((v - f) / (1.0 - f));
}

float getShadowVisibility(vec3 worldPos, uint cascade)
{
    // This is multiplied to the final color. 1 is fully lit, 0 is fully shadowed.

    // Getting fragment depth
    vec4 lightPos = perFrame.mShadowCascadesViewProj[cascade] * vec4(worldPos, 1.0);
    lightPos /= lightPos.w;
    float zf = lightPos.z;

    // Sample shadow map
    vec2 uv = lightPos.xy * 0.5 + 0.5;
    if(uv.x < 0.0 || uv.x > 1.0
    || uv.y < 0.0 || uv.y > 1.0)
    {
        return 1.0;
    }

    vec4 moments = texture(sampler2D(getSampledTexture(perFrame.mHandleShadowMaps[cascade]), samplerLinear), uv);

    float result = msmGetShadowVisibility(moments, zf, shadowConstants.mDepthBias * 0.001f, shadowConstants.mMomentBias * 0.001f);

    result = reduceLightBleeding(result);

    return result;
}

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
vec3 F_F0(vec3 color, float metallic)
{
    return mix(vec3(0.04), color, metallic);
}

vec3 F_Schlick(float u, vec3 f0)
{
    return f0 + (vec3(1.0) - f0) * pow(1.0 - u, 5.0);
}

vec3 specularBRDF(float NoH, float NoV, float NoL, float LoH, vec3 surfaceColor,
        float roughness, float metallic)
{
    float D = D_GGX(NoH, roughness);
    float V = V_SmithGGXCorrelated(NoV, NoL, roughness);
    vec3 F = F_Schlick(LoH, F_F0(surfaceColor, metallic));

    return (D * V) * F;
}

// Diffuse BRDF
float Fd_Lambert()
{
    return 1.0f / PI;
}

vec3 diffuseBRDF(vec3 surfaceColor, float metallic)
{
    vec3 diffuseColor = (1.0f - metallic) * surfaceColor;
    return Fd_Lambert() * diffuseColor;
}

void main()
{
    vec4 sampleA = texture(sampler2D(getSampledTexture(perFrame.mHandleGBufferA), samplerLinear), inUV);   // rgb = Diffuse color, a = Metallic
    vec4 sampleB = texture(sampler2D(getSampledTexture(perFrame.mHandleGBufferB), samplerLinear), inUV);   // rg = Encoded normal, b = Roughness
    float depth = texture(sampler2D(getSampledTexture(perFrame.mHandleDepthBuffer), samplerLinear), inUV).r;

    // BRDF inputs
    vec3 L = normalize(-perFrame.mLight1.xyz);
    vec3 N = DecodeNormal(sampleB.xy);

    vec3 worldPos = worldPositionFromDepth(depth, inUV, perFrame.mView, perFrame.mProj);
    vec3 camPos = perFrame.mCamWorldPos.xyz;

    vec3 V = normalize(camPos - worldPos);
    vec3 H = normalize(V + L);

    float NoV = abs(dot(N, V)) + 1e-5;
    float NoL = clamp(dot(N, L), 0.0f, 1.0f);
    float NoH = clamp(dot(N, H), 0.0f, 1.0f);
    float LoH = clamp(dot(L, H), 0.0f, 1.0f);

    vec3 surfaceColor = sampleA.rgb;
    float roughness = sampleA.a;
    roughness *= roughness;     // Linear roughness -> Perceptual roughness
    float metallic = sampleB.b;

    // Specular BRDF
    vec3 Fr = specularBRDF(NoH, NoV, NoL, LoH, surfaceColor, roughness, metallic);

    // Diffuse BRDF
    vec3 Fd = diffuseBRDF(surfaceColor.rgb, metallic);

    float intensity = perFrame.mLight1.w;
    vec3 lightColor = perFrame.mLight2.rgb;
    vec4 result = vec4(0.f);

    // Ambient lighting
    float ambient = perFrame.mLight2.w;
    result.rgb += ambient * surfaceColor * lightColor;

    // TODO(caio): Shadow cascades have sharp cutoff. Need to implement some sort of blending.
    // This should be more apparent with soft shadows.
    uint shadowCascade = getCascadeIndex(worldPos);
    float shadowVisibility = getShadowVisibility(worldPos, shadowCascade);

    // Directional lighting
    result.rgb += (Fr + Fd) * NoL * intensity * lightColor * shadowVisibility; 

    outColor = vec4(result.rgb, 1.0f);
}

#endif // PIXEL_SHADER
