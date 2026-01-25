#version 460 core
#include "common.glsl"

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

const float kAmbient = 0.05f;
const vec3 dirLight = vec3(1.0f, -0.5f, 0.5f);

PS_IN(0) vec2 inUV;

PS_OUT(0) vec4 outColor;

void main()
{
    vec3 L = -normalize(dirLight);
    vec3 N = texture(sampler2D(gbufferB, samplerPoint), inUV).xyz;
    float NoL = dot(N, L);
    float kDiffuse = max(NoL, 0.0f);

    vec3 diffuseColor = texture(sampler2D(gbufferA, samplerPoint), inUV).rgb;

    vec4 result;
    result.rgb = (kAmbient + kDiffuse) * diffuseColor;
    outColor = vec4(result.rgb, 1.0f);
}

#endif // PIXEL_SHADER
