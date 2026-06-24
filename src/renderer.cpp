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
#include "dw/src/render/shader.hpp"
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
    v3f sphereCenter = {0,0,0};
    for(int32 fc = 0; fc < 8; fc++)
    {
        sphereCenter = sphereCenter + corners[fc];
    }
    sphereCenter = sphereCenter * (1.f/8.f);

    float sphereRadius = 0.f;
    for(int32 fc = 0; fc < 8; fc++)
    {
        sphereRadius = MAX(sphereRadius, magn(corners[fc] - sphereCenter));
    }

    // Make an orthographic frustum that encompasses the entire bounding sphere
    // TODO(caio): Texel snapping
    v3f lightDir = pSceneRenderer->mDirLight.mDir;
    v3f frustumCenter = sphereCenter - (lightDir * sphereRadius * 2.f);

    m4f frustumView = lookAtViewRH(frustumCenter, sphereCenter, {0,1,0});
    m4f frustumProj = orthoRH(-sphereRadius, sphereRadius, -sphereRadius, sphereRadius, -sphereRadius * 6.f, sphereRadius * 6.f);

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
            loadTexture(pAssetManager, pRenderer, texPath, (ImageFormat)texInfo.mFormat, false, &pTex);
            pSceneRenderer->pTexMaterialMaps[t + FALLBACK_TEXTURE_COUNT] = pTex;
        }
    }

    // Load fallback textures
    {
        Texture* pTexFallbackBaseColor = NULL;
        loadTexture(pAssetManager, pRenderer, str("../../res/textures/white.png"),
                FORMAT_RGBA8_SRGB, false, &pTexFallbackBaseColor);

        Texture* pTexFallbackNormal = NULL;
        loadTexture(pAssetManager, pRenderer, str("../../res/textures/flat_normal.png"),
                FORMAT_RGBA8_UNORM, false, &pTexFallbackNormal);

        Texture* pTexFallbackMRS = NULL;
        loadTexture(pAssetManager, pRenderer, str("../../res/textures/black.png"),
                FORMAT_RGBA8_UNORM, false, &pTexFallbackMRS);

        pSceneRenderer->pTexMaterialMaps[FALLBACK_BASECOLOR_INDEX] = pTexFallbackBaseColor;
        pSceneRenderer->pTexMaterialMaps[FALLBACK_NORMAL_INDEX] = pTexFallbackNormal;
        pSceneRenderer->pTexMaterialMaps[FALLBACK_MRS_INDEX] = pTexFallbackMRS;
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

    // // Draw call buffers
    // // Passes:
    // // - Opaque objects
    // // - Double-sided opaque objects
    // Buffer* pDBDrawCmdsOpaque = NULL;
    // Buffer* pDBDrawCmdsOpaqueDoubleSided = NULL;
    // Buffer* pDBDrawCmdCount = NULL;
    // Buffer* pSBPerDraw = NULL;
        BufferDesc vbDesc = {};
        vbDesc.mType = BUFFER_TYPE_VERTEX;
        vbDesc.mSize = ARR_LEN(vertexData) * sizeof(float);
        vbDesc.mCount = ARR_LEN(vertexData) / 4;
        vbDesc.mStride = sizeof(float);     // Should this be * 4?
        addBuffer(pRenderer, vbDesc, &pSceneRenderer->pVBScreenQuad, vertexData);

        BufferDesc ibDesc = {};
        ibDesc.mType = BUFFER_TYPE_INDEX;
        ibDesc.mSize = ARR_LEN(indexData) * sizeof(uint16);
        ibDesc.mCount = ARR_LEN(indexData);
        ibDesc.mStride = sizeof(uint16);
        addBuffer(pRenderer, ibDesc, &pSceneRenderer->pIBScreenQuad, indexData);
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
            addBuffer(pRenderer, vbDesc, &pSceneRenderer->pVBDebug[i]);
        }
    }

    // Geometry vertex/index buffers
    {
        BufferDesc vbDesc = {};
        vbDesc.mType = BUFFER_TYPE_VERTEX;
        vbDesc.mSize = pScene->vertexCount * sizeof(float) * 12;
        vbDesc.mCount = pScene->vertexCount;
        vbDesc.mStride = sizeof(float);     // Should this be * 12?
        addBuffer(pRenderer, vbDesc, &pSceneRenderer->pVBSceneGeometry, pScene->pVertexData);

        BufferDesc ibDesc = {};
        ibDesc.mType = BUFFER_TYPE_INDEX;
        ibDesc.mSize = pScene->indexCount * sizeof(uint16);
        ibDesc.mCount = pScene->indexCount;
        ibDesc.mStride = sizeof(uint16);
        addBuffer(pRenderer, ibDesc, &pSceneRenderer->pIBSceneGeometry, pScene->pIndexData);
    }

    // Scene nodes/meshes buffers
    {
        BufferDesc desc = {};
        desc.mType = BUFFER_TYPE_STORAGE;
        desc.mSize = sizeof(SceneNode) * SCENE_MAX_NODES;
        desc.mCount = SCENE_MAX_NODES;
        desc.mStride = sizeof(SceneNode);
        addBuffer(pRenderer, desc, &pSceneRenderer->pSBSceneNodes, &pScene->mNodes[0]);

        desc.mType = BUFFER_TYPE_STORAGE;
        desc.mSize = sizeof(SceneMesh) * SCENE_MAX_MESHES;
        desc.mCount = SCENE_MAX_MESHES;
        desc.mStride = sizeof(SceneMesh);
        addBuffer(pRenderer, desc, &pSceneRenderer->pSBSceneMeshes, &pScene->mMeshes[0]);

        desc.mType = BUFFER_TYPE_STORAGE;
        desc.mSize = sizeof(SceneMaterial) * SCENE_MAX_MATERIALS;
        desc.mCount = SCENE_MAX_MATERIALS;
        desc.mStride = sizeof(SceneMaterial);
        addBuffer(pRenderer, desc, &pSceneRenderer->pSBSceneMaterials, &pScene->mMaterials[0]);
    }

    // Default samplers
    {
        SamplerDesc desc = {};
        desc.mMinFilter = SAMPLER_FILTER_LINEAR;
        desc.mMagFilter = SAMPLER_FILTER_LINEAR;
        desc.mMipFilter = SAMPLER_FILTER_LINEAR;
        addSampler(pRenderer, desc, &pSceneRenderer->pSamplerLinear);
        desc.mMinFilter = SAMPLER_FILTER_NEAREST;
        desc.mMagFilter = SAMPLER_FILTER_NEAREST;
        desc.mMipFilter = SAMPLER_FILTER_NEAREST;
        addSampler(pRenderer, desc, &pSceneRenderer->pSamplerPoint);
    }

    // GPU draw call buffers
    initDrawBuffers(pSceneRenderer->pRenderer, &pSceneRenderer->mDrawBuffers);

    // Per frame data uniform buffer
    for(uint32 i = 0; i < CONCURRENT_FRAMES; i++)
    {
        BufferDesc desc = {};
        desc.mType = BUFFER_TYPE_UNIFORM;
        desc.mSize = sizeof(PerFrameUniforms);
        desc.mCount = 1;
        desc.mStride = sizeof(PerFrameUniforms);
        addBuffer(pRenderer, desc, &pSceneRenderer->pUBPerFrame[i]);
    }

    // Instance buffers
    for(uint32 i = 0; i < CONCURRENT_FRAMES; i++)
    {
        BufferDesc desc = {};
        desc.mType = BUFFER_TYPE_STORAGE;
        desc.mSize = MAX_DRAWS * sizeof(InstanceData);
        desc.mCount = 1;
        desc.mStride = sizeof(InstanceData);
        addBuffer(pRenderer, desc, &pSceneRenderer->pSBInstancesOpaque[i]);
        addBuffer(pRenderer, desc, &pSceneRenderer->pSBInstancesOpaqueDouble[i]);
        desc.mSize *= MAX_CASCADES;
        addBuffer(pRenderer, desc, &pSceneRenderer->pSBInstancesShadow[i]);
    }

    // App controls
    CameraDesc camDesc = {};
    float fovX = TO_RAD(90.f);
    float aspect = getAspectRatio(pApp);
    camDesc.mFovY = fovHtoV(fovX, aspect);
    camDesc.mAspect = aspect;
    camDesc.mNear = 0.001f;
    camDesc.mFar = 100.f;
    initCamera(
            {0,0,-5}, 
            {0,0,0}, 
            camDesc, 
            &pSceneRenderer->mCamera);

    DirectionalLight light = {};
    light.mDir = {1.f, -0.5f, 0.5f};
    light.mIntensity = 1.f;
    light.mColor = {1,1,1};
    pSceneRenderer->mDirLight = light;
    pSceneRenderer->mAmbient = 0.05f;

    pSceneRenderer->mDebugVerts = array<float>(&pApp->mAppArena, MAX_DEBUG_VERTS);

    initGpuTimer(pRenderer, &pSceneRenderer->mGpuTimer);
}

