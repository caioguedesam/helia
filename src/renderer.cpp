#include "renderer.hpp"
#include "../dw/src/core/profile.hpp"
#include "../dw/src/asset/asset.hpp"
#include "../dw/src/render/render.hpp"
#include "../dw/src/render/ui.hpp"
#include "../dw/src/render/texture.hpp"
#include "../dw/src/core/base.hpp"
#include "dw/src/core/app.hpp"
#include "dw/src/math/math.hpp"
#include "dw/src/math/volumes.hpp"
#include "dw/src/render/buffer.hpp"
#include "dw/src/render/camera.hpp"
#include "dw/src/render/resource_manager.hpp"
#include "dw/src/render/shader.hpp"
#include "dw/src/render/timings.hpp"
#include "src/draw_buffers.hpp"

void getCascadeDistances(SceneRenderer* pSceneRenderer, Camera* pCam, float* pDistances)
{
    // https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-10-parallel-split-shadow-maps-programmable-gpus

    float zNear = pCam->mDesc.mNear;
    float zFar = pCam->mDesc.mFar;

    float lambda = pSceneRenderer->mShadowSettings.kSplitFactor;

    // Distances are stored as far plane for respective cascade.
    // Near for cascade n is far for cascade n-1.
    for(int32 i = 0; i < MAX_CASCADES; i++)
    {
        float cLog = zNear * powf((zFar / zNear), (float)(i + 1) / MAX_CASCADES);
        float cLin = zNear + ((zFar - zNear) * (float)(i + 1) / MAX_CASCADES);
        pDistances[i] = lambda * cLog + (1.f - lambda) * cLin;
    }
}

m4f getCascadeViewProj(SceneRenderer* pSceneRenderer, Camera* pCam, float* pDistances, uint32 cascade)
{
    ASSERT(cascade < MAX_CASCADES);

    // Make cascade sub-frustum from main camera frustum
    Camera cascadeCam;
    CameraDesc desc = pSceneRenderer->mCamera.mDesc;
    if(cascade > 0)
    {
        desc.mNear = pDistances[cascade - 1];
    }
    desc.mFar = pDistances[cascade];
    initCamera(pSceneRenderer->mCamera.mPos, pSceneRenderer->mCamera.mLookAt, desc, &cascadeCam);

    m4f cascadeView = getView(&cascadeCam);
    m4f cascadeProj = getProj(&cascadeCam);

    v3f corners[8];
    frustumCorners(cascadeView, cascadeProj, corners, 0);

    // https://alextardif.com/shadowmapping.html
    // Make bounding sphere to contain all of cascade's sub-frustum
    v3f frustumCenter = {0,0,0};
    for(int32 fc = 0; fc < 8; fc++)
    {
        frustumCenter = frustumCenter + corners[fc];
    }
    frustumCenter = frustumCenter * (1.f/8.f);

    float radius = 0.f;
    for(int32 fc = 0; fc < 8; fc++)
    {
        radius = MAX(radius, magn(corners[fc] - frustumCenter));
    }

    v3f lightDir = pSceneRenderer->mDirLight.mDir;
    // Texel snapping: light view/proj are changed in texel sized increments, to avoid shadow shimmering when moving camera
    {
        // Divide shadow map size by twice the radius (covered space in world units by cascade)
        float texelsPerUnit = (float)SHADOW_MAP_SIZE / (radius * 2.f);

        // Make a look at matrix with unit size of one texel in the shadow map
        m4f t = scale(texelsPerUnit);
        m4f lookAt = lookAtViewRH({0,0,0}, lightDir, {0,1,0});
        lookAt = matMul(t, lookAt);
        m4f invLookAt = inverse(lookAt);

        // Transform frustum center to the new look at matrix, then floor to move only in unit increments (shadow map texels)
        v4f newCenter = matMul(lookAt, to4f(frustumCenter, 1.f));
        newCenter.x = (float)floorf(newCenter.x);
        newCenter.y = (float)floorf(newCenter.y);

        // Transform back
        newCenter = matMul(invLookAt, newCenter);
        frustumCenter = to3f(newCenter);
    }

    // Make an orthographic frustum that encompasses the entire bounding sphere
    v3f frustumEye = frustumCenter - (lightDir * radius * 2.f);

    m4f frustumView = lookAtViewRH(frustumEye, frustumCenter, {0,1,0});
    m4f frustumProj = orthoRH(-radius, radius, -radius, radius, -radius * 6.f, radius * 6.f);

    return matMul(frustumProj, frustumView);
}

