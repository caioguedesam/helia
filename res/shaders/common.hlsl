#define PI 3.1415926535
#define EPSILON 0.000001
#define dot_c(A, B) max(dot((A), (B)), 0.0)
#define FLT_MAX 3.402823466e+38F

#include "../../src/shared_defines.hpp"

struct SceneMesh
{
    int mVertexOffset;
    uint mIndexStart;
    uint mIndexCount;
};

struct SceneNode
{
    float4x4 mTransform;
    float4 mMinAABB;
    float4 mMaxAABB;
    uint mMeshId;
    uint mMaterialId;

    uint mPadding0;
    uint mPadding1;
};

struct SceneMaterial
{
    float4 mBaseColor;
    float mMetallic;
    float mRoughness;

    uint mBaseColorTexId;
    uint mNormalTexId;
    uint mMetallicRoughnessTexId;

    float mAlphaCutoff;
    uint mDoubleSided;
    
    uint mPadding0;
};

struct PerFrameUniforms
{
    float4x4 mView;
    float4x4 mProj;
    float4x4 mMainView;
    float4x4 mMainProj;
    float4x4 mViewProj;
    float4x4 mInvView;
    float4x4 mInvProj;

    float4x4 mShadowCascadesViewProj[MAX_CASCADES];

    // TODO(caio): Pass precalculated matrices
    // (inverses, tranpose inverse for normals, composites, etc.)

    float4 mCamWorldPos;
    float4 mLight1;
    float4 mLight2;

    float4 mShadowCascadeDistances;

    // Target handles
    uint mHandleGBufferA;
    uint mHandleGBufferB;
    uint mHandleDepthBuffer;
    uint mHandleLightingAccum;
    uint mHandleShadowMaps[MAX_CASCADES];
    uint mHandleHiZ[HIZ_MAX];
};

struct ShadowConstants
{
    float mDepthBias;
    float mMomentBias;
    float mBleedingReduction;
};

struct InstanceData
{
    uint mNodeId;
};

struct IndirectDraw
{
    uint  mIndexCount;
    uint  mInstanceCount;
    uint  mFirstIndex;
    int   mVertexOffset;
    uint  mFirstInstance;
};

float2 EncodeNormal(float3 n)
{
    // Sphere to octahedron
    n /= (abs(n.x) + abs(n.y) + abs(n.z));

    // Octahedron to quad (reflect negative z)
    n.xy = n.z >= 0.0
        ? n.xy
        : (1.0 - abs(n.yx)) * sign(n.xy);

    // [-1, 1] to [0, 1]
    return n.xy * 0.5 + 0.5;
}

float3 DecodeNormal(float2 o)
{
    // [0, 1] to [-1, 1]
    o = o * 2.0f - 1.0f;

    // https://twitter.com/Stubbesaurus/status/937994790553227264
    float3 n = float3(o.x, o.y, 1.0 - abs(o.x) - abs(o.y));

    n.xy = n.z >= 0
        ? n.xy
        : (1.0 - abs(n.yx)) * sign(n.xy);

    return normalize(n);
}

float3x3 As3x3(float4x4 m)
{
    return float3x3(
        m._11, m._12, m._13,
        m._21, m._22, m._23,
        m._31, m._32, m._33
    );
}

#define BINDING(SET,SPACE) [[vk::binding(SET, SPACE)]]

// Common resource sets
// 0 -> Persistent resources
//- Texture2D sampledTextures[MAX_TEXTURES] : register(t0, space0);
//- // TODO_CONTINUE: HLSL doens't allow untyped RWTexture2D arrays. I'll need a separate ResouceManager for each
//- // RWTexture2D resource type (currently R32F for hiz, will need float4 for filtered moment maps)
//- RWTexture2D<float> storageTextures_R32F[MAX_TEXTURES] : register(u1, space0);
//- StructuredBuffer<SceneNode> sceneNodes : register(t2, space0);
//- StructuredBuffer<SceneMesh> sceneMeshes : register(t3, space0);
//- StructuredBuffer<SceneMaterial> sceneMaterials : register(t4, space0);
//- SamplerState samplerLinear : register(s5, space0);
//- SamplerState samplerPoint : register(s6, space0);
BINDING(0, 0) Texture2D sampledTextures[MAX_TEXTURES];
// TODO_CONTINUE: HLSL doens't allow untyped RWTexture2D arrays. I'll need a separate ResouceManager for each
// RWTexture2D resource type (currently R32F for hiz, will need float4 for filtered moment maps)
BINDING(1, 0) RWTexture2D<float> storageTextures_R32F[MAX_TEXTURES];
BINDING(2, 0) StructuredBuffer<SceneNode> sceneNodes;
BINDING(3, 0) StructuredBuffer<SceneMesh> sceneMeshes;
BINDING(4, 0) StructuredBuffer<SceneMaterial> sceneMaterials;
BINDING(5, 0) SamplerState samplerLinear;
BINDING(6, 0) SamplerState samplerPoint;

#define GetSampledTexture(N) sampledTextures[N]
#define GetStorageTextureR32F(N) storageTextures_R32F[N]

// 1 -> Per frame resources
//- ConstantBuffer<PerFrameUniforms> perFrame : register(b0, space1);
//- RWStructuredBuffer<IndirectDraw> drawBuffers : register(u1, space0);
//- RWStructuredBuffer<uint> drawCounts : register(u2, space0);
//- RWStructuredBuffer<InstanceData> instancesOpaque : register(u3, space0);
//- RWStructuredBuffer<InstanceData> instancesOpaqueDouble : register(u4, space0);
//- RWStructuredBuffer<InstanceData> instancesShadow : register(u5, space0);
//- ConstantBuffer<ShadowConstants> shadowConstants : register(b6, space1);
BINDING(0, 1) ConstantBuffer<PerFrameUniforms> perFrame : register(b0, space1);
BINDING(1, 1) RWStructuredBuffer<IndirectDraw> drawBuffers : register(u1, space0);
BINDING(2, 1) RWStructuredBuffer<uint> drawCounts : register(u2, space0);
BINDING(3, 1) RWStructuredBuffer<InstanceData> instancesOpaque : register(u3, space0);
BINDING(4, 1) RWStructuredBuffer<InstanceData> instancesOpaqueDouble : register(u4, space0);
BINDING(5, 1) RWStructuredBuffer<InstanceData> instancesShadow : register(u5, space0);
BINDING(6, 1) ConstantBuffer<ShadowConstants> shadowConstants : register(b6, space1);

// Push constant macro helper
#define PUSH_CONSTANTS_BEGIN() struct PushConstants {
#define PUSH_CONSTANTS_END() }; \
    [[vk::push_constant]] ConstantBuffer<PushConstants> pushConstants;

#define BUILTIN_DRAW_ID [[vk::builtin("DrawIndex")]]

