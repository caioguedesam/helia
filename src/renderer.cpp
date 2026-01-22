#include "renderer.hpp"
#include "../dw/src/render/render.hpp"
#include "../dw/src/render/ui.hpp"
#include "../dw/src/core/profile.hpp"

void initSceneRenderer(SceneRenderer* pSceneRenderer,
        App* pApp, Renderer* pRenderer, AssetManager* pAssetManager,
        Scene* pScene)
{
    ASSERT(pSceneRenderer);
    ASSERT(pApp && pRenderer && pAssetManager && pScene);
    pSceneRenderer->pApp = pApp;
    pSceneRenderer->pRenderer = pRenderer;
    pSceneRenderer->pAssetManager = pAssetManager;
    pSceneRenderer->pScene = pScene;

    // Geometry vertex layout
    {
        VertexLayoutDesc desc = {};
        desc.mCount = 4;
        desc.mAttribs[0] = ATTRIBUTE_FLOAT3;
        desc.mAttribs[1] = ATTRIBUTE_FLOAT3;
        desc.mAttribs[2] = ATTRIBUTE_FLOAT2;
        desc.mAttribs[3] = ATTRIBUTE_FLOAT4;
        initVertexLayout(desc, &pSceneRenderer->mVLSceneGeometry);
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
    }

    // GPU draw call buffers
    {
        BufferDesc desc = {};
        desc.mType = BUFFER_TYPE_INDIRECT;
        desc.mSize = sizeof(IndirectDraw) * SCENE_MAX_DRAWS;
        desc.mCount = SCENE_MAX_DRAWS;
        desc.mStride = sizeof(IndirectDraw);
        addBuffer(pRenderer, desc, &pSceneRenderer->pDBDrawCmds);

        // TODO: Refactor this. This can be a single buffer with offsets.
        desc.mType = BUFFER_TYPE_STORAGE;
        desc.mSize = sizeof(uint32);
        desc.mCount = 1;
        desc.mStride = sizeof(uint32);
        addBuffer(pRenderer, desc, &pSceneRenderer->pSBDrawCmdCount);
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
    camDesc.mNear = 0.01f;
    camDesc.mFar = 1000.f;
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
    removeBuffer(pRenderer, &pSceneRenderer->pUBPerFrame);
    removeBuffer(pRenderer, &pSceneRenderer->pSBDrawCmdCount);
    removeBuffer(pRenderer, &pSceneRenderer->pDBDrawCmds);
    removeBuffer(pRenderer, &pSceneRenderer->pVBSceneGeometry);
    removeBuffer(pRenderer, &pSceneRenderer->pIBSceneGeometry);
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
        addRenderTarget(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pRTSceneGeometryColor);

        desc.mFormat = FORMAT_D16_UNORM;
        desc.mClear.mDepth = 0;
        addDepthTarget(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pRTSceneGeometryDepth);
    }
}

