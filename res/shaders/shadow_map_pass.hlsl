#include "common.hlsl"

PUSH_CONSTANTS_BEGIN()
uint cascade;
PUSH_CONSTANTS_END()

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

    // TODO(caio): Reenable handling of double-sided objects on shadow pass (check if I need to)
    uint cascade = pushConstants.cascade;
    InstanceData instanceData = instancesShadow[(cascade * MAX_DRAWS) + drawId];
    output.nodeId = instanceData.mNodeId;

    SceneNode node = sceneNodes[output.nodeId];
    float4 worldPos = mul(node.mTransform, float4(vsIn.pos, 1.0f));
    output.pos = mul(perFrame.mShadowCascadesViewProj[cascade], worldPos);

    output.uv = vsIn.uv;

    return output;
}

float4 PSMain(PSIn psIn) : SV_Target
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

    // Storing moments 1 - 4 in shadow map
    float z = psIn.pos.z;
    float z2 = pow(z, 2.0);
    float z3 = pow(z, 3.0);
    float z4 = pow(z, 4.0);
    return float4(z, z2, z3, z4);
}
