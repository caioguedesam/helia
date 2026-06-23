#include "macros.glsl"
#include "../../src/shared_defines.hpp"

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

    STRUCT_PADDING_UINT(0, 2);
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
    
    STRUCT_PADDING_UINT(0, 1);
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

    //vec4 mCameraFrustumPlanes[6];

    //vec4 mShadowFrustumPlanes[MAX_CASCADES * 6];

    STRUCT_PADDING_VEC4(0, 1);  // Alignment 64 bytes (mat4)
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
DEFINE_STORAGE_BLOCK(0, 0)
{
    SceneNode sceneNodes[];
};

DEFINE_STORAGE_BLOCK(0, 1)
{
    SceneMesh sceneMeshes[];
};

DEFINE_STORAGE_BLOCK(0, 2)
{
    SceneMaterial sceneMaterials[];
};

DEFINE_TEXTURE2D(0, 3) materialMaps[SCENE_MAX_TEXTURES];
DEFINE_SAMPLER(0, 4) samplerLinear;
DEFINE_SAMPLER(0, 5) samplerPoint;

DEFINE_TEXTURE2D(0, 6) gbufferA;
DEFINE_TEXTURE2D(0, 7) gbufferB;
DEFINE_TEXTURE2D(0, 8) lightingAccum;

DEFINE_TEXTURE2D(0, 9)         depthBuffer;
DEFINE_IMAGE2D(0, 10, r32f)     hiz[HIZ_MAX];

DEFINE_TEXTURE2D(0, 11) shadowMaps[MAX_CASCADES];

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
