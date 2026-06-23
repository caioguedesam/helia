#version 460 core
#include "common.glsl"

DEFINE_CONSTANT_BLOCK
{
    uint frameId;
};

// ====================================================
#ifdef VERTEX_SHADER
VS_IN(0) vec3 inPosition;
VS_IN(1) vec3 inColor;

VS_OUT(0) vec3 outColor;

void main()
{
    gl_Position = perFrame.mProj * perFrame.mView * vec4(inPosition, 1.0f);
    outColor = inColor;
}
#endif

// ====================================================
#ifdef PIXEL_SHADER
PS_IN(0) vec3 inColor;

PS_OUT(0) vec4 outColor;

void main()
{
    outColor = vec4(inColor.rgb, 0);
}
#endif
