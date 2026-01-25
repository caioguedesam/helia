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

struct PerDrawData
{
    uint32 mNodeIdOpaque = MAX_UINT32;
    uint32 mNodeIdDoubleSided = MAX_UINT32;
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
    Buffer* pVBSceneGeometry    = NULL;
    Buffer* pIBSceneGeometry    = NULL;
    Buffer* pSBSceneNodes       = NULL;
    Buffer* pSBSceneMeshes      = NULL;
    Buffer* pSBSceneMaterials   = NULL;
    Buffer* pUBPerFrame         = NULL;

    Texture* pTexMaterialMaps[SCENE_MAX_TEXTURES];
    uint32 mMaterialMapCount = 0;
    Sampler* pSamplerLinear = NULL;
    Sampler* pSamplerPoint = NULL;
    
    // Draw call buffers
    // Passes:
    // - Opaque objects
    // - Double-sided opaque objects
    Buffer* pDBDrawCmdsOpaque = NULL;
    Buffer* pDBDrawCmdsOpaqueDoubleSided = NULL;
    Buffer* pDBDrawCmdCount = NULL;
    Buffer* pSBPerDraw = NULL;

    // Descriptor sets
    DescriptorSet* pDSPerFrame = NULL;
    DescriptorSet* pDSGlobal = NULL;
    DescriptorSet* pDSLightingPass = NULL;

    // Draw call buffer pass
    Shader* pCSGenerateDraws = NULL;
    ComputePipeline* pPipeGenerateDraws = NULL;

    // GBuffer draw pass
    RenderTarget* pRTGBufferA = NULL;
    RenderTarget* pRTGBufferB = NULL;
    RenderTarget* pRTGBufferC = NULL;
    RenderTarget* pRTGBufferDepth = NULL;
    VertexLayout mVLGBuffer = {};
    Shader* pVSGBufferOpaque = NULL;
    Shader* pPSGBufferOpaque = NULL;
    GraphicsPipeline* pPipeGBufferOpaque = NULL;

    // Lighting pass
    RenderTarget* pRTAccum = NULL;
    Shader* pVSLighting = NULL;
    Shader* pPSLighting = NULL;
    VertexLayout mVLLighting = {};
    GraphicsPipeline* pPipeLighting = NULL;

    Shader* pVSGBufferOpaqueDoubleSided = NULL;
    Shader* pPSGBufferOpaqueDoubleSided = NULL;
    GraphicsPipeline* pPipeGBufferOpaqueDoubleSided = NULL;
};

void initSceneRenderer(SceneRenderer* pSceneRenderer,
        App* pApp, Renderer* pRenderer, AssetManager* pAssetManager,
        Scene* pScene, 
        String rootPath, String* pTexPaths, uint32 texCount);
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