void addSceneShaders(SceneRenderer* pSceneRenderer)
{
    if(!pSceneRenderer->pVSSceneGeometry)
    {
        loadShader(pSceneRenderer->pAssetManager, pSceneRenderer->pRenderer, 
                str("../../res/shaders/geometry.vert"), 
                &pSceneRenderer->pVSSceneGeometry);
    }

    if(!pSceneRenderer->pPSSceneGeometry)
    {
        loadShader(pSceneRenderer->pAssetManager, pSceneRenderer->pRenderer, 
                str("../../res/shaders/geometry.frag"), 
                &pSceneRenderer->pPSSceneGeometry);
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
        desc.mCount = 4;
        desc.mResources[0] = { DESCRIPTOR_STORAGE_BUFFER, pSceneRenderer->pSBSceneNodes, 1 };
        desc.mResources[1] = { DESCRIPTOR_STORAGE_BUFFER, pSceneRenderer->pSBSceneMeshes, 1 };
        desc.mResources[2] = { DESCRIPTOR_STORAGE_BUFFER, pSceneRenderer->pDBDrawCmds, 1 };
        desc.mResources[3] = { DESCRIPTOR_STORAGE_BUFFER, pSceneRenderer->pSBDrawCmdCount, 1 };
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
    if(!pSceneRenderer->pPipeSceneGeometry)
    {
        GraphicsPipelineDesc desc = {};
        desc.mRenderTargetCount = 1;
        desc.mRenderTargetFormats[0] = pSceneRenderer->pRTSceneGeometryColor->mDesc.mFormat;
        desc.mDepthTargetFormat = pSceneRenderer->pRTSceneGeometryDepth->mDesc.mFormat;

        desc.mVertexLayout = pSceneRenderer->mVLSceneGeometry;
        desc.pVS = pSceneRenderer->pVSSceneGeometry;
        desc.pFS = pSceneRenderer->pPSSceneGeometry;

        desc.mCullMode = CULL_MODE_NONE;    // TODO(caio): Activate proper face culling
        desc.mFrontFace = FRONT_FACE_CW;

        desc.mDepthTest = true;
        desc.mDepthWrite = true;
        desc.mDepthOp = COMPARE_GREATER;

        desc.mDescriptorSetCount = 2;
        desc.pDescriptorSets[0] = pSceneRenderer->pDSPerFrame;
        desc.pDescriptorSets[1] = pSceneRenderer->pDSSceneGeometry;

        // Constants:
        // - Active frame (uint32)
        // - Current node (uint32)
        desc.mConstantBlockCount = 1;
        desc.mConstantBlocks[0].mShaderTypes = SHADER_TYPE_VERT | SHADER_TYPE_FRAG;
        desc.mConstantBlocks[0].mSize = sizeof(uint32) * 2;

        addPipeline(pSceneRenderer->pRenderer, desc, &pSceneRenderer->pPipeSceneGeometry);
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
    removeRenderTarget(pSceneRenderer->pRenderer, &pSceneRenderer->pRTSceneGeometryColor);
    removeRenderTarget(pSceneRenderer->pRenderer, &pSceneRenderer->pRTSceneGeometryDepth);
}

void removeSceneShaders(SceneRenderer* pSceneRenderer)
{
    if(pSceneRenderer->pVSSceneGeometry)
        removeShader(pSceneRenderer->pRenderer, &pSceneRenderer->pVSSceneGeometry);
    if(pSceneRenderer->pPSSceneGeometry)
        removeShader(pSceneRenderer->pRenderer, &pSceneRenderer->pPSSceneGeometry);
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
    if(pSceneRenderer->pPipeSceneGeometry)
        removePipeline(pSceneRenderer->pRenderer, &pSceneRenderer->pPipeSceneGeometry);
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

        uint32 size = (uint32)sqrtf(SCENE_MAX_NODES);
        uint32 localSize = 8;
        cmdDispatch(pCmd, size / localSize, size / localSize, 1);

        Barrier barrier = {};
        barrier.mSrcStage = PIPELINE_STAGE_COMPUTE_SHADER;
        barrier.mDstStage = PIPELINE_STAGE_DRAW_INDIRECT;
        barrier.mSrcAccess = MEMORY_ACCESS_SHADER_WRITE;
        barrier.mDstAccess = MEMORY_ACCESS_INDIRECT_READ;
        cmdBarrier(pCmd, 1, &barrier);
    }

    // Scene geometry render pass
    {
        RenderTarget* pRTColor = pSceneRenderer->pRTSceneGeometryColor;
        RenderTarget* pRTDepth = pSceneRenderer->pRTSceneGeometryDepth;
        GraphicsPipeline* pPipeline = pSceneRenderer->pPipeSceneGeometry;

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

        Scene* pScene = pSceneRenderer->pScene;
        for(uint32 n = 0; n < pScene->mNodeCount; n++)
        {
            uint32 constants[2];
            constants[0] = pRenderer->mActiveFrame;
            constants[1] = n;
            cmdSetConstants(pCmd, pPipeline, 0, sizeof(uint32) * 2, &constants);

            SceneNode* pNode = &pScene->mNodes[n];
            SceneMesh* pMesh = &pScene->mMeshes[pNode->mMeshId];
            cmdDrawIndexed(pCmd, 
                    pMesh->mIndexCount, 
                    1, 
                    pMesh->mIndexOffset, 
                    pMesh->mVertexOffset);
        }

        cmdUnbindRenderTargets(pCmd);
        gpuTimestamp(str("Unlit Pass"), &gpuTimerParams);
    }

    // UI pass
    {
        RenderTarget* pRTColor = pSceneRenderer->pRTSceneGeometryColor;
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
        RenderTarget* pRTColor = pSceneRenderer->pRTSceneGeometryColor;
        RenderTargetBarrier barrier = {pRTColor, IMAGE_LAYOUT_COLOR_OUTPUT, IMAGE_LAYOUT_TRANSFER_SRC };
        cmdRenderTargetBarrier(pCmd, 1, &barrier);
        cmdCopyToSwapChain(pCmd, &pRenderer->mSwapChain, pRTColor->pTexture);
        gpuTimestamp(str("Swap Chain copy"), &gpuTimerParams);
    }

    endCmd(pCmd);
    submitFrameCmd(pRenderer, pCmd);
    present(pRenderer);
}
