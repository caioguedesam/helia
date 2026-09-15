#include "common.slang"

struct Params
{
    uint pFrame;
    uint pInstancesOpaque;
    uint pInstancesOpaqueDoubleSided;
    uint pNodes;
    uint pMaterials;
};

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

VS_ENTRY
PSIn VSMain(VSIn vsIn,
          uint drawId : SV_DrawIndex,
          uniform Params params)
{
    PSIn result;

    PerFrame frame = GetBufferConstants(PerFrame, params.pFrame);
#if DOUBLE_SIDED
    InstanceData instanceData = GetBuffer(InstanceData, params.pInstancesOpaqueDoubleSided)[drawId];
#else
    InstanceData instanceData = GetBuffer(InstanceData, params.pInstancesOpaque)[drawId];
#endif
    result.nodeId = instanceData.mNodeId;

    SceneNode node = GetBuffer(SceneNode, params.pNodes)[result.nodeId];
    float4 worldPos = mul(node.mTransform, float4(vsIn.pos, 1.0f));
    result.pos = mul(frame.mViewProj, worldPos);

    result.uv = vsIn.uv;

    return result;
}

PS_ENTRY
void PSMain(PSIn psIn, uniform Params params)
{
    SamplerState samplerLinear = GetSamplerLinear();
    SceneNode node = GetBuffer(SceneNode, params.pNodes)[psIn.nodeId];
    SceneMaterial material = GetBuffer(SceneMaterial, params.pMaterials)[node.mMaterialId];

    // Need to sample alpha texture in depth pre pass for alpha masked objects.
    // There's probably a better way to separate these materials to make this shader
    // more minimal.
    Texture2D<float4> baseColorMap = GetTexture2DFloat4(material.mBaseColorTexId);
    float4 baseColor = baseColorMap.Sample(samplerLinear, psIn.uv);
    float mask = step(material.mAlphaCutoff, baseColor.a);
    if(mask < 1.0)
    {
        discard;
    }
}
