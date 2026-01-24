#include "renderer.hpp"
#include "../dw/src/core/profile.hpp"
#include "../dw/src/asset/asset.hpp"
#include "../dw/src/render/render.hpp"
#include "../dw/src/render/ui.hpp"
#include "../dw/src/render/texture.hpp"

void initSceneRenderer(SceneRenderer* pSceneRenderer,
        App* pApp, Renderer* pRenderer, AssetManager* pAssetManager,
        Scene* pScene, 
        String rootPath, String* pTexPaths, uint32 texCount)
{
    PROFILE_SCOPE;

    ASSERT(pSceneRenderer);
    ASSERT(pApp && pRenderer && pAssetManager && pScene);
    ASSERT(texCount + FALLBACK_TEXTURE_COUNT < SCENE_MAX_TEXTURES);  // Textures + fallbacks can't exceed max
    pSceneRenderer->pApp = pApp;
    pSceneRenderer->pRenderer = pRenderer;
    pSceneRenderer->pAssetManager = pAssetManager;
    pSceneRenderer->pScene = pScene;

    // Load textures from scene model
    {
        PROFILE_SCOPE_NAME("initSceneRenderer::Load Textures");
        pSceneRenderer->mMaterialMapCount = texCount + FALLBACK_TEXTURE_COUNT;
        for(uint32 t = 0; t < texCount; t++)
        {
            PROFILE_SCOPE_NAME("initSceneRenderer::Load Texture");
            Texture* pTex = NULL;
            char buf[256];
            String texPath = strf(buf, "%.*s/%.*s", STRF_ARG(rootPath), STRF_ARG(pTexPaths[t]));
            loadTexture(pAssetManager, pRenderer, texPath, false, &pTex);
            pSceneRenderer->pTexMaterialMaps[t + FALLBACK_TEXTURE_COUNT] = pTex;
        }
    }

    // Load fallback textures
    {
        Texture* pTexFallbackBaseColor = NULL;
        loadTexture(pAssetManager, pRenderer, str("../../res/textures/white.png"),
                false, &pTexFallbackBaseColor);

        Texture* pTexFallbackNormal = NULL;
        loadTexture(pAssetManager, pRenderer, str("../../res/textures/flat_normal.png"),
                false, &pTexFallbackNormal);

        Texture* pTexFallbackMRS = NULL;
        loadTexture(pAssetManager, pRenderer, str("../../res/textures/black.png"),
                false, &pTexFallbackMRS);

        pSceneRenderer->pTexMaterialMaps[FALLBACK_BASECOLOR_INDEX] = pTexFallbackBaseColor;
        pSceneRenderer->pTexMaterialMaps[FALLBACK_NORMAL_INDEX] = pTexFallbackNormal;
        pSceneRenderer->pTexMaterialMaps[FALLBACK_MRS_INDEX] = pTexFallbackMRS;
    }

    // Geometry vertex layout
    {
        VertexLayoutDesc desc = {};
        desc.mCount = 4;
        desc.mAttribs[0] = ATTRIBUTE_FLOAT3;
        desc.mAttribs[1] = ATTRIBUTE_FLOAT3;
        desc.mAttribs[2] = ATTRIBUTE_FLOAT2;
        desc.mAttribs[3] = ATTRIBUTE_FLOAT4;
        initVertexLayout(desc, &pSceneRenderer->mVLGBuffer);
    }

    // Geometry vertex/index buffers
    {
        BufferDesc vbDesc = {};
        vbDesc.mType = BUFFER_TYPE_VERTEX;
        vbDesc.mSize = pScene->vertexCount * sizeof(float) * 12;
        vbDesc.mCount = pScene->vertexCount;
        vbDesc.mStride = sizeof(float);
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

    // Default sampler
    {
        SamplerDesc desc = {};
        desc.mMinFilter = SAMPLER_FILTER_LINEAR;
        desc.mMagFilter = SAMPLER_FILTER_LINEAR;
        desc.mMipFilter = SAMPLER_FILTER_LINEAR;
        addSampler(pRenderer, desc, &pSceneRenderer->pSamplerLinear);
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
    removeBuffer(pRenderer, &pSceneRenderer->pUBPerFrame);
    removeBuffer(pRenderer, &pSceneRenderer->pSBPerDraw);
    removeBuffer(pRenderer, &pSceneRenderer->pDBDrawCmdCount);
    removeBuffer(pRenderer, &pSceneRenderer->pDBDrawCmdsOpaqueDoubleSided);
    removeBuffer(pRenderer, &pSceneRenderer->pDBDrawCmdsOpaque);
    removeBuffer(pRenderer, &pSceneRenderer->pVBSceneGeometry);
    removeBuffer(pRenderer, &pSceneRenderer->pIBSceneGeometry);
    removeBuffer(pRenderer, &pSceneRenderer->pSBSceneMaterials);
    removeBuffer(pRenderer, &pSceneRenderer->pSBSceneMeshes);
    removeBuffer(pRenderer, &pSceneRenderer->pSBSceneNodes);

    *pSceneRenderer = {};
}

void addSceneRenderTargets(SceneRenderer* pSceneRenderer)
{
    // Geometry pass RT
    {
        RenderTargetDesc desc = {};
        desc.mFormat = FORMAT_RGBA8_UNORM;
        desc.mClear = {{0,0,0,0}};
        desc.mWidth =   pSceneRenderer->pApp->mWindow.mWidth;
        desc.mHeight =  pSceneRenderer->pApp->mWindow.mHeight;
        addRenderTarget(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pRTGBufferColor);

        desc.mFormat = FORMAT_D32_FLOAT;
        desc.mClear.mDepth = 0;
        addDepthTarget(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pRTGBufferDepth);
    }
}

void addSceneShaders(SceneRenderer* pSceneRenderer)
{
    if(!pSceneRenderer->pVSGBufferOpaque)
    {
        loadShader(pSceneRenderer->pAssetManager, pSceneRenderer->pRenderer, 
                str("../../res/shaders/geometry.vert"), 
                &pSceneRenderer->pVSGBufferOpaque);
    }

    if(!pSceneRenderer->pPSGBufferOpaque)
    {
        loadShader(pSceneRenderer->pAssetManager, pSceneRenderer->pRenderer, 
                str("../../res/shaders/geometry.frag"), 
                &pSceneRenderer->pPSGBufferOpaque);
    }

    // TODO(caio): Same shader but with different compile time defines
    // to avoid having multiple files
    if(!pSceneRenderer->pVSGBufferOpaqueDoubleSided)
    {
        loadShader(pSceneRenderer->pAssetManager, pSceneRenderer->pRenderer, 
                str("../../res/shaders/geometry_ds.vert"), 
                &pSceneRenderer->pVSGBufferOpaqueDoubleSided);
    }

    if(!pSceneRenderer->pPSGBufferOpaqueDoubleSided)
    {
        loadShader(pSceneRenderer->pAssetManager, pSceneRenderer->pRenderer, 
                str("../../res/shaders/geometry_ds.frag"), 
                &pSceneRenderer->pPSGBufferOpaqueDoubleSided);
    }

    if(!pSceneRenderer->pCSGenerateDraws)
    {
        loadShader(pSceneRenderer->pAssetManager, pSceneRenderer->pRenderer, 
                str("../../res/shaders/generate_draws.comp"), 
                &pSceneRenderer->pCSGenerateDraws);
    }
}

void addSceneDescriptors(SceneRenderer* pSceneRenderer)
{
    // Scene global descriptor set
    if(!pSceneRenderer->pDSSceneGeometry)
    {
        DescriptorSetDesc desc = {};
        desc.mCount = 9;
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
        addDescriptorSet(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pDSSceneGeometry);
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
    // Scene geometry pass pipeline
    //if(!pSceneRenderer->pPipeGBufferOpaque)
    {
        GraphicsPipelineDesc desc = {};
        desc.mRenderTargetCount = 1;
        desc.mRenderTargetFormats[0] = pSceneRenderer->pRTGBufferColor->mDesc.mFormat;
        desc.mDepthTargetFormat = pSceneRenderer->pRTGBufferDepth->mDesc.mFormat;

        desc.mVertexLayout = pSceneRenderer->mVLGBuffer;
        desc.pVS = pSceneRenderer->pVSGBufferOpaque;
        desc.pFS = pSceneRenderer->pPSGBufferOpaque;

        desc.mCullMode = CULL_MODE_BACK;
        desc.mFrontFace = FRONT_FACE_CCW;

        desc.mDepthTest = true;
        desc.mDepthWrite = true;
        desc.mDepthOp = COMPARE_GREATER;

        desc.mDescriptorSetCount = 2;
        desc.pDescriptorSets[0] = pSceneRenderer->pDSPerFrame;
        desc.pDescriptorSets[1] = pSceneRenderer->pDSSceneGeometry;

        // Constants:
        // - Active frame (uint32)
        desc.mConstantBlockCount = 1;
        desc.mConstantBlocks[0].mShaderTypes = SHADER_TYPE_VERT | SHADER_TYPE_FRAG;
        desc.mConstantBlocks[0].mSize = sizeof(uint32) * 2;

        if(!pSceneRenderer->pPipeGBufferOpaque)
        {
            addPipeline(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pPipeGBufferOpaque);
        }

        desc.mCullMode = CULL_MODE_NONE;
        desc.pVS = pSceneRenderer->pVSGBufferOpaqueDoubleSided;
        desc.pFS = pSceneRenderer->pPSGBufferOpaqueDoubleSided;
        if(!pSceneRenderer->pPipeGBufferOpaqueDoubleSided)
        {
            addPipeline(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pPipeGBufferOpaqueDoubleSided);
        }
    }

    if(!pSceneRenderer->pPipeGenerateDraws)
    {
        ComputePipelineDesc desc = {};

        desc.pCS = pSceneRenderer->pCSGenerateDraws;

        desc.mDescriptorSetCount = 2;
        desc.pDescriptorSets[0] = pSceneRenderer->pDSPerFrame;
        desc.pDescriptorSets[1] = pSceneRenderer->pDSSceneGeometry;

        // Constants:
        // - Total node count (uint32)
        desc.mConstantBlockCount = 1;
        desc.mConstantBlocks[0].mShaderTypes = SHADER_TYPE_COMP;
        desc.mConstantBlocks[0].mSize = sizeof(uint32);

        addPipeline(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pPipeGenerateDraws);
    }
}

void removeSceneRenderTargets(SceneRenderer* pSceneRenderer)
{
    removeRenderTarget(pSceneRenderer->pRenderer, &pSceneRenderer->pRTGBufferColor);
    removeRenderTarget(pSceneRenderer->pRenderer, &pSceneRenderer->pRTGBufferDepth);
}

void removeSceneShaders(SceneRenderer* pSceneRenderer)
{
    if(pSceneRenderer->pVSGBufferOpaque)
        removeShader(pSceneRenderer->pRenderer, &pSceneRenderer->pVSGBufferOpaque);
    if(pSceneRenderer->pPSGBufferOpaque)
        removeShader(pSceneRenderer->pRenderer, &pSceneRenderer->pPSGBufferOpaque);
    if(pSceneRenderer->pVSGBufferOpaqueDoubleSided)
        removeShader(pSceneRenderer->pRenderer, &pSceneRenderer->pVSGBufferOpaqueDoubleSided);
    if(pSceneRenderer->pPSGBufferOpaqueDoubleSided)
        removeShader(pSceneRenderer->pRenderer, &pSceneRenderer->pPSGBufferOpaqueDoubleSided);
    if(pSceneRenderer->pCSGenerateDraws)
        removeShader(pSceneRenderer->pRenderer, &pSceneRenderer->pCSGenerateDraws);
}

void removeSceneDescriptors(SceneRenderer* pSceneRenderer)
{
    if(pSceneRenderer->pDSSceneGeometry)
        removeDescriptorSet(pSceneRenderer->pRenderer, &pSceneRenderer->pDSSceneGeometry);
    if(pSceneRenderer->pDSPerFrame)
        removeDescriptorSet(pSceneRenderer->pRenderer, &pSceneRenderer->pDSPerFrame);
}

void removeScenePipelines(SceneRenderer* pSceneRenderer)
{
    if(pSceneRenderer->pPipeGBufferOpaque)
        removePipeline(pSceneRenderer->pRenderer, &pSceneRenderer->pPipeGBufferOpaque);
    if(pSceneRenderer->pPipeGBufferOpaqueDoubleSided)
        removePipeline(pSceneRenderer->pRenderer, &pSceneRenderer->pPipeGBufferOpaqueDoubleSided);
    if(pSceneRenderer->pPipeGenerateDraws)
        removePipeline(pSceneRenderer->pRenderer, &pSceneRenderer->pPipeGenerateDraws);
}

void updatePerFrameUniforms(SceneRenderer* pSceneRenderer)
{
    uint32 activeFrame = pSceneRenderer->pRenderer->mActiveFrame;
    pSceneRenderer->perFrameUniforms[activeFrame].mWorld = identity();
    pSceneRenderer->perFrameUniforms[activeFrame].mView = getView(&pSceneRenderer->mCamera);
    pSceneRenderer->perFrameUniforms[activeFrame].mProj = getProj(&pSceneRenderer->mCamera);
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

    // Generate draws compute pass
    {
        ComputePipeline* pPipeline = pSceneRenderer->pPipeGenerateDraws;

        cmdBindComputePipeline(pCmd, pPipeline);
        cmdBindDescriptorSet(pCmd, pPipeline, pSceneRenderer->pDSPerFrame, 0);
        cmdBindDescriptorSet(pCmd, pPipeline, pSceneRenderer->pDSSceneGeometry, 1);

        cmdSetConstants(pCmd, pPipeline, 0, sizeof(uint32), &pSceneRenderer->pScene->mNodeCount);

        cmdFillBuffer(pCmd, pSceneRenderer->pDBDrawCmdCount, 0);
        cmdFillBuffer(pCmd, pSceneRenderer->pSBPerDraw, 0);
        cmdDispatch(pCmd, SCENE_MAX_NODES / 32, 1, 1);

        gpuTimestamp(str("Generate Draws Pass"), &gpuTimerParams);

        Barrier barrier = {};
        barrier.mSrcStage = PIPELINE_STAGE_COMPUTE_SHADER;
        barrier.mDstStage = PIPELINE_STAGE_DRAW_INDIRECT;
        barrier.mSrcAccess = MEMORY_ACCESS_SHADER_WRITE;
        barrier.mDstAccess = MEMORY_ACCESS_INDIRECT_READ;
        cmdBarrier(pCmd, 1, &barrier);
    }

    // Scene geometry render pass
    {
        RenderTarget* pRTColor = pSceneRenderer->pRTGBufferColor;
        RenderTarget* pRTDepth = pSceneRenderer->pRTGBufferDepth;
        GraphicsPipeline* pPipeline = pSceneRenderer->pPipeGBufferOpaque;

        RenderTargetBarrier barriers[2];
        barriers[0] = {pRTColor, getImageLayout(pRTColor), IMAGE_LAYOUT_COLOR_OUTPUT };
        barriers[1] = {pRTDepth, getImageLayout(pRTDepth), IMAGE_LAYOUT_DEPTH_STENCIL_OUTPUT };
        cmdRenderTargetBarrier(pCmd, 2, barriers);

        RenderTargetBindDesc bindDesc = {};
        bindDesc.mColorCount = 1;
        bindDesc.mColorBindings[0] = { pRTColor, LOAD_OP_CLEAR, STORE_OP_STORE };
        bindDesc.mDepthBinding = { pRTDepth, LOAD_OP_CLEAR, STORE_OP_STORE };
        cmdBindRenderTargets(pCmd, bindDesc);

        cmdBindGraphicsPipeline(pCmd, pPipeline);
        cmdBindDescriptorSet(pCmd, pPipeline, pSceneRenderer->pDSPerFrame, 0);
        cmdBindDescriptorSet(pCmd, pPipeline, pSceneRenderer->pDSSceneGeometry, 1);

        cmdSetViewport(pCmd, pRTColor);
        cmdSetScissor(pCmd, pRTColor);

        cmdBindVertexBuffer(pCmd, pSceneRenderer->pVBSceneGeometry);
        cmdBindIndexBuffer(pCmd, pSceneRenderer->pIBSceneGeometry);

        uint32 constants[2];
        constants[0] = pRenderer->mActiveFrame;
        cmdSetConstants(pCmd, pPipeline, 0, sizeof(uint32) * 2, &constants);

        // Opaque
        cmdDrawIndexedIndirect(pCmd, 
                pSceneRenderer->pDBDrawCmdsOpaque, 
                pSceneRenderer->pDBDrawCmdCount, 
                0,
                SCENE_MAX_DRAWS);

        // TODO(caio): Change pipeline to use double sided (different shaders for normals (maybe no need), no culling)

        pPipeline = pSceneRenderer->pPipeGBufferOpaqueDoubleSided;
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

    // UI pass
    {
        RenderTarget* pRTColor = pSceneRenderer->pRTGBufferColor;
        RenderTargetBindDesc bindDesc = {};
        bindDesc.mColorCount = 1;
        bindDesc.mColorBindings[0] = { pRTColor, LOAD_OP_LOAD, STORE_OP_STORE };

        uiStartFrame();
        uiGpuTimingsWindow(&pSceneRenderer->pApp->mAppArena, &pSceneRenderer->mGpuTimer, 
                -400, 0, 400, 0);
        uiEndFrame(pCmd, bindDesc);
        gpuTimestamp(str("UI pass"), &gpuTimerParams);
    }

    // Copy to swap chain
    {
        RenderTarget* pRTColor = pSceneRenderer->pRTGBufferColor;
        RenderTargetBarrier barrier = {pRTColor, IMAGE_LAYOUT_COLOR_OUTPUT, IMAGE_LAYOUT_TRANSFER_SRC };
        cmdRenderTargetBarrier(pCmd, 1, &barrier);
        cmdCopyToSwapChain(pCmd, &pRenderer->mSwapChain, pRTColor->pTexture);
        gpuTimestamp(str("Swap Chain copy"), &gpuTimerParams);
    }

    endCmd(pCmd);
    submitFrameCmd(pRenderer, pCmd);
    present(pRenderer);
}
