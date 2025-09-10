#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderTarget.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/TextureSampler.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>

#include <utils/EntityManager.h>

#include <image/ImageSampler.h>
#include <image/LinearImage.h>

#include <filamentapp/Config.h>
#include <filamentapp/FilamentApp.h>

#include <cmath>
#include <fstream>
#include <iostream>

#define NOMINMAX
#include<Windows.h>

#include <filament/Renderer.h>
#include<filament/LightManager.h>
#include<thread>
#include<chrono>
#include "generated/resources/resources.h"

using namespace filament;
using namespace filament::math;

using utils::Entity;
using utils::EntityManager;

using MinFilter = TextureSampler::MinFilter;
using MagFilter = TextureSampler::MagFilter;
using AttributeType = VertexBuffer::AttributeType;

extern "C" __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;

struct SSurfel {
    float3 _Position;
    float3 _Normal;
    float _Radius;
    float4 _Color;
};

struct SVertex {
    math::float3 _Position;
    math::float3 _Normal;
    float _Radius;
    math::float4 _Color;
    math::float2 _Quad;
};

struct SApp {
    Engine* _pEngine;
    //Camera* _pCamera;
    // Entity camera;
    Skybox* _pSkybox;

    VertexBuffer* _pVb;
    IndexBuffer* _pIb;
    Material* _pMat;
    MaterialInstance* _pMatInstance;

    Texture* _pTex;
    Entity _Renderable;

    std::vector<SSurfel> _Surfels;
    std::vector<SVertex> _Vertices;
    float3 _MinBounds;
    float3 _MaxBounds;
};

static const char* pRsfPath = "MyAssets/painted_santa_kd.rsf";

static void initVbIb(Engine* vEngine, SApp& vApp);
static void setupCamera(SApp& vApp);
static void createSplatTexture(Engine* vEngine, SApp& vApp);
static void createPointRender(Engine* vEngine, Scene* vScene, SApp& vApp);
static void createSurfaceSplat(Engine* vEngine, Scene* vScene, SApp& vApp);

static bool readBinFile(const std::string& vFileName, std::vector<char>& voBuffer);
static bool loadRsfFile(const std::string& vFileName, std::vector<SSurfel>& voSurfels);

static std::vector<uint32_t> kIndices;

