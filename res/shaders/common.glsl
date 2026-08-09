#include "macros.glsl"
#include "../../src/shared_defines.hpp"

// Enabling bindless resources
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_shader_image_load_formatted : enable

struct SceneMesh
{
    int mVertexOffset;
    uint mIndexStart;
    uint mIndexCount;
};

struct SceneNode
{
    mat4 mTransform;
    vec4 mMinAABB;
    vec4 mMaxAABB;
    uint mMeshId;
    uint mMaterialId;

    uint mPadding0;
    uint mPadding1;
};

struct SceneMaterial
{
    vec4 mBaseColor;
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
    mat4 mView;
    mat4 mProj;
    mat4 mMainView;
    mat4 mMainProj;

    mat4 mShadowCascadesViewProj[MAX_CASCADES];

    // TODO(caio): Pass precalculated matrices
    // (inverses, tranpose inverse for normals, composites, etc.)

    vec4 mCamWorldPos;
    vec4 mLight1;
    vec4 mLight2;

    vec4 mShadowCascadeDistances;

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

vec2 EncodeNormal(vec3 n)
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

vec3 DecodeNormal(vec2 o)
{
    // [0, 1] to [-1, 1]
    o = o * 2.0f - 1.0f;

    // https://twitter.com/Stubbesaurus/status/937994790553227264
    vec3 n = vec3(o.x, o.y, 1.0 - abs(o.x) - abs(o.y));

    n.xy = n.z >= 0
        ? n.xy
        : (1.0 - abs(n.yx)) * sign(n.xy);

    return normalize(n);
}

// Common resource sets
// 0 -> Persistent Resources

// TODO(caio): Do this for samplers and buffers.
// I think I can only do this with buffers on HLSL and SM6.6
DEFINE_TEXTURE2D(0, 0) sampledTextureResources[MAX_TEXTURES];
DEFINE_IMAGE2D(0, 1) storageTextureResources[MAX_TEXTURES];

DEFINE_STORAGE_BLOCK(0, 2)
{
    SceneNode sceneNodes[];
};

DEFINE_STORAGE_BLOCK(0, 3)
{
    SceneMesh sceneMeshes[];
};

DEFINE_STORAGE_BLOCK(0, 4)
{
    SceneMaterial sceneMaterials[];
};

DEFINE_SAMPLER(0, 5) samplerLinear;
DEFINE_SAMPLER(0, 6) samplerPoint;

#define getSampledTexture(N) sampledTextureResources[N]
#define getStorageTexture(N) storageTextureResources[N]

// 1 -> Per Frame Resources (each of these is double-buffered, one for each concurrent frame)
DEFINE_UNIFORM_BLOCK(1, 0)
{
    PerFrameUniforms perFrame;
};

DEFINE_STORAGE_BLOCK(1, 1)
{
    IndirectDraw drawBuffers[]; // MAX_DRAWS * MAX_DRAW_BUFFERS
};

DEFINE_STORAGE_BLOCK(1, 2)
{
    uint drawCounts[];          // MAX_DRAW_BUFFERS
};

DEFINE_STORAGE_BLOCK(1, 3)
{
    InstanceData instancesOpaque[];
};

DEFINE_STORAGE_BLOCK(1, 4)
{
    InstanceData instancesOpaqueDouble[];
};

DEFINE_STORAGE_BLOCK(1, 5)
{
    InstanceData instancesShadow[];
};

DEFINE_UNIFORM_BLOCK(1, 6)
{
    ShadowConstants shadowConstants;
};
