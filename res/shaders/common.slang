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

    uint mPadding0;
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

struct PerFrame
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
    uint mHandleShadowMaps[MAX_CASCADES];
    uint mHandleHiZ[HIZ_MAX];

    uint mPadding0;
    uint mPadding1;
    uint mPadding2;
};

struct ShadowConstants
{
    float mDepthBias;
    float mMomentBias;
    float mBleedingReduction;

    uint mPadding0;
};

// TODO(caio): Do I need to align this to 16 byte?
struct InstanceData
{
    uint mNodeId;
};

// TODO(caio): Do I need to align this to 16 byte?
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

#define BINDING(SET, SPACE) [[vk::binding(SPACE, SET)]]

struct BufferAddressTable
{
    uint* addresses[MAX_BUFFERS];
};

// Global resource arrays
BINDING(0, 0) ConstantBuffer<BufferAddressTable>    BufferArray;
BINDING(0, 1) SamplerState                          SamplerArray[];
BINDING(0, 2) Texture2D<float4>                     Texture2DFloat4Array[];
BINDING(0, 3) RWTexture2D<float4>                   RWTexture2DFloat4Array[];
BINDING(0, 4) RWTexture2D<float>                    RWTexture2DFloatArray[];

#define GetTexture2DFloat4(N)           Texture2DFloat4Array[N]
#define GetRWTexture2DFloat4(N)         RWTexture2DFloat4Array[N]
#define GetRWTexture2DFloat(N)          RWTexture2DFloatArray[N]
#define GetSampler(N)                   SamplerArray[N]
#define GetBuffer(TYPE, N)              reinterpret<TYPE*>(BufferArray.addresses[N])
#define GetBufferConstants(TYPE, N)     reinterpret<TYPE*>(BufferArray.addresses[N])[0]

// Reserved resources
#define GetSamplerLinear()          SamplerArray[0]
#define GetSamplerPoint()           SamplerArray[1]

#define VS_ENTRY [shader("vertex")]
#define PS_ENTRY [shader("fragment")]
#define CS_ENTRY [shader("compute")]

// Project shader defines
#ifndef SHADOW_MAP
#define SHADOW_MAP 0
#endif
#ifndef DOUBLE_SIDED
#define DOUBLE_SIDED 0
#endif