// 窗口回调
LRESULT CALLBACK WndProc(HWND vHwnd, UINT vMsg, WPARAM vWParam, LPARAM vLParam) {
    if (vMsg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(vHwnd, vMsg, vWParam, vLParam);
}

auto dumpCamera = [&](Camera* c, const char* tag) {
    auto eye = c->getPosition();
    auto fwd = c->getForwardVector();
    auto up = c->getUpVector();
    auto vm = c->getViewMatrix();
    printf("[%s] eye: %f %f %f\n", tag, eye.x, eye.y, eye.z);
    printf("[%s] fwd: %f %f %f\n", tag, fwd.x, fwd.y, fwd.z);
    printf("[%s] up : %f %f %f\n", tag, up.x, up.y, up.z);
    printf("[%s] viewMat:\n", tag);
    // 注意 mat4f 索引方式
    for (int r = 0; r < 4; ++r)
        printf("%f %f %f %f\n", vm(r, 0), vm(r, 1), vm(r, 2), vm(r, 3));
    };

filament::VertexBuffer* createCubeVertexBuffer(filament::Engine& vEngine) {
    static const float cubeVertices[] = {
        // pos          // tangents (xyz=normal, w=sign) // uv
        -1,-1, 1,       0,0,1,1,   0,0,
         1,-1, 1,       0,0,1,1,   1,0,
         1, 1, 1,       0,0,1,1,   1,1,
        -1, 1, 1,       0,0,1,1,   0,1,
        -1,-1,-1,       0,0,-1,1,  1,0,
        -1, 1,-1,       0,0,-1,1,  1,1,
         1, 1,-1,       0,0,-1,1,  0,1,
         1,-1,-1,       0,0,-1,1,  0,0,
    };

    filament::VertexBuffer* vb = filament::VertexBuffer::Builder()
        .vertexCount(8)
        .bufferCount(1)
        .attribute(VertexAttribute::POSITION, 0,
            VertexBuffer::AttributeType::FLOAT3, 0, sizeof(float) * 9)
        .attribute(VertexAttribute::TANGENTS, 0,
            VertexBuffer::AttributeType::FLOAT4, sizeof(float) * 3, sizeof(float) * 9)
        .attribute(VertexAttribute::UV0, 0,
            VertexBuffer::AttributeType::FLOAT2, sizeof(float) * 7, sizeof(float) * 9)
        .build(vEngine);

    vb->setBufferAt(vEngine, 0,
        filament::VertexBuffer::BufferDescriptor(cubeVertices, sizeof(cubeVertices), nullptr));

    return vb;
}

filament::IndexBuffer* createCubeIndexBuffer(filament::Engine& vEngine) {
    static const uint16_t cubeIndices[] = {
        //front
        0, 1, 2,  2, 3, 0,
        //back
        4, 5, 6,  6, 7, 4,
        //left
        4, 0, 3,  3, 5, 4,
        //right
        1, 7, 6,  6, 2, 1,
        //top
        3, 2, 6,  6, 5, 3,
        //bottom
        4, 7, 1,  1, 0, 4
    };

    filament::IndexBuffer* ib = filament::IndexBuffer::Builder()
        .indexCount(36)
        .bufferType(filament::IndexBuffer::IndexType::USHORT)
        .build(vEngine);

    ib->setBuffer(vEngine, filament::IndexBuffer::BufferDescriptor(cubeIndices, sizeof(cubeIndices), nullptr));

    return ib;
}
int main(int argc, char* argv[]) {
    HINSTANCE hInstance = GetModuleHandle(nullptr);
    const char CLASS_NAME[] = "FilamentWindowClass";

    // 注册窗口
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    RegisterClass(&wc);

    // 创建窗口
    HWND hwnd = CreateWindowEx(
        0, CLASS_NAME, "Filament Cube Example",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        800, 600, nullptr, nullptr, hInstance, nullptr
    );
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    // Filament 初始化
    Engine* pEngine = Engine::create();
    SwapChain* pSwapChain = pEngine->createSwapChain((void*)hwnd);
    Renderer* pRenderer = pEngine->createRenderer();
    Scene* pScene = pEngine->createScene();
    View* pView = pEngine->createView();
    pView->setScene(pScene);
    pView->setViewport({ 0, 0, 800, 600 });
    Skybox* pSkybox = Skybox::Builder().color({ 0.1, 0.125, 0.25, 1.0 }).build(*pEngine);
    pScene->setSkybox(pSkybox);

    Entity light = EntityManager::get().create();
    LightManager::Builder(LightManager::Type::DIRECTIONAL)
        .color({ 1.0f, 1.0f, 1.0f })
        .intensity(100000.0f)
        .direction({ 1.0f, 1.0f, 1.0f })
        .castShadows(false)
        .build(*pEngine, light);

    pScene->addEntity(light);

    // 相机
    Entity cameraEntity = EntityManager::get().create();
    Camera* camera = pEngine->createCamera(cameraEntity);

    camera->setProjection(60.0, 800.0 / 600.0, 0.1, 100.0);

    camera->lookAt({ 2,2,2 }, { 0,0,0 }, { 0,1,0 });
    pView->setCamera(camera);

    //filament::VertexBuffer* pCubeVb = createCubeVertexBuffer(*engine);
    //filament::IndexBuffer* pCubeIb = createCubeIndexBuffer(*engine);
    //Material* pMat = Material::Builder()
    //    .package(RESOURCES_TESTWORLDPOS_DATA, RESOURCES_TESTWORLDPOS_SIZE)
    //    .build(*engine);
    //MaterialInstance* pMatInstance = pMat->createInstance();
    //Entity cube = EntityManager::get().create();
    //RenderableManager::Builder(1)
    //    .boundingBox({ {-1,-1,-1},{1,1,1} })
    //    .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, pCubeVb, pCubeIb)
    //    .culling(false)
    //    .material(0, pMatInstance)
    //    .build(*engine, cube);
    //pScene->addEntity(cube);

    SApp app;
    createSplatTexture(pEngine, app);
    // 加载rsf文件并初始化Vb,Ib
    initVbIb(pEngine, app);
    createSurfaceSplat(pEngine, pScene, app);

    MSG msg = {};
    bool running = true;

    while (running) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        auto start = std::chrono::high_resolution_clock::now();

        if (pRenderer->beginFrame(pSwapChain)) {
            pRenderer->render(pView);
            pRenderer->endFrame();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        if (elapsed < 16) { // 限制到 ~60 FPS
            std::this_thread::sleep_for(std::chrono::milliseconds(16 - elapsed));
        }
    }

    // 清理
    pEngine->destroy(cameraEntity);
    pEngine->destroy(pView);
    pEngine->destroy(pScene);
    pEngine->destroy(pRenderer);
    pEngine->destroy(pSwapChain);
    Engine::destroy(&pEngine);

    return 0;
}

constexpr uint32_t TEXTURE_SIZE = 128;
static void createSplatTexture(Engine* vEngine, SApp& vApp) {
    static image::LinearImage splat(3, 3, 1);
    splat.getPixelRef(1, 1)[0] = 0.25f;
    splat = image::resampleImage(splat, TEXTURE_SIZE, TEXTURE_SIZE,
        image::Filter::GAUSSIAN_SCALARS);
    // 创建纹理缓冲区
    Texture::PixelBufferDescriptor buffer(splat.getPixelRef(),
        size_t(TEXTURE_SIZE * TEXTURE_SIZE * sizeof(float)), Texture::Format::R,
        Texture::Type::FLOAT);
    vApp._pTex = Texture::Builder()
        .width(TEXTURE_SIZE)
        .height(TEXTURE_SIZE)
        .levels(1)
        .sampler(Texture::Sampler::SAMPLER_2D)
        .format(Texture::InternalFormat::R32F)
        .build(*vEngine);
    vApp._pTex->setImage(*vEngine, 0, std::move(buffer));
}
void loadtestFile(std::vector<SSurfel>& voSurfels) {
    voSurfels.resize(3);
    for (size_t i = 0; i < voSurfels.size(); ++i) {
        voSurfels[i]._Position = float3(0 + i, i + i * i, 0 + i * i);
        voSurfels[i]._Radius = 2.0f;
        voSurfels[i]._Normal = float3(0, 0, -1);
        voSurfels[i]._Color = float4(1, 0, 0, 1);
    }
}

static void initVbIb(Engine* vEngine, SApp& vApp) {
    // 加载Rsf文件
    std::cout << "isloadRsfSuccess : " << loadRsfFile(FilamentApp::getRootAssetsPath() + pRsfPath, vApp._Surfels) << std::endl;
    //loadtestFile(vApp._Surfels);
    std::cout << "Point number: " << vApp._Surfels.size() << std::endl;
    // VBO
    vApp._Vertices.reserve(vApp._Surfels.size() * 4);
    static const math::float2 quad[4] = { { -1, -1 }, { -1, 1 }, { 1, -1 }, { 1, 1 } };
    for (const auto& s : vApp._Surfels) {
        for (int i = 0; i < 4; ++i)
            vApp._Vertices.push_back({ s._Position, s._Normal, s._Radius, s._Color, quad[i] });
    }
    const uint32_t pointNumber = vApp._Vertices.size();
    std::cout << "Vertex number: " << pointNumber << std::endl;
    // VI
    static constexpr uint32_t quadIndices[6] = { 0, 1, 2, 3, 2, 1 };
    kIndices.reserve(vApp._Surfels.size() * 6);
    for (uint32_t surfId = 0; surfId < vApp._Surfels.size(); ++surfId) {
        uint32_t base = surfId * 4; // 每个 surfel 占 4 个顶点
        for (uint32_t k = 0; k < 6; ++k) kIndices.push_back(base + quadIndices[k]);
    }

    float minx = 1e9, miny = 1e9, minz = 1e9;
    float maxx = -1e9, maxy = -1e9, maxz = -1e9;
    for (size_t i = 0; i < pointNumber; i++) {
        minx = std::min(minx, vApp._Vertices[i]._Position.x);
        miny = std::min(miny, vApp._Vertices[i]._Position.y);
        minz = std::min(minz, vApp._Vertices[i]._Position.z);
        maxx = std::max(maxx, vApp._Vertices[i]._Position.x);
        maxy = std::max(maxy, vApp._Vertices[i]._Position.y);
        maxz = std::max(maxz, vApp._Vertices[i]._Position.z);
    }
    vApp._MinBounds = { minx, miny, minz };
    vApp._MaxBounds = { maxx, maxy, maxz };
    std::cout << minx << " " << miny << " " << minz << " " << maxx << " " << maxy << " " << maxz
        << std::endl;

    for (size_t i = 0; i < pointNumber; i++) {
        vApp._Vertices[i]._Position.x =
            2.0f * (vApp._Vertices[i]._Position.x - minx) / (maxx - minx) - 1.0f;
        vApp._Vertices[i]._Position.y =
            2.0f * (vApp._Vertices[i]._Position.y - miny) / (maxy - miny) - 1.0f;
        vApp._Vertices[i]._Position.z =
            2.0f * (vApp._Vertices[i]._Position.z - minz) / (maxz - minz) - 1.0f;
    }


    vApp._pVb = VertexBuffer::Builder()
        .vertexCount(static_cast<uint32_t>(vApp._Vertices.size()))
        .bufferCount(1)
        .attribute(VertexAttribute::POSITION, 0, AttributeType::FLOAT3,
            offsetof(SVertex, _Position), sizeof(SVertex))
        .attribute(VertexAttribute::CUSTOM1, 0, AttributeType::FLOAT3,
            offsetof(SVertex, _Normal), sizeof(SVertex))
        .attribute(VertexAttribute::CUSTOM0, 0, AttributeType::FLOAT,
            offsetof(SVertex, _Radius), sizeof(SVertex))
        .attribute(VertexAttribute::COLOR, 0, AttributeType::FLOAT4,
            offsetof(SVertex, _Color), sizeof(SVertex))
        .attribute(VertexAttribute::CUSTOM2, 0, AttributeType::FLOAT2,
            offsetof(SVertex, _Quad), sizeof(SVertex))
        .build(*vEngine);
    vApp._pVb->setBufferAt(*vEngine, 0,
        VertexBuffer::BufferDescriptor(vApp._Vertices.data(),
            vApp._Vertices.size() * sizeof(SVertex), nullptr));

    vApp._pIb = IndexBuffer::Builder()
        .indexCount(kIndices.size())
        .bufferType(IndexBuffer::IndexType::UINT)
        .build(*vEngine);

    size_t byteCount = kIndices.size() * sizeof(uint32_t);
    vApp._pIb->setBuffer(*vEngine, IndexBuffer::BufferDescriptor(kIndices.data(), byteCount, nullptr));
}
static void createSurfaceSplat(Engine* vEngine, Scene* vScene, SApp& vApp) {
    // 创建材质
    vApp._pMat = Material::Builder().package(RESOURCES_SPLAT_DATA, RESOURCES_SPLAT_SIZE)
        .build(*vEngine);
    vApp._Renderable = EntityManager::get().create();
    vApp._pMatInstance = vApp._pMat->createInstance();

    vApp._pMatInstance->setParameter("radiusScale", 0.15f);
    vApp._pMatInstance->setParameter("forwardFactor", 0.5f);
    vApp._pMatInstance->setParameter("depthPrepass", false);

    RenderableManager::Builder(1)
        .boundingBox({ vApp._MinBounds, vApp._MaxBounds })
        .material(0, vApp._pMatInstance)
        .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vApp._pVb, vApp._pIb, 0,
            vApp._Surfels.size() * 6)
        .culling(false)
        .receiveShadows(false)
        .castShadows(false)
        .build(*vEngine, vApp._Renderable);
    vScene->addEntity(vApp._Renderable);

    auto& tm = vEngine->getTransformManager();
    auto instance = tm.getInstance(vApp._Renderable);
    mat4f modelMat = tm.getTransform(instance);

    // 打印
    std::cout << "Model Matrix:" << std::endl;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++)
            std::cout << modelMat[i][j] << " ";
        std::cout << std::endl;
    }
}
// 摄像机设置
//static void setupCamera(SApp& vApp) {
//    // 计算点云中心和合适的距离
//    float centerX = (vApp._MinBounds.x + vApp._MaxBounds.x) / 2.0f;
//    float centerY = (vApp._MinBounds.y + vApp._MaxBounds.y) / 2.0f;
//    float centerZ = (vApp._MinBounds.z + vApp._MaxBounds.z) / 2.0f;
//    float sizeX = vApp._MaxBounds.x - vApp._MinBounds.x;
//    float sizeY = vApp._MaxBounds.y - vApp._MinBounds.y;
//    float sizeZ = vApp._MaxBounds.z - vApp._MinBounds.z;
//    float _Radius = std::sqrt(sizeX * sizeX + sizeY * sizeY + sizeZ * sizeZ) * 0.5f;
//    float cameraDistance = _Radius * 2.5f;
//
//    filament::math::float3 eye(centerX + cameraDistance, centerY, centerZ + cameraDistance);
//    filament::math::float3 lookAt(centerX, centerY, centerZ);
//    filament::math::float3 up(0, 1, 0);
//    vApp._pCamera->lookAt(eye, lookAt, up);
//    std::cout << vApp._pCamera->getPosition().x << " " << vApp._pCamera->getPosition().y << " "
//              << vApp._pCamera->getPosition().z << std::endl;
//}