void destroySceneRenderer(SceneRenderer* pSceneRenderer)
{
    destroyGpuTimer(&pSceneRenderer->mGpuTimer);

    Renderer* pRenderer = pSceneRenderer->pRenderer;

    for(uint32 t = 0; t < pSceneRenderer->mMaterialMapCount; t++)
    {
        removeTexture(pRenderer, &pSceneRenderer->pTexMaterialMaps[t]);
    }

    removeSampler(pRenderer, &pSceneRenderer->pSamplerLinear);
    removeSampler(pRenderer, &pSceneRenderer->pSamplerPoint);
    for(uint32 i = 0; i < CONCURRENT_FRAMES; i++)
    {
        removeBuffer(pRenderer, &pSceneRenderer->pUBPerFrame[i]);
        removeBuffer(pRenderer, &pSceneRenderer->pSBInstancesOpaque[i]);
        removeBuffer(pRenderer, &pSceneRenderer->pSBInstancesOpaqueDouble[i]);
        removeBuffer(pRenderer, &pSceneRenderer->pSBInstancesShadow[i]);
        removeBuffer(pRenderer, &pSceneRenderer->pVBDebug[i]);
    }
    destroyDrawBuffers(pRenderer, &pSceneRenderer->mDrawBuffers);
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
        //desc.mFormat = FORMAT_RGBA32_SFLOAT;
        desc.mFormat = FORMAT_B10G11R11_UFLOAT;
        desc.mClear = {{0,0,0,0}};
        desc.mWidth =   pSceneRenderer->pApp->mWindow.mWidth;
        desc.mHeight =  pSceneRenderer->pApp->mWindow.mHeight;
        addRenderTarget(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pRTAccum);
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
        addDepthTarget(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pRTSceneDepth);
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

            addTexture(pSceneRenderer->pRenderer, hizDesc, &pSceneRenderer->pDepthHierarchyTextures[i]);

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
        desc.mClear = {{0,0,0,0}};
        uint32 w = SHADOW_MAP_SIZE;
        uint32 h = SHADOW_MAP_SIZE;
        desc.mFormat = FORMAT_D32_SFLOAT;
        desc.mClear.mDepth = 0;

        desc.mWidth = w;
        desc.mHeight = h;
        for(int32 i = 0; i < MAX_CASCADES; i++)
        {
            addDepthTarget(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pRTShadowMaps[i]);
        }
    }

    // GBuffer pass RT
    {
        RenderTargetDesc desc = {};
        desc.mFormat = FORMAT_RGBA8_SRGB;
        desc.mClear = {{0,0,0,0}};
        desc.mWidth =   pSceneRenderer->pApp->mWindow.mWidth;
        desc.mHeight =  pSceneRenderer->pApp->mWindow.mHeight;
        addRenderTarget(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pRTGBufferA);
        desc.mFormat = FORMAT_A2RGB10_UNORM;
        addRenderTarget(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pRTGBufferB);
    }

    // Final present RT
    {
        RenderTargetDesc desc = {};
        desc.mFormat = FORMAT_RGBA8_UNORM;
        desc.mClear = {{0,0,0,0}};
        desc.mWidth =   pSceneRenderer->pApp->mWindow.mWidth;
        desc.mHeight =  pSceneRenderer->pApp->mWindow.mHeight;
        addRenderTarget(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pRTPresent);
    }

    // Transitioning render targets so they can be bound to descriptor sets
    {
        CommandBuffer* pCmd = getCmd(pSceneRenderer->pRenderer, true);
        beginCmd(pCmd);

        RenderTargetBarrier barriers[3];
        barriers[0] = {pSceneRenderer->pRTGBufferA,     IMAGE_LAYOUT_UNDEFINED, IMAGE_LAYOUT_GENERAL };
        barriers[1] = {pSceneRenderer->pRTGBufferB,     IMAGE_LAYOUT_UNDEFINED, IMAGE_LAYOUT_GENERAL };
        barriers[2] = {pSceneRenderer->pRTAccum,        IMAGE_LAYOUT_UNDEFINED, IMAGE_LAYOUT_GENERAL };
        cmdRenderTargetBarrier(pCmd, ARR_LEN(barriers), barriers);

        RenderTargetBarrier smBarriers[MAX_CASCADES];
        for(int i = 0; i < MAX_CASCADES; i++)
        {
            smBarriers[i] = {pSceneRenderer->pRTShadowMaps[i], IMAGE_LAYOUT_UNDEFINED, IMAGE_LAYOUT_GENERAL};
        }
        cmdRenderTargetBarrier(pCmd, ARR_LEN(smBarriers), smBarriers);

        TextureBarrier hizBarriers[HIZ_MAX];
        for(uint32 i = 0; i < pSceneRenderer->mDepthHierarchyCount; i++)
        {
            hizBarriers[i] = {pSceneRenderer->pDepthHierarchyTextures[i], IMAGE_LAYOUT_UNDEFINED, IMAGE_LAYOUT_GENERAL };
        }
        cmdTextureBarrier(pCmd, pSceneRenderer->mDepthHierarchyCount, hizBarriers);

        endCmd(pCmd);
        submitImmediateCmd(pSceneRenderer->pRenderer, pCmd);
    }
}

