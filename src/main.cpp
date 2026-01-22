#include "../generated/build_includes.hpp"
#include "../dw/src/asset/asset.hpp"
#include "../dw/src/core/base.hpp"
#include "../dw/src/core/input.hpp"
#include "../dw/src/core/app.hpp"
#include "../dw/src/core/profile.hpp"
#include "../dw/src/math/math.hpp"
#include "../dw/src/render/buffer.hpp"
#include "../dw/src/render/command_buffer.hpp"
#include "../dw/src/render/descriptor.hpp"
#include "../dw/src/render/render.hpp"
#include "../dw/src/render/shader.hpp"
#include "../dw/src/render/camera.hpp"
#include "../dw/src/render/texture.hpp"
#include "../dw/src/render/ui.hpp"
#include "../dw/src/render/timings.hpp"

#include "scene.hpp"

App gApp;
AssetManager gAssetManager;
Renderer gRenderer;
UIState gUI;

// Geometry pass
RenderTarget*       pTargetUnlit = NULL;
RenderTarget*       pDepthUnlit = NULL;
DescriptorSet*      pDescriptorSetPerFrame = NULL;

struct PerFrameUniforms
{
    m4f mWorld = {};
    m4f mView = {};
    m4f mProj = {};
};
PerFrameUniforms perFrameUniforms[CONCURRENT_FRAMES];
Buffer* pUBPerFrame = NULL;

Camera gCamera = {};
Timer gTimer = {};
uint32 gFrame = 0;
GpuTimer gGpuTimer = {};

// Helia
Scene gScene = {};

#define APP_WIDTH 800
#define APP_HEIGHT 600

void addRenderTargets()
{
    // Fullscreen RT
    {
        RenderTargetDesc desc = {};
        desc.mFormat = FORMAT_RGBA8_UNORM;
        desc.mClear = {{0,0,0,0}};
        desc.mWidth = gApp.mWindow.mWidth;
        desc.mHeight = gApp.mWindow.mHeight;
        addRenderTarget(&gRenderer, desc, &pTargetUnlit);

        desc.mFormat = FORMAT_D16_UNORM;
        desc.mClear.mDepth = 0;
        addDepthTarget(&gRenderer, desc, &pDepthUnlit);
    }
}

void removeRenderTargets()
{
    removeRenderTarget(&gRenderer, &pDepthUnlit);
    removeRenderTarget(&gRenderer, &pTargetUnlit);
}

void addShaders()
{
    addSceneShaders(&gScene, &gRenderer, &gAssetManager);
}

void removeShaders()
{
    removeSceneShaders(&gScene, &gRenderer);
}

void addDescriptors()
{
    // Per frame
    {
        DescriptorSetDesc desc = {};
        desc.mCount = 1;
        desc.mResources[0] = { DESCRIPTOR_UNIFORM_BUFFER, pUBPerFrame, 1 };
        addDescriptorSet(&gRenderer, desc, &pDescriptorSetPerFrame);
    }
    addSceneDescriptors(&gScene, &gRenderer);
}

void removeDescriptors()
{
    removeDescriptorSet(&gRenderer, &pDescriptorSetPerFrame);
    removeSceneDescriptors(&gScene, &gRenderer);
}

void addPipelines()
{
    addScenePipelines(&gScene, &gRenderer);
}

void removePipelines()
{
    removeScenePipelines(&gScene, &gRenderer);
}

void init()
{
    initApp(APP_WIDTH, APP_HEIGHT, "DW App", &gApp);
    AssetManagerDesc assetManagerDesc;
    assetManagerDesc.mPermanentArenaSize = GB(1);
    assetManagerDesc.mTempArenaSize = MB(128);
    initAssetManager(assetManagerDesc, &gAssetManager);

    RendererDesc rendererDesc = {};
    rendererDesc.pApp = &gApp;
    initRenderer(rendererDesc, &gRenderer);

    UIDesc uiDesc = {};
    uiDesc.pApp = &gApp;
    uiDesc.pRenderer = &gRenderer;
    uiDesc.mTargetFormat = FORMAT_RGBA8_UNORM;
    initUI(uiDesc, &gUI);

    // Per frame uniform buffer
    {
        BufferDesc desc = {};
        desc.mType = BUFFER_TYPE_UNIFORM;
        desc.mSize = sizeof(PerFrameUniforms) * 2;
        desc.mCount = 1;
        desc.mStride = sizeof(PerFrameUniforms);
        addBuffer(&gRenderer, desc, &pUBPerFrame);
    }

    // Helia
    initScene(&gScene, MB(512), MB(128));
    setupSceneModel(&gScene, str("../../res/models/sponza/glTF/Sponza.gltf"));
    addSceneRenderResources(&gScene, &gRenderer, &gAssetManager, pUBPerFrame);

    addRenderTargets();
    addShaders();
    addDescriptors();
    addPipelines();

    // App controls
    CameraDesc camDesc = {};
    float fovX = TO_RAD(90.f);
    float aspect = getAspectRatio(&gApp);
    camDesc.mFovY = fovHtoV(fovX, aspect);
    camDesc.mAspect = aspect;
    camDesc.mNear = 0.01f;
    camDesc.mFar = 1000.f;
    initCamera(
            {0,0,-5}, 
            {0,0,0}, 
            camDesc, 
            &gCamera);

    initGpuTimer(&gRenderer, &gGpuTimer);

}

