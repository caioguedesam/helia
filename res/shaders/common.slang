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
    float4x4 mInvView;
    float4x4 mCullView;

    // Jittered matrices
    float4x4 mProj;
    float4x4 mInvProj;
    float4x4 mViewProj;
    float4x4 mInvViewProj;
    float4x4 mCullProj;

    // Unjittered matrices
    float4x4 mProjUnjitter;
    float4x4 mInvProjUnjitter;
    float4x4 mViewProjUnjitter;
    float4x4 mInvViewProjUnjitter;
    float4x4 mPrevViewProjUnjitter;

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
    uint mMode;
    float mDepthBias;
    float mMomentBias;
    float mBleedingReduction;
    float mCascadeSplitFactor;
    float mFilterRadius;

    uint mPadding0;
    uint mPadding1;
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

float3 WorldPositionFromDepth(float z, float2 uv, float4x4 mInvViewProj)
{
    // From depth [0, 1] to clip space
    float4 clipPos = float4(uv * 2.0f - 1.0f, z, 1.0f);
    // Clip to world space
    float4 worldPos = mul(mInvViewProj, clipPos);
    // Perspective divide
    return worldPos.xyz / worldPos.w;
}

float4 SampleTextureCatmullRom5(Texture2D<float4> tex, SamplerState bilinearSampler, float2 uv, float2 texSize)
{
    float2 texel = uv * texSize;
    float2 idx = floor(texel - 0.5) + 0.5;
    float2 f = texel - idx;

    // Catmull-Rom weight functions
    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);

    float2 w12 = w1 + w2;

    float2 offset0 = idx - 1.0;
    float2 offset3 = idx + 2.0;
    float2 offset12 = idx + w2 / w12;

    float2 invTexSize = 1.0 / texSize;

    float4 sampleCenter = tex.SampleLevel(bilinearSampler, float2(offset12.x, offset12.y) * invTexSize, 0.0);
    float4 sampleLeft   = tex.SampleLevel(bilinearSampler, float2(offset0.x,  offset12.y) * invTexSize, 0.0);
    float4 sampleRight  = tex.SampleLevel(bilinearSampler, float2(offset3.x,  offset12.y) * invTexSize, 0.0);
    float4 sampleTop    = tex.SampleLevel(bilinearSampler, float2(offset12.x, offset0.y)  * invTexSize, 0.0);
    float4 sampleBottom = tex.SampleLevel(bilinearSampler, float2(offset12.x, offset3.y)  * invTexSize, 0.0);
    
    float4 color = sampleCenter * (w12.x * w12.y) +
                   sampleLeft   * (w0.x  * w12.y) +
                   sampleRight  * (w3.x  * w12.y) +
                   sampleTop    * (w12.x * w0.y)  +
                   sampleBottom * (w12.x * w3.y);

    return color;
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
#define SAMPLER_BILINEAR_WRAP       SamplerArray[0]
#define SAMPLER_TRILINEAR_WRAP      SamplerArray[1]
#define SAMPLER_POINT_WRAP          SamplerArray[2]
#define SAMPLER_BILINEAR_CLAMP      SamplerArray[3]
#define SAMPLER_TRILINEAR_CLAMP     SamplerArray[4]
#define SAMPLER_POINT_CLAMP         SamplerArray[5]

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
#ifndef BLUR_DOWNSAMPLE
#define BLUR_DOWNSAMPLE 0
#endif
#ifndef BLUR_UPSAMPLE
#define BLUR_UPSAMPLE 0
#endif
