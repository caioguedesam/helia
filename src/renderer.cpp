#include "renderer.hpp"
#include "../dw/src/core/profile.hpp"
#include "../dw/src/asset/asset.hpp"
#include "../dw/src/render/render.hpp"
#include "../dw/src/render/ui.hpp"
#include "../dw/src/render/texture.hpp"
#include "../dw/src/core/base.hpp"
#include "dw/src/math/math.hpp"
#include "dw/src/math/volumes.hpp"
#include "dw/src/render/buffer.hpp"
#include "dw/src/render/camera.hpp"
#include "dw/src/render/shader.hpp"

void initSceneRenderer(SceneRenderer* pSceneRenderer,
        App* pApp, Renderer* pRenderer, AssetManager* pAssetManager,
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
        addBuffer(pRenderer, vbDesc, &pSceneRenderer->pVBDebug);
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
    {
        BufferDesc desc = {};
        desc.mType = BUFFER_TYPE_INDIRECT;
        desc.mSize = sizeof(IndirectDraw) * SCENE_MAX_DRAWS;
        desc.mCount = SCENE_MAX_DRAWS;
        desc.mStride = sizeof(IndirectDraw);
        addBuffer(pRenderer, desc, &pSceneRenderer->pDBDrawCmdsOpaque);
        addBuffer(pRenderer, desc, &pSceneRenderer->pDBDrawCmdsOpaqueDoubleSided);

        desc.mType = BUFFER_TYPE_INDIRECT;
        desc.mSize = sizeof(uint32) * 2;
        desc.mCount = 2;
        desc.mStride = sizeof(uint32);
        addBuffer(pRenderer, desc, &pSceneRenderer->pDBDrawCmdCount);

        desc.mType = (BufferType)(BUFFER_TYPE_STORAGE | BUFFER_TYPE_TRANSFER_DST);
        desc.mSize = sizeof(PerDrawData) * SCENE_MAX_DRAWS;
        desc.mCount = SCENE_MAX_DRAWS;
        desc.mStride = sizeof(PerDrawData);
        addBuffer(pRenderer, desc, &pSceneRenderer->pSBPerDraw);
    }

    // Per frame data uniform buffer
    {
        BufferDesc desc = {};
        desc.mType = BUFFER_TYPE_UNIFORM;
        desc.mSize = sizeof(PerFrameUniforms) * 2;
        desc.mCount = 1;
        desc.mStride = sizeof(PerFrameUniforms);
        addBuffer(pRenderer, desc, &pSceneRenderer->pUBPerFrame);
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
    removeBuffer(pRenderer, &pSceneRenderer->pUBPerFrame);
    removeBuffer(pRenderer, &pSceneRenderer->pSBPerDraw);
    removeBuffer(pRenderer, &pSceneRenderer->pDBDrawCmdCount);
    removeBuffer(pRenderer, &pSceneRenderer->pDBDrawCmdsOpaqueDoubleSided);
    removeBuffer(pRenderer, &pSceneRenderer->pDBDrawCmdsOpaque);
    removeBuffer(pRenderer, &pSceneRenderer->pVBScreenQuad);
    removeBuffer(pRenderer, &pSceneRenderer->pIBScreenQuad);
    removeBuffer(pRenderer, &pSceneRenderer->pVBDebug);
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
    if(!pSceneRenderer->pDSGlobal)
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
        desc.mCount = 16;
        // TODO(caio): Buffer arrays?
        desc.mResources[0] = { DESCRIPTOR_STORAGE_BUFFER, pSceneRenderer->pDBDrawCmdsOpaque, 1 };
        desc.mResources[1] = { DESCRIPTOR_STORAGE_BUFFER, pSceneRenderer->pDBDrawCmdsOpaqueDoubleSided, 1 };
        desc.mResources[2] = { DESCRIPTOR_STORAGE_BUFFER, pSceneRenderer->pDBDrawCmdCount, 1 };
        desc.mResources[3] = { DESCRIPTOR_STORAGE_BUFFER, pSceneRenderer->pSBPerDraw, 1 };
        desc.mResources[4] = { DESCRIPTOR_STORAGE_BUFFER, pSceneRenderer->pSBSceneNodes, 1 };
        desc.mResources[5] = { DESCRIPTOR_STORAGE_BUFFER, pSceneRenderer->pSBSceneMeshes, 1 };
        desc.mResources[6] = { DESCRIPTOR_STORAGE_BUFFER, pSceneRenderer->pSBSceneMaterials, 1 };
        desc.mResources[7] = { DESCRIPTOR_TEXTURE, pSceneRenderer->pTexMaterialMaps, 
            pSceneRenderer->mMaterialMapCount, 
            SCENE_MAX_TEXTURES };
        desc.mResources[8] = { DESCRIPTOR_SAMPLER, pSceneRenderer->pSamplerLinear, 1 };
        desc.mResources[9] = { DESCRIPTOR_SAMPLER, pSceneRenderer->pSamplerPoint, 1 };
        desc.mResources[10] = { DESCRIPTOR_TEXTURE, pSceneRenderer->pRTGBufferA->pTexture, 1 };
        desc.mResources[11] = { DESCRIPTOR_TEXTURE, pSceneRenderer->pRTGBufferB->pTexture, 1 };
        desc.mResources[12] = { DESCRIPTOR_TEXTURE, pSceneRenderer->pRTAccum->pTexture, 1 };
        desc.mResources[13] = { DESCRIPTOR_TEXTURE, pSceneRenderer->pRTSceneDepth->pTexture, 1 };
        desc.mResources[14] = { DESCRIPTOR_STORAGE_IMAGE, storageDepthTextures,
            pSceneRenderer->mDepthHierarchyCount,
            HIZ_MAX};
        desc.mResources[15] = { DESCRIPTOR_TEXTURE, shadowMapTextures,
            MAX_CASCADES, MAX_CASCADES};
        addDescriptorSet(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pDSGlobal);
    }

    // Per frame resource set
    if(!pSceneRenderer->pDSPerFrame)
    {
        DescriptorSetDesc desc = {};
        desc.mCount = 1;
        desc.mResources[0] = { DESCRIPTOR_UNIFORM_BUFFER, pSceneRenderer->pUBPerFrame, 1 };
        addDescriptorSet(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pDSPerFrame);
    }
}