void initSceneRenderer(SceneRenderer* pSceneRenderer,
        App* pApp, Renderer* pRenderer, AssetManager* pAssetManager, UIState* pUI,
        Scene* pScene, 
        String rootPath)
{
    PROFILE_SCOPE;

    ASSERT(pSceneRenderer);
    ASSERT(pApp && pRenderer && pAssetManager && pScene);
    ASSERT(pScene->mTexCount + FALLBACK_TEXTURE_COUNT < SCENE_MAX_TEXTURES);  // Textures + fallbacks can't exceed max
    pSceneRenderer->pApp = pApp;
    pSceneRenderer->pRenderer = pRenderer;
    pSceneRenderer->pAssetManager = pAssetManager;
    pSceneRenderer->pScene = pScene;
    pSceneRenderer->pUI = pUI;

    Arena* pAppArena = &pApp->mAppArena;
    initResourceManager(pRenderer, pAppArena, &pSceneRenderer->mResMan);
    ResourceManager* pResMan = &pSceneRenderer->mResMan;

    // Bindless fallback texture (must be first)
    {
        TextureDesc desc = {};
        desc.mWidth = 1;
        desc.mHeight = 1;
        desc.mDepth = 1;
        desc.mSamples = 1;
        desc.mFormat = FORMAT_R32_SFLOAT;
        desc.mMipCount = 1;
        desc.mType = TEXTURE_TYPE_2D;
        desc.mUsage =
            TEXTURE_USAGE_TRANSFER_SRC |
            TEXTURE_USAGE_TRANSFER_DST |
            TEXTURE_USAGE_SAMPLED      |
            TEXTURE_USAGE_STORAGE;

        initTexture(&pSceneRenderer->mResMan, desc, &pSceneRenderer->pTexSampledStorageFallback);

        CommandBuffer* pCmd = getCmd(pRenderer, true);
        beginCmd(pCmd);
        TextureBarrier barrier = {pSceneRenderer->pTexSampledStorageFallback, IMAGE_LAYOUT_UNDEFINED, IMAGE_LAYOUT_GENERAL };
        cmdTextureBarrier(pCmd, 1, &barrier);

        endCmd(pCmd);
        submitImmediateCmd(pRenderer, pCmd);
    }

    // Load textures from scene model
    {
        PROFILE_SCOPE_NAME("initSceneRenderer::Load Textures");
        pSceneRenderer->mMaterialMapCount = pScene->mTexCount + FALLBACK_TEXTURE_COUNT;
        for(uint32 t = 0; t < pScene->mTexCount; t++)
        {
            PROFILE_SCOPE_NAME("initSceneRenderer::Load Texture");
            Texture* pTex = NULL;
            char buf[256];
            MaterialTextureInfo texInfo = pScene->mTexInfos[t];
            String texPath = strf(buf, "%.*s/%.*s", STRF_ARG(rootPath), STRF_ARG(texInfo.mPath));
            loadTexture(pAssetManager, &pSceneRenderer->mResMan, texPath, (ImageFormat)texInfo.mFormat, false, &pTex);
            pSceneRenderer->pTexMaterialMaps[t + FALLBACK_TEXTURE_COUNT] = pTex;
        }
    }

    // Load fallback textures
    {
        Texture* pTexFallbackBaseColor = NULL;
        loadTexture(pAssetManager, &pSceneRenderer->mResMan, str("../../res/textures/white.png"),
                FORMAT_RGBA8_SRGB, false, &pTexFallbackBaseColor);

        Texture* pTexFallbackNormal = NULL;
        loadTexture(pAssetManager, &pSceneRenderer->mResMan, str("../../res/textures/flat_normal.png"),
                FORMAT_RGBA8_UNORM, false, &pTexFallbackNormal);

        Texture* pTexFallbackMRS = NULL;
        loadTexture(pAssetManager, &pSceneRenderer->mResMan, str("../../res/textures/black.png"),
                FORMAT_RGBA8_UNORM, false, &pTexFallbackMRS);

        pSceneRenderer->pTexMaterialMaps[FALLBACK_BASECOLOR_INDEX] = pTexFallbackBaseColor;
        pSceneRenderer->pTexMaterialMaps[FALLBACK_NORMAL_INDEX] = pTexFallbackNormal;
        pSceneRenderer->pTexMaterialMaps[FALLBACK_MRS_INDEX] = pTexFallbackMRS;
    }

    // Substituting relative material texture indices with actual bindless handles in materials
    for(uint32 i = 0; i < pScene->mMaterialCount; i++)
    {
        SceneMaterial* pMat = &pScene->mMaterials[i];
        pMat->mBaseColorTexture = getHandle(pSceneRenderer->pTexMaterialMaps[pMat->mBaseColorTexture]);
        pMat->mNormalTexture = getHandle(pSceneRenderer->pTexMaterialMaps[pMat->mNormalTexture]);
        pMat->mMetallicRoughnessTexture = getHandle(pSceneRenderer->pTexMaterialMaps[pMat->mMetallicRoughnessTexture]);
    }

    // Geometry vertex layout
    {
        VertexLayoutDesc desc = {};
        desc.mCount = 4;
        desc.mAttribs[0] = ATTRIBUTE_FLOAT3;    // Position
        desc.mAttribs[1] = ATTRIBUTE_FLOAT3;    // Normal
        desc.mAttribs[2] = ATTRIBUTE_FLOAT2;    // UV
        desc.mAttribs[3] = ATTRIBUTE_FLOAT4;    // Tangent
        initVertexLayout(desc, &pSceneRenderer->mVLSceneGeometry);
    }

    // Screen quad vertex layout
    {
        VertexLayoutDesc desc = {};
        desc.mCount = 2;
        desc.mAttribs[0] = ATTRIBUTE_FLOAT2;    // Position
        desc.mAttribs[1] = ATTRIBUTE_FLOAT2;    // UV
        initVertexLayout(desc, &pSceneRenderer->mVLScreenQuad);
    }

    // Debug vertex layout
    {
        VertexLayoutDesc desc = {};
        desc.mCount = 2;
        desc.mAttribs[0] = ATTRIBUTE_FLOAT3;    // Position
        desc.mAttribs[1] = ATTRIBUTE_FLOAT3;    // Color
        initVertexLayout(desc, &pSceneRenderer->mVLDebug);
    }

    // Screen quad vertex/index buffers
    {
        // Screen quad is a triangle in NDC, which is parially rendered to avoid overdraw.
        float vertexData[] =
        {
            -1.f, -1.f, 0.f, 0.f,
            3.f, -1.f, 2.f, 0.f,
            -1.f, 3.f, 0.f, 2.f,
        };

        uint16 indexData[] =
        {
            0, 2, 1,
        };

        BufferDesc vbDesc = {};
        vbDesc.mType = BUFFER_TYPE_VERTEX;
        vbDesc.mSize = ARR_LEN(vertexData) * sizeof(float);
        vbDesc.mCount = ARR_LEN(vertexData) / 4;
        vbDesc.mStride = sizeof(float);     // Should this be * 4?
        initBuffer(pResMan, vbDesc, &pSceneRenderer->pVBScreenQuad, vertexData);

        BufferDesc ibDesc = {};
        ibDesc.mType = BUFFER_TYPE_INDEX;
        ibDesc.mSize = ARR_LEN(indexData) * sizeof(uint16);
        ibDesc.mCount = ARR_LEN(indexData);
        ibDesc.mStride = sizeof(uint16);
        initBuffer(pResMan, ibDesc, &pSceneRenderer->pIBScreenQuad, indexData);
    }

    // Debug vertex buffer
    {
        BufferDesc vbDesc = {};
        vbDesc.mType = BUFFER_TYPE_VERTEX;
        vbDesc.mSize = MAX_DEBUG_VERTS;
        vbDesc.mCount = MAX_DEBUG_VERTS / 6;
        vbDesc.mStride = sizeof(float);     // Should this be * 6?
        for(uint32 i = 0; i < CONCURRENT_FRAMES; i++)
        {
            initBuffer(pResMan, vbDesc, &pSceneRenderer->pVBDebug[i]);
        }
    }

    // Geometry vertex/index buffers
    {
        BufferDesc vbDesc = {};
        vbDesc.mType = BUFFER_TYPE_VERTEX;
        vbDesc.mSize = pScene->vertexCount * sizeof(float) * 12;
        vbDesc.mCount = pScene->vertexCount;
        vbDesc.mStride = sizeof(float);     // Should this be * 12?
        initBuffer(pResMan, vbDesc, &pSceneRenderer->pVBSceneGeometry, pScene->pVertexData);

        BufferDesc ibDesc = {};
        ibDesc.mType = BUFFER_TYPE_INDEX;
        ibDesc.mSize = pScene->indexCount * sizeof(uint16);
        ibDesc.mCount = pScene->indexCount;
        ibDesc.mStride = sizeof(uint16);
        initBuffer(pResMan, ibDesc, &pSceneRenderer->pIBSceneGeometry, pScene->pIndexData);
    }

    // Scene nodes/meshes buffers
    {
        BufferDesc desc = {};
        desc.mType = BUFFER_TYPE_STORAGE;
        desc.mSize = sizeof(SceneNode) * SCENE_MAX_NODES;
        desc.mCount = SCENE_MAX_NODES;
        desc.mStride = sizeof(SceneNode);
        initBuffer(pResMan, desc, &pSceneRenderer->pSBSceneNodes, &pScene->mNodes[0]);

        desc.mType = BUFFER_TYPE_STORAGE;
        desc.mSize = sizeof(SceneMesh) * SCENE_MAX_MESHES;
        desc.mCount = SCENE_MAX_MESHES;
        desc.mStride = sizeof(SceneMesh);
        initBuffer(pResMan, desc, &pSceneRenderer->pSBSceneMeshes, &pScene->mMeshes[0]);

        desc.mType = BUFFER_TYPE_STORAGE;
        desc.mSize = sizeof(SceneMaterial) * SCENE_MAX_MATERIALS;
        desc.mCount = SCENE_MAX_MATERIALS;
        desc.mStride = sizeof(SceneMaterial);
        initBuffer(pResMan, desc, &pSceneRenderer->pSBSceneMaterials, &pScene->mMaterials[0]);
    }

    // Default samplers
    {
        SamplerDesc desc = {};
        desc.mMinFilter = SAMPLER_FILTER_LINEAR;
        desc.mMagFilter = SAMPLER_FILTER_LINEAR;
        desc.mMipFilter = SAMPLER_FILTER_LINEAR;
        initSampler(pResMan, desc, &pSceneRenderer->pSamplerLinear);
        desc.mMinFilter = SAMPLER_FILTER_NEAREST;
        desc.mMagFilter = SAMPLER_FILTER_NEAREST;
        desc.mMipFilter = SAMPLER_FILTER_NEAREST;
        initSampler(pResMan, desc, &pSceneRenderer->pSamplerPoint);
    }

    // GPU draw call buffers
    initDrawBuffers(pResMan, &pSceneRenderer->mDrawBuffers);

    // Per frame data uniform buffer
    for(uint32 i = 0; i < CONCURRENT_FRAMES; i++)
    {
        BufferDesc desc = {};
        desc.mType = BUFFER_TYPE_UNIFORM;
        desc.mSize = sizeof(PerFrame);
        desc.mCount = 1;
        desc.mStride = sizeof(PerFrame);
        initBuffer(pResMan, desc, &pSceneRenderer->pCBPerFrame[i]);
    }

    // Shadow constants buffer
    for(uint32 i = 0; i < CONCURRENT_FRAMES; i++)
    {
        BufferDesc desc = {};
        desc.mType = BUFFER_TYPE_UNIFORM;
        desc.mSize = sizeof(ShadowConstants);
        desc.mCount = 1;
        desc.mStride = sizeof(ShadowConstants);
        initBuffer(pResMan, desc, &pSceneRenderer->pCBShadowConstants[i]);
    }

    // Instance buffers
    for(uint32 i = 0; i < CONCURRENT_FRAMES; i++)
    {
        BufferDesc desc = {};
        desc.mType = BUFFER_TYPE_STORAGE;
        desc.mSize = MAX_DRAWS * sizeof(InstanceData);
        desc.mCount = 1;
        desc.mStride = sizeof(InstanceData);
        initBuffer(pResMan, desc, &pSceneRenderer->pSBInstancesOpaque[i]);
        initBuffer(pResMan, desc, &pSceneRenderer->pSBInstancesOpaqueDouble[i]);
        desc.mSize *= MAX_CASCADES;
        initBuffer(pResMan, desc, &pSceneRenderer->pSBInstancesShadow[i]);
    }

    // App controls
    CameraDesc camDesc = {};
    float fovX = TO_RAD(90.f);
    float aspect = getAspectRatio(pApp);
    camDesc.mFovY = fovHtoV(fovX, aspect);
    camDesc.mAspect = aspect;
    camDesc.mNear = 0.001f;
    camDesc.mFar = 20.f;
    initCamera(
            {0,0,-5}, 
            {0,0,0}, 
            camDesc, 
            &pSceneRenderer->mCamera);

    DirectionalLight light = {};
    light.mDir = {0.f, -1.f, 0.f};
    light.mIntensity = 1.f;
    light.mColor = {1,1,1};
    pSceneRenderer->mDirLight = light;
    pSceneRenderer->mAmbient = 0.05f;

    pSceneRenderer->mDebugVerts = array<float>(pAppArena, MAX_DEBUG_VERTS);

    initGpuTimer(pRenderer, &pSceneRenderer->mGpuTimer);
}

void destroySceneRenderer(SceneRenderer* pSceneRenderer)
{
    destroyGpuTimer(&pSceneRenderer->mGpuTimer);

    Renderer* pRenderer = pSceneRenderer->pRenderer;

    for(uint32 t = 0; t < pSceneRenderer->mMaterialMapCount; t++)
    {
        destroyTexture(&pSceneRenderer->mResMan, &pSceneRenderer->pTexMaterialMaps[t]);
    }
    destroyTexture(&pSceneRenderer->mResMan, &pSceneRenderer->pTexSampledStorageFallback);

    removeSampler(pRenderer, &pSceneRenderer->pSamplerLinear);
    removeSampler(pRenderer, &pSceneRenderer->pSamplerPoint);
    for(uint32 i = 0; i < CONCURRENT_FRAMES; i++)
    {
        removeBuffer(pRenderer, &pSceneRenderer->pCBPerFrame[i]);
        removeBuffer(pRenderer, &pSceneRenderer->pCBShadowConstants[i]);
        removeBuffer(pRenderer, &pSceneRenderer->pSBInstancesOpaque[i]);
        removeBuffer(pRenderer, &pSceneRenderer->pSBInstancesOpaqueDouble[i]);
        removeBuffer(pRenderer, &pSceneRenderer->pSBInstancesShadow[i]);
        removeBuffer(pRenderer, &pSceneRenderer->pVBDebug[i]);
    }
    destroyDrawBuffers(&pSceneRenderer->mResMan, &pSceneRenderer->mDrawBuffers);
    removeBuffer(pRenderer, &pSceneRenderer->pVBScreenQuad);
    removeBuffer(pRenderer, &pSceneRenderer->pIBScreenQuad);
    removeBuffer(pRenderer, &pSceneRenderer->pVBSceneGeometry);
    removeBuffer(pRenderer, &pSceneRenderer->pIBSceneGeometry);
    removeBuffer(pRenderer, &pSceneRenderer->pSBSceneMaterials);
    removeBuffer(pRenderer, &pSceneRenderer->pSBSceneMeshes);
    removeBuffer(pRenderer, &pSceneRenderer->pSBSceneNodes);

    *pSceneRenderer = {};
}

