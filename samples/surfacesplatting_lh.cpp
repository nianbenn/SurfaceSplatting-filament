#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderTarget.h>
#include <filament/RenderableManager.h>
#include <filament/Renderer.h>
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

#include "generated/resources/resources.h"

using namespace filament;
using namespace filament::math;

using utils::Entity;
using utils::EntityManager;

using MinFilter = TextureSampler::MinFilter;
using MagFilter = TextureSampler::MagFilter;
using AttributeType = VertexBuffer::AttributeType;

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
    Skybox* _pSkybox;

    VertexBuffer* _pVb;
    IndexBuffer* _pIb;
    Material* _pMat;
    MaterialInstance* _pMatInstance;
    Material* _pDepthMat;
    MaterialInstance* _PDepthMatInstance;

    Texture* _pTex;
    Entity _Renderable[2];

    std::vector<SSurfel> _Surfels;
    std::vector<SVertex> _Vertices;
    std::vector<uint32_t> _KIndices;
    float3 _MinBounds;
    float3 _MaxBounds;
};

static const char* pRsfPath = "MyAssets/painted_santa_kd.rsf";

static void setupCamera(SApp& vApp);
static void createSplatTexture(Engine* vEngine, SApp& vApp);
static void createPointRender(Engine* vEngine, Scene* vScene, SApp& vApp);
static void createSurfaceSplat(Engine* vEngine, Scene* vScene, SApp& vApp);

static bool readBinFile(const std::string& vFileName, std::vector<char>& voBuffer);
static bool loadRsfFile(const std::string& vFileName, std::vector<SSurfel>& voSurfels);

int main(int argc, char* argv[]) {
    Config config;
    config.title = "surfacesplatting";
    SApp app;
    auto setup = [&app](Engine* vEngine, View* vView, Scene* vScene) {
        app._pEngine = vEngine;

        std::cout << "isloadRsfSuccess : "
            << loadRsfFile(FilamentApp::getRootAssetsPath() + pRsfPath, app._Surfels)
            << std::endl;

        #pragma region InitVbIb
        app._Vertices.resize(app._Surfels.size() * 4);
        static const math::float2 quad[4] = { { -1, -1 }, { -1, 1 }, { 1, -1 }, { 1, 1 } };
        for (const auto& s: app._Surfels) {
            for (int i = 0; i < 4; ++i)
                app._Vertices.push_back({ s._Position, s._Normal, s._Radius, s._Color, quad[i] });
        }
        const uint32_t pointNumber = app._Vertices.size();
        static constexpr uint32_t quadIndices[6] = { 0, 1, 2, 3, 2, 1 };
        app._KIndices.resize(app._Surfels.size() * 6);

        for (uint32_t surfId = 0; surfId < app._Surfels.size(); ++surfId) {
            uint32_t base = surfId * 4; // 每个 surfel 占 4 个顶点
            for (uint32_t k = 0; k < 6; ++k) app._KIndices.push_back(base + quadIndices[k]);
        }

        app._pVb = VertexBuffer::Builder()
                        .vertexCount(static_cast<uint32_t>(app._Vertices.size()))
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

        app._pVb->setBufferAt(*vEngine, 0,
                VertexBuffer::BufferDescriptor(app._Vertices.data(),
                        app._Vertices.size() * sizeof(SVertex), nullptr));

        app._pIb = IndexBuffer::Builder()
                        .indexCount(app._KIndices.size())
                        .bufferType(IndexBuffer::IndexType::UINT)
                        .build(*vEngine);
        size_t byteCount = app._KIndices.size() * sizeof(uint32_t);
        app._pIb->setBuffer(*vEngine,
                IndexBuffer::BufferDescriptor(app._KIndices.data(), byteCount, nullptr));

        #pragma endregion

        createSurfaceSplat(vEngine, vScene, app);

        app._pSkybox = Skybox::Builder().color({ 0.1, 0.125, 0.25, 1.0 }).build(*vEngine);
        vScene->setSkybox(app._pSkybox);
    };

    auto cleanup = [&app](Engine* vEngine, View* view, Scene* vScene) {
        vEngine->destroy(app._pSkybox);
        vEngine->destroy(app._Renderable[0]);
        vEngine->destroy(app._Renderable[1]);
        vEngine->destroy(app._pMatInstance);
        vEngine->destroy(app._pMat);
        vEngine->destroy(app._PDepthMatInstance);
        vEngine->destroy(app._pDepthMat);
        vEngine->destroy(app._pVb);
        vEngine->destroy(app._pIb);
    };

    FilamentApp& filamentApp = FilamentApp::get();
    filamentApp.run(config, setup, cleanup);
    return 0;
}

static void createSurfaceSplat(Engine* vEngine, Scene* vScene, SApp& vApp) {
    vApp._pDepthMat = Material::Builder()
                              .package(RESOURCES_SPLATDEPTH_DATA, RESOURCES_SPLATDEPTH_SIZE)
                              .build(*vEngine);
    vApp._Renderable[1] = EntityManager::get().create();
    vApp._PDepthMatInstance = vApp._pDepthMat->createInstance();
    vApp._PDepthMatInstance->setParameter("radiusScale", 0.15f);
    vApp._PDepthMatInstance->setParameter("forwardFactor", 0.1f);
    vApp._PDepthMatInstance->setParameter("depthPrepass", true);

    RenderableManager::Builder(1)
            .material(0, vApp._PDepthMatInstance)
            .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vApp._pVb, vApp._pIb, 0,
                    vApp._Vertices.size())
            .culling(false)
            .receiveShadows(false)
            .castShadows(false)
            .build(*vEngine, vApp._Renderable[1]);

    vApp._pMat =
            Material::Builder().package(RESOURCES_SPLAT_DATA, RESOURCES_SPLAT_SIZE).build(*vEngine);
    vApp._Renderable[0] = EntityManager::get().create();
    vApp._pMatInstance = vApp._pMat->createInstance();

    vApp._pMatInstance->setParameter("radiusScale", 0.15f);
    vApp._pMatInstance->setParameter("forwardFactor", 0.1f);
    vApp._pMatInstance->setParameter("depthPrepass", false);

    RenderableManager::Builder(1)
            .material(0, vApp._pMatInstance)
            .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vApp._pVb, vApp._pIb, 0,
                    vApp._Vertices.size())
            .culling(false)
            .receiveShadows(false)
            .castShadows(false)
            .build(*vEngine, vApp._Renderable[0]);

    vScene->addEntity(vApp._Renderable[0]);
}

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
