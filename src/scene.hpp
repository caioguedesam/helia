#pragma once
#include "../dw/src/core/memory.hpp"
#include "../dw/src/core/string.hpp"
#include "../dw/src/math/math.hpp"
#include "../dw/src/render/buffer.hpp"

struct Mesh
{
    uint64 mIndexStart      = 0;
    uint64 mIndexCount      = 0;
    // TODO(caio): Add material data
};

struct SceneNode
{
    uint32 mMesh        = MAX_UINT32;   // Index for this node's mesh in the scene
    m4f mTransform      = identity();   // World-space transform
    // TODO(caio): Add parent/child hierarchy?
};

#define SCENE_MAX_NODES 1024
#define SCENE_MAX_MESHES SCENE_MAX_NODES
struct Scene
{
    Arena mArena = {};
    Arena mTempArena = {};

    Mesh mMeshes[SCENE_MAX_MESHES];
    SceneNode mNodes[SCENE_MAX_NODES];
    uint32 mMeshCount = 0;
    uint32 mNodeCount = 0;

    // All GLTF vertices/indices sit in a single buffer.
    Buffer* pVertexData = NULL;
    Buffer* pIndexData = NULL;
};

void initScene(Scene* pScene, uint64 arenaSize, uint64 stagingSize);
void destroyScene(Scene* pScene);

void setupSceneModel(Scene* pScene, String modelPath);
