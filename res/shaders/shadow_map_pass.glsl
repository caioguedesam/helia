#version 460 core
#include "common.glsl"

DEFINE_CONSTANT_BLOCK
{
    uint frameId;
    uint cascade;
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
    InstanceData instanceData = instancesShadow[(cascade * MAX_DRAWS) + gl_DrawID];
    outNodeId = instanceData.mNodeId;

    SceneNode node = sceneNodes[outNodeId];
    vec4 worldPos = node.mTransform * vec4(inPosition, 1.0f);
    gl_Position = perFrame.mShadowCascadesViewProj[cascade] * worldPos;

    outUV = inUV;
}
#endif  // VERTEX_SHADER

// ====================================================
#ifdef PIXEL_SHADER
PS_IN_NOINTERP(0) uint inNodeId;
PS_IN(1) vec2 inUV;

PS_OUT(0) vec4 outShadow;

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

    outShadow = vec4(gl_FragCoord.z, 0, 0, 1);
}
#endif  // PIXEL_SHADER
