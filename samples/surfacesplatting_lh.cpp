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

#include <math.h>

#include <cmath>
#include <fstream>
#include <iostream>

#include "generated/resources/resources.h"

using namespace filament;
using namespace filament::math;

using utils::Entity;
using utils::EntityManager;

using AttributeType = VertexBuffer::AttributeType;

struct SSurfel {
    float3 _Position;
    float3 _Normal;
    float _Radius;
    float4 _Color;
};

struct SApp {
    Engine* _pEngine;
    Camera* _pCamera;
    Skybox* _pSkybox;

    VertexBuffer* _pVb;
    IndexBuffer* _pIb;
    Material* _pMat;
    MaterialInstance* _pMatInstance;

    Entity _RenderableEntity;

    std::vector<SSurfel> _Surfels;
    std::vector<uint32_t> _Indexs;
    float3 _MinBounds;
    float3 _MaxBounds;
};

static const char* pRsfPath =
        "F:/Filament/Windows/SurfaceSplatting-filament/samples/MyAssets/painted_santa_kd.rsf";

static void setupCamera(SApp& vApp);
static void createPointRender(Engine* vEngine, Scene* vScene, SApp& vApp);

static inline float radians(float degress);

static bool readBinFile(const std::string& vFileName, std::vector<char>& voBuffer);
static bool loadRsfFile(const std::string& vFileName, std::vector<SSurfel>& voSurfels);
int main(int argc, char* argv[]) {
    Config config;
    config.title = "surfacesplatting";
    SApp app;

    auto setup = [&app](Engine* vEngine, View* vView, Scene* vScene) {
        app._pEngine = vEngine;

        std::cout << "isLoadRsfSuccess:" << loadRsfFile(pRsfPath, app._Surfels) << std::endl;

        #pragma region InitVA0AndIA0
        const uint32_t pointNumber = app._Surfels.size();
        app._Indexs.resize(pointNumber);
        for (size_t i = 0; i < pointNumber; ++i) app._Indexs[i] = static_cast<uint32_t>(i);
        app._pVb = VertexBuffer::Builder()
                           .vertexCount(pointNumber)
                           .bufferCount(1)
                           .attribute(VertexAttribute::POSITION, 0, AttributeType::FLOAT3,
                                   offsetof(SSurfel, _Position), sizeof(SSurfel))
                           .attribute(VertexAttribute::TANGENTS, 0, AttributeType::FLOAT3,
                                   offsetof(SSurfel, _Normal), sizeof(SSurfel))
                           .attribute(VertexAttribute::CUSTOM0, 0, AttributeType::FLOAT,
                                   offsetof(SSurfel, _Radius), sizeof(SSurfel))
                           .attribute(VertexAttribute::COLOR, 0, AttributeType::FLOAT4,
                                   offsetof(SSurfel, _Color), sizeof(SSurfel))
                           .build(*vEngine);
        app._pVb->setBufferAt(*vEngine, 0,
                VertexBuffer::BufferDescriptor(app._Surfels.data(),
                        app._Surfels.size() * sizeof(SSurfel), nullptr));

        app._pIb = IndexBuffer::Builder()
                           .indexCount(app._Indexs.size())
                           .bufferType(IndexBuffer::IndexType::UINT)
                           .build(*vEngine);
        app._pIb->setBuffer(*vEngine, IndexBuffer::BufferDescriptor(app._Indexs.data(),
                                              app._Indexs.size() * sizeof(uint32_t), nullptr));
        #pragma endregion

        #pragma region LoadMat
        app._pMat = Material::Builder()
                            .package(RESOURCES_POINTRENDER_DATA, RESOURCES_POINTRENDER_SIZE)
                            .build(*vEngine);
        app._RenderableEntity = EntityManager::get().create();
        app._pMatInstance = app._pMat->createInstance();
        app._pMatInstance->setParameter("pointSizeScale", 10.0f);

        RenderableManager::Builder(1)
                .material(0, app._pMatInstance)
                .geometry(0, RenderableManager::PrimitiveType::POINTS, app._pVb, app._pIb)
                .culling(false)
                .receiveShadows(false)
                .castShadows(false)
                .build(*vEngine, app._RenderableEntity);
        auto& tcm = vEngine->getTransformManager();
        tcm.setTransform(tcm.getInstance(app._RenderableEntity), mat4f::scaling(0.07f));
        vScene->addEntity(app._RenderableEntity);
        #pragma endregion

        app._pCamera = &vView->getCamera();
        Viewport vp = vView->getViewport();
        app._pCamera->setProjection(60.0f, float(vp.width) / float(vp.height), 0.1f,100.0f);

        app._pSkybox = Skybox::Builder().color({ 0.1, 0.125, 0.25, 1.0 }).build(*vEngine);
        vScene->setSkybox(app._pSkybox);
    };
    auto cleanup = [&app](Engine* vEngine, View* view, Scene* vScene) {
        vEngine->destroy(app._pSkybox);
        vEngine->destroy(app._RenderableEntity);
        vEngine->destroy(app._pMatInstance);
        vEngine->destroy(app._pMat);
        vEngine->destroy(app._pVb);
        vEngine->destroy(app._pIb);
    };

    FilamentApp& filamentApp = FilamentApp::get();
    //filamentApp.animate([&app](Engine* engine, View* view, double now) {
    //    const uint32_t w = view->getViewport().width;
    //    const uint32_t h = view->getViewport().height;
    //    const float aspect = (float) w / h;
    //    app._pCamera->setProjection(60.0f, aspect, 0.1f, 100.0f);
    //    app._pCamera->lookAt(float3(0, 0, 100), float3(0, 0, 0));
    //});
    filamentApp.run(config, setup, cleanup);
    return 0;
}

static inline float radians(float degress) { return degress * M_PI / 180.0f; }
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