void addSceneRenderTargets(SceneRenderer* pSceneRenderer)
{
    // Accumulation buffer
    {
        RenderTargetDesc desc = {};
        desc.mFormat = FORMAT_B10G11R11_UFLOAT;
        desc.mClear = {{0,0,0,0}};
        desc.mWidth =   pSceneRenderer->pApp->mWindow.mWidth;
        desc.mHeight =  pSceneRenderer->pApp->mWindow.mHeight;
        initRenderTarget(&pSceneRenderer->mResMan, desc, &pSceneRenderer->pRTAccum);
    }

    // Scene depth targets
    {
        RenderTargetDesc desc = {};
        desc.mClear = {{0,0,0,0}};
        uint32 w =   pSceneRenderer->pApp->mWindow.mWidth;
        uint32 h =  pSceneRenderer->pApp->mWindow.mHeight;
        desc.mFormat = FORMAT_D32_SFLOAT;
        desc.mClear.mDepth = 0;

        desc.mWidth = w;
        desc.mHeight = h;
        initDepthTarget(&pSceneRenderer->mResMan, desc, &pSceneRenderer->pRTSceneDepth);
        pSceneRenderer->pDepthHierarchyTextures[0] = pSceneRenderer->pRTSceneDepth->pTexture;
        pSceneRenderer->mDepthHierarchyCount = 1;

        for(uint32 i = 1; i < HIZ_MAX; i++)
        {
            w = MAX(w / 2, 1);
            h = MAX(h / 2, 1);

            TextureDesc hizDesc = {};
            hizDesc.mWidth = w;
            hizDesc.mHeight = h;
            hizDesc.mDepth = 1;
            hizDesc.mSamples = 1;
            hizDesc.mFormat = FORMAT_R32_SFLOAT;
            hizDesc.mMipCount = 1;
            hizDesc.mType = TEXTURE_TYPE_2D;
            hizDesc.mUsage =
                TEXTURE_USAGE_TRANSFER_SRC |
                TEXTURE_USAGE_TRANSFER_DST |
                TEXTURE_USAGE_SAMPLED      |
                TEXTURE_USAGE_STORAGE;

            initTexture(&pSceneRenderer->mResMan, hizDesc, &pSceneRenderer->pDepthHierarchyTextures[i]);

            pSceneRenderer->mDepthHierarchyCount++;

            if(w == 1 || h == 1)
            {
                break;
            }
        }
    }

    // Shadow map cascades
    {
        RenderTargetDesc desc = {};
        uint32 w = SHADOW_MAP_SIZE;
        uint32 h = SHADOW_MAP_SIZE;
        desc.mClear = {{0,0,0,0}, 0};

        desc.mWidth = w;
        desc.mHeight = h;
        for(int32 i = 0; i < MAX_CASCADES; i++)
        {
            desc.mFormat = FORMAT_RGBA32_SFLOAT;
            initRenderTarget(&pSceneRenderer->mResMan, desc, &pSceneRenderer->pRTShadows[i]);
            desc.mFormat = FORMAT_D16_UNORM;
            initDepthTarget(&pSceneRenderer->mResMan, desc, &pSceneRenderer->pRTShadowsDepth[i]);
        }
    }

    // GBuffer pass RT
    {
        RenderTargetDesc desc = {};
        desc.mFormat = FORMAT_RGBA8_SRGB;
        desc.mClear = {{0,0,0,0}};
        desc.mWidth =   pSceneRenderer->pApp->mWindow.mWidth;
        desc.mHeight =  pSceneRenderer->pApp->mWindow.mHeight;
        initRenderTarget(&pSceneRenderer->mResMan, desc, &pSceneRenderer->pRTGBufferA);
        desc.mFormat = FORMAT_A2RGB10_UNORM;
        initRenderTarget(&pSceneRenderer->mResMan, desc, &pSceneRenderer->pRTGBufferB);
    }

    // Final present RT
    Renderer* pRenderer = pSceneRenderer->pRenderer;
    {
        RenderTargetDesc desc = {};
        desc.mFormat = FORMAT_RGBA8_UNORM;
        desc.mClear = {{0,0,0,0}};
        desc.mWidth =   pSceneRenderer->pApp->mWindow.mWidth;
        desc.mHeight =  pSceneRenderer->pApp->mWindow.mHeight;
        initRenderTarget(&pSceneRenderer->mResMan, desc, &pSceneRenderer->pRTPresent);

        CommandBuffer* pCmd = getCmd(pRenderer, true);
        beginCmd(pCmd);

        RenderTargetBarrier barrier = {pSceneRenderer->pRTPresent, IMAGE_LAYOUT_UNDEFINED, IMAGE_LAYOUT_GENERAL};
        cmdRenderTargetBarrier(pCmd, 1, &barrier);

        endCmd(pCmd);
        submitImmediateCmd(pRenderer, pCmd);
    }

    // Transitioning render targets so they can be bound to descriptor sets
    {
        CommandBuffer* pCmd = getCmd(pRenderer, true);
        beginCmd(pCmd);

        RenderTargetBarrier barriers[3];
        barriers[0] = {pSceneRenderer->pRTGBufferA,     IMAGE_LAYOUT_UNDEFINED, IMAGE_LAYOUT_GENERAL };
        barriers[1] = {pSceneRenderer->pRTGBufferB,     IMAGE_LAYOUT_UNDEFINED, IMAGE_LAYOUT_GENERAL };
        barriers[2] = {pSceneRenderer->pRTAccum,        IMAGE_LAYOUT_UNDEFINED, IMAGE_LAYOUT_GENERAL };
        cmdRenderTargetBarrier(pCmd, ARR_LEN(barriers), barriers);

        RenderTargetBarrier smBarriers[MAX_CASCADES * 2];
        for(int i = 0; i < MAX_CASCADES; i++)
        {
            smBarriers[i * 2 + 0] = {pSceneRenderer->pRTShadows[i], IMAGE_LAYOUT_UNDEFINED, IMAGE_LAYOUT_GENERAL};
            smBarriers[i * 2 + 1] = {pSceneRenderer->pRTShadowsDepth[i], IMAGE_LAYOUT_UNDEFINED, IMAGE_LAYOUT_GENERAL};
        }
        cmdRenderTargetBarrier(pCmd, ARR_LEN(smBarriers), smBarriers);

        TextureBarrier hizBarriers[HIZ_MAX];
        for(uint32 i = 0; i < pSceneRenderer->mDepthHierarchyCount; i++)
        {
            hizBarriers[i] = {pSceneRenderer->pDepthHierarchyTextures[i], IMAGE_LAYOUT_UNDEFINED, IMAGE_LAYOUT_GENERAL };
        }
        cmdTextureBarrier(pCmd, pSceneRenderer->mDepthHierarchyCount, hizBarriers);

        endCmd(pCmd);
        submitImmediateCmd(pRenderer, pCmd);
    }
}

void addSceneShaders(SceneRenderer* pSceneRenderer)
{
    String generateDrawsShaderPath = str("generate_draws");
    String hiZDownsampleShaderPath = str("hiz_downsample");
    String depthPrepassShaderPath = str("depth_prepass");
    String shadowPassShaderPath = str("shadow_map_pass");
    String gbufferShaderPath = str("gbuffer");
    String lightingShaderPath = str("lighting");
    String debugShaderPath = str("debug");
    String tonemappingShaderPath = str("tone_mapping");
    String shadowMapDefines[] =
    {
        str("SHADOW_MAP"),
    };
    String doubleSidedDefines[] =
    {
        str("DOUBLE_SIDED"),
    };

    struct ShaderLoadDesc
    {
        String path;
        ShaderType type;
        String* pDefines = NULL;
        uint32 defineCount = 0;
        Shader** ppOut = NULL;
    };

    ShaderLoadDesc shaders[] =
    {
        {depthPrepassShaderPath, SHADER_TYPE_VERT, NULL, 0, &pSceneRenderer->pVSDepthPrePass},
        {depthPrepassShaderPath, SHADER_TYPE_FRAG, NULL, 0, &pSceneRenderer->pPSDepthPrePass},
        {depthPrepassShaderPath, SHADER_TYPE_VERT, doubleSidedDefines, ARR_LEN(doubleSidedDefines), &pSceneRenderer->pVSDepthPrePassDoubleSided},
        {depthPrepassShaderPath, SHADER_TYPE_FRAG, doubleSidedDefines, ARR_LEN(doubleSidedDefines), &pSceneRenderer->pPSDepthPrePassDoubleSided},

        {shadowPassShaderPath, SHADER_TYPE_VERT, NULL, 0, &pSceneRenderer->pVSShadowMapPass},
        {shadowPassShaderPath, SHADER_TYPE_FRAG, NULL, 0, &pSceneRenderer->pPSShadowMapPass},
        {shadowPassShaderPath, SHADER_TYPE_VERT, doubleSidedDefines, ARR_LEN(doubleSidedDefines), &pSceneRenderer->pVSShadowMapPassDoubleSided},
        {shadowPassShaderPath, SHADER_TYPE_FRAG, doubleSidedDefines, ARR_LEN(doubleSidedDefines), &pSceneRenderer->pPSShadowMapPassDoubleSided},

        {gbufferShaderPath, SHADER_TYPE_VERT, NULL, 0, &pSceneRenderer->pVSGBuffer},
        {gbufferShaderPath, SHADER_TYPE_FRAG, NULL, 0, &pSceneRenderer->pPSGBuffer},
        {gbufferShaderPath, SHADER_TYPE_VERT, doubleSidedDefines, ARR_LEN(doubleSidedDefines), &pSceneRenderer->pVSGBufferDoubleSided},
        {gbufferShaderPath, SHADER_TYPE_FRAG, doubleSidedDefines, ARR_LEN(doubleSidedDefines), &pSceneRenderer->pPSGBufferDoubleSided},

        {generateDrawsShaderPath, SHADER_TYPE_COMP, NULL, 0, &pSceneRenderer->pCSGenerateDraws},
        {generateDrawsShaderPath, SHADER_TYPE_COMP, shadowMapDefines, ARR_LEN(shadowMapDefines), &pSceneRenderer->pCSGenerateDrawsShadowMap},
        {hiZDownsampleShaderPath, SHADER_TYPE_COMP, NULL, 0, &pSceneRenderer->pCSHiZDownsample},

        {lightingShaderPath, SHADER_TYPE_VERT, NULL, 0, &pSceneRenderer->pVSLighting},
        {lightingShaderPath, SHADER_TYPE_FRAG, NULL, 0, &pSceneRenderer->pPSLighting},

        {debugShaderPath, SHADER_TYPE_VERT, NULL, 0, &pSceneRenderer->pVSDebug},
        {debugShaderPath, SHADER_TYPE_FRAG, NULL, 0, &pSceneRenderer->pPSDebug},

        {tonemappingShaderPath, SHADER_TYPE_VERT, NULL, 0, &pSceneRenderer->pVSTonemapping},
        {tonemappingShaderPath, SHADER_TYPE_FRAG, NULL, 0, &pSceneRenderer->pPSTonemapping},
    };

    for(uint32 i = 0; i < ARR_LEN(shaders); i++)
    {
        ShaderLoadDesc shader = shaders[i];
        if(!(*shader.ppOut))
        {
            loadShader(pSceneRenderer->pAssetManager, pSceneRenderer->pRenderer, 
                    shader.path, shader.type, shader.pDefines, shader.defineCount, shader.ppOut);
        }
    }
}

void addSceneResources(SceneRenderer* pSceneRenderer)
{
    setResources(&pSceneRenderer->mResMan, &pSceneRenderer->pApp->mAppArena);
}

