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
    float3 normal : TEXCOORD2;
    float3 worldPos : TEXCOORD3;
    float3 tangent : TEXCOORD4;
    float3 bitangent : TEXCOORD5;
};

struct PSOut
{
    float4 gbufferA : SV_Target0;
    float4 gbufferB : SV_Target1;
};

float3x3 GetTBN(float3 N, float3 p, float2 uv)
{
    // get edge vectors of the pixel triangle
    float3 dp1 = ddx(p);
    float3 dp2 = ddy(p);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);

    // solve the linear system
    float3 dp2perp = cross(dp2, N);
    float3 dp1perp = cross(N, dp1);
    float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    // construct a scale-invariant frame 
    float invMax = rsqrt(max(dot(T,T), dot(B,B)));
    T *= invMax;
    B *= invMax;

    return float3x3(T, B, N);
}

PSIn VSMain(VSIn vsIn,
          BUILTIN_DRAW_ID uint drawId : TEXCOORD15)
{
    PSIn result;

    result.uv = vsIn.uv;

#if DOUBLE_SIDED
    InstanceData instanceData = instancesOpaqueDouble[drawId];
#else
    InstanceData instanceData = instancesOpaque[drawId];
#endif
    result.nodeId = instanceData.mNodeId;

    SceneNode node = sceneNodes[result.nodeId];
    float4 worldPos = mul(node.mTransform, float4(vsIn.pos, 1.0f));
    result.pos = mul(perFrame.mViewProj, worldPos);

    // TODO(caio): Readd inverse here but precalculate instead of computing on shader.
    // Currently without doing transpose inverse this breaks for non-uniform scaling.
    //float3x3 mN = transpose(inverse(as3x3(node.mTransform)));
    float3x3 mN = As3x3(node.mTransform);
    result.normal = normalize(mul(mN, vsIn.normal));
    result.worldPos = worldPos.xyz;

#if VERTEX_TANGENTS
    result.tangent = normalize(mul(mN, vsIn.tangent.xyz));
    result.bitangent = cross(result.normal, result.tangent) * vsIn.tangent.w;
#endif

    return result;
}

PSOut PSMain(PSIn psIn,
            bool isFrontFace : SV_IsFrontFace)
{
    PSOut output;

    SceneNode node = sceneNodes[psIn.nodeId];
    SceneMaterial material = sceneMaterials[node.mMaterialId];

    // Diffuse color
    float2 uv = psIn.uv;
    float4 baseColor = GetSampledTexture(material.mBaseColorTexId).Sample(samplerLinear, uv);
    float mask = step(material.mAlphaCutoff, baseColor.a);
    if(mask < 1.0)
    {
        discard;
    }

    // Normals
    float3 vertexNormal = psIn.normal;
#if VERTEX_TANGENTS
    float3 vertexTangent = psIn.tangent;
#endif
#if DOUBLE_SIDED
    if(!isFrontFace)
    {
        vertexNormal = -vertexNormal;
#if VERTEX_TANGENTS
        vertexTangent = -vertexTangent;
#endif
    }
#endif

#if VERTEX_TANGENTS
    float3x3 TBN = transpose(float3x3(vertexTangent, psIn.bitangent, vertexNormal));
#else
    float3 camPos = perFrame.mCamWorldPos.xyz;
    float3 V = normalize(camPos - psIn.worldPos);
    float3x3 TBN = GetTBN(vertexNormal, -V, uv);
#endif
    float3 normalMap = GetSampledTexture(material.mNormalTexId).Sample(samplerLinear, uv).rgb;
    normalMap = normalize(normalMap * 2.0f - 1.0f);
    float3 normal = normalize(mul(TBN, normalMap));

    // Metallic + Roughness
    float4 metallicRoughness = GetSampledTexture(material.mMetallicRoughnessTexId).Sample(samplerLinear, uv);

    // Writing outputs to GBuffer
    output.gbufferA = float4(baseColor.rgb * material.mBaseColor.rgb,
            metallicRoughness.g * material.mRoughness);

    output.gbufferB = float4(EncodeNormal(normal),
            metallicRoughness.b * material.mMetallic,
            0.0);

    return output;
}
