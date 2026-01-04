#pragma once
#include "../dw/src/core/memory.hpp"
#include "../dw/src/core/string.hpp"
#include "../dw/src/math/math.hpp"
#include "../dw/src/render/buffer.hpp"

struct Mesh
{
    uint32 mVertexStart     = 0;
    uint32 mIndexStart      = 0;
    uint32 mIndexCount      = 0;
};

struct SceneNode
{
    Mesh* pMesh         = NULL;
    m4f mTransform      = identity();   // World-space transform
    // TODO(caio): Verify if parent/child hierarchy is even needed.
    // TODO(caio): Add material data
};

#define SCENE_MAX_NODES 512
struct Scene
{
    Arena mArena = {};
    Arena mStagingArena = {};

    SceneNode mNodes[SCENE_MAX_NODES];
    uint32 mNodeCount = 0;

    // All GLTF vertices/indices sit in a single buffer.
    Buffer* pVertexData = NULL;
    Buffer* pIndexData = NULL;
};

void initScene(Scene* pScene, uint64 arenaSize, uint64 stagingSize);
void destroyScene(Scene* pScene);

void setupSceneModel(Scene* pScene, String modelPath);