void addScenePipelines(SceneRenderer* pSceneRenderer)
{
    Renderer* pRenderer = pSceneRenderer->pRenderer;
    // Shadow pass pipeline
    {
        GraphicsPipelineDesc desc = {};
        desc.mRenderTargetCount = 1;
        desc.mRenderTargetFormats[0] = pSceneRenderer->pRTShadows[0]->mDesc.mFormat;
        desc.mDepthTargetFormat = pSceneRenderer->pRTShadowsDepth[0]->mDesc.mFormat;

        desc.mVertexLayout = pSceneRenderer->mVLSceneGeometry;
        desc.pVS = pSceneRenderer->pVSShadowMapPass;
        desc.pFS = pSceneRenderer->pPSShadowMapPass;

        desc.mCullMode = CULL_MODE_BACK;
        desc.mFrontFace = FRONT_FACE_CCW;

        desc.mDepthTest = true;
        desc.mDepthWrite = true;
        desc.mDepthOp = COMPARE_GREATER;

        if(!pSceneRenderer->pPipeShadowMapPass)
        {
            addPipeline(pRenderer, desc, &pSceneRenderer->pPipeShadowMapPass);
        }

        desc.mCullMode = CULL_MODE_NONE;
        desc.pVS = pSceneRenderer->pVSShadowMapPassDoubleSided;
        desc.pFS = pSceneRenderer->pPSShadowMapPassDoubleSided;
        if(!pSceneRenderer->pPipeShadowMapPassDoubleSided)
        {
            addPipeline(pRenderer, desc, &pSceneRenderer->pPipeShadowMapPassDoubleSided);
        }
    }


    // Depth pre-pass pipeline
    {
        GraphicsPipelineDesc desc = {};
        desc.mRenderTargetCount = 0;
        desc.mDepthTargetFormat = pSceneRenderer->pRTSceneDepth->mDesc.mFormat;

        desc.mVertexLayout = pSceneRenderer->mVLSceneGeometry;
        desc.pVS = pSceneRenderer->pVSDepthPrePass;
        desc.pFS = pSceneRenderer->pPSDepthPrePass;

        desc.mCullMode = CULL_MODE_BACK;
        desc.mFrontFace = FRONT_FACE_CCW;

        desc.mDepthTest = true;
        desc.mDepthWrite = true;
        desc.mDepthOp = COMPARE_GREATER;

        if(!pSceneRenderer->pPipeDepthPrePass)
        {
            addPipeline(pRenderer, desc, &pSceneRenderer->pPipeDepthPrePass);
        }

        desc.mCullMode = CULL_MODE_NONE;
        desc.pVS = pSceneRenderer->pVSDepthPrePassDoubleSided;
        desc.pFS = pSceneRenderer->pPSDepthPrePassDoubleSided;
        if(!pSceneRenderer->pPipeDepthPrePassDoubleSided)
        {
            addPipeline(pRenderer, desc, &pSceneRenderer->pPipeDepthPrePassDoubleSided);
        }
    }

    // GBuffer pass pipeline
    {
        GraphicsPipelineDesc desc = {};
        desc.mRenderTargetCount = 2;
        desc.mRenderTargetFormats[0] = pSceneRenderer->pRTGBufferA->mDesc.mFormat;
        desc.mRenderTargetFormats[1] = pSceneRenderer->pRTGBufferB->mDesc.mFormat;
        desc.mDepthTargetFormat = pSceneRenderer->pRTSceneDepth->mDesc.mFormat;

        desc.mVertexLayout = pSceneRenderer->mVLSceneGeometry;
        desc.pVS = pSceneRenderer->pVSGBuffer;
        desc.pFS = pSceneRenderer->pPSGBuffer;

        desc.mCullMode = CULL_MODE_BACK;
        desc.mFrontFace = FRONT_FACE_CCW;

        //desc.mDepthTest = true;
        //desc.mDepthWrite = true;
        //desc.mDepthOp = COMPARE_GREATER;
        desc.mDepthTest = true;
        desc.mDepthWrite = false;
        desc.mDepthOp = COMPARE_EQUAL;

        if(!pSceneRenderer->pPipeGBuffer)
        {
            addPipeline(pRenderer, desc, &pSceneRenderer->pPipeGBuffer);
        }

        desc.mCullMode = CULL_MODE_NONE;
        desc.pVS = pSceneRenderer->pVSGBufferDoubleSided;
        desc.pFS = pSceneRenderer->pPSGBufferDoubleSided;
        if(!pSceneRenderer->pPipeGBufferDoubleSided)
        {
            addPipeline(pRenderer, desc, &pSceneRenderer->pPipeGBufferDoubleSided);
        }
    }

    if(!pSceneRenderer->pPipeGenerateDraws)
    {
        ComputePipelineDesc desc = {};

        desc.pCS = pSceneRenderer->pCSGenerateDraws;

        addPipeline(pRenderer, desc, &pSceneRenderer->pPipeGenerateDraws);

        if(!pSceneRenderer->pPipeGenerateDrawsShadowMap)
        {
            desc.pCS = pSceneRenderer->pCSGenerateDrawsShadowMap;
            addPipeline(pRenderer, desc, &pSceneRenderer->pPipeGenerateDrawsShadowMap);
        }
    }

    if(!pSceneRenderer->pPipeHiZDownsample)
    {
        ComputePipelineDesc desc = {};

        desc.pCS = pSceneRenderer->pCSHiZDownsample;

        addPipeline(pRenderer, desc, &pSceneRenderer->pPipeHiZDownsample);
    }

    if(!pSceneRenderer->pPipeLighting)
    {
        GraphicsPipelineDesc desc = {};

        desc.mRenderTargetCount = 1;
        desc.mRenderTargetFormats[0] = pSceneRenderer->pRTAccum->mDesc.mFormat;

        desc.mVertexLayout = pSceneRenderer->mVLScreenQuad;
        desc.pVS = pSceneRenderer->pVSLighting;
        desc.pFS = pSceneRenderer->pPSLighting;

        desc.mCullMode = CULL_MODE_BACK;
        desc.mFrontFace = FRONT_FACE_CCW;

        desc.mDepthTest = false;
        desc.mDepthWrite = false;

        addPipeline(pRenderer, desc, &pSceneRenderer->pPipeLighting);
    }

    if(!pSceneRenderer->pPipeDebug)
    {
        GraphicsPipelineDesc desc = {};

        desc.mRenderTargetCount = 1;
        desc.mRenderTargetFormats[0] = pSceneRenderer->pRTAccum->mDesc.mFormat;

        desc.mVertexLayout = pSceneRenderer->mVLDebug;
        desc.pVS = pSceneRenderer->pVSDebug;
        desc.pFS = pSceneRenderer->pPSDebug;

        desc.mCullMode = CULL_MODE_NONE;
        desc.mFrontFace = FRONT_FACE_CCW;
        desc.mFillMode = FILL_MODE_LINE;
        desc.mLineWidth = 2.f;

        desc.mDepthTest = false;
        desc.mDepthWrite = false;

        addPipeline(pRenderer, desc, &pSceneRenderer->pPipeDebug);
    }

    if(!pSceneRenderer->pPipeTonemapping)
    {
        GraphicsPipelineDesc desc = {};

        desc.mRenderTargetCount = 1;
        desc.mRenderTargetFormats[0] = pSceneRenderer->pRTPresent->mDesc.mFormat;

        desc.mVertexLayout = pSceneRenderer->mVLScreenQuad;
        desc.pVS = pSceneRenderer->pVSTonemapping;
        desc.pFS = pSceneRenderer->pPSTonemapping;

        desc.mCullMode = CULL_MODE_BACK;
        desc.mFrontFace = FRONT_FACE_CCW;

        desc.mDepthTest = false;
        desc.mDepthWrite = false;

        addPipeline(pRenderer, desc, &pSceneRenderer->pPipeTonemapping);
    }
}

void removeSceneRenderTargets(SceneRenderer* pSceneRenderer)
{
    destroyRenderTarget(&pSceneRenderer->mResMan, &pSceneRenderer->pRTPresent);
    destroyRenderTarget(&pSceneRenderer->mResMan, &pSceneRenderer->pRTAccum);
    destroyRenderTarget(&pSceneRenderer->mResMan, &pSceneRenderer->pRTGBufferA);
    destroyRenderTarget(&pSceneRenderer->mResMan, &pSceneRenderer->pRTGBufferB);
    destroyRenderTarget(&pSceneRenderer->mResMan, &pSceneRenderer->pRTSceneDepth);
    for(int32 i = 0; i < MAX_CASCADES; i++)
    {
        destroyRenderTarget(&pSceneRenderer->mResMan, &pSceneRenderer->pRTShadows[i]);
        destroyRenderTarget(&pSceneRenderer->mResMan, &pSceneRenderer->pRTShadowsDepth[i]);
    }
    for(int32 i = 1; i < pSceneRenderer->mDepthHierarchyCount; i++)
    {
        destroyTexture(&pSceneRenderer->mResMan, &pSceneRenderer->pDepthHierarchyTextures[i]);
    }
}

void removeSceneShaders(SceneRenderer* pSceneRenderer)
{
    Shader** shaders[] =
    {
        &pSceneRenderer->pVSShadowMapPass,
        &pSceneRenderer->pPSShadowMapPass,
        &pSceneRenderer->pVSShadowMapPassDoubleSided,
        &pSceneRenderer->pPSShadowMapPassDoubleSided,
        &pSceneRenderer->pVSDepthPrePass,
        &pSceneRenderer->pPSDepthPrePass,
        &pSceneRenderer->pVSDepthPrePassDoubleSided,
        &pSceneRenderer->pPSDepthPrePassDoubleSided,
        &pSceneRenderer->pVSGBuffer,
        &pSceneRenderer->pPSGBuffer,
        &pSceneRenderer->pVSGBufferDoubleSided,
        &pSceneRenderer->pPSGBufferDoubleSided,
        &pSceneRenderer->pCSGenerateDraws,
        &pSceneRenderer->pCSGenerateDrawsShadowMap,
        &pSceneRenderer->pCSHiZDownsample,
        &pSceneRenderer->pVSLighting,
        &pSceneRenderer->pPSLighting,
        &pSceneRenderer->pVSDebug,
        &pSceneRenderer->pPSDebug,
        &pSceneRenderer->pVSTonemapping,
        &pSceneRenderer->pPSTonemapping,
    };

    for(uint32 i = 0; i < ARR_LEN(shaders); i++)
    {
        if(*shaders[i])
        {
            removeShader(pSceneRenderer->pRenderer, shaders[i]);
        }
    }
}

void removeSceneResources(SceneRenderer* pSceneRenderer)
{
    unsetResources(&pSceneRenderer->mResMan);
}

void removeScenePipelines(SceneRenderer* pSceneRenderer)
{
    if(pSceneRenderer->pPipeShadowMapPass)
        removePipeline(pSceneRenderer->pRenderer, &pSceneRenderer->pPipeShadowMapPass);
    if(pSceneRenderer->pPipeShadowMapPassDoubleSided)
        removePipeline(pSceneRenderer->pRenderer, &pSceneRenderer->pPipeShadowMapPassDoubleSided);
    if(pSceneRenderer->pPipeDepthPrePass)
        removePipeline(pSceneRenderer->pRenderer, &pSceneRenderer->pPipeDepthPrePass);
    if(pSceneRenderer->pPipeDepthPrePassDoubleSided)
        removePipeline(pSceneRenderer->pRenderer, &pSceneRenderer->pPipeDepthPrePassDoubleSided);
    if(pSceneRenderer->pPipeGBuffer)
        removePipeline(pSceneRenderer->pRenderer, &pSceneRenderer->pPipeGBuffer);
    if(pSceneRenderer->pPipeGBufferDoubleSided)
        removePipeline(pSceneRenderer->pRenderer, &pSceneRenderer->pPipeGBufferDoubleSided);
    if(pSceneRenderer->pPipeGenerateDraws)
        removePipeline(pSceneRenderer->pRenderer, &pSceneRenderer->pPipeGenerateDraws);
    if(pSceneRenderer->pPipeGenerateDrawsShadowMap)
        removePipeline(pSceneRenderer->pRenderer, &pSceneRenderer->pPipeGenerateDrawsShadowMap);
    if(pSceneRenderer->pPipeHiZDownsample)
        removePipeline(pSceneRenderer->pRenderer, &pSceneRenderer->pPipeHiZDownsample);
    if(pSceneRenderer->pPipeLighting)
        removePipeline(pSceneRenderer->pRenderer, &pSceneRenderer->pPipeLighting);
    if(pSceneRenderer->pPipeDebug)
        removePipeline(pSceneRenderer->pRenderer, &pSceneRenderer->pPipeDebug);
    if(pSceneRenderer->pPipeTonemapping)
        removePipeline(pSceneRenderer->pRenderer, &pSceneRenderer->pPipeTonemapping);
}

