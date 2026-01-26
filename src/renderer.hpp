#pragma once
#include "../dw/src/asset/asset.hpp"
#include "../dw/src/render/buffer.hpp"
#include "../dw/src/render/shader.hpp"
#include "../dw/src/render/render.hpp"
#include "../dw/src/render/camera.hpp"
#include "../dw/src/render/timings.hpp"
#include "scene.hpp"

struct DirectionalLight
{
    v3f mDir = {0,0,0};
    float mIntensity = 1.f;
    v3f mColor = {1,1,1};
};

struct PerFrameUniforms
{
    m4f mWorld = {};
    m4f mView = {};
    m4f mProj = {};

    v4f mCamWorldPos = {0,0,0,1};
    v4f mDirLight1 = {0,0,0,1};          // xyz = Direction, w = Intensity
    v4f mDirLight2 = {1,1,1,0.05f};      // rgb = Color, a = Ambient factor

    v4f mPadding0;
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
    DirectionalLight mDirLight = {};
    float mAmbient = 0.f;
    GpuTimer mGpuTimer = {};

    // Render resources
    Buffer* pVBScreenQuad       = NULL;
    Buffer* pIBScreenQuad       = NULL;
    VertexLayout mVLScreenQuad = {};
    Buffer* pVBSceneGeometry    = NULL;
    Buffer* pIBSceneGeometry    = NULL;
    VertexLayout mVLSceneGeometry = {};
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

    // Draw call buffer pass
    Shader* pCSGenerateDraws = NULL;
    ComputePipeline* pPipeGenerateDraws = NULL;

    // GBuffer draw pass
    RenderTarget* pRTGBufferA = NULL;
    RenderTarget* pRTGBufferB = NULL;
    RenderTarget* pRTGBufferC = NULL;
    RenderTarget* pRTGBufferDepth = NULL;
    Shader* pVSGBufferOpaque = NULL;
    Shader* pPSGBufferOpaque = NULL;
    GraphicsPipeline* pPipeGBufferOpaque = NULL;

    // Lighting pass
    RenderTarget* pRTAccum = NULL;
    Shader* pVSLighting = NULL;
    Shader* pPSLighting = NULL;
    GraphicsPipeline* pPipeLighting = NULL;

    // Final present pass
    RenderTarget* pRTPresent = NULL;
    Shader* pVSTonemapping = NULL;
    Shader* pPSTonemapping = NULL;
    GraphicsPipeline* pPipeTonemapping = NULL;

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
