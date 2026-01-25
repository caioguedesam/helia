#version 460 core
#include "common.glsl"

// ====================================================
#ifdef VERTEX_SHADER
VS_IN(0) vec3 inPosition;
VS_IN(1) vec3 inNormal;
VS_IN(2) vec2 inUV;
VS_IN(3) vec4 inTangent;

VS_OUT_NOINTERP(0) uint outNodeId;
VS_OUT(1) vec2 outUV;
VS_OUT(2) vec3 outNormal;

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

    outNormal = inNormal;
}
#endif  // VERTEX_SHADER

// ====================================================
#ifdef PIXEL_SHADER
PS_IN_NOINTERP(0) uint inNodeId;
PS_IN(1) vec2 inUV;
PS_IN(2) vec3 inNormal;

PS_OUT(0) vec4 outGBufferA;
PS_OUT(1) vec4 outGBufferB;
PS_OUT(2) vec2 outGBufferC;

void main()
{
    SceneNode node = sceneNodes[inNodeId];
    SceneMaterial material = sceneMaterials[node.mMaterialId];

    // Diffuse color
    vec4 baseColor = texture(
            sampler2D(materialMaps[material.mBaseColorTexId], samplerLinear),
            inUV);
    float mask = step(material.mAlphaCutoff, baseColor.a);
    if(mask < 1.0)
    {
        discard;
    }

    outGBufferA = vec4(baseColor.rgb, 1.0);

    // Normals
    vec3 vertexNormal = normalize(inNormal);
    outGBufferB = vec4(vertexNormal, 0.0);

    // Metallic + Roughness
    vec4 metallicRoughness = texture(
            sampler2D(materialMaps[material.mMetallicRoughnessTexId], samplerLinear),
            inUV);
    outGBufferC = vec2(metallicRoughness.gb);
}
#endif  // PIXEL_SHADER
