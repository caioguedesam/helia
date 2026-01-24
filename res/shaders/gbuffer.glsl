#version 460 core
#include "common.glsl"

// ====================================================
#ifdef VERTEX_SHADER
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

    PerDrawData drawData = perDraw[gl_DrawID];
#if DOUBLE_SIDED
    outNodeId = drawData.mNodeIdOpaqueDoubleSided;
#else
    outNodeId = drawData.mNodeIdOpaque;
#endif
    SceneNode node = sceneNodes[outNodeId];
    PerFrameUniforms perFrame = perFrameUniforms[frameId];
    gl_Position = perFrame.mProj * perFrame.mView * node.mTransform * vec4(inPosition, 1.0f);
}
#endif  // VERTEX_SHADER

// ====================================================
#ifdef PIXEL_SHADER
PS_IN(0) vec2 inUV;
PS_IN_NOINTERP(1) uint inNodeId;

PS_OUT(0) vec4 outColor;

void main()
{
    SceneNode node = sceneNodes[inNodeId];
    SceneMaterial material = sceneMaterials[node.mMaterialId];
    vec4 baseColor = texture(
            sampler2D(materialMaps[material.mBaseColorTexId], samplerLinear),
            inUV);
    float mask = step(material.mAlphaCutoff, baseColor.a);
    if(mask < 1.0)
    {
        discard;
    }

    outColor = vec4(baseColor.rgb, 1.0);
}
#endif  // PIXEL_SHADER
