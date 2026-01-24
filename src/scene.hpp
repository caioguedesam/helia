#pragma once
#include "../dw/src/core/memory.hpp"
#include "../dw/src/core/string.hpp"
#include "../dw/src/math/math.hpp"

struct SceneMesh
{
    int32 mVertexOffset     = 0;
    uint32 mIndexOffset     = 0;
    uint32 mIndexCount      = 0;
};

struct SceneMaterial
{
    v4f     mBaseColor = {1,1,1,1};
    float   mMetallic = 0.f;
    float   mRoughness = 1.f;

    uint32  mBaseColorTexture           = MAX_UINT16;
    uint32  mNormalTexture              = MAX_UINT16;
    uint32  mMetallicRoughnessTexture   = MAX_UINT16;
    // TODO(caio): Emissive, occlusion, other stuff

    uint32 mPadding0[3];
};

struct SceneNode
{
    m4f mTransform      = identity();   // World-space transform
    uint32 mMeshId      = MAX_UINT32;   // Index for this node's mesh in the scene
    uint32 mMaterialId  = MAX_UINT32;

    uint32 mPadding0[2];
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
};

void initScene(Scene* pScene, uint64 arenaSize, uint64 stagingSize);
void destroyScene(Scene* pScene);

void setupSceneModel(Scene* pScene, String modelPath, String* pTexPaths, uint32* pTexCount);
