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

float cubeVertices[] = {
    // Front (+Z)
    -0.5f, -0.5f,  0.5f,  0.0f, 1.0f, // bottom-left
     0.5f, -0.5f,  0.5f,  1.0f, 1.0f, // bottom-right
     0.5f,  0.5f,  0.5f,  1.0f, 0.0f, // top-right
    -0.5f,  0.5f,  0.5f,  0.0f, 0.0f, // top-left

    // Back (-Z)
     0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,  1.0f, 1.0f,
    -0.5f,  0.5f, -0.5f,  1.0f, 0.0f,
     0.5f,  0.5f, -0.5f,  0.0f, 0.0f,

    // Left (-X)
    -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
    -0.5f, -0.5f,  0.5f,  1.0f, 1.0f,
    -0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
    -0.5f,  0.5f, -0.5f,  0.0f, 0.0f,

    // Right (+X)
     0.5f, -0.5f,  0.5f,  0.0f, 1.0f,
     0.5f, -0.5f, -0.5f,  1.0f, 1.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 0.0f,
     0.5f,  0.5f,  0.5f,  0.0f, 0.0f,

    // Top (+Y)
    -0.5f,  0.5f,  0.5f,  0.0f, 1.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 1.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 0.0f,
    -0.5f,  0.5f, -0.5f,  0.0f, 0.0f,

    // Bottom (-Y)
    -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
     0.5f, -0.5f, -0.5f,  1.0f, 1.0f,
     0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
    -0.5f, -0.5f,  0.5f,  0.0f, 0.0f
};

uint16 cubeIndices[] = {
    // Front
    0, 1, 2,  0, 2, 3,
    // Back
    4, 5, 6,  4, 6, 7,
    // Left
    8, 9,10,  8,10,11,
    // Right
    12,13,14, 12,14,15,
     // Top
    16,17,18, 16,18,19,
     // Bottom
    20,21,22, 20,22,23
};

App gApp;
AssetManager gAssetManager;
Renderer gRenderer;
UIState gUI;

VertexLayout gCubeVertexLayout = {};
Buffer* pVBCube = NULL;
Buffer* pIBCube = NULL;
Texture* pTexCube = NULL;

Sampler* pSamplerPoint = NULL;

// Unlit
RenderTarget*       pTargetUnlit = NULL;
RenderTarget*       pDepthUnlit = NULL;
Shader*             pVSUnlit = NULL;
Shader*             pFSUnlit = NULL;
GraphicsPipeline*   pPipelineUnlit = NULL;
DescriptorSet*      pDescriptorSetPerFrame = NULL;

struct PerFrameUniforms
{
    m4f mWorld = {};
    m4f mView = {};
    m4f mProj = {};
};
PerFrameUniforms perFrameUniforms = {};
Buffer* pUBPerFrame = NULL;

Camera gCamera = {};
Timer gTimer = {};
uint32 gFrame = 0;
GpuTimer gGpuTimer = {};

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
    loadShader(&gAssetManager, &gRenderer, 
            str("../../res/shaders/unlit.vert"), 
            &pVSUnlit);
    loadShader(&gAssetManager, &gRenderer, 
            str("../../res/shaders/unlit.frag"), 
            &pFSUnlit);
}

void removeShaders()
{
    removeShader(&gRenderer, &pFSUnlit);
    removeShader(&gRenderer, &pVSUnlit);
}

void addDescriptors()
{
    // Per frame
    {
        DescriptorSetDesc desc = {};
        desc.mCount = 3;
        desc.mResources[0] = { DESCRIPTOR_UNIFORM_BUFFER, pUBPerFrame, 1 };
        desc.mResources[1] = { DESCRIPTOR_TEXTURE, pTexCube, 1 };
        desc.mResources[2] = { DESCRIPTOR_SAMPLER, pSamplerPoint, 1 };
        addDescriptorSet(&gRenderer, desc, &pDescriptorSetPerFrame);
    }
}

void removeDescriptors()
{
    removeDescriptorSet(&gRenderer, &pDescriptorSetPerFrame);
}

