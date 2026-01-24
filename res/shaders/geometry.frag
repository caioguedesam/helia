#version 460 core
#include "common.glsl"

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
    outColor = vec4(baseColor.rgb, 1.0);
}