void updatePerFrameData(SceneRenderer* pSceneRenderer)
{
    // Per frame constants
    m4f cameraView = getView(&pSceneRenderer->mCamera);
    m4f cameraProj = getProj(&pSceneRenderer->mCamera);
    pSceneRenderer->perFrameData.mView = cameraView;
    pSceneRenderer->perFrameData.mProj = cameraProj;
    pSceneRenderer->perFrameData.mViewProj = matMul(cameraProj, cameraView);
    pSceneRenderer->perFrameData.mInvView = inverse(cameraView);
    pSceneRenderer->perFrameData.mInvProj = inverse(cameraProj);
    pSceneRenderer->perFrameData.mCamWorldPos = to4f(pSceneRenderer->mCamera.mPos, 1);
    if(!pSceneRenderer->mFreezeMainCam)
    {
        pSceneRenderer->perFrameData.mMainView = cameraView;
        pSceneRenderer->perFrameData.mMainProj = cameraProj;
    }

    float cascadeSplitDistances[MAX_CASCADES];
    getCascadeDistances(pSceneRenderer, &pSceneRenderer->mCamera, cascadeSplitDistances);
    for(int32 i = 0; i < MAX_CASCADES; i++)
    {
        m4f cascadeViewProj = getCascadeViewProj(pSceneRenderer, &pSceneRenderer->mCamera, cascadeSplitDistances, i);
        pSceneRenderer->perFrameData.mShadowCascadesViewProj[i] = cascadeViewProj;
    }

    DirectionalLight light = pSceneRenderer->mDirLight;
    pSceneRenderer->perFrameData.mDirLight1 = to4f(normalize(light.mDir), light.mIntensity);
    pSceneRenderer->perFrameData.mDirLight2 = to4f(light.mColor, pSceneRenderer->mAmbient);

    memcpy(pSceneRenderer->perFrameData.mShadowCascadeDistances.mData, cascadeSplitDistances, MAX_CASCADES * sizeof(float));

    // Shadow constants
    // TODO(caio): Control shadow data here

    // Target handles
    ResourceManager* pResMan = &pSceneRenderer->mResMan;
    for(int32 i = 0; i < MAX_CASCADES; i++)
    {
        pSceneRenderer->perFrameData.mHandleShadowMaps[i] = getHandle(pSceneRenderer->pRTShadows[i]->pTexture);
    }
    for(int32 i = 0; i < HIZ_MAX; i++)
    {
        pSceneRenderer->perFrameData.mHandleHiZ[i] = getRWHandle(pSceneRenderer->pDepthHierarchyTextures[i]);
    }
}

void uploadPerFrameData(SceneRenderer* pSceneRenderer)
{
    Renderer* pRenderer = pSceneRenderer->pRenderer;
    uint32 activeFrame = pRenderer->mActiveFrame;
    copyToBuffer(pRenderer, 
            pSceneRenderer->pCBPerFrame[activeFrame], 
            0, 
            &pSceneRenderer->perFrameData, 
            sizeof(PerFrame));

    copyToBuffer(pRenderer, 
            pSceneRenderer->pCBShadowConstants[activeFrame], 
            0, 
            &pSceneRenderer->shadowConstants, 
            sizeof(ShadowConstants));
}

void debugAddVertex(SceneRenderer* pSceneRenderer, v3f pos, v3f col)
{
    pSceneRenderer->mDebugVerts.push(pos.x);
    pSceneRenderer->mDebugVerts.push(pos.y);
    pSceneRenderer->mDebugVerts.push(pos.z);
    pSceneRenderer->mDebugVerts.push(col.x);
    pSceneRenderer->mDebugVerts.push(col.y);
    pSceneRenderer->mDebugVerts.push(col.z);
}

void debugAddTri(SceneRenderer* pSceneRenderer, v3f p0, v3f p1, v3f p2, v3f col)
{
    // CCW p0 > p1 > p2
    debugAddVertex(pSceneRenderer, p0, col);
    debugAddVertex(pSceneRenderer, p2, col);
    debugAddVertex(pSceneRenderer, p1, col);
}

void debugAddSphere(SceneRenderer* pSceneRenderer, v3f center, float radius, v3f col, uint32 stacks, uint32 slices)
{
    for(uint32 st = 0; st < stacks; st++)
    {
        float theta0 = ((float)st / stacks) * PI;
        float theta1 = ((float)(st + 1) / stacks) * PI;

        for(uint32 sl = 0; sl < slices; sl++)
        {
            float phi0 = ((float)sl / slices) * PI * 2;
            float phi1 = ((float)(sl + 1) / slices) * PI * 2;

            v3f v0 = fromPolar(radius, theta0, phi0) + center;
            v3f v1 = fromPolar(radius, theta0, phi1) + center;
            v3f v2 = fromPolar(radius, theta1, phi0) + center;
            v3f v3 = fromPolar(radius, theta1, phi1) + center;

            if(st == 0)
            {
                debugAddTri(pSceneRenderer, v0, v2, v3, col);
            }
            else if(st + 1 == stacks)
            {
                debugAddTri(pSceneRenderer, v2, v0, v1, col);
            }
            else
            {
                debugAddTri(pSceneRenderer, v0, v1, v3, col);
                //debugAddTri(pSceneRenderer, v0, v2, v3, col);
            }
        }
    }
}

void debugAddPoint(SceneRenderer* pSceneRenderer, v3f p, v3f col)
{
    debugAddSphere(pSceneRenderer, p, 0.0001f, col, 4, 4);
}

void debugAddCylinder(SceneRenderer* pSceneRenderer, v3f start, v3f dir, float radius, v3f color, uint32 divs)
{
    v3f centerBottom = start;
    v3f centerTop = start + dir;

    v3f z = normalize(dir);
    v3f x = normalize(cross(dir, {0,1,0}));
    v3f y = normalize(cross(x, z));

    if(magn(x) < 0.00001f)  // Edge case: dir is {0, 1, 0}
    {
        x = normalize(cross({0, 0, 1}, dir));
        y = normalize(cross(x, z));
    }

    for(uint32 div = 0; div < divs; div++)
    {
        float a0 = TO_RAD(360.f / divs) * div;
        float a0x = radius * cosf(a0);
        float a0y = radius * sinf(a0);

        float a1 = TO_RAD(360.f / divs) * (div + 1);
        float a1x = radius * cosf(a1);
        float a1y = radius * sinf(a1);

        v3f b0 = centerBottom + (a0x * x) + (a0y * y);
        v3f b1 = centerBottom + (a1x * x) + (a1y * y);
        v3f t0 = b0 + dir;
        v3f t1 = b1 + dir;

        debugAddTri(pSceneRenderer, centerBottom, b0, b1, color);
        debugAddTri(pSceneRenderer, b0, t0, t1, color);
        debugAddTri(pSceneRenderer, b0, b1, t1, color);
        debugAddTri(pSceneRenderer, centerTop, t0, t1, color);
    }
}

void debugAddCone(SceneRenderer* pSceneRenderer, v3f start, v3f dir, float radius, v3f color, uint32 divs)
{
    v3f centerBottom = start;
    v3f centerTop = start + dir;

    v3f z = normalize(dir);
    v3f x = normalize(cross(dir, {0,1,0}));
    v3f y = normalize(cross(x, z));

    if(magn(x) < 0.00001f)  // Edge case: dir is {0, 1, 0}
    {
        x = normalize(cross({0, 0, 1}, dir));
        y = normalize(cross(x, z));
    }

    for(uint32 div = 0; div < divs; div++)
    {
        float a0 = TO_RAD(360.f / divs) * div;
        float a0x = radius * cosf(a0);
        float a0y = radius * sinf(a0);

        float a1 = TO_RAD(360.f / divs) * (div + 1);
        float a1x = radius * cosf(a1);
        float a1y = radius * sinf(a1);

        v3f b0 = centerBottom + (a0x * x) + (a0y * y);
        v3f b1 = centerBottom + (a1x * x) + (a1y * y);

        debugAddTri(pSceneRenderer, centerBottom, b0, b1, color);
        debugAddTri(pSceneRenderer, centerTop, b0, b1, color);
    }
}

void debugAddVector(SceneRenderer* pSceneRenderer, v3f start, v3f dir, v3f color)
{
    float cylinderRadius = 0.005f;
    debugAddCylinder(pSceneRenderer, start, dir, cylinderRadius, color, 12);
    debugAddCone(pSceneRenderer, start + dir, normalize(dir) * 0.05f, cylinderRadius * 5, color, 12);
}

void debugAddPlane(SceneRenderer* pSceneRenderer, v3f p0, v3f p1, v3f p2, v3f p3, v3f color1, v3f color2)
{
    float cylinderRadius = 0.005f;
    debugAddCylinder(pSceneRenderer, p0, p1 - p0, cylinderRadius, color1, 6);
    debugAddCylinder(pSceneRenderer, p1, p3 - p1, cylinderRadius, color1, 6);
    debugAddCylinder(pSceneRenderer, p3, p2 - p3, cylinderRadius, color1, 6);
    debugAddCylinder(pSceneRenderer, p2, p0 - p2, cylinderRadius, color1, 6);

    v3f c = p0 + (0.5f * (p3 - p0));
    v3f n = normalize(cross(p1 - p0, p2 - p0));
    debugAddVector(pSceneRenderer, c, n, color2);
}

void debugAddAABB(SceneRenderer* pSceneRenderer, AABB aabb, m4f xform, v3f color)
{
    AABB aabbWorld = transformAABB(aabb, xform);

    v3f points[8] =
    {
        {aabbWorld.min.x, aabbWorld.min.y, aabbWorld.min.z},
        {aabbWorld.max.x, aabbWorld.min.y, aabbWorld.min.z},
        {aabbWorld.min.x, aabbWorld.max.y, aabbWorld.min.z},
        {aabbWorld.max.x, aabbWorld.max.y, aabbWorld.min.z},
        {aabbWorld.min.x, aabbWorld.min.y, aabbWorld.max.z},
        {aabbWorld.max.x, aabbWorld.min.y, aabbWorld.max.z},
        {aabbWorld.min.x, aabbWorld.max.y, aabbWorld.max.z},
        {aabbWorld.max.x, aabbWorld.max.y, aabbWorld.max.z},
    };

    debugAddTri(pSceneRenderer, points[0], points[3], points[1], color);
    debugAddTri(pSceneRenderer, points[0], points[2], points[3], color);
    debugAddTri(pSceneRenderer, points[4], points[7], points[5], color);
    debugAddTri(pSceneRenderer, points[4], points[6], points[7], color);
    debugAddTri(pSceneRenderer, points[2], points[3], points[7], color);
    debugAddTri(pSceneRenderer, points[2], points[7], points[6], color);
    debugAddTri(pSceneRenderer, points[0], points[1], points[5], color);
    debugAddTri(pSceneRenderer, points[0], points[5], points[4], color);
    debugAddTri(pSceneRenderer, points[4], points[0], points[2], color);
    debugAddTri(pSceneRenderer, points[4], points[2], points[6], color);
    debugAddTri(pSceneRenderer, points[1], points[5], points[7], color);
    debugAddTri(pSceneRenderer, points[1], points[7], points[3], color);
}

