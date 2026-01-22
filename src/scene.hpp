#pragma once
#include "../dw/src/core/memory.hpp"
#include "../dw/src/core/string.hpp"
#include "../dw/src/math/math.hpp"
#include "../dw/src/render/buffer.hpp"
#include "../dw/src/render/shader.hpp"
#include "../dw/src/render/render.hpp"
#include "../dw/src/asset/asset.hpp"

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

    // Scene render resources
    // All GLTF vertices/indices sit in a single buffer.
    Buffer* pVBScene = NULL;
    Buffer* pIBScene = NULL;

    Buffer* pUBPerFrame = NULL;
    Buffer* pSBNodes = NULL;

    Buffer* pDBDrawCmds = NULL;
    Buffer* pSBDrawCmdCount = NULL;
    Shader* pCSComputeDraws = NULL;
    ComputePipeline* pPipeComputeDraws = NULL;  // TODO: CONTINUE setup draw indirect pass
    // Idea: descriptor set binding design:
    //      - Global set
    //      - Per frame set
    //      - Per draw set (maybe)

    VertexLayout mMeshVertexLayout;
    Shader* pVSGeometry = NULL;
    Shader* pPSGeometry = NULL;
    GraphicsPipeline* pPipeGeometry = NULL;

    DescriptorSet* pDSScene = NULL;
};

void initScene(Scene* pScene, uint64 arenaSize, uint64 stagingSize);
void destroyScene(Scene* pScene);

void setupSceneModel(Scene* pScene, String modelPath);

void addSceneShaders(Scene* pScene, Renderer* pRenderer, AssetManager* pAssetManager);
void addSceneDescriptors(Scene* pScene, Renderer* pRenderer);
void addScenePipelines(Scene* pScene, Renderer* pRenderer);
void addSceneRenderResources(Scene* pScene, Renderer* pRenderer, AssetManager* pAssetManager,
        Buffer* pUBPerFrame);

void removeSceneShaders(Scene* pScene, Renderer* pRenderer);
void removeSceneDescriptors(Scene* pScene, Renderer* pRenderer);
void removeScenePipelines(Scene* pScene, Renderer* pRenderer);
void removeSceneRenderResources(Scene* pScene, Renderer* pRenderer);
