#version 460 core
#include "common.glsl"

DEFINE_CONSTANT_BLOCK
{
    uint frameId;
};

// ====================================================
#ifdef VERTEX_SHADER
VS_IN(0) vec3 inPosition;
VS_IN(1) vec3 inNormal;
VS_IN(2) vec2 inUV;
VS_IN(3) vec4 inTangent;

VS_OUT_NOINTERP(0) uint outNodeId;
VS_OUT(1) vec2 outUV;

void main()
{
    PerDrawData drawData = perDraw[gl_DrawID];
#if DOUBLE_SIDED
    outNodeId = drawData.mNodeIdOpaqueDoubleSided;
#else
    outNodeId = drawData.mNodeIdOpaque;
#endif
    SceneNode node = sceneNodes[outNodeId];
    PerFrameUniforms perFrame = perFrameUniforms[frameId];
    vec4 worldPos = node.mTransform * vec4(inPosition, 1.0f);
    gl_Position = perFrame.mProj * perFrame.mView * worldPos;

    outUV = inUV;
}
#endif  // VERTEX_SHADER

// ====================================================
#ifdef PIXEL_SHADER
PS_IN_NOINTERP(0) uint inNodeId;
PS_IN(1) vec2 inUV;

void main()
{
    SceneNode node = sceneNodes[inNodeId];
    SceneMaterial material = sceneMaterials[node.mMaterialId];

    // Need to sample alpha texture in depth pre pass for alpha masked objects.
    // There's probably a better way to separate these materials to make this shader
    // more minimal.
    vec4 baseColor = texture(
            sampler2D(materialMaps[material.mBaseColorTexId], samplerLinear),
            inUV);
    float mask = step(material.mAlphaCutoff, baseColor.a);
    if(mask < 1.0)
    {
        discard;
    }
}
#endif  // PIXEL_SHADER