void debugAddFrustum(SceneRenderer* pSceneRenderer, m4f view, m4f proj, v3f color, float zOffset)
{
    v3f corners[8];
    frustumCorners(view, proj, corners, zOffset);

    // Near plane
    //debugAddTri(pSceneRenderer, corners[0], corners[2], corners[1], color);
    //debugAddTri(pSceneRenderer, corners[2], corners[3], corners[1], color);

    // Far plane
    //debugAddTri(pSceneRenderer, corners[4], corners[6], corners[5], color);
    //debugAddTri(pSceneRenderer, corners[6], corners[7], corners[5], color);

    // Top plane
    debugAddTri(pSceneRenderer, corners[0], corners[5], corners[4], color);
    debugAddTri(pSceneRenderer, corners[0], corners[1], corners[5], color);
    
    // Bottom plane
    debugAddTri(pSceneRenderer, corners[2], corners[7], corners[6], color);
    debugAddTri(pSceneRenderer, corners[2], corners[3], corners[7], color);

    // Left plane
    debugAddTri(pSceneRenderer, corners[0], corners[2], corners[4], color);
    debugAddTri(pSceneRenderer, corners[2], corners[6], corners[4], color);

    // Right plane
    debugAddTri(pSceneRenderer, corners[1], corners[3], corners[5], color);
    debugAddTri(pSceneRenderer, corners[3], corners[7], corners[5], color);
}

void debugGeometryStart(SceneRenderer* pSceneRenderer)
{
    pSceneRenderer->mDebugVerts.clear();
}

void debugGeometryEnd(SceneRenderer* pSceneRenderer)
{
    static Camera debugCam = pSceneRenderer->mCamera;

    if(pSceneRenderer->mFreezeMainCam)
    {
        // World Coordinate Axis
        {
            debugAddVector(pSceneRenderer, {0,0,0}, {1,0,0}, {1,0,0});
            debugAddVector(pSceneRenderer, {0,0,0}, {0,1,0}, {0,1,0});
            debugAddVector(pSceneRenderer, {0,0,0}, {0,0,1}, {0,0,1});
        }

        // Insert debug visualizations with frozen camera here!
    }
    else
    {
        debugCam = pSceneRenderer->mCamera;
    }

    // Insert debug visualizations here!

    if(!pSceneRenderer->mDebugVerts.mCount)
    {
        return;
    }

    // Copying data to GPU vertex buffer
    copyToBuffer(pSceneRenderer->pRenderer, 
            pSceneRenderer->pVBDebug[pSceneRenderer->pRenderer->mActiveFrame], 
            0, 
            pSceneRenderer->mDebugVerts.mData, 
            pSceneRenderer->mDebugVerts.mCount * sizeof(float));
}

void freezeMainCamera(SceneRenderer* pSceneRenderer, bool freeze)
{
    pSceneRenderer->mFreezeMainCam = freeze;
}

void addUIControls(SceneRenderer* pSceneRenderer)
{
    ASSERT(pSceneRenderer);

    const float panelWidth = 420.f;

    uiStartWindow(str("Renderer"), -panelWidth, 0, panelWidth, 0);

    if(uiStartTabBar(str("RendererTabs")))
    {
        if(uiStartTab(str("Controls")))
        {
            uiSeparator(str("Camera"));

            uiDragf(
                str("Near Plane"),
                &pSceneRenderer->mCamera.mDesc.mNear,
                0.1f,
                0.0001f,
                10.f);

            uiDragf(
                str("Far Plane"),
                &pSceneRenderer->mCamera.mDesc.mFar,
                1.f,
                10.f,
                100.f);

            // Controls are exposed as horizontal FoV while the
            // camera internally stores vertical FoV.
            static float fovX = 90.f;

            float aspect = getAspectRatio(pSceneRenderer->pApp);

            uiDragf(
                str("FoV (X)"),
                &fovX,
                1.f,
                30.f,
                150.f);

            pSceneRenderer->mCamera.mDesc.mFovY =
                fovHtoV(TO_RAD(fovX), aspect);

            uiCheckbox(
                str("Freeze Main Camera"),
                &pSceneRenderer->mFreezeMainCam);

            uiSeparator(str("Directional Light"));

            uiSlider3f(
                str("Direction"),
                &pSceneRenderer->mDirLight.mDir.mData[0],
                -1.f,
                1.f);

            uiSliderf(
                str("Intensity"),
                &pSceneRenderer->mDirLight.mIntensity,
                0.f,
                10.f);

            uiColor3f(
                str("Color"),
                &pSceneRenderer->mDirLight.mColor.mData[0]);

            uiSliderf(
                str("Ambient"),
                &pSceneRenderer->mAmbient,
                0.f,
                1.f);

            uiSeparator(str("Shadows"));

            uiSliderf(
                str("Light Bleeding Reduction"),
                &pSceneRenderer->shadowConstants.mBleedingReduction,
                0.f,
                1.f);

            uiSliderf(
                str("Cascade Split Factor"),
                &pSceneRenderer->mShadowSettings.kSplitFactor,
                0.f,
                1.f);

            // Show the actual cascade boundaries.
            float cascadeDistances[MAX_CASCADES];
            getCascadeDistances(
                pSceneRenderer,
                &pSceneRenderer->mCamera,
                cascadeDistances);

            uiSeparator(str("Cascade Distances"));

            for(int32 i = 0; i < MAX_CASCADES; ++i)
            {
                char label[64];
                strf(label, "Cascade %d", i);

                uiInputf(
                    str(label),
                    &cascadeDistances[i]);
            }

            uiEndTab();
        }

        if(uiStartTab(str("GPU Timings")))
        {
            uiGpuTimings(&pSceneRenderer->pApp->mAppArena, &pSceneRenderer->mGpuTimer);

            uiEndTab();
        }

        if(uiStartTab(str("Shadows")))
        {
            uiSeparator(str("Cascade Shadow Maps"));

            // Two cascades per row.
            const uint32 imageSize = 180;

            for(int32 i = 0; i < MAX_CASCADES; ++i)
            {
                char label[64];
                strf(label, "Cascade %d", i);

                uiText(str(label));

                Texture* pTex =
                    pSceneRenderer->pRTShadows[i]->pTexture;

                uiImage(
                    pSceneRenderer->pUI,
                    pTex,
                    pSceneRenderer->pSamplerPoint,
                    imageSize,
                    imageSize);

                if((i & 1) == 0 && i + 1 < MAX_CASCADES)
                {
                    uiSameLine();
                }
            }

            uiSeparator(str("Cascade Information"));

            float cascadeDistances[MAX_CASCADES];
            getCascadeDistances(
                pSceneRenderer,
                &pSceneRenderer->mCamera,
                cascadeDistances);

            for(int32 i = 0; i < MAX_CASCADES; ++i)
            {
                char label[64];
                strf(label, "Cascade %d", i);

                uiInputf(
                    str(label),
                    &cascadeDistances[i]);
            }

            uiEndTab();
        }

        uiEndTabBar();
    }

    uiEndWindow();
}

#define RENDERER_SCOPE_BEGIN(NAME) cmdScopeBegin(pSceneRenderer->pRenderer, pCmd, str(NAME))
#define RENDERER_SCOPE_END() cmdScopeEnd(pSceneRenderer->pRenderer, pCmd)

void passIssueDrawCalls(CommandBuffer* pCmd, SceneRenderer* pSceneRenderer, DrawCallIssuePass type, uint32 frame)
{
    ComputePipeline* pPipeline = NULL;
    if(type == DRAW_CALL_ISSUE_SHADOWS)
    {
        RENDERER_SCOPE_BEGIN("Populate Draws (Shadows)");
        pPipeline = pSceneRenderer->pPipeGenerateDrawsShadowMap;
    }
    else
    {
        RENDERER_SCOPE_BEGIN("Populate Draws (Opaque)");
        pPipeline = pSceneRenderer->pPipeGenerateDraws;
    }

    cmdBindComputePipeline(pCmd, pPipeline);

    cmdResetShaderConstants(pCmd);
    cmdPushShaderConstant(pCmd, pSceneRenderer->pScene->mNodeCount);
    cmdPushShaderConstant(pCmd, getHandle(pSceneRenderer->pCBPerFrame[frame]));
    cmdPushShaderConstant(pCmd, getHandle(pSceneRenderer->mDrawBuffers.pDrawBuffers[frame]));
    cmdPushShaderConstant(pCmd, getHandle(pSceneRenderer->mDrawBuffers.pDrawCountBuffers[frame]));
    cmdPushShaderConstant(pCmd, getHandle(pSceneRenderer->pSBInstancesShadow[frame]));
    cmdPushShaderConstant(pCmd, getHandle(pSceneRenderer->pSBInstancesOpaque[frame]));
    cmdPushShaderConstant(pCmd, getHandle(pSceneRenderer->pSBInstancesOpaqueDouble[frame]));
    cmdPushShaderConstant(pCmd, getHandle(pSceneRenderer->pSBSceneNodes));
    cmdPushShaderConstant(pCmd, getHandle(pSceneRenderer->pSBSceneMeshes));
    cmdPushShaderConstant(pCmd, getHandle(pSceneRenderer->pSBSceneMaterials));
    cmdSetShaderConstants(pCmd, pSceneRenderer->pRenderer);

    cmdDispatch(pCmd, SCENE_MAX_NODES / 32, 1, 1);
    RENDERER_SCOPE_END();
}

