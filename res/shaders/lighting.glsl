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

void main()
{
    PerFrameUniforms perFrame = perFrameUniforms[frameId];

    vec3 lightColor = perFrame.mLight2.rgb;

    // Ambient light
    vec3 ambientLight = perFrame.mLight2.w * lightColor;

    // Diffuse light
    vec3 L = -(perFrame.mLight1.xyz);
    vec3 N = texture(sampler2D(gbufferB, samplerLinear), inUV).xyz;
    float NoL = dot(N, L);
    float diffuse = max(NoL, 0.0f);
    vec3 diffuseLight = diffuse * perFrame.mLight1.w * lightColor;

    // Specular light
    float depth = texture(sampler2D(depthBuffer, samplerLinear), inUV).r;
    vec3 worldPos = worldPositionFromDepth(depth, inUV, perFrame.mView, perFrame.mProj);
    vec3 camPos = perFrame.mCamWorldPos.xyz;
    vec3 V = normalize(camPos - worldPos);
    vec3 R = reflect(-L, N);
    float specular = pow(max(dot(V, R), 0.0), 32);
    vec3 specularLight = specular * lightColor;

    vec3 surfaceColor = texture(sampler2D(gbufferA, samplerLinear), inUV).rgb;

    vec4 result;
    result.rgb = (ambientLight + diffuseLight + specularLight) * surfaceColor;
    outColor = vec4(result.rgb, 1.0f);
}

#endif // PIXEL_SHADER
