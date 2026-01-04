#include "scene.hpp"
#include "../dw/src/core/debug.hpp"
#include "../dw/src/core/memory.hpp"
#include "../dw/src/core/file.hpp"

#define CGLTF_IMPLEMENTATION
#include "third_party/cgltf.h"

void initScene(Scene* pScene, uint64 arenaSize, uint64 stagingSize)
{
    ASSERT(pScene);
    *pScene = {};

    initArena(arenaSize, &pScene->mArena);
    initArena(stagingSize, &pScene->mStagingArena);
}

void destroyScene(Scene* pScene)
{
    ASSERT(pScene);

    destroyArena(&pScene->mArena);
    destroyArena(&pScene->mStagingArena);

    *pScene = {};
}

void* cgltfAlloc(void* user, cgltf_size size)
{
    Scene* pScene = (Scene*)user;
    void* ptr = arenaPushZero(&pScene->mStagingArena, size);
    return ptr;
}

void cgltfFree(void* user, void* ptr)
{
    // Free is not relevant because of arena allocation strategy.
}

void setupSceneModel(Scene* pScene, String modelPath)
{
    // Load model asset to scratch memory
    ARENA_CHECKPOINT_SET(&pScene->mStagingArena, sceneModel);

    uint64 jsonDataSize = 0;
    byte* pJsonData = readFile(&pScene->mStagingArena,
            modelPath, &jsonDataSize);

    cgltf_options options = {};
    options.memory.user_data = (void*)pScene;
    options.memory.alloc_func = cgltfAlloc;
    options.memory.free_func = cgltfFree;
    cgltf_data* pGltfData = NULL;
    cgltf_result result = cgltf_parse(&options, (void*)pJsonData, jsonDataSize, &pGltfData);
    ASSERT(result == cgltf_result_success);

    // TODO(caio):
    // Populate scene data with gltf asset data, creating nodes and meshes

    // Reset scratch memory
    cgltf_free(pGltfData);
    ARENA_CHECKPOINT_RESET(&pScene->mStagingArena, sceneModel);
}