void passShadowMap(CommandBuffer* pCmd, SceneRenderer* pSceneRenderer, uint32 frame)
{
    RENDERER_SCOPE_BEGIN("Shadow Pass");
    Barrier barrier = {};
    barrier.mSrcStage = PIPELINE_STAGE_COMPUTE_SHADER;
    barrier.mDstStage = PIPELINE_STAGE_DRAW_INDIRECT;
    barrier.mSrcAccess = MEMORY_ACCESS_SHADER_WRITE;
    barrier.mDstAccess = MEMORY_ACCESS_INDIRECT_READ;
    cmdBarrier(pCmd, 1, &barrier);

    for(uint32 i = 0; i < MAX_CASCADES; i++)
    {
        RenderTarget* pRT = pSceneRenderer->pRTShadows[i];
        RenderTarget* pRTDepth = pSceneRenderer->pRTShadowsDepth[i];
        RenderTargetBarrier barriers[2];
        barriers[0] = {pRT, getImageLayout(pRT), IMAGE_LAYOUT_COLOR_OUTPUT };
        barriers[1] = {pRTDepth, getImageLayout(pRTDepth), IMAGE_LAYOUT_DEPTH_STENCIL_OUTPUT };
        cmdRenderTargetBarrier(pCmd, ARR_LEN(barriers), barriers);

        RenderTargetBindDesc bindDesc = {};
        bindDesc.mColorCount = 1;
        bindDesc.mColorBindings[0] = { pRT, LOAD_OP_CLEAR, STORE_OP_STORE };
        bindDesc.mDepthBinding = { pRTDepth, LOAD_OP_CLEAR, STORE_OP_STORE };
        cmdBindRenderTargets(pCmd, bindDesc);

        GraphicsPipeline* pPipeline = pSceneRenderer->pPipeShadowMapPass;
        cmdBindGraphicsPipeline(pCmd, pPipeline);

        cmdSetViewport(pCmd, pRTDepth);
        cmdSetScissor(pCmd, pRTDepth);

        cmdBindVertexBuffer(pCmd, pSceneRenderer->pVBSceneGeometry);
        cmdBindIndexBuffer(pCmd, pSceneRenderer->pIBSceneGeometry);

        cmdResetShaderConstants(pCmd);
        cmdPushShaderConstant(pCmd, getHandle(pSceneRenderer->pCBPerFrame[frame]));
        cmdPushShaderConstant(pCmd, getHandle(pSceneRenderer->pSBInstancesShadow[frame]));
        cmdPushShaderConstant(pCmd, getHandle(pSceneRenderer->pSBSceneNodes));
        cmdPushShaderConstant(pCmd, getHandle(pSceneRenderer->pSBSceneMaterials));
        cmdPushShaderConstant(pCmd, i);
        cmdSetShaderConstants(pCmd, pSceneRenderer->pRenderer);

        cmdDrawIndirectBuffer(pCmd, &pSceneRenderer->mDrawBuffers, DB_SHADOW_0 + i, frame);

        // TODO(caio): Reenable double sided shadow pass after draw buffers are working

        cmdUnbindRenderTargets(pCmd);

        barriers[0] = {pRT, getImageLayout(pRT), IMAGE_LAYOUT_SHADER_READ_ONLY };
        cmdRenderTargetBarrier(pCmd, 1, barriers);
    }
    RENDERER_SCOPE_END();
}

void passHiZDownsample(CommandBuffer* pCmd, SceneRenderer* pSceneRenderer, uint32 frame)
{
    RENDERER_SCOPE_BEGIN("Hi-Z Downsample");
    ComputePipeline* pPipeline = pSceneRenderer->pPipeHiZDownsample;

    cmdBindComputePipeline(pCmd, pPipeline);

    v2u size = { 
        pSceneRenderer->pRTSceneDepth->mDesc.mWidth, 
        pSceneRenderer->pRTSceneDepth->mDesc.mHeight, 
    };

    uint32 baseMip = 0;
    while(baseMip < HIZ_MAX)
    {
        uint32 mipCount = 3; 
        v2u mipSize = size;
        for(int32 i = 1; i <= 3; i++)
        {
            mipSize = { mipSize.x / 2, mipSize.y / 2 };
            if (mipSize.x == 0 || mipSize.y == 0)
                mipCount--;
        }

        cmdResetShaderConstants(pCmd);
        cmdPushShaderConstant(pCmd, getHandle(pSceneRenderer->pCBPerFrame[frame]));
        cmdPushShaderConstant(pCmd, baseMip);
        cmdPushShaderConstant(pCmd, mipCount);
        cmdSetShaderConstants(pCmd, pSceneRenderer->pRenderer);

        uint32 groupSize = (uint32)pow(2, mipCount);
        v2u dispatchSize = { 
            size.x % groupSize == 0 ? size.x : size.x + (groupSize - (size.x % groupSize)),
            size.y % groupSize == 0 ? size.y : size.y + (groupSize - (size.y % groupSize)),
        };
        cmdDispatch(pCmd, dispatchSize.x, dispatchSize.y, 1);

        if(mipCount != 3)
        {
            break;
        }

        baseMip += mipCount;
        size = { size.x / groupSize, size.y / groupSize };
    }

    Barrier barrier = {};
    barrier.mSrcStage = PIPELINE_STAGE_COMPUTE_SHADER;
    barrier.mDstStage = PIPELINE_STAGE_COMPUTE_SHADER;
    barrier.mSrcAccess = MEMORY_ACCESS_SHADER_WRITE;
    barrier.mDstAccess = MEMORY_ACCESS_SHADER_READ;
    cmdBarrier(pCmd, 1, &barrier);
    RENDERER_SCOPE_END();
}

void passPreDepth(CommandBuffer* pCmd, SceneRenderer* pSceneRenderer, uint32 frame)
{
    RENDERER_SCOPE_BEGIN("Depth Pre-Pass");
    RenderTarget* pRTDepth = pSceneRenderer->pRTSceneDepth;
    RenderTargetBarrier barriers[1];
    barriers[0] = {pRTDepth, getImageLayout(pRTDepth), IMAGE_LAYOUT_DEPTH_STENCIL_OUTPUT };
    cmdRenderTargetBarrier(pCmd, ARR_LEN(barriers), barriers);

    RenderTargetBindDesc bindDesc = {};
    bindDesc.mColorCount = 0;
    bindDesc.mDepthBinding = { pRTDepth, LOAD_OP_CLEAR, STORE_OP_STORE };
    cmdBindRenderTargets(pCmd, bindDesc);

    GraphicsPipeline* pPipeline = pSceneRenderer->pPipeDepthPrePass;
    cmdBindGraphicsPipeline(pCmd, pPipeline);

    cmdSetViewport(pCmd, pRTDepth);
    cmdSetScissor(pCmd, pRTDepth);

    cmdBindVertexBuffer(pCmd, pSceneRenderer->pVBSceneGeometry);
    cmdBindIndexBuffer(pCmd, pSceneRenderer->pIBSceneGeometry);

    cmdResetShaderConstants(pCmd);
    cmdPushShaderConstant(pCmd, getHandle(pSceneRenderer->pCBPerFrame[frame]));
    cmdPushShaderConstant(pCmd, getHandle(pSceneRenderer->pSBInstancesOpaque[frame]));
    cmdPushShaderConstant(pCmd, getHandle(pSceneRenderer->pSBInstancesOpaqueDouble[frame]));
    cmdPushShaderConstant(pCmd, getHandle(pSceneRenderer->pSBSceneNodes));
    cmdPushShaderConstant(pCmd, getHandle(pSceneRenderer->pSBSceneMaterials));
    cmdSetShaderConstants(pCmd, pSceneRenderer->pRenderer);

    // Opaque
    cmdDrawIndirectBuffer(pCmd, &pSceneRenderer->mDrawBuffers, DB_GBUFFER_OPAQUE, frame);

    pPipeline = pSceneRenderer->pPipeDepthPrePassDoubleSided;
    cmdBindGraphicsPipeline(pCmd, pPipeline);

    // Double-sided opaque
    cmdDrawIndirectBuffer(pCmd, &pSceneRenderer->mDrawBuffers, DB_GBUFFER_OPAQUE_DOUBLE, frame);

    cmdUnbindRenderTargets(pCmd);
    RENDERER_SCOPE_END();
}

void passGBuffer(CommandBuffer* pCmd, SceneRenderer* pSceneRenderer, uint32 frame)
{
    RENDERER_SCOPE_BEGIN("G-Buffer Pass");
    RenderTarget* pRTGBufferA = pSceneRenderer->pRTGBufferA;
    RenderTarget* pRTGBufferB = pSceneRenderer->pRTGBufferB;
    RenderTarget* pRTDepth = pSceneRenderer->pRTSceneDepth;

    GraphicsPipeline* pPipeline = pSceneRenderer->pPipeGBuffer;

    RenderTargetBarrier barriers[2];
    barriers[0] = {pRTGBufferA, getImageLayout(pRTGBufferA), IMAGE_LAYOUT_COLOR_OUTPUT };
    barriers[1] = {pRTGBufferB, getImageLayout(pRTGBufferB), IMAGE_LAYOUT_COLOR_OUTPUT };
    cmdRenderTargetBarrier(pCmd, ARR_LEN(barriers), barriers);

    RenderTargetBindDesc bindDesc = {};
    bindDesc.mColorCount = 2;
    bindDesc.mColorBindings[0] = { pRTGBufferA, LOAD_OP_CLEAR, STORE_OP_STORE };
    bindDesc.mColorBindings[1] = { pRTGBufferB, LOAD_OP_CLEAR, STORE_OP_STORE };
    bindDesc.mDepthBinding = { pRTDepth, LOAD_OP_LOAD, STORE_OP_STORE };
    cmdBindRenderTargets(pCmd, bindDesc);

    cmdBindGraphicsPipeline(pCmd, pPipeline);

    cmdSetViewport(pCmd, pRTGBufferA);
    cmdSetScissor(pCmd, pRTGBufferA);

    cmdBindVertexBuffer(pCmd, pSceneRenderer->pVBSceneGeometry);
    cmdBindIndexBuffer(pCmd, pSceneRenderer->pIBSceneGeometry);

    cmdResetShaderConstants(pCmd);
    cmdPushShaderConstant(pCmd, getHandle(pSceneRenderer->pCBPerFrame[frame]));
    cmdPushShaderConstant(pCmd, getHandle(pSceneRenderer->pSBInstancesOpaque[frame]));
    cmdPushShaderConstant(pCmd, getHandle(pSceneRenderer->pSBInstancesOpaqueDouble[frame]));
    cmdPushShaderConstant(pCmd, getHandle(pSceneRenderer->pSBSceneNodes));
    cmdPushShaderConstant(pCmd, getHandle(pSceneRenderer->pSBSceneMaterials));
    cmdSetShaderConstants(pCmd, pSceneRenderer->pRenderer);

    // Opaque
    cmdDrawIndirectBuffer(pCmd, &pSceneRenderer->mDrawBuffers, DB_GBUFFER_OPAQUE, frame);

    pPipeline = pSceneRenderer->pPipeGBufferDoubleSided;
    cmdBindGraphicsPipeline(pCmd, pPipeline);

    // Double-sided opaque
    cmdDrawIndirectBuffer(pCmd, &pSceneRenderer->mDrawBuffers, DB_GBUFFER_OPAQUE_DOUBLE, frame);

    cmdUnbindRenderTargets(pCmd);
    RENDERER_SCOPE_END();
}

