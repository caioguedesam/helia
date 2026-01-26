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

void main()
{
    PerFrameUniforms perFrame = perFrameUniforms[frameId];

    vec3 lightColor = perFrame.mLight2.rgb;
    vec3 ambientLight = perFrame.mLight2.w * lightColor;

    vec3 L = -(perFrame.mLight1.xyz);
    vec3 N = texture(sampler2D(gbufferB, samplerPoint), inUV).xyz;
    float NoL = dot(N, L);
    vec3 diffuseLight = max(NoL, 0.0f) * perFrame.mLight1.w * lightColor;

    vec3 surfaceColor = texture(sampler2D(gbufferA, samplerPoint), inUV).rgb;

    vec4 result;
    result.rgb = (ambientLight + diffuseLight) * surfaceColor;
    outColor = vec4(result.rgb, 1.0f);
}

#endif // PIXEL_SHADER