void addPipelines()
{
    // Fullscreen pipeline
    {
        GraphicsPipelineDesc desc = {};
        desc.mRenderTargetCount = 1;
        desc.mRenderTargetFormats[0] = pTargetUnlit->mDesc.mFormat;
        desc.mDepthTargetFormat = pDepthUnlit->mDesc.mFormat;

        desc.mVertexLayout = gCubeVertexLayout;
        desc.pVS = pVSUnlit;
        desc.pFS = pFSUnlit;

        desc.mCullMode = CULL_MODE_NONE;
        desc.mFrontFace = FRONT_FACE_CW;

        desc.mDepthTest = true;
        desc.mDepthWrite = true;
        desc.mDepthOp = COMPARE_GREATER;

        desc.mDescriptorSetCount = 1;
        desc.pDescriptorSets[0] = pDescriptorSetPerFrame;

        addPipeline(&gRenderer, desc, &pPipelineUnlit);
    }
}

void removePipelines()
{
    removePipeline(&gRenderer, &pPipelineUnlit);
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

    // Cube vertex/index buffer
    {
        VertexLayoutDesc layoutDesc = {};
        layoutDesc.mCount = 2;
        layoutDesc.mAttribs[0] = ATTRIBUTE_FLOAT3;
        layoutDesc.mAttribs[1] = ATTRIBUTE_FLOAT2;
        initVertexLayout(layoutDesc, &gCubeVertexLayout);

        BufferDesc vbDesc = {};
        vbDesc.mType = BUFFER_TYPE_VERTEX;
        vbDesc.mSize = ARR_SIZE(cubeVertices);
        vbDesc.mCount = ARR_LEN(cubeVertices);
        vbDesc.mStride = sizeof(float);
        addBuffer(&gRenderer, vbDesc, &pVBCube, cubeVertices);

        BufferDesc ibDesc = {};
        ibDesc.mType = BUFFER_TYPE_INDEX;
        ibDesc.mSize = ARR_SIZE(cubeIndices);
        ibDesc.mCount = ARR_LEN(cubeIndices);
        ibDesc.mStride = sizeof(uint16);
        addBuffer(&gRenderer, ibDesc, &pIBCube, cubeIndices);
    }

    // Cube texture
    {
        loadTexture(&gAssetManager, &gRenderer, 
                str("../../res/textures/default_color.png"),
                false, 
                &pTexCube);
        SamplerDesc desc = {};
        addSampler(&gRenderer, desc, &pSamplerPoint);
    }

    // Per frame uniform buffer
    {
        BufferDesc desc = {};
        desc.mType = BUFFER_TYPE_UNIFORM;
        desc.mSize = sizeof(PerFrameUniforms);
        desc.mCount = 1;
        desc.mStride = sizeof(PerFrameUniforms);
        addBuffer(&gRenderer, desc, &pUBPerFrame);
    }

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

    removeSampler(&gRenderer, &pSamplerPoint);
    removeTexture(&gRenderer, &pTexCube);
    removeBuffer(&gRenderer, &pUBPerFrame);
    removeBuffer(&gRenderer, &pIBCube);
    removeBuffer(&gRenderer, &pVBCube);

    destroyGpuTimer(&gGpuTimer);
    destroyUI(&gUI);
    destroyRenderer(&gRenderer);
    destroyAssetManager(&gAssetManager);
    destroyApp(&gApp);
}

void updatePerFrameUniforms()
{
    perFrameUniforms.mWorld = identity();
    perFrameUniforms.mView = getView(&gCamera);
    perFrameUniforms.mProj = getProj(&gCamera);
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

    updatePerFrameUniforms();
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
    copyToBuffer(&gRenderer, pUBPerFrame, 0, &perFrameUniforms, sizeof(PerFrameUniforms));
    gpuTimestamp(str("Upload PerFrame"), &gpuTimerParams);

    // Unlit pass
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

        cmdBindDescriptorSet(pCmd, pPipelineUnlit, pDescriptorSetPerFrame, 0);
        cmdBindGraphicsPipeline(pCmd, pPipelineUnlit);

        cmdSetViewport(pCmd, pTargetUnlit);
        cmdSetScissor(pCmd, pTargetUnlit);

        cmdBindVertexBuffer(pCmd, pVBCube);
        cmdBindIndexBuffer(pCmd, pIBCube);

        cmdDrawIndexed(pCmd, ARR_LEN(cubeIndices), 1);

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