void passLighting(CommandBuffer* pCmd, SceneRenderer* pSceneRenderer, uint32 frame)
{
    RENDERER_SCOPE_BEGIN("Lighting Pass");
    RenderTarget* pRTGBufferA = pSceneRenderer->pRTGBufferA;
    RenderTarget* pRTGBufferB = pSceneRenderer->pRTGBufferB;
    RenderTarget* pRTDepth = pSceneRenderer->pRTSceneDepth;
    RenderTarget* pRTAccum = pSceneRenderer->pRTAccum;

    GraphicsPipeline* pPipeline = pSceneRenderer->pPipeLighting;

    RenderTargetBarrier barriers[4];
    barriers[0] = {pRTGBufferA, getImageLayout(pRTGBufferA), IMAGE_LAYOUT_SHADER_READ_ONLY };
    barriers[1] = {pRTGBufferB, getImageLayout(pRTGBufferB), IMAGE_LAYOUT_SHADER_READ_ONLY };
    barriers[2] = {pRTDepth, getImageLayout(pRTDepth), IMAGE_LAYOUT_SHADER_READ_ONLY };
    barriers[3] = {pRTAccum, getImageLayout(pRTAccum), IMAGE_LAYOUT_COLOR_OUTPUT };
    cmdRenderTargetBarrier(pCmd, ARR_LEN(barriers), barriers);

    RenderTargetBindDesc bindDesc = {};
    bindDesc.mColorCount = 1;
    bindDesc.mColorBindings[0] = { pRTAccum, LOAD_OP_CLEAR, STORE_OP_STORE };
    cmdBindRenderTargets(pCmd, bindDesc);

    cmdBindGraphicsPipeline(pCmd, pPipeline);

    cmdSetViewport(pCmd, pRTAccum);
    cmdSetScissor(pCmd, pRTAccum);

    cmdBindVertexBuffer(pCmd, pSceneRenderer->pVBScreenQuad);
    cmdBindIndexBuffer(pCmd, pSceneRenderer->pIBScreenQuad);

    cmdResetShaderConstants(pCmd);
    cmdPushShaderConstant(pCmd, getHandle(pSceneRenderer->pCBPerFrame[frame]));
    cmdPushShaderConstant(pCmd, getHandle(pSceneRenderer->pCBShadowConstants[frame]));
    cmdPushShaderConstant(pCmd, getHandle(pRTGBufferA));
    cmdPushShaderConstant(pCmd, getHandle(pRTGBufferB));
    cmdPushShaderConstant(pCmd, getHandle(pRTDepth));
    cmdSetShaderConstants(pCmd, pSceneRenderer->pRenderer);

    cmdDrawIndexed(pCmd, 
            3, 1, 0, 0);

    cmdUnbindRenderTargets(pCmd);
    RENDERER_SCOPE_END();
}

void passDebug(CommandBuffer* pCmd, SceneRenderer* pSceneRenderer, uint32 frame)
{
    RENDERER_SCOPE_BEGIN("Debug Geometry");
    RenderTarget* pRTAccum = pSceneRenderer->pRTAccum;

    GraphicsPipeline* pPipeline = pSceneRenderer->pPipeDebug;

    RenderTargetBindDesc bindDesc = {};
    bindDesc.mColorCount = 1;
    bindDesc.mColorBindings[0] = { pRTAccum, LOAD_OP_LOAD, STORE_OP_STORE };
    cmdBindRenderTargets(pCmd, bindDesc);

    cmdBindGraphicsPipeline(pCmd, pPipeline);

    cmdSetViewport(pCmd, pRTAccum);
    cmdSetScissor(pCmd, pRTAccum);

    cmdBindVertexBuffer(pCmd, pSceneRenderer->pVBDebug[frame]);

    cmdResetShaderConstants(pCmd);
    cmdPushShaderConstant(pCmd, getHandle(pSceneRenderer->pCBPerFrame[frame]));
    cmdSetShaderConstants(pCmd, pSceneRenderer->pRenderer);

    cmdDraw(pCmd, pSceneRenderer->mDebugVerts.mCount / 6, 1);

    cmdUnbindRenderTargets(pCmd);
    RENDERER_SCOPE_END();
}

void passTonemapping(CommandBuffer* pCmd, SceneRenderer* pSceneRenderer, uint32 frame)
{
    RENDERER_SCOPE_BEGIN("Tone Mapping");
    RenderTarget* pRTAccum = pSceneRenderer->pRTAccum;
    RenderTarget* pRTPresent = pSceneRenderer->pRTPresent;
    
    GraphicsPipeline* pPipeline = pSceneRenderer->pPipeTonemapping;
    
    RenderTargetBarrier barriers[2];
    barriers[0] = {pRTAccum, getImageLayout(pRTAccum), IMAGE_LAYOUT_SHADER_READ_ONLY };
    barriers[1] = {pRTPresent, getImageLayout(pRTPresent), IMAGE_LAYOUT_COLOR_OUTPUT };
    cmdRenderTargetBarrier(pCmd, ARR_LEN(barriers), barriers);
    
    RenderTargetBindDesc bindDesc = {};
    bindDesc.mColorCount = 1;
    bindDesc.mColorBindings[0] = { pRTPresent, LOAD_OP_CLEAR, STORE_OP_STORE };
    cmdBindRenderTargets(pCmd, bindDesc);
    
    cmdBindGraphicsPipeline(pCmd, pPipeline);
    
    cmdSetViewport(pCmd, pRTPresent);
    cmdSetScissor(pCmd, pRTPresent);
    
    cmdBindVertexBuffer(pCmd, pSceneRenderer->pVBScreenQuad);
    cmdBindIndexBuffer(pCmd, pSceneRenderer->pIBScreenQuad);
    
    cmdResetShaderConstants(pCmd);
    cmdPushShaderConstant(pCmd, getHandle(pSceneRenderer->pCBPerFrame[frame]));
    cmdPushShaderConstant(pCmd, getHandle(pRTAccum));
    cmdSetShaderConstants(pCmd, pSceneRenderer->pRenderer);
    
    cmdDrawIndexed(pCmd, 
            3, 1, 0, 0);
    
    cmdUnbindRenderTargets(pCmd);
    RENDERER_SCOPE_END();
}

void passUI(CommandBuffer* pCmd, SceneRenderer* pSceneRenderer, uint32 frame)
{
    RENDERER_SCOPE_BEGIN("UI Pass");
    RenderTarget* pRTColor = pSceneRenderer->pRTPresent;
    RenderTargetBindDesc bindDesc = {};
    bindDesc.mColorCount = 1;
    bindDesc.mColorBindings[0] = { pRTColor, LOAD_OP_LOAD, STORE_OP_STORE };

    uiStartFrame();
    addUIControls(pSceneRenderer);
    uiEndFrame(pCmd, bindDesc);
    RENDERER_SCOPE_END();
}

void passSwapChainCopy(CommandBuffer* pCmd, SceneRenderer* pSceneRenderer, uint32 frame)
{
    RENDERER_SCOPE_BEGIN("Swap Chain Copy");
    // Transitioning depth buffer back to general, in case descriptors reload
    {
        RenderTarget* pRTDepth = pSceneRenderer->pRTSceneDepth;
        RenderTargetBarrier barriers[1];
        barriers[0] = { pRTDepth, getImageLayout(pRTDepth), IMAGE_LAYOUT_GENERAL };
        cmdRenderTargetBarrier(pCmd, ARR_LEN(barriers), barriers);

        barriers[0] = { pSceneRenderer->pRTPresent, getImageLayout(pSceneRenderer->pRTPresent), IMAGE_LAYOUT_COLOR_OUTPUT };
        cmdRenderTargetBarrier(pCmd, ARR_LEN(barriers), barriers);

    }

    // Copy to swap chain
    {
        RenderTarget* pRTPresent = pSceneRenderer->pRTPresent;
        RenderTargetBarrier barrier = {pRTPresent, IMAGE_LAYOUT_COLOR_OUTPUT, IMAGE_LAYOUT_TRANSFER_SRC };
        cmdRenderTargetBarrier(pCmd, 1, &barrier);
        cmdCopyToSwapChain(pCmd, &pSceneRenderer->pRenderer->mSwapChain, pRTPresent->pTexture);
    }
    RENDERER_SCOPE_END();
}

#define GPU_TIMINGS_START(FRAME) \
    GpuTimestampParams __gpuTimerParams = {}; \
    __gpuTimerParams.pGpuTimer = &pSceneRenderer->mGpuTimer; \
    __gpuTimerParams.pCmd = pCmd; \
    __gpuTimerParams.queryPool = FRAME; \
    gpuTimerReadResults(&__gpuTimerParams); \
    gpuTimerStart(&__gpuTimerParams); \

#define GPU_TIMESTAMP(NAME) \
        gpuTimestamp(str(NAME), &__gpuTimerParams); \

void renderScene(SceneRenderer* pSceneRenderer, uint32 frame)
{
    PROFILE_SCOPE;
    Renderer* pRenderer = pSceneRenderer->pRenderer;

    advanceFrame(pRenderer, frame);

    CommandBuffer* pCmd = getCmd(pRenderer);
    beginCmd(pCmd);

    acquireNextImage(pRenderer);

    cmdBindResources(pCmd, pRenderer);

    // Start GPU timings
    uint32 activeFrame = pRenderer->mActiveFrame; 
    GPU_TIMINGS_START(activeFrame);

    // Upload per frame data
    uploadPerFrameData(pSceneRenderer);
    GPU_TIMESTAMP("Upload Frame Data");

    cmdPrepareDrawBuffers(pCmd, &pSceneRenderer->mDrawBuffers, activeFrame);

    debugGeometryStart(pSceneRenderer);

    // CSM draw call generation
    passIssueDrawCalls(pCmd, pSceneRenderer, DRAW_CALL_ISSUE_SHADOWS, activeFrame);
    GPU_TIMESTAMP("Draw Call Issue Pass (Shadows)");

    // Cascaded Shadow Map pass
    passShadowMap(pCmd, pSceneRenderer, activeFrame);
    GPU_TIMESTAMP("Shadow Map Render Pass");

    // Hierarchical Z Downsampling pass
    if(!pSceneRenderer->mFreezeMainCam)
    {
        passHiZDownsample(pCmd, pSceneRenderer, activeFrame);
        GPU_TIMESTAMP("Shadow Map Render Pass");
    }

    // Generate draws pass
    passIssueDrawCalls(pCmd, pSceneRenderer, DRAW_CALL_ISSUE_OPAQUE, activeFrame);
    GPU_TIMESTAMP("Draw Call Issue Pass (Opaque)");

    // Depth pre pass
    passPreDepth(pCmd, pSceneRenderer, activeFrame);
    GPU_TIMESTAMP("Depth Pre-pass");

    // GBuffer render pass
    passGBuffer(pCmd, pSceneRenderer, activeFrame);
    GPU_TIMESTAMP("GBuffer Pass");

    // Lighting pass
    passLighting(pCmd, pSceneRenderer, activeFrame);
    GPU_TIMESTAMP("Lighting Pass");

    // Debug geometry pass
    debugGeometryEnd(pSceneRenderer);
    if(pSceneRenderer->mDebugVerts.mCount)
    {
        passDebug(pCmd, pSceneRenderer, activeFrame);
        GPU_TIMESTAMP("Debug Pass");
    }

    // Tone mapping pass
    passTonemapping(pCmd, pSceneRenderer, activeFrame);
    GPU_TIMESTAMP("Tone Mapping Pass");

    // UI pass
    passUI(pCmd, pSceneRenderer, activeFrame);
    GPU_TIMESTAMP("UI Pass");

    passSwapChainCopy(pCmd, pSceneRenderer, activeFrame);
    GPU_TIMESTAMP("Swap Chain Copy");

    endCmd(pCmd);
    submitFrameCmd(pRenderer, pCmd);
    present(pRenderer);
}
