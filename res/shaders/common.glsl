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
    
    STRUCT_PADDING_UINT(0, 3);
};

struct PerFrameUniforms
{
    mat4 mWorld;
    mat4 mView;
    mat4 mProj;
};

struct PerDrawData
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

// Common resource sets
// 0 -> Per Frame Resources
DEFINE_UNIFORM_BLOCK(0, 0)
{
    PerFrameUniforms perFrameUniforms[2];
};

// 1 -> Scene Resources
DEFINE_STORAGE_BLOCK(1, 0)
{
    IndirectDraw drawCmds[];
};

DEFINE_STORAGE_BLOCK(1, 1)
{
    uint drawCmdCount;
};

DEFINE_STORAGE_BLOCK(1, 2)
{
    PerDrawData perDraw[];
};

DEFINE_STORAGE_BLOCK(1, 3)
{
    SceneNode sceneNodes[];
};

DEFINE_STORAGE_BLOCK(1, 4)
{
    SceneMesh sceneMeshes[];
};

DEFINE_STORAGE_BLOCK(1, 5)
{
    SceneMaterial sceneMaterials[];
};

#define SCENE_MAX_TEXTURES 1024
DEFINE_TEXTURE2D(1, 6) materialMaps[SCENE_MAX_TEXTURES];
DEFINE_SAMPLER(1, 7) samplerLinear;
