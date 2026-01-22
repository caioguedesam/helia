#pragma once
#include "../dw/src/core/memory.hpp"
#include "../dw/src/core/string.hpp"
#include "../dw/src/math/math.hpp"

struct Mesh
{
    uint32 mVertexOffset    = 0;
    uint32 mIndexOffset     = 0;
    uint32 mIndexCount      = 0;
    // TODO(caio): Add material data
};

struct SceneNode
{
    m4f mTransform      = identity();   // World-space transform
    uint32 mMeshId      = MAX_UINT32;   // Index for this node's mesh in the scene
    // TODO(caio): Add parent/child hierarchy?

    uint32 mPadding0[3];
};

#define SCENE_MAX_NODES 1024
#define SCENE_MAX_MESHES SCENE_MAX_NODES
#define SCENE_MAX_DRAWS 512
struct Scene
{
    Arena mArena = {};
    Arena mTempArena = {};

    // Scene elements and data
    Mesh mMeshes[SCENE_MAX_MESHES];
    SceneNode mNodes[SCENE_MAX_NODES];
    uint32 mMeshCount = 0;
    uint32 mNodeCount = 0;

    void* pVertexData = NULL;
    void* pIndexData = NULL;
    uint64 vertexCount = 0;
    uint64 indexCount = 0;
};

void initScene(Scene* pScene, uint64 arenaSize, uint64 stagingSize);
void destroyScene(Scene* pScene);

void setupSceneModel(Scene* pScene, String modelPath);