void addSceneShaders(SceneRenderer* pSceneRenderer)
{
    String generateDrawsShaderPath = str("../../res/shaders/generate_draws.glsl");
    String hiZDownsampleShaderPath = str("../../res/shaders/hiz_downsample.glsl");
    String depthPrepassShaderPath = str("../../res/shaders/depth_prepass.glsl");
    String shadowPassShaderPath = str("../../res/shaders/shadow_map_pass.glsl");
    String gbufferShaderPath = str("../../res/shaders/gbuffer.glsl");
    String lightingShaderPath = str("../../res/shaders/lighting.glsl");
    String debugShaderPath = str("../../res/shaders/debug.glsl");
    String tonemappingShaderPath = str("../../res/shaders/tone_mapping.glsl");
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

void addSceneDescriptors(SceneRenderer* pSceneRenderer)
{
    // Scene global descriptor set
    if(!pSceneRenderer->pDSPersistent)
    {
        Texture* storageDepthTextures[HIZ_MAX];
        for(uint32 i = 0; i < pSceneRenderer->mDepthHierarchyCount; i++)
        {
            storageDepthTextures[i] = pSceneRenderer->pDepthHierarchyTextures[i];
        }

        Texture* shadowMapTextures[MAX_CASCADES];
        for(int32 i = 0; i < MAX_CASCADES; i++)
        {
            shadowMapTextures[i] = pSceneRenderer->pRTShadowMaps[i]->pTexture;
        }

        DescriptorSetDesc desc = {};
        desc.mCount = 12;
        // TODO(caio): Buffer arrays?
        desc.mResources[0] = { DESCRIPTOR_STORAGE_BUFFER, pSceneRenderer->pSBSceneNodes, 1 };
        desc.mResources[1] = { DESCRIPTOR_STORAGE_BUFFER, pSceneRenderer->pSBSceneMeshes, 1 };
        desc.mResources[2] = { DESCRIPTOR_STORAGE_BUFFER, pSceneRenderer->pSBSceneMaterials, 1 };
        desc.mResources[3] = { DESCRIPTOR_TEXTURE, pSceneRenderer->pTexMaterialMaps, 
            pSceneRenderer->mMaterialMapCount, 
            SCENE_MAX_TEXTURES };
        desc.mResources[4] = { DESCRIPTOR_SAMPLER, pSceneRenderer->pSamplerLinear, 1 };
        desc.mResources[5] = { DESCRIPTOR_SAMPLER, pSceneRenderer->pSamplerPoint, 1 };
        desc.mResources[6] = { DESCRIPTOR_TEXTURE, pSceneRenderer->pRTGBufferA->pTexture, 1 };
        desc.mResources[7] = { DESCRIPTOR_TEXTURE, pSceneRenderer->pRTGBufferB->pTexture, 1 };
        desc.mResources[8] = { DESCRIPTOR_TEXTURE, pSceneRenderer->pRTAccum->pTexture, 1 };
        desc.mResources[9] = { DESCRIPTOR_TEXTURE, pSceneRenderer->pRTSceneDepth->pTexture, 1 };
        desc.mResources[10] = { DESCRIPTOR_STORAGE_IMAGE, storageDepthTextures,
            pSceneRenderer->mDepthHierarchyCount,
            HIZ_MAX};
        desc.mResources[11] = { DESCRIPTOR_TEXTURE, shadowMapTextures,
            MAX_CASCADES, MAX_CASCADES};
        addDescriptorSet(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pDSPersistent);
    }

    // Per frame resource set
    if(!pSceneRenderer->pDSPerFrame[0])
    {
        for(uint32 i = 0; i < CONCURRENT_FRAMES; i++)
        {
            DescriptorSetDesc desc = {};
            desc.mCount = 6;
            desc.mResources[0] = { DESCRIPTOR_UNIFORM_BUFFER, pSceneRenderer->pUBPerFrame[i], 1 };
            desc.mResources[1] = { DESCRIPTOR_STORAGE_BUFFER, pSceneRenderer->mDrawBuffers.pDrawBuffers[i], 1 };
            desc.mResources[2] = { DESCRIPTOR_STORAGE_BUFFER, pSceneRenderer->mDrawBuffers.pDrawCountBuffers[i], 1 };
            desc.mResources[3] = { DESCRIPTOR_STORAGE_BUFFER, pSceneRenderer->pSBInstancesOpaque[i], 1 };
            desc.mResources[4] = { DESCRIPTOR_STORAGE_BUFFER, pSceneRenderer->pSBInstancesOpaqueDouble[i], 1 };
            desc.mResources[5] = { DESCRIPTOR_STORAGE_BUFFER, pSceneRenderer->pSBInstancesShadow[i], 1 };
            addDescriptorSet(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pDSPerFrame[i]);
        }
    }
}

void addScenePipelines(SceneRenderer* pSceneRenderer)
{
    // Shadow pass pipeline
    {
        GraphicsPipelineDesc desc = {};
        desc.mRenderTargetCount = 0;
        desc.mDepthTargetFormat = pSceneRenderer->pRTShadowMaps[0]->mDesc.mFormat;

        desc.mVertexLayout = pSceneRenderer->mVLSceneGeometry;
        desc.pVS = pSceneRenderer->pVSShadowMapPass;
        desc.pFS = pSceneRenderer->pPSShadowMapPass;

        desc.mCullMode = CULL_MODE_BACK;
        desc.mFrontFace = FRONT_FACE_CCW;

        desc.mDepthTest = true;
        desc.mDepthWrite = true;
        desc.mDepthOp = COMPARE_GREATER;

        desc.mDescriptorSetCount = 2;
        desc.pDescriptorSets[0] = pSceneRenderer->pDSPersistent;
        desc.pDescriptorSets[1] = pSceneRenderer->pDSPerFrame[0];

        // Constants:
        // - Active frame (uint32)
        // - Current cascade
        desc.mConstantBlockCount = 1;
        desc.mConstantBlocks[0].mShaderTypes = SHADER_TYPE_VERT | SHADER_TYPE_FRAG;
        desc.mConstantBlocks[0].mSize = sizeof(uint32) * 2;

        if(!pSceneRenderer->pPipeShadowMapPass)
        {
            addPipeline(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pPipeShadowMapPass);
        }

        desc.mCullMode = CULL_MODE_NONE;
        desc.pVS = pSceneRenderer->pVSShadowMapPassDoubleSided;
        desc.pFS = pSceneRenderer->pPSShadowMapPassDoubleSided;
        if(!pSceneRenderer->pPipeShadowMapPassDoubleSided)
        {
            addPipeline(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pPipeShadowMapPassDoubleSided);
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

        desc.mDescriptorSetCount = 2;
        desc.pDescriptorSets[0] = pSceneRenderer->pDSPersistent;
        desc.pDescriptorSets[1] = pSceneRenderer->pDSPerFrame[0];

        // Constants:
        // - Active frame (uint32)
        desc.mConstantBlockCount = 1;
        desc.mConstantBlocks[0].mShaderTypes = SHADER_TYPE_VERT | SHADER_TYPE_FRAG;
        desc.mConstantBlocks[0].mSize = sizeof(uint32);

        if(!pSceneRenderer->pPipeDepthPrePass)
        {
            addPipeline(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pPipeDepthPrePass);
        }

        desc.mCullMode = CULL_MODE_NONE;
        desc.pVS = pSceneRenderer->pVSDepthPrePassDoubleSided;
        desc.pFS = pSceneRenderer->pPSDepthPrePassDoubleSided;
        if(!pSceneRenderer->pPipeDepthPrePassDoubleSided)
        {
            addPipeline(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pPipeDepthPrePassDoubleSided);
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

        desc.mDescriptorSetCount = 2;
        desc.pDescriptorSets[0] = pSceneRenderer->pDSPersistent;
        desc.pDescriptorSets[1] = pSceneRenderer->pDSPerFrame[0];

        // Constants:
        // - Active frame (uint32)
        desc.mConstantBlockCount = 1;
        desc.mConstantBlocks[0].mShaderTypes = SHADER_TYPE_VERT | SHADER_TYPE_FRAG;
        desc.mConstantBlocks[0].mSize = sizeof(uint32);

        if(!pSceneRenderer->pPipeGBuffer)
        {
            addPipeline(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pPipeGBuffer);
        }

        desc.mCullMode = CULL_MODE_NONE;
        desc.pVS = pSceneRenderer->pVSGBufferDoubleSided;
        desc.pFS = pSceneRenderer->pPSGBufferDoubleSided;
        if(!pSceneRenderer->pPipeGBufferDoubleSided)
        {
            addPipeline(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pPipeGBufferDoubleSided);
        }
    }

    if(!pSceneRenderer->pPipeGenerateDraws)
    {
        ComputePipelineDesc desc = {};

        desc.pCS = pSceneRenderer->pCSGenerateDraws;

        desc.mDescriptorSetCount = 2;
        desc.pDescriptorSets[0] = pSceneRenderer->pDSPersistent;
        desc.pDescriptorSets[1] = pSceneRenderer->pDSPerFrame[0];

        // Constants:
        // - Total node count (uint32)
        // - Active frame (uint32)
        desc.mConstantBlockCount = 1;
        desc.mConstantBlocks[0].mShaderTypes = SHADER_TYPE_COMP;
        desc.mConstantBlocks[0].mSize = sizeof(uint32) * 2;

        addPipeline(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pPipeGenerateDraws);

        if(!pSceneRenderer->pPipeGenerateDrawsShadowMap)
        {
            desc.pCS = pSceneRenderer->pCSGenerateDrawsShadowMap;
            addPipeline(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pPipeGenerateDrawsShadowMap);
        }
    }

    if(!pSceneRenderer->pPipeHiZDownsample)
    {
        ComputePipelineDesc desc = {};

        desc.pCS = pSceneRenderer->pCSHiZDownsample;

        desc.mDescriptorSetCount = 2;
        desc.pDescriptorSets[0] = pSceneRenderer->pDSPersistent;
        desc.pDescriptorSets[1] = pSceneRenderer->pDSPerFrame[0];

        // Constants:
        // - Base mip (uint32)
        // - Mips to generate (uint32)
        desc.mConstantBlockCount = 1;
        desc.mConstantBlocks[0].mShaderTypes = SHADER_TYPE_COMP;
        desc.mConstantBlocks[0].mSize = sizeof(uint32) * 2;

        addPipeline(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pPipeHiZDownsample);
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

        desc.mDescriptorSetCount = 2;
        desc.pDescriptorSets[0] = pSceneRenderer->pDSPersistent;
        desc.pDescriptorSets[1] = pSceneRenderer->pDSPerFrame[0];

        // Constants:
        // - Active frame (uint32)
        desc.mConstantBlockCount = 1;
        desc.mConstantBlocks[0].mShaderTypes = SHADER_TYPE_VERT | SHADER_TYPE_FRAG;
        desc.mConstantBlocks[0].mSize = sizeof(uint32) * 2;

        addPipeline(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pPipeLighting);
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

        desc.mDescriptorSetCount = 2;
        desc.pDescriptorSets[0] = pSceneRenderer->pDSPersistent;
        desc.pDescriptorSets[1] = pSceneRenderer->pDSPerFrame[0];

        // Constants:
        // - Active frame (uint32)
        desc.mConstantBlockCount = 1;
        desc.mConstantBlocks[0].mShaderTypes = SHADER_TYPE_VERT | SHADER_TYPE_FRAG;
        desc.mConstantBlocks[0].mSize = sizeof(uint32) * 2;

        addPipeline(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pPipeDebug);
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

        desc.mDescriptorSetCount = 2;
        desc.pDescriptorSets[0] = pSceneRenderer->pDSPersistent;
        desc.pDescriptorSets[1] = pSceneRenderer->pDSPerFrame[0];

        // Constants:
        desc.mConstantBlockCount = 0;

        addPipeline(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pPipeTonemapping);
    }
}

void removeSceneRenderTargets(SceneRenderer* pSceneRenderer)
{
    removeRenderTarget(pSceneRenderer->pRenderer, &pSceneRenderer->pRTPresent);
    removeRenderTarget(pSceneRenderer->pRenderer, &pSceneRenderer->pRTAccum);
    removeRenderTarget(pSceneRenderer->pRenderer, &pSceneRenderer->pRTGBufferA);
    removeRenderTarget(pSceneRenderer->pRenderer, &pSceneRenderer->pRTGBufferB);
    removeRenderTarget(pSceneRenderer->pRenderer, &pSceneRenderer->pRTSceneDepth);
    for(int32 i = 0; i < MAX_CASCADES; i++)
    {
        removeRenderTarget(pSceneRenderer->pRenderer, &pSceneRenderer->pRTShadowMaps[i]);
    }
    for(int32 i = 1; i < pSceneRenderer->mDepthHierarchyCount; i++)
    {
        removeTexture(pSceneRenderer->pRenderer, &pSceneRenderer->pDepthHierarchyTextures[i]);
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

void removeSceneDescriptors(SceneRenderer* pSceneRenderer)
{
    if(pSceneRenderer->pDSPersistent)
        removeDescriptorSet(pSceneRenderer->pRenderer, &pSceneRenderer->pDSPersistent);
    if(pSceneRenderer->pDSPerFrame[0])
    {
        for(uint32 i = 0; i < CONCURRENT_FRAMES; i++)
        {
            removeDescriptorSet(pSceneRenderer->pRenderer, &pSceneRenderer->pDSPerFrame[i]);
        }
    }
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

void updatePerFrameUniforms(SceneRenderer* pSceneRenderer)
{
    m4f cameraView = getView(&pSceneRenderer->mCamera);
    m4f cameraProj = getProj(&pSceneRenderer->mCamera);
    pSceneRenderer->perFrameUniforms.mView = cameraView;
    pSceneRenderer->perFrameUniforms.mProj = cameraProj;
    pSceneRenderer->perFrameUniforms.mCamWorldPos = to4f(pSceneRenderer->mCamera.mPos, 1);
    if(!pSceneRenderer->mFreezeMainCam)
    {
        pSceneRenderer->perFrameUniforms.mMainView = cameraView;
        pSceneRenderer->perFrameUniforms.mMainProj = cameraProj;
    }

    float cascadeSplitDistances[MAX_CASCADES];
    getCascadeDistances(pSceneRenderer, &pSceneRenderer->mCamera, cascadeSplitDistances);
    for(int32 i = 0; i < MAX_CASCADES; i++)
    {
        m4f cascadeViewProj = getCascadeViewProj(pSceneRenderer, &pSceneRenderer->mCamera, cascadeSplitDistances, i);
        pSceneRenderer->perFrameUniforms.mShadowCascadesViewProj[i] = cascadeViewProj;
    }

    DirectionalLight light = pSceneRenderer->mDirLight;
    pSceneRenderer->perFrameUniforms.mDirLight1 = to4f(normalize(light.mDir), light.mIntensity);
    pSceneRenderer->perFrameUniforms.mDirLight2 = to4f(light.mColor, pSceneRenderer->mAmbient);

    memcpy(pSceneRenderer->perFrameUniforms.mShadowCascadeDistances.mData, cascadeSplitDistances, MAX_CASCADES * sizeof(float));
}

void uploadPerFrameUniforms(SceneRenderer* pSceneRenderer)
{
    Renderer* pRenderer = pSceneRenderer->pRenderer;
    uint32 activeFrame = pRenderer->mActiveFrame;
    copyToBuffer(pRenderer, 
            pSceneRenderer->pUBPerFrame[activeFrame], 
            0, 
            &pSceneRenderer->perFrameUniforms, 
            sizeof(PerFrameUniforms));
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
    uiSeparator(str("Camera Settings"));
    uiDragf(str("Near Plane"), &pSceneRenderer->mCamera.mDesc.mNear, 0.1f, 0.0001f, 10.f);
    uiDragf(str("Far Plane"), &pSceneRenderer->mCamera.mDesc.mFar, 1.f, 10.f, 100.f);
    // Controls are for fovX, but camera stores fovY
    static float fovX = 90.f;
    float aspect = getAspectRatio(pSceneRenderer->pApp);
    uiDragf(str("FoV (X)"), &fovX, 1.f, 30.f, 150.f);
    pSceneRenderer->mCamera.mDesc.mFovY = fovHtoV(TO_RAD(fovX), aspect);

    uiSeparator(str("Light Settings"));
    uiSlider3f(str("Direction"), 
            &pSceneRenderer->mDirLight.mDir.mData[0], 
            -1.f, 1.f);
    uiSliderf(str("Intensity"), &pSceneRenderer->mDirLight.mIntensity, 0.f, 10.f);
    uiColor3f(str("Color"), 
            &pSceneRenderer->mDirLight.mColor.mData[0]);
    uiSliderf(str("Ambient"), &pSceneRenderer->mAmbient, 0.f, 1.f);

    uiSeparator(str("Shadow Settings"));
    uiSliderf(str("Cascade Split Factor"), &pSceneRenderer->mShadowSettings.kSplitFactor, 0.f, 1.f);
    static bool showShadowMaps = false;
    uiCheckbox(str("Show Shadow Maps"), &showShadowMaps);
    if(showShadowMaps)
    {
        uiStartWindow(str("Shadow Maps"), -400, 0, 400, 0);
        for(int32 i = 0; i < MAX_CASCADES; i++)
        {
            Texture* pTex = pSceneRenderer->pRTShadowMaps[i]->pTexture;
            Sampler* pSampler = pSceneRenderer->pSamplerPoint;
            uiImage(pSceneRenderer->pUI, pTex, pSampler, 256, 256);
        }
        uiEndWindow();
    }

    uiSeparator(str("Profiling"));
    static bool showGpuTimings = false;
    uiCheckbox(str("Show GPU Timings"), &showGpuTimings);
    if(showGpuTimings)
    {
        uiGpuTimingsWindow(&pSceneRenderer->pApp->mAppArena, &pSceneRenderer->mGpuTimer, 
                -400, 0, 400, 0);
    }
}

#define RENDERER_SCOPE_BEGIN(NAME) cmdScopeBegin(pRenderer, pCmd, str(NAME))
#define RENDERER_SCOPE_END() cmdScopeEnd(pRenderer, pCmd)
void renderScene(SceneRenderer* pSceneRenderer, uint32 frame)
{
    PROFILE_SCOPE;
    Renderer* pRenderer = pSceneRenderer->pRenderer;
    acquireNextImage(pRenderer, frame);

    CommandBuffer* pCmd = getCmd(pRenderer);
    beginCmd(pCmd);

    // Start GPU timings
    uint32 activeFrame = pRenderer->mActiveFrame; 
    GpuTimestampParams gpuTimerParams = {};
    gpuTimerParams.pGpuTimer = &pSceneRenderer->mGpuTimer;
    gpuTimerParams.pCmd = pCmd;
    gpuTimerParams.queryPool = activeFrame;
    gpuTimerReadResults(&gpuTimerParams);
    gpuTimerStart(&gpuTimerParams);

    // Upload per frame data
    uploadPerFrameUniforms(pSceneRenderer);
    gpuTimestamp(str("Upload PerFrame"), &gpuTimerParams);

    cmdPrepareDrawBuffers(pCmd, &pSceneRenderer->mDrawBuffers, activeFrame);

    DescriptorSet* pDSPersistent = pSceneRenderer->pDSPersistent;
    DescriptorSet* pDSPerFrame = pSceneRenderer->pDSPerFrame[activeFrame];

    debugGeometryStart(pSceneRenderer);

    // CSM draw call generation
    {
        RENDERER_SCOPE_BEGIN("Populate Draws (Shadows)");
        ComputePipeline* pPipeline = pSceneRenderer->pPipeGenerateDrawsShadowMap;

        cmdBindComputePipeline(pCmd, pPipeline);
        cmdBindDescriptorSet(pCmd, pPipeline, pDSPersistent, 0);
        cmdBindDescriptorSet(pCmd, pPipeline, pDSPerFrame, 1);

        uint32 drawBufferConstants[2];
        drawBufferConstants[0] = pSceneRenderer->pScene->mNodeCount;
        drawBufferConstants[1] = activeFrame;

        cmdSetConstants(pCmd, pPipeline, 0, ARR_SIZE(drawBufferConstants), &drawBufferConstants[0]);

        cmdDispatch(pCmd, SCENE_MAX_NODES / 32, 1, 1);
        
        char buf[256];
        String tsName = strf(buf, "Draw Buffer Pass (Shadows)");
        gpuTimestamp(tsName, &gpuTimerParams);
        RENDERER_SCOPE_END();
    }

    // Cascaded Shadow Map pass
    {
        RENDERER_SCOPE_BEGIN("Shadow Pass");
        Barrier barrier = {};
        barrier.mSrcStage = PIPELINE_STAGE_COMPUTE_SHADER;
        barrier.mDstStage = PIPELINE_STAGE_DRAW_INDIRECT;
        barrier.mSrcAccess = MEMORY_ACCESS_SHADER_WRITE;
        barrier.mDstAccess = MEMORY_ACCESS_INDIRECT_READ;
        cmdBarrier(pCmd, 1, &barrier);

        for(int32 i = 0; i < MAX_CASCADES; i++)
        {
            RenderTarget* pRTDepth = pSceneRenderer->pRTShadowMaps[i];
            RenderTargetBarrier barriers[1];
            barriers[0] = {pRTDepth, getImageLayout(pRTDepth), IMAGE_LAYOUT_DEPTH_STENCIL_OUTPUT };
            cmdRenderTargetBarrier(pCmd, ARR_LEN(barriers), barriers);

            RenderTargetBindDesc bindDesc = {};
            bindDesc.mColorCount = 0;
            bindDesc.mDepthBinding = { pRTDepth, LOAD_OP_CLEAR, STORE_OP_STORE };
            cmdBindRenderTargets(pCmd, bindDesc);

            GraphicsPipeline* pPipeline = pSceneRenderer->pPipeShadowMapPass;

            cmdBindGraphicsPipeline(pCmd, pPipeline);
            cmdBindDescriptorSet(pCmd, pPipeline, pDSPersistent, 0);
            cmdBindDescriptorSet(pCmd, pPipeline, pDSPerFrame, 1);

            cmdSetViewport(pCmd, pRTDepth);
            cmdSetScissor(pCmd, pRTDepth);

            cmdBindVertexBuffer(pCmd, pSceneRenderer->pVBSceneGeometry);
            cmdBindIndexBuffer(pCmd, pSceneRenderer->pIBSceneGeometry);

            uint32 constants[2];
            constants[0] = pRenderer->mActiveFrame;
            constants[1] = i;
            cmdSetConstants(pCmd, pPipeline, 0, ARR_SIZE(constants), &constants);

            cmdDrawIndirectBuffer(pCmd, &pSceneRenderer->mDrawBuffers, DB_SHADOW_0 + i, activeFrame);

            // TODO(caio): Reenable double sided shadow pass after draw buffers are working

            cmdUnbindRenderTargets(pCmd);

            barriers[0] = {pRTDepth, getImageLayout(pRTDepth), IMAGE_LAYOUT_SHADER_READ_ONLY };
            cmdRenderTargetBarrier(pCmd, ARR_LEN(barriers), barriers);

            char buf[256];
            String tsName = strf(buf, "Shadow Draw Pass (Cascade %d)", i);
            gpuTimestamp(tsName, &gpuTimerParams);
        }
        RENDERER_SCOPE_END();
    }

    // Hierarchical Z Downsampling pass
    if(!pSceneRenderer->mFreezeMainCam)
    {
        RENDERER_SCOPE_BEGIN("Hi-Z Downsample");
        ComputePipeline* pPipeline = pSceneRenderer->pPipeHiZDownsample;

        cmdBindComputePipeline(pCmd, pPipeline);
        cmdBindDescriptorSet(pCmd, pPipeline, pDSPersistent, 0);
        cmdBindDescriptorSet(pCmd, pPipeline, pDSPerFrame, 1);

        v2u size = { 
            pSceneRenderer->pRTSceneDepth->mDesc.mWidth, 
            pSceneRenderer->pRTSceneDepth->mDesc.mHeight, 
        };

        uint32 constants[2];
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

            constants[0] = baseMip;
            constants[1] = mipCount;
            cmdSetConstants(pCmd, pPipeline, 0, ARR_SIZE(constants), &constants);

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

        gpuTimestamp(str("Hi-Z Downsample Pass"), &gpuTimerParams);

        Barrier barrier = {};
        barrier.mSrcStage = PIPELINE_STAGE_COMPUTE_SHADER;
        barrier.mDstStage = PIPELINE_STAGE_COMPUTE_SHADER;
        barrier.mSrcAccess = MEMORY_ACCESS_SHADER_WRITE;
        barrier.mDstAccess = MEMORY_ACCESS_SHADER_READ;
        cmdBarrier(pCmd, 1, &barrier);
        RENDERER_SCOPE_END();
    }

    // Generate draws pass
    {
        RENDERER_SCOPE_BEGIN("Populate Draws (Opaque)");
        Barrier barrier = {};
        barrier.mSrcStage = PIPELINE_STAGE_DRAW_INDIRECT;
        barrier.mDstStage = PIPELINE_STAGE_TRANSFER;
        barrier.mSrcAccess = MEMORY_ACCESS_INDIRECT_READ;
        barrier.mDstAccess = MEMORY_ACCESS_TRANSFER_WRITE;
        cmdBarrier(pCmd, 1, &barrier);

        ComputePipeline* pPipeline = pSceneRenderer->pPipeGenerateDraws;

        cmdBindComputePipeline(pCmd, pPipeline);
        cmdBindDescriptorSet(pCmd, pPipeline, pDSPersistent, 0);
        cmdBindDescriptorSet(pCmd, pPipeline, pDSPerFrame, 1);

        uint32 constants[2];
        constants[0] = pSceneRenderer->pScene->mNodeCount;
        constants[1] = pRenderer->mActiveFrame;
        cmdSetConstants(pCmd, pPipeline, 0, ARR_SIZE(constants), &constants[0]);

        cmdDispatch(pCmd, SCENE_MAX_NODES / 32, 1, 1);

        gpuTimestamp(str("Generate Draws Pass"), &gpuTimerParams);

        barrier = {};
        barrier.mSrcStage = PIPELINE_STAGE_COMPUTE_SHADER;
        barrier.mDstStage = PIPELINE_STAGE_DRAW_INDIRECT;
        barrier.mSrcAccess = MEMORY_ACCESS_SHADER_WRITE;
        barrier.mDstAccess = MEMORY_ACCESS_INDIRECT_READ;
        cmdBarrier(pCmd, 1, &barrier);
        RENDERER_SCOPE_END();
    }

    // Depth pre pass
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
        cmdBindDescriptorSet(pCmd, pPipeline, pDSPersistent, 0);
        cmdBindDescriptorSet(pCmd, pPipeline, pDSPerFrame, 1);

        cmdSetViewport(pCmd, pRTDepth);
        cmdSetScissor(pCmd, pRTDepth);

        cmdBindVertexBuffer(pCmd, pSceneRenderer->pVBSceneGeometry);
        cmdBindIndexBuffer(pCmd, pSceneRenderer->pIBSceneGeometry);

        uint32 constants[1];
        constants[0] = pRenderer->mActiveFrame;
        cmdSetConstants(pCmd, pPipeline, 0, sizeof(uint32), &constants);

        // Opaque
        cmdDrawIndirectBuffer(pCmd, &pSceneRenderer->mDrawBuffers, DB_GBUFFER_OPAQUE, activeFrame);

        pPipeline = pSceneRenderer->pPipeDepthPrePassDoubleSided;
        cmdBindGraphicsPipeline(pCmd, pPipeline);

        // Double-sided opaque
        cmdDrawIndirectBuffer(pCmd, &pSceneRenderer->mDrawBuffers, DB_GBUFFER_OPAQUE_DOUBLE, activeFrame);

        cmdUnbindRenderTargets(pCmd);
        gpuTimestamp(str("Depth Pre-pass"), &gpuTimerParams);
        RENDERER_SCOPE_END();
    }

    // GBuffer render pass
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
        cmdBindDescriptorSet(pCmd, pPipeline, pDSPersistent, 0);
        cmdBindDescriptorSet(pCmd, pPipeline, pDSPerFrame, 1);

        cmdSetViewport(pCmd, pRTGBufferA);
        cmdSetScissor(pCmd, pRTGBufferA);

        cmdBindVertexBuffer(pCmd, pSceneRenderer->pVBSceneGeometry);
        cmdBindIndexBuffer(pCmd, pSceneRenderer->pIBSceneGeometry);

        uint32 constants[1];
        constants[0] = pRenderer->mActiveFrame;
        cmdSetConstants(pCmd, pPipeline, 0, sizeof(uint32), &constants);

        // Opaque
        cmdDrawIndirectBuffer(pCmd, &pSceneRenderer->mDrawBuffers, DB_GBUFFER_OPAQUE, activeFrame);

        pPipeline = pSceneRenderer->pPipeGBufferDoubleSided;
        cmdBindGraphicsPipeline(pCmd, pPipeline);

        // Double-sided opaque
        cmdDrawIndirectBuffer(pCmd, &pSceneRenderer->mDrawBuffers, DB_GBUFFER_OPAQUE_DOUBLE, activeFrame);

        cmdUnbindRenderTargets(pCmd);
        gpuTimestamp(str("GBuffer Pass"), &gpuTimerParams);
        RENDERER_SCOPE_END();
    }

    // Lighting pass
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
        cmdBindDescriptorSet(pCmd, pPipeline, pDSPersistent, 0);
        cmdBindDescriptorSet(pCmd, pPipeline, pDSPerFrame, 1);

        cmdSetViewport(pCmd, pRTAccum);
        cmdSetScissor(pCmd, pRTAccum);

        cmdBindVertexBuffer(pCmd, pSceneRenderer->pVBScreenQuad);
        cmdBindIndexBuffer(pCmd, pSceneRenderer->pIBScreenQuad);

        uint32 constants[1];
        constants[0] = pRenderer->mActiveFrame;
        cmdSetConstants(pCmd, pPipeline, 0, sizeof(uint32), &constants);

        cmdDrawIndexed(pCmd, 
                3, 1, 0, 0);

        cmdUnbindRenderTargets(pCmd);
        gpuTimestamp(str("Lighting Pass"), &gpuTimerParams);
        RENDERER_SCOPE_END();
    }

    // Debug geometry pass
    debugGeometryEnd(pSceneRenderer);
    if(pSceneRenderer->mDebugVerts.mCount)
    {
        RENDERER_SCOPE_BEGIN("Debug Geometry");
        RenderTarget* pRTAccum = pSceneRenderer->pRTAccum;

        GraphicsPipeline* pPipeline = pSceneRenderer->pPipeDebug;

        RenderTargetBindDesc bindDesc = {};
        bindDesc.mColorCount = 1;
        bindDesc.mColorBindings[0] = { pRTAccum, LOAD_OP_LOAD, STORE_OP_STORE };
        cmdBindRenderTargets(pCmd, bindDesc);

        cmdBindGraphicsPipeline(pCmd, pPipeline);
        cmdBindDescriptorSet(pCmd, pPipeline, pDSPersistent, 0);
        cmdBindDescriptorSet(pCmd, pPipeline, pDSPerFrame, 1);

        cmdSetViewport(pCmd, pRTAccum);
        cmdSetScissor(pCmd, pRTAccum);

        cmdBindVertexBuffer(pCmd, pSceneRenderer->pVBDebug[activeFrame]);

        uint32 constants[1];
        constants[0] = pRenderer->mActiveFrame;
        cmdSetConstants(pCmd, pPipeline, 0, sizeof(uint32), &constants);

        cmdDraw(pCmd, pSceneRenderer->mDebugVerts.mCount / 6, 1);

        cmdUnbindRenderTargets(pCmd);
        gpuTimestamp(str("Debug Pass"), &gpuTimerParams);
        RENDERER_SCOPE_END();
    }

    // Tone mapping pass
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
        cmdBindDescriptorSet(pCmd, pPipeline, pDSPersistent, 0);
        cmdBindDescriptorSet(pCmd, pPipeline, pDSPerFrame, 1);

        cmdSetViewport(pCmd, pRTPresent);
        cmdSetScissor(pCmd, pRTPresent);

        cmdBindVertexBuffer(pCmd, pSceneRenderer->pVBScreenQuad);
        cmdBindIndexBuffer(pCmd, pSceneRenderer->pIBScreenQuad);

        cmdDrawIndexed(pCmd, 
                3, 1, 0, 0);

        gpuTimestamp(str("Tone Mapping"), &gpuTimerParams);
        RENDERER_SCOPE_END();
    }

    // UI pass
    {
        RENDERER_SCOPE_BEGIN("UI Pass");
        RenderTarget* pRTColor = pSceneRenderer->pRTPresent;
        RenderTargetBindDesc bindDesc = {};
        bindDesc.mColorCount = 1;
        bindDesc.mColorBindings[0] = { pRTColor, LOAD_OP_LOAD, STORE_OP_STORE };

        uiStartFrame();
        addUIControls(pSceneRenderer);
        uiEndFrame(pCmd, bindDesc);

        gpuTimestamp(str("UI pass"), &gpuTimerParams);
        RENDERER_SCOPE_END();
    }

    // Transitioning depth buffer back to general, in case descriptors reload
    {
        RenderTarget* pRTDepth = pSceneRenderer->pRTSceneDepth;
        RenderTargetBarrier barriers[1];
        barriers[0] = { pRTDepth, getImageLayout(pRTDepth), IMAGE_LAYOUT_GENERAL };
        cmdRenderTargetBarrier(pCmd, ARR_LEN(barriers), barriers);

    }

    // Copy to swap chain
    {
        RenderTarget* pRTPresent = pSceneRenderer->pRTPresent;
        RenderTargetBarrier barrier = {pRTPresent, IMAGE_LAYOUT_COLOR_OUTPUT, IMAGE_LAYOUT_TRANSFER_SRC };
        cmdRenderTargetBarrier(pCmd, 1, &barrier);
        cmdCopyToSwapChain(pCmd, &pRenderer->mSwapChain, pRTPresent->pTexture);
        gpuTimestamp(str("Swap Chain copy"), &gpuTimerParams);
    }

    endCmd(pCmd);
    submitFrameCmd(pRenderer, pCmd);
    present(pRenderer);
}