void shutdown()
{
    waitForCommands(&gRenderer);

    removePipelines();
    removeDescriptors();
    removeShaders();
    removeRenderTargets();

    removeSceneRenderResources(&gScene, &gRenderer);

    removeBuffer(&gRenderer, &pUBPerFrame);

    destroyScene(&gScene);

    destroyGpuTimer(&gGpuTimer);
    destroyUI(&gUI);
    destroyRenderer(&gRenderer);
    destroyAssetManager(&gAssetManager);
    destroyApp(&gApp);
}

void updatePerFrameUniforms(Renderer* pRenderer)
{
    perFrameUniforms[pRenderer->mActiveFrame].mWorld = identity();
    perFrameUniforms[pRenderer->mActiveFrame].mView = getView(&gCamera);
    perFrameUniforms[pRenderer->mActiveFrame].mProj = getProj(&gCamera);
}

void update()
{
    PROFILE_SCOPE;
    if(isJustDown(&gApp.mKeys, KEY_R))
    {
        addLoadRequest(&gApp, LOAD_REQUEST_SHADER);
    }

    if(isJustDown(&gApp.mKeys, KEY_ESCAPE))
    {
        gApp.mRunning = false;
    }

    // Camera
    {
        v3f moveDir = {0, 0, 0};
        if(isDown(&gApp.mKeys, KEY_W)) moveDir.z = -1;
        if(isDown(&gApp.mKeys, KEY_S)) moveDir.z = 1;
        if(isDown(&gApp.mKeys, KEY_A)) moveDir.x = -1;
        if(isDown(&gApp.mKeys, KEY_D)) moveDir.x = 1;
        moveCamera(&gCamera, moveDir, gApp.mDt);
        
        bool isRotating = isDown(&gApp.mKeys, KEY_RMB);
        setHidden(&gApp.mCursor, isRotating);
        setLocked(&gApp.mCursor, isRotating);
        if(isRotating)
        {
            setHidden(&gApp.mCursor, true);
            setLocked(&gApp.mCursor, true);
            v2f rotateDir;
            getDelta(&gApp.mCursor, &rotateDir.x, &rotateDir.y);
            rotateCamera(&gCamera, -rotateDir, gApp.mDt);
        }
    }

    updateCamera(&gCamera, gApp.mDt);

    updatePerFrameUniforms(&gRenderer);
}

