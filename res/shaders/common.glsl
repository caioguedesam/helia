#include "macros.glsl"

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
    // TODO(caio): Pass precalculated matrices
    // (inverses, tranpose inverse for normals, composites, etc.)

    vec4 mCamWorldPos;
    vec4 mLight1;
    vec4 mLight2;

    vec4 mCameraFrustumPlanes[6];

    STRUCT_PADDING_VEC4(0, 3);
};

struct PerDrawData
{
    uint mNodeIdOpaque;
    uint mNodeIdOpaqueDoubleSided;
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
// 0 -> Per Frame Resources
DEFINE_UNIFORM_BLOCK(0, 0)
{
    PerFrameUniforms perFrameUniforms[2];
};

// 1 -> Scene Resources
DEFINE_STORAGE_BLOCK(1, 0)
{
    IndirectDraw opaqueDrawCmds[];
};

DEFINE_STORAGE_BLOCK(1, 1)
{
    IndirectDraw doubleSidedOpaqueDrawCmds[];
};

DEFINE_STORAGE_BLOCK(1, 2)
{
    uint opaqueDrawCount;
    uint doubleSidedOpaqueDrawCount;
};

DEFINE_STORAGE_BLOCK(1, 3)
{
    PerDrawData perDraw[];
};

DEFINE_STORAGE_BLOCK(1, 4)
{
    SceneNode sceneNodes[];
};

DEFINE_STORAGE_BLOCK(1, 5)
{
    SceneMesh sceneMeshes[];
};

DEFINE_STORAGE_BLOCK(1, 6)
{
    SceneMaterial sceneMaterials[];
};

#define SCENE_MAX_TEXTURES 1024
DEFINE_TEXTURE2D(1, 7) materialMaps[SCENE_MAX_TEXTURES];
DEFINE_SAMPLER(1, 8) samplerLinear;
DEFINE_SAMPLER(1, 9) samplerPoint;

DEFINE_TEXTURE2D(1, 10) gbufferA;
DEFINE_TEXTURE2D(1, 11) gbufferB;
DEFINE_TEXTURE2D(1, 12) lightingAccum;

#define HIZ_MAX 10
DEFINE_TEXTURE2D(1, 13)         depthBuffer;
DEFINE_IMAGE2D(1, 14, r32f)     hiz[HIZ_MAX];