void addScenePipelines(SceneRenderer* pSceneRenderer)
{
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
        desc.pDescriptorSets[0] = pSceneRenderer->pDSPerFrame;
        desc.pDescriptorSets[1] = pSceneRenderer->pDSGlobal;

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
        desc.pDescriptorSets[0] = pSceneRenderer->pDSPerFrame;
        desc.pDescriptorSets[1] = pSceneRenderer->pDSGlobal;

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
        desc.pDescriptorSets[0] = pSceneRenderer->pDSPerFrame;
        desc.pDescriptorSets[1] = pSceneRenderer->pDSGlobal;

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
        desc.pDescriptorSets[0] = pSceneRenderer->pDSPerFrame;
        desc.pDescriptorSets[1] = pSceneRenderer->pDSGlobal;

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
        desc.pDescriptorSets[0] = pSceneRenderer->pDSPerFrame;
        desc.pDescriptorSets[1] = pSceneRenderer->pDSGlobal;

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
        desc.pDescriptorSets[0] = pSceneRenderer->pDSPerFrame;
        desc.pDescriptorSets[1] = pSceneRenderer->pDSGlobal;

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
        desc.pDescriptorSets[0] = pSceneRenderer->pDSPerFrame;
        desc.pDescriptorSets[1] = pSceneRenderer->pDSGlobal;

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
    if(pSceneRenderer->pDSGlobal)
        removeDescriptorSet(pSceneRenderer->pRenderer, &pSceneRenderer->pDSGlobal);
    if(pSceneRenderer->pDSPerFrame)
        removeDescriptorSet(pSceneRenderer->pRenderer, &pSceneRenderer->pDSPerFrame);
}

void removeScenePipelines(SceneRenderer* pSceneRenderer)
{
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
    uint32 activeFrame = pSceneRenderer->pRenderer->mActiveFrame;
    m4f cameraView = getView(&pSceneRenderer->mCamera);
    m4f cameraProj = getProj(&pSceneRenderer->mCamera);
    pSceneRenderer->perFrameUniforms[activeFrame].mView = cameraView;
    pSceneRenderer->perFrameUniforms[activeFrame].mProj = cameraProj;
    pSceneRenderer->perFrameUniforms[activeFrame].mCamWorldPos = to4f(pSceneRenderer->mCamera.mPos, 1);

    DirectionalLight light = pSceneRenderer->mDirLight;
    pSceneRenderer->perFrameUniforms[activeFrame].mDirLight1 = to4f(normalize(light.mDir), light.mIntensity);
    pSceneRenderer->perFrameUniforms[activeFrame].mDirLight2 = to4f(light.mColor, pSceneRenderer->mAmbient);
}

