#version 460 core

layout(location = 0) in vec3 viPosition;
layout(location = 1) in vec2 viTexCoord;

layout(location = 0) out vec2 voTexCoord;

struct PerFrameUniforms
{
    mat4 mWorld;
    mat4 mView;
    mat4 mProj;
};

layout(std140, set = 0, binding = 0) uniform UNIFORM_BLOCK
{
    PerFrameUniforms perFrame;
};

void main()
{
    voTexCoord = viTexCoord;
    gl_Position = perFrame.mProj * perFrame.mView * perFrame.mWorld * vec4(viPosition, 1.0f);
}
