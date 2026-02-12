#pragma once
#include "../dw/src/core/memory.hpp"
#include "../dw/src/core/string.hpp"
#include "../dw/src/math/math.hpp"
#include "../dw/src/math/volumes.hpp"

struct SceneMesh
{
    int32 mVertexOffset     = 0;
    uint32 mIndexOffset     = 0;
    uint32 mIndexCount      = 0;
};

#define SCENE_MAX_TEXTURES 1024
#define FALLBACK_BASECOLOR_INDEX 0
#define FALLBACK_NORMAL_INDEX 1
#define FALLBACK_MRS_INDEX 2
#define FALLBACK_TEXTURE_COUNT 3
struct SceneMaterial
{
    v4f     mBaseColor = {1,1,1,1};
    float   mMetallic = 1.f;
    float   mRoughness = 1.f;

    uint32  mBaseColorTexture           = FALLBACK_BASECOLOR_INDEX;
    uint32  mNormalTexture              = FALLBACK_NORMAL_INDEX;
    uint32  mMetallicRoughnessTexture   = FALLBACK_MRS_INDEX;
    // TODO(caio): Emissive, occlusion, other stuff

    float   mAlphaCutoff = 0.f;
    uint32  mDoubleSided = 1;

    uint32  mPadding0[1];
};

struct SceneNode
{
    m4f mTransform      = identity();   // World-space transform
    v4f mMinAABB        = {};
    v4f mMaxAABB        = {};
    uint32 mMeshId      = MAX_UINT32;   // Index for this node's mesh in the scene
    uint32 mMaterialId  = MAX_UINT32;

    uint32 mPadding0[2];
};

struct MaterialTextureInfo
{
    String mPath = {};
    uint32 mFormat = 0; 
};

#define SCENE_MAX_NODES 1024
#define SCENE_MAX_MESHES SCENE_MAX_NODES
#define SCENE_MAX_MATERIALS SCENE_MAX_NODES
#define SCENE_MAX_DRAWS 512
struct Scene
{
    Arena mArena = {};
    Arena mTempArena = {};

    // Scene elements and data
    SceneMesh mMeshes[SCENE_MAX_MESHES];
    SceneNode mNodes[SCENE_MAX_NODES];
    SceneMaterial mMaterials[SCENE_MAX_MATERIALS];
    uint32 mMeshCount       = 0;
    uint32 mNodeCount       = 0;
    uint32 mMaterialCount   = 0;

    void* pVertexData = NULL;
    void* pIndexData = NULL;
    uint64 vertexCount = 0;
    uint64 indexCount = 0;

    MaterialTextureInfo mTexInfos[SCENE_MAX_TEXTURES];
    uint32 mTexCount = 0;
};

void initScene(Scene* pScene, uint64 arenaSize, uint64 stagingSize);
void destroyScene(Scene* pScene);

void setupSceneModel(Scene* pScene, String modelPath);
