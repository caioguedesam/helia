#include "common.hlsl"

// Normal mapping can opt in to precomputed tangents. Useful for checking if model has proper tangents.
#define VERTEX_TANGENTS 1

struct VSIn
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT;
};

struct PSIn
{
    float4 pos : SV_Position;
    nointerpolation uint nodeId : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

PSIn VSMain(VSIn vsIn,
          BUILTIN_DRAW_ID uint drawId : TEXCOORD15)
{
    PSIn output;

#if DOUBLE_SIDED
    InstanceData instanceData = instancesOpaqueDouble[drawId];
#else
    InstanceData instanceData = instancesOpaque[drawId];
#endif
    output.nodeId = instanceData.mNodeId;

    SceneNode node = sceneNodes[output.nodeId];
    float4 worldPos = mul(node.mTransform, float4(vsIn.pos, 1.0f));
    output.pos = mul(perFrame.mViewProj, worldPos);

    output.uv = vsIn.uv;

    return output;
}

void PSMain(PSIn psIn)
{
    SceneNode node = sceneNodes[psIn.nodeId];
    SceneMaterial material = sceneMaterials[node.mMaterialId];

    // Need to sample alpha texture in depth pre pass for alpha masked objects.
    // There's probably a better way to separate these materials to make this shader
    // more minimal.
    float4 baseColor = GetSampledTexture(material.mBaseColorTexId).Sample(samplerLinear, psIn.uv);
    float mask = step(material.mAlphaCutoff, baseColor.a);
    if(mask < 1.0)
    {
        discard;
    }
}
