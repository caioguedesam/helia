#version 460 core
#include "common.glsl"

// ====================================================
#ifdef VERTEX_SHADER
VS_IN(0) vec2 inPosition;
VS_IN(1) vec2 inUV;

VS_OUT(0) vec2 outUV;

void main()
{
    gl_Position = vec4(inPosition, 0.0, 1.0);
    outUV = inUV;
}
#endif // VERTEX_SHADER

// ====================================================
#ifdef PIXEL_SHADER

PS_IN(0) vec2 inUV;

PS_OUT(0) vec4 outColor;

void main()
{
    // TODO(caio): Tone mapping
    vec4 result = texture(
            sampler2D(lightingAccum, samplerPoint), inUV);
    outColor = vec4(result.rgb, 1.0);
}

#endif // PIXEL_SHADER
