#pragma once
#include "../dw/src/asset/asset.hpp"
#include "../dw/src/render/buffer.hpp"
#include "../dw/src/render/shader.hpp"
#include "../dw/src/render/render.hpp"
#include "../dw/src/render/camera.hpp"
#include "../dw/src/render/timings.hpp"
#include "scene.hpp"

struct PerFrameUniforms
{
    m4f mWorld = {};
    m4f mView = {};
    m4f mProj = {};
};

struct SceneRenderer
{
    // References
    App* pApp = NULL;
    Renderer* pRenderer = NULL;
    AssetManager* pAssetManager = NULL;
    Scene* pScene = NULL;

    // Render data
    PerFrameUniforms perFrameUniforms[CONCURRENT_FRAMES];
    Camera mCamera = {};
    GpuTimer mGpuTimer = {};

    // Render resources
    Buffer* pVBSceneGeometry = NULL;
    Buffer* pIBSceneGeometry = NULL;
    Buffer* pSBSceneNodes = NULL;
    Buffer* pUBPerFrame = NULL;
    
    Buffer* pDBDrawCmds = NULL;
    Buffer* pSBDrawCmdCount = NULL;

    // Descriptor sets
    DescriptorSet* pDSPerFrame = NULL;
    DescriptorSet* pDSSceneGeometry = NULL;

    // Draw call buffer pass
    ComputePipeline* pPipeComputeDraws = NULL;

    // GBuffer draw pass
    RenderTarget* pRTSceneGeometryColor = NULL;
    RenderTarget* pRTSceneGeometryDepth = NULL;
    VertexLayout mVLSceneGeometry;
    Shader* pVSSceneGeometry = NULL;
    Shader* pPSSceneGeometry = NULL;
    GraphicsPipeline* pPipeSceneGeometry = NULL;
};

void initSceneRenderer(SceneRenderer* pSceneRenderer,
        App* pApp, Renderer* pRenderer, AssetManager* pAssetManager,
        Scene* pScene);
void destroySceneRenderer(SceneRenderer* pSceneRenderer);

void addSceneRenderTargets(SceneRenderer* pSceneRenderer);
void addSceneShaders(SceneRenderer* pSceneRenderer);
void addSceneDescriptors(SceneRenderer* pSceneRenderer);
void addScenePipelines(SceneRenderer* pSceneRenderer);
void addSceneRenderResources(SceneRenderer* pSceneRenderer);

void removeSceneRenderTargets(SceneRenderer* pSceneRenderer);
void removeSceneShaders(SceneRenderer* pSceneRenderer);
void removeSceneDescriptors(SceneRenderer* pSceneRenderer);
void removeScenePipelines(SceneRenderer* pSceneRenderer);
void removeSceneRenderResources(SceneRenderer* pSceneRenderer);

void updatePerFrameUniforms(SceneRenderer* pSceneRenderer);
void uploadPerFrameUniforms(SceneRenderer* pSceneRenderer);

void renderScene(SceneRenderer* pSceneRenderer, uint32 frame);