void render()
{
    PROFILE_SCOPE;
    acquireNextImage(&gRenderer, gFrame);

    CommandBuffer* pCmd = getCmd(&gRenderer);
    beginCmd(pCmd);

    // Start GPU timings
    GpuTimestampParams gpuTimerParams = {};
    gpuTimerParams.pGpuTimer = &gGpuTimer;
    gpuTimerParams.pCmd = pCmd;
    gpuTimerParams.queryPool = gRenderer.mActiveFrame;
    gpuTimerReadResults(&gpuTimerParams);
    gpuTimerStart(&gpuTimerParams);

    // Upload per frame data
    copyToBuffer(&gRenderer, 
            pUBPerFrame, 
            sizeof(PerFrameUniforms) * gRenderer.mActiveFrame, 
            &perFrameUniforms[gRenderer.mActiveFrame], 
            sizeof(PerFrameUniforms));
    gpuTimestamp(str("Upload PerFrame"), &gpuTimerParams);

    // Scene geometry pass
    {
        RenderTargetBarrier barriers[2];
        barriers[0] = {pTargetUnlit, getImageLayout(pTargetUnlit), IMAGE_LAYOUT_COLOR_OUTPUT };
        barriers[1] = {pDepthUnlit, getImageLayout(pDepthUnlit), IMAGE_LAYOUT_DEPTH_STENCIL_OUTPUT };
        cmdRenderTargetBarrier(pCmd, 2, barriers);

        RenderTargetBindDesc bindDesc = {};
        bindDesc.mColorCount = 1;
        bindDesc.mColorBindings[0] = { pTargetUnlit, LOAD_OP_CLEAR, STORE_OP_STORE };
        bindDesc.mDepthBinding = { pDepthUnlit, LOAD_OP_CLEAR, STORE_OP_STORE };
        cmdBindRenderTargets(pCmd, bindDesc);

        cmdBindDescriptorSet(pCmd, gScene.pPipeGeometry, gScene.pDSScene, 0);
        cmdBindGraphicsPipeline(pCmd, gScene.pPipeGeometry);

        //cmdBindDescriptorSet(pCmd, pPipelineUnlit, pDescriptorSetPerFrame, 0);
        //cmdBindGraphicsPipeline(pCmd, pPipelineUnlit);

        cmdSetViewport(pCmd, pTargetUnlit);
        cmdSetScissor(pCmd, pTargetUnlit);

        cmdBindVertexBuffer(pCmd, gScene.pVBScene);
        cmdBindIndexBuffer(pCmd, gScene.pIBScene);

        for(uint32 n = 0; n < gScene.mNodeCount; n++)
        {
            uint32 constants[2];
            constants[0] = gRenderer.mActiveFrame;
            constants[1] = n;
            cmdSetConstants(pCmd, gScene.pPipeGeometry, 0, sizeof(uint32) * 2, &constants);

            SceneNode* pNode = &gScene.mNodes[n];
            Mesh* pMesh = &gScene.mMeshes[pNode->mMeshId];
            //cmdDrawIndexed(pCmd, pMesh->mIndexStart, pMesh->mIndexCount, 1);
            //cmdDrawIndexed(pCmd, pMesh->mIndexCount, 1, 0, pMesh->mVertexOffset);
            cmdDrawIndexed(pCmd, pMesh->mIndexCount, 1, pMesh->mIndexOffset, pMesh->mVertexOffset);
        }

        //cmdBindVertexBuffer(pCmd, pVBCube);
        //cmdBindIndexBuffer(pCmd, pIBCube);

        //cmdDrawIndexed(pCmd, ARR_LEN(cubeIndices), 1);

        cmdUnbindRenderTargets(pCmd);
        gpuTimestamp(str("Unlit Pass"), &gpuTimerParams);
    }

    // UI
    {
        RenderTargetBindDesc bindDesc = {};
        bindDesc.mColorCount = 1;
        bindDesc.mColorBindings[0] = { pTargetUnlit, LOAD_OP_LOAD, STORE_OP_STORE };

        uiStartFrame();
        uiGpuTimingsWindow(&gApp.mAppArena, &gGpuTimer, -400, 0, 400, 0);
        uiEndFrame(pCmd, bindDesc);
        gpuTimestamp(str("UI pass"), &gpuTimerParams);
    }

    // Copy output to swap chain
    {
        RenderTargetBarrier barrier = {pTargetUnlit, IMAGE_LAYOUT_COLOR_OUTPUT, IMAGE_LAYOUT_TRANSFER_SRC };
        cmdRenderTargetBarrier(pCmd, 1, &barrier);
        cmdCopyToSwapChain(pCmd, &gRenderer.mSwapChain, pTargetUnlit->pTexture);
        gpuTimestamp(str("Swap Chain copy"), &gpuTimerParams);
    }

    endCmd(pCmd);
    submitFrameCmd(&gRenderer, pCmd);
    present(&gRenderer);
    gFrame++;
}

void processLoadRequests(App* pApp)
{
    if(!pApp->mLoadRequests)
    {
        return;
    }

    waitForCommands(&gRenderer);
    if(pApp->mLoadRequests & LOAD_REQUEST_RESIZE)
    {
        removePipelines();
        removeDescriptors();
        removeRenderTargets();
        destroySwapChain(&gRenderer, &gRenderer.mSwapChain);

        initSwapChain(&gRenderer, &gRenderer.mSwapChain);
        addRenderTargets();
        addDescriptors();
        addPipelines();

        gCamera.mDesc.mAspect = getAspectRatio(&gApp);

        removeLoadRequest(&gApp, LOAD_REQUEST_RESIZE);
    }
    if(pApp->mLoadRequests & LOAD_REQUEST_SHADER)
    {
        LOG("Reloading shaders...");
        removePipelines();
        removeDescriptors();
        removeShaders();

        addShaders();
        addDescriptors();
        addPipelines();
        removeLoadRequest(&gApp, LOAD_REQUEST_SHADER);
    }
}

DW_MAIN()
{
    BEGIN_MAIN;

    init();

    while(true)
    {
        PROFILE_SCOPE_NAME("Main Loop");
        poll(&gApp);
        if(!gApp.mRunning)
        {
            break;
        }
        processLoadRequests(&gApp);

        update();        
        render();
    }

    shutdown();

    END_MAIN;
}

