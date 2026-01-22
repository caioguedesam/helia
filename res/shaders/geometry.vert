#version 460 core
#include "common.glsl"

VS_IN(0) vec3 inPosition;
VS_IN(1) vec3 inNormal;
VS_IN(2) vec2 inUV;
VS_IN(3) vec4 inTangent;

DEFINE_UNIFORM_BLOCK(0, 0)
{
    PerFrameUniforms perFrameUniforms[2];
};

DEFINE_STORAGE_BLOCK(0, 1)
{
    SceneNode sceneNodes[];
};

DEFINE_CONSTANT_BLOCK
{
    uint frameId;
    uint nodeId;
};

void main()
{
    SceneNode node = sceneNodes[nodeId];
    //gl_Position = perFrame.mProj * perFrame.mView * perFrame.mWorld * vec4(viPosition, 1.0f);
    PerFrameUniforms perFrame = perFrameUniforms[frameId];
    gl_Position = perFrame.mProj * perFrame.mView * node.mTransform * vec4(inPosition, 1.0f);
}