static bool readBinFile(const std::string& vFileName, std::vector<char>& voBuffer) {
    std::ifstream File(vFileName, std::ios::binary);
    if (!File) {
        std::cerr << "cannot open this file: " << vFileName << '\n';
        return false;
    }
    File.seekg(0, std::ios::end);
    const std::streamsize Size = File.tellg();
    File.seekg(0, std::ios::beg);
    voBuffer.resize(Size);
    if (!File.read(voBuffer.data(), Size)) {
        std::cerr << "fail to read file:" << vFileName << '\n';
    }
    File.close();
    return true;
}
static bool loadRsfFile(const std::string& vFileName, std::vector<SSurfel>& voSurfels) {
    std::vector<char> Buffer;
    if (!readBinFile(vFileName, Buffer)) return false;

    std::array<std::uint32_t, 4> Header;
    std::array<float, 6> Bounds;
    std::memcpy(Header.data(), Buffer.data(), sizeof(Header));
    std::memcpy(Bounds.data(), Buffer.data() + sizeof(Header), sizeof(Bounds));

    const std::uint32_t NumSurfels = Header[0];
    // const std::uint32_t NumSurfels = 10000;
    std::vector<float> PosRadiusNormal(static_cast<size_t>(NumSurfels * 8));
    std::vector<std::uint8_t> Colors(static_cast<size_t>(NumSurfels * 4));
    std::memcpy(PosRadiusNormal.data(), Buffer.data() + Header[1],
        sizeof(float) * PosRadiusNormal.size());
    std::memcpy(Colors.data(), Buffer.data() + Header[1] + sizeof(float) * PosRadiusNormal.size(),
        sizeof(std::uint8_t) * Colors.size());

    voSurfels.resize(NumSurfels);
    for (size_t i = 0; i < voSurfels.size(); ++i) {
        voSurfels[i]._Position = float3(PosRadiusNormal[i * 8 + 0], PosRadiusNormal[i * 8 + 1],
            PosRadiusNormal[i * 8 + 2]);
        voSurfels[i]._Radius = PosRadiusNormal[i * 8 + 3];
        voSurfels[i]._Normal = float3(PosRadiusNormal[i * 8 + 4], PosRadiusNormal[i * 8 + 5],
            PosRadiusNormal[i * 8 + 6]);
        voSurfels[i]._Color = float4(static_cast<float>(Colors[i * 4 + 0]) / 255.0f,
            static_cast<float>(Colors[i * 4 + 1]) / 255.0f,
            static_cast<float>(Colors[i * 4 + 2]) / 255.0f,
            static_cast<float>(Colors[i * 4 + 3]) / 255.0f);
    }
    return true;
}
