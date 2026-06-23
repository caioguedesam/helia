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
    vec4 sampleA = texture(sampler2D(gbufferA, samplerLinear), inUV);   // rgb = Diffuse color, a = Metallic
    vec4 sampleB = texture(sampler2D(gbufferB, samplerLinear), inUV);   // rg = Encoded normal, b = Roughness
    float depth = texture(sampler2D(depthBuffer, samplerLinear), inUV).r;

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

    // Directional lighting
    result.rgb += (Fr + Fd) * NoL * intensity * lightColor; 

    outColor = vec4(result.rgb, 1.0f);
}

#endif // PIXEL_SHADER
