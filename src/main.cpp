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
#include "../dw/src/render/texture.hpp"
#include "../dw/src/render/ui.hpp"

#include "../src/shared_defines.hpp"
#include "../src/scene.hpp"
#include "../src/renderer.hpp"

App gApp;
AssetManager gAssetManager;
Renderer gRenderer;
UIState gUI;

uint32 gFrame = 0;

// TODO(caio): Move these to scene so current model can be changed via UI
String gModelPaths[] =
{
    str("../../res/models/sponza/glTF/Sponza.gltf"),
    str("../../res/models/metal_rough_spheres/MetalRoughSpheres.gltf"),
    str("../../res/models/helmet/DamagedHelmet.gltf"),
};
String gModelNames[] =
{
    str("Sponza"),
    str("Metal Roughness Spheres"),
    str("Helmet"),
};
uint32 gCurrentModel = 0;

// Helia
Scene gScene = {};
SceneRenderer gSceneRenderer = {};

void addRenderTargets()
{
    addSceneRenderTargets(&gSceneRenderer);
}

void removeRenderTargets()
{
    removeSceneRenderTargets(&gSceneRenderer);
}

void addShaders()
{
    addSceneShaders(&gSceneRenderer);
}

void removeShaders()
{
    removeSceneShaders(&gSceneRenderer);
}

void addResources()
{
    addSceneResources(&gSceneRenderer);
}

void removeResources()
{
    removeSceneResources(&gSceneRenderer);
}

void addPipelines()
{
    addScenePipelines(&gSceneRenderer);
}

void removePipelines()
{
    removeScenePipelines(&gSceneRenderer);
}

void loadScene(uint32 modelId)
{
    AssetManagerDesc assetManagerDesc;
    assetManagerDesc.mPermanentArenaSize = GB(1);
    assetManagerDesc.mTempArenaSize = MB(128);
    initAssetManager(assetManagerDesc, &gAssetManager);

    initScene(&gScene, MB(512), MB(128), gCurrentModel);
    String texPaths[SCENE_MAX_TEXTURES];
    uint32 texCount = 0;
    setupSceneModel(&gScene, gModelPaths[modelId]);
    initSceneRenderer(&gSceneRenderer, &gApp, &gRenderer, &gAssetManager, &gUI, &gScene, 
            getFileDir(gModelPaths[modelId]));

    addRenderTargets();
    addShaders();
    addResources();
    addPipelines();
}

void unloadScene()
{
    removePipelines();
    removeResources();
    removeShaders();
    removeRenderTargets();

    destroySceneRenderer(&gSceneRenderer);
    destroyScene(&gScene);

    destroyAssetManager(&gAssetManager);
}

void init()
{
    initApp(APP_WIDTH, APP_HEIGHT, "DW App", &gApp);

    RendererDesc rendererDesc = {};
    rendererDesc.pApp = &gApp;
    initRenderer(rendererDesc, &gRenderer);
    initShaderCompiler();

    UIDesc uiDesc = {};
    uiDesc.pApp = &gApp;
    uiDesc.pRenderer = &gRenderer;
    uiDesc.mTargetFormat = FORMAT_RGBA8_UNORM;
    initUI(uiDesc, &gUI);

    loadScene(gCurrentModel);
}

void shutdown()
{
    waitForCommands(&gRenderer);

    unloadScene(); 

    destroyUI(&gUI);
    destroyShaderCompiler();
    destroyRenderer(&gRenderer);
    destroyApp(&gApp);
}

void update()
{
    PROFILE_SCOPE;
    if(isJustDown(&gApp.mKeys, KEY_R))
    {
        addLoadRequest(&gApp, LOAD_REQUEST_SHADER);
    }

    if(isJustDown(&gApp.mKeys, KEY_O))
    {
        addLoadRequest(&gApp, LOAD_REQUEST_SCENE);
    }

    if(isJustDown(&gApp.mKeys, KEY_F))
    {
        freezeMainCamera(&gSceneRenderer, !gSceneRenderer.mFreezeMainCam);
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
        moveCamera(&gSceneRenderer.mCamera, moveDir, gApp.mDt);
        
        bool isRotating = isDown(&gApp.mKeys, KEY_RMB);
        setHidden(&gApp.mCursor, isRotating);
        setLocked(&gApp.mCursor, isRotating);
        if(isRotating)
        {
            setHidden(&gApp.mCursor, true);
            setLocked(&gApp.mCursor, true);
            v2f rotateDir;
            getDelta(&gApp.mCursor, &rotateDir.x, &rotateDir.y);
            rotateCamera(&gSceneRenderer.mCamera, -rotateDir, gApp.mDt);
        }
    }

    updateCamera(&gSceneRenderer.mCamera, gApp.mDt);

    updatePerFrameData(&gSceneRenderer, gFrame);
}

void render()
{
    renderScene(&gSceneRenderer, gFrame);
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
        removeResources();
        removeRenderTargets();
        destroySwapChain(&gRenderer, &gRenderer.mSwapChain);

        initSwapChain(&gRenderer, &gRenderer.mSwapChain);
        addRenderTargets();
        addResources();
        addPipelines();

        gSceneRenderer.mCamera.mDesc.mAspect = getAspectRatio(&gApp);

        removeLoadRequest(&gApp, LOAD_REQUEST_RESIZE);
    }
    if(pApp->mLoadRequests & LOAD_REQUEST_SHADER)
    {
        LOG("Reloading shaders...");
        removePipelines();
        removeResources();
        removeRenderTargets();
        removeShaders();

        addShaders();
        addRenderTargets();
        addResources();
        addPipelines();
        removeLoadRequest(&gApp, LOAD_REQUEST_SHADER);
    }
    if(pApp->mLoadRequests & LOAD_REQUEST_SCENE)
    {
        LOG("Reloading scene...");

        unloadScene();
        loadScene(gCurrentModel);
        removeLoadRequest(&gApp, LOAD_REQUEST_SCENE);
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

