#version 460 core
#include "common.glsl"

VS_IN(0) vec3 inPosition;
VS_IN(1) vec3 inNormal;
VS_IN(2) vec2 inUV;
VS_IN(3) vec4 inTangent;

VS_OUT(0) vec2 outUV;
VS_OUT_NOINTERP(1) uint outNodeId;

DEFINE_CONSTANT_BLOCK
{
    uint frameId;
};

void main()
{
    outUV = inUV;

    // TODO(caio): Flip normals based on gl_FrontFace
    PerDrawData drawData = perDraw[gl_DrawID];
    outNodeId = drawData.mNodeIdOpaqueDoubleSided;
    SceneNode node = sceneNodes[outNodeId];
    PerFrameUniforms perFrame = perFrameUniforms[frameId];
    gl_Position = perFrame.mProj * perFrame.mView * node.mTransform * vec4(inPosition, 1.0f);
}