void uploadPerFrameUniforms(SceneRenderer* pSceneRenderer)
{
    Renderer* pRenderer = pSceneRenderer->pRenderer;
    uint32 activeFrame = pRenderer->mActiveFrame;
    copyToBuffer(pRenderer, 
            pSceneRenderer->pUBPerFrame, 
            sizeof(PerFrameUniforms) * activeFrame, 
            &pSceneRenderer->perFrameUniforms[activeFrame], 
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
    debugAddSphere(pSceneRenderer, p, 0.05f, col, 4, 4);
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

void debugGeometry(SceneRenderer* pSceneRenderer)
{
    pSceneRenderer->mDebugVerts.clear();

    // Add debug geometry here

    if(!pSceneRenderer->mDebugVerts.mCount)
    {
        return;
    }

    // Copying data to GPU vertex buffer
    copyToBuffer(pSceneRenderer->pRenderer, 
            pSceneRenderer->pVBDebug, 
            0, 
            pSceneRenderer->mDebugVerts.mData, 
            pSceneRenderer->mDebugVerts.mCount * sizeof(float));
}

void renderScene(SceneRenderer* pSceneRenderer, uint32 frame)
{
    PROFILE_SCOPE;
    Renderer* pRenderer = pSceneRenderer->pRenderer;
    acquireNextImage(pRenderer, frame);

    CommandBuffer* pCmd = getCmd(pRenderer);
    beginCmd(pCmd);

    // Start GPU timings
    GpuTimestampParams gpuTimerParams = {};
    gpuTimerParams.pGpuTimer = &pSceneRenderer->mGpuTimer;
    gpuTimerParams.pCmd = pCmd;
    gpuTimerParams.queryPool = pRenderer->mActiveFrame;
    gpuTimerReadResults(&gpuTimerParams);
    gpuTimerStart(&gpuTimerParams);

    // Upload per frame data
    uploadPerFrameUniforms(pSceneRenderer);
    gpuTimestamp(str("Upload PerFrame"), &gpuTimerParams);

    // Depth pre pass
    {
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
        cmdBindDescriptorSet(pCmd, pPipeline, pSceneRenderer->pDSPerFrame, 0);
        cmdBindDescriptorSet(pCmd, pPipeline, pSceneRenderer->pDSGlobal, 1);

        cmdSetViewport(pCmd, pRTDepth);
        cmdSetScissor(pCmd, pRTDepth);

        cmdBindVertexBuffer(pCmd, pSceneRenderer->pVBSceneGeometry);
        cmdBindIndexBuffer(pCmd, pSceneRenderer->pIBSceneGeometry);

        uint32 constants[1];
        constants[0] = pRenderer->mActiveFrame;
        cmdSetConstants(pCmd, pPipeline, 0, sizeof(uint32), &constants);

        // Opaque
        cmdDrawIndexedIndirect(pCmd, 
                pSceneRenderer->pDBDrawCmdsOpaque, 
                pSceneRenderer->pDBDrawCmdCount, 
                0,
                SCENE_MAX_DRAWS);

        pPipeline = pSceneRenderer->pPipeDepthPrePassDoubleSided;
        cmdBindGraphicsPipeline(pCmd, pPipeline);
        // Double sided opaque
        cmdDrawIndexedIndirect(pCmd, 
                pSceneRenderer->pDBDrawCmdsOpaqueDoubleSided, 
                pSceneRenderer->pDBDrawCmdCount, 
                sizeof(uint32),
                SCENE_MAX_DRAWS);

        cmdUnbindRenderTargets(pCmd);
        gpuTimestamp(str("Depth Pre-pass"), &gpuTimerParams);
    }

    // Hierarchical Z Downsampling pass
    {
        ComputePipeline* pPipeline = pSceneRenderer->pPipeHiZDownsample;

        cmdBindComputePipeline(pCmd, pPipeline);
        cmdBindDescriptorSet(pCmd, pPipeline, pSceneRenderer->pDSPerFrame, 0);
        cmdBindDescriptorSet(pCmd, pPipeline, pSceneRenderer->pDSGlobal, 1);

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
    }

    // Generate draws pass
    {
        Barrier barrier = {};
        barrier.mSrcStage = PIPELINE_STAGE_DRAW_INDIRECT;
        barrier.mDstStage = PIPELINE_STAGE_TRANSFER;
        barrier.mSrcAccess = MEMORY_ACCESS_INDIRECT_READ;
        barrier.mDstAccess = MEMORY_ACCESS_TRANSFER_WRITE;
        cmdBarrier(pCmd, 1, &barrier);

        ComputePipeline* pPipeline = pSceneRenderer->pPipeGenerateDraws;

        cmdBindComputePipeline(pCmd, pPipeline);
        cmdBindDescriptorSet(pCmd, pPipeline, pSceneRenderer->pDSPerFrame, 0);
        cmdBindDescriptorSet(pCmd, pPipeline, pSceneRenderer->pDSGlobal, 1);

        uint32 constants[2];
        constants[0] = pSceneRenderer->pScene->mNodeCount;
        constants[1] = pRenderer->mActiveFrame;
        cmdSetConstants(pCmd, pPipeline, 0, ARR_SIZE(constants), &constants[0]);

        cmdFillBuffer(pCmd, pSceneRenderer->pDBDrawCmdCount, 0);
        cmdFillBuffer(pCmd, pSceneRenderer->pSBPerDraw, 0);
        cmdDispatch(pCmd, SCENE_MAX_NODES / 32, 1, 1);

        gpuTimestamp(str("Generate Draws Pass"), &gpuTimerParams);

        barrier = {};
        barrier.mSrcStage = PIPELINE_STAGE_COMPUTE_SHADER;
        barrier.mDstStage = PIPELINE_STAGE_DRAW_INDIRECT;
        barrier.mSrcAccess = MEMORY_ACCESS_SHADER_WRITE;
        barrier.mDstAccess = MEMORY_ACCESS_INDIRECT_READ;
        cmdBarrier(pCmd, 1, &barrier);
    }

    // GBuffer render pass
    {
        RenderTarget* pRTGBufferA = pSceneRenderer->pRTGBufferA;
        RenderTarget* pRTGBufferB = pSceneRenderer->pRTGBufferB;
        RenderTarget* pRTDepth = pSceneRenderer->pRTSceneDepth;

        GraphicsPipeline* pPipeline = pSceneRenderer->pPipeGBuffer;

        RenderTargetBarrier barriers[2];
        barriers[0] = {pRTGBufferA, getImageLayout(pRTGBufferA), IMAGE_LAYOUT_COLOR_OUTPUT };
        barriers[1] = {pRTGBufferB, getImageLayout(pRTGBufferB), IMAGE_LAYOUT_COLOR_OUTPUT };
        //barriers[2] = {pRTDepth, getImageLayout(pRTDepth), IMAGE_LAYOUT_DEPTH_STENCIL_OUTPUT };
        cmdRenderTargetBarrier(pCmd, ARR_LEN(barriers), barriers);

        RenderTargetBindDesc bindDesc = {};
        bindDesc.mColorCount = 2;
        bindDesc.mColorBindings[0] = { pRTGBufferA, LOAD_OP_CLEAR, STORE_OP_STORE };
        bindDesc.mColorBindings[1] = { pRTGBufferB, LOAD_OP_CLEAR, STORE_OP_STORE };
        //bindDesc.mDepthBinding = { pRTDepth, LOAD_OP_CLEAR, STORE_OP_STORE };
        bindDesc.mDepthBinding = { pRTDepth, LOAD_OP_LOAD, STORE_OP_STORE };
        cmdBindRenderTargets(pCmd, bindDesc);

        cmdBindGraphicsPipeline(pCmd, pPipeline);
        cmdBindDescriptorSet(pCmd, pPipeline, pSceneRenderer->pDSPerFrame, 0);
        cmdBindDescriptorSet(pCmd, pPipeline, pSceneRenderer->pDSGlobal, 1);

        cmdSetViewport(pCmd, pRTGBufferA);
        cmdSetScissor(pCmd, pRTGBufferA);

        cmdBindVertexBuffer(pCmd, pSceneRenderer->pVBSceneGeometry);
        cmdBindIndexBuffer(pCmd, pSceneRenderer->pIBSceneGeometry);

        uint32 constants[1];
        constants[0] = pRenderer->mActiveFrame;
        cmdSetConstants(pCmd, pPipeline, 0, sizeof(uint32), &constants);

        // Opaque
        cmdDrawIndexedIndirect(pCmd, 
                pSceneRenderer->pDBDrawCmdsOpaque, 
                pSceneRenderer->pDBDrawCmdCount, 
                0,
                SCENE_MAX_DRAWS);

        pPipeline = pSceneRenderer->pPipeGBufferDoubleSided;
        cmdBindGraphicsPipeline(pCmd, pPipeline);
        // Double sided opaque
        cmdDrawIndexedIndirect(pCmd, 
                pSceneRenderer->pDBDrawCmdsOpaqueDoubleSided, 
                pSceneRenderer->pDBDrawCmdCount, 
                sizeof(uint32),
                SCENE_MAX_DRAWS);

        cmdUnbindRenderTargets(pCmd);
        gpuTimestamp(str("GBuffer Pass"), &gpuTimerParams);
    }

    // Lighting pass
    {
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
        cmdBindDescriptorSet(pCmd, pPipeline, pSceneRenderer->pDSPerFrame, 0);
        cmdBindDescriptorSet(pCmd, pPipeline, pSceneRenderer->pDSGlobal, 1);

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
    }

    // Debug geometry pass
    debugGeometry(pSceneRenderer);
    if(pSceneRenderer->mDebugVerts.mCount)
    {
        RenderTarget* pRTAccum = pSceneRenderer->pRTAccum;

        GraphicsPipeline* pPipeline = pSceneRenderer->pPipeDebug;

        RenderTargetBindDesc bindDesc = {};
        bindDesc.mColorCount = 1;
        bindDesc.mColorBindings[0] = { pRTAccum, LOAD_OP_LOAD, STORE_OP_STORE };
        cmdBindRenderTargets(pCmd, bindDesc);

        cmdBindGraphicsPipeline(pCmd, pPipeline);
        cmdBindDescriptorSet(pCmd, pPipeline, pSceneRenderer->pDSPerFrame, 0);
        cmdBindDescriptorSet(pCmd, pPipeline, pSceneRenderer->pDSGlobal, 1);

        cmdSetViewport(pCmd, pRTAccum);
        cmdSetScissor(pCmd, pRTAccum);

        cmdBindVertexBuffer(pCmd, pSceneRenderer->pVBDebug);

        uint32 constants[1];
        constants[0] = pRenderer->mActiveFrame;
        cmdSetConstants(pCmd, pPipeline, 0, sizeof(uint32), &constants);

        cmdDraw(pCmd, pSceneRenderer->mDebugVerts.mCount / 6, 1);

        cmdUnbindRenderTargets(pCmd);
        gpuTimestamp(str("Debug Pass"), &gpuTimerParams);
    }

    // Tone mapping pass
    {
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
        cmdBindDescriptorSet(pCmd, pPipeline, pSceneRenderer->pDSPerFrame, 0);
        cmdBindDescriptorSet(pCmd, pPipeline, pSceneRenderer->pDSGlobal, 1);

        cmdSetViewport(pCmd, pRTPresent);
        cmdSetScissor(pCmd, pRTPresent);

        cmdBindVertexBuffer(pCmd, pSceneRenderer->pVBScreenQuad);
        cmdBindIndexBuffer(pCmd, pSceneRenderer->pIBScreenQuad);

        cmdDrawIndexed(pCmd, 
                3, 1, 0, 0);

        gpuTimestamp(str("Tone Mapping"), &gpuTimerParams);
    }

    // UI pass
    {
        RenderTarget* pRTColor = pSceneRenderer->pRTPresent;
        RenderTargetBindDesc bindDesc = {};
        bindDesc.mColorCount = 1;
        bindDesc.mColorBindings[0] = { pRTColor, LOAD_OP_LOAD, STORE_OP_STORE };

        uiStartFrame();
        uiSlider3f(str("Light Direction"), 
                &pSceneRenderer->mDirLight.mDir.mData[0], 
                -1.f, 1.f);
        uiSliderf(str("Light Intensity"), &pSceneRenderer->mDirLight.mIntensity, 0.f, 10.f);
        uiColor3f(str("Light Color"), 
                &pSceneRenderer->mDirLight.mColor.mData[0]);
        uiSliderf(str("Ambient"), &pSceneRenderer->mAmbient, 0.f, 1.f);
        uiSeparator();
        uiGpuTimingsWindow(&pSceneRenderer->pApp->mAppArena, &pSceneRenderer->mGpuTimer, 
                -400, 0, 400, 0);
        uiEndFrame(pCmd, bindDesc);
        gpuTimestamp(str("UI pass"), &gpuTimerParams);
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
