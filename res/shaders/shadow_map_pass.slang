#include "common.slang"

struct Params
{
    uint pFrame;
    uint pInstancesShadow;
    uint pNodes;
    uint pMaterials;
    uint cascade;
};

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

    // TODO(caio): Reenable handling of double-sided objects on shadow pass (check if I need to)
    uint cascade = params.cascade;
    InstanceData instanceData = GetBuffer(InstanceData, params.pInstancesShadow)[(cascade * MAX_DRAWS) + drawId];
    result.nodeId = instanceData.mNodeId;

    SceneNode node = GetBuffer(SceneNode, params.pNodes)[result.nodeId];
    float4 worldPos = mul(node.mTransform, float4(vsIn.pos, 1.0f));
    result.pos = mul(frame.mShadowCascadesViewProj[cascade], worldPos);

    result.uv = vsIn.uv;

    return result;
}

PS_ENTRY
float4 PSMain(PSIn psIn, uniform Params params) : SV_Target
{
    // TODO(caio): Not great that shadow pass
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

    // Storing moments 1 - 4 in shadow map
    float z = psIn.pos.z;
    float z2 = pow(z, 2.0);
    float z3 = pow(z, 3.0);
    float z4 = pow(z, 4.0);
    return float4(z, z2, z3, z4);
}
