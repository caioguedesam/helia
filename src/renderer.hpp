#pragma once
#include "../dw/src/core/array.hpp"
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

#define MAX_CASCADES 3
struct ShadowSettings
{
    float kSplitFactor = 0.5f;  // PSSM split weight between log/lin schemes.
};

struct PerFrameUniforms
{
    m4f mView = {};
    m4f mProj = {};
    m4f mMainView = {};
    m4f mMainProj = {};

    m4f mShadowCascadesViewProj[MAX_CASCADES];

    v4f mCamWorldPos = {0,0,0,1};
    v4f mDirLight1 = {0,0,0,1};          // xyz = Direction, w = Intensity
    v4f mDirLight2 = {1,1,1,0.05f};      // rgb = Color, a = Ambient factor

    v4f mPadding0[1];
};

struct PerDrawData
{
    uint32 mNodeIdOpaque = MAX_UINT32;
    uint32 mNodeIdDoubleSided = MAX_UINT32;
};

#define MAX_DEBUG_VERTS 1000000
#define HIZ_MAX 10
#define SHADOW_MAP_SIZE 1024
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
    ShadowSettings mShadowSettings = {};
    float mAmbient = 0.f;
    GpuTimer mGpuTimer = {};
    Array<float> mDebugVerts;

    // Render resources
    Buffer* pVBScreenQuad           = NULL;
    Buffer* pIBScreenQuad           = NULL;
    Buffer* pVBDebug                = NULL;
    VertexLayout mVLScreenQuad      = {};
    VertexLayout mVLDebug           = {};
    Buffer* pVBSceneGeometry        = NULL;
    Buffer* pIBSceneGeometry        = NULL;
    VertexLayout mVLSceneGeometry   = {};
    Buffer* pSBSceneNodes           = NULL;
    Buffer* pSBSceneMeshes          = NULL;
    Buffer* pSBSceneMaterials       = NULL;
    Buffer* pUBPerFrame             = NULL;

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
    Shader* pCSGenerateDrawsShadowMap = NULL;
    ComputePipeline* pPipeGenerateDrawsShadowMap = NULL;

    // Hi-Z mip generation pass
    Shader* pCSHiZDownsample = NULL;
    ComputePipeline* pPipeHiZDownsample = NULL;

    // Shadow map pass
    RenderTarget* pRTShadowMaps[MAX_CASCADES];

    // Depth pre-pass
    RenderTarget* pRTSceneDepth = NULL;
    //RenderTarget* pRTSceneDepth[HIZ_MAX];
    Texture* pDepthHierarchyTextures[HIZ_MAX];
    uint32 mDepthHierarchyCount = 0;
    Shader* pVSDepthPrePass = NULL;
    Shader* pPSDepthPrePass = NULL;
    GraphicsPipeline* pPipeDepthPrePass = NULL;
    Shader* pVSDepthPrePassDoubleSided = NULL;
    Shader* pPSDepthPrePassDoubleSided = NULL;
    GraphicsPipeline* pPipeDepthPrePassDoubleSided = NULL;

    // GBuffer draw pass
    RenderTarget* pRTGBufferA = NULL;
    RenderTarget* pRTGBufferB = NULL;
    Shader* pVSGBuffer = NULL;
    Shader* pPSGBuffer = NULL;
    GraphicsPipeline* pPipeGBuffer = NULL;
    Shader* pVSGBufferDoubleSided = NULL;
    Shader* pPSGBufferDoubleSided = NULL;
    GraphicsPipeline* pPipeGBufferDoubleSided = NULL;

    // Lighting pass
    RenderTarget* pRTAccum = NULL;
    Shader* pVSLighting = NULL;
    Shader* pPSLighting = NULL;
    GraphicsPipeline* pPipeLighting = NULL;

    // Debug pass
    Shader* pVSDebug = NULL;
    Shader* pPSDebug = NULL;
    GraphicsPipeline* pPipeDebug = NULL;
    bool mFreezeMainCam = false;

    // Final present pass
    RenderTarget* pRTPresent = NULL;
    Shader* pVSTonemapping = NULL;
    Shader* pPSTonemapping = NULL;
    GraphicsPipeline* pPipeTonemapping = NULL;
};

void initSceneRenderer(SceneRenderer* pSceneRenderer,
        App* pApp, Renderer* pRenderer, AssetManager* pAssetManager,
        Scene* pScene, 
        String rootPath);
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

void debugAddVertex(SceneRenderer* pSceneRenderer, v3f pos, v3f col);
void debugAddTri(SceneRenderer* pSceneRenderer, v3f p0, v3f p1, v3f p2, v3f col);
void debugAddSphere(SceneRenderer* pSceneRenderer, v3f center, float radius, v3f col, uint32 stacks, uint32 slices);
void debugAddPoint(SceneRenderer* pSceneRenderer, v3f p, v3f col);
void debugAddCylinder(SceneRenderer* pSceneRenderer, v3f start, v3f dir, float radius, v3f color, uint32 divs);
void debugAddCone(SceneRenderer* pSceneRenderer, v3f start, v3f dir, float radius, v3f color, uint32 divs);
void debugAddVector(SceneRenderer* pSceneRenderer, v3f start, v3f dir, v3f color);
void debugAddPlane(SceneRenderer* pSceneRenderer, v3f p0, v3f p1, v3f p2, v3f p3, v3f color1, v3f color2);
void debugAddAABB(SceneRenderer* pSceneRenderer, AABB aabb, m4f xform, v3f color);
void debugAddFrustum(SceneRenderer* pSceneRenderer, m4f view, m4f proj, v3f color, float zOffset = 0.00001f);
void debugGeometry(SceneRenderer* pSceneRenderer);
void freezeMainCamera(SceneRenderer* pSceneRenderer, bool freeze);

void addUIControls(SceneRenderer* pSceneRenderer);

void renderScene(SceneRenderer* pSceneRenderer, uint32 frame);
