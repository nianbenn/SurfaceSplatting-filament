#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderTarget.h>
#include <filament/Renderer.h>
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

    Texture* offscreenColorTexture = nullptr;
    Texture* offscreenDepthTexture = nullptr;
    RenderTarget* depthRenderTarget = nullptr;
    View* offscreenView = nullptr;
    Scene* offscreenScene = nullptr;
    Camera* offscreenCamera = nullptr;

};

static const char* pRsfPath = "MyAssets/painted_santa_kd.rsf";

static void initVbIb(Engine* vEngine, SApp& vApp);
static void setupCamera(SApp& vApp);
static void createSplatTexture(Engine* vEngine, SApp& vApp);
static void createPointRender(Engine* vEngine, Scene* vScene, SApp& vApp);
static void createSurfaceSplat(Engine* vEngine, Scene* vScene, SApp& vApp);

static bool readBinFile(const std::string& vFileName, std::vector<char>& voBuffer);
static bool loadRsfFile(const std::string& vFileName, std::vector<SSurfel>& voSurfels);

//static std::vector<uint32_t> kIndices;

int main(int argc, char* argv[]) {
    Config config;
    config.title = "surfacesplatting";
    SApp app;
    auto setup = [&app](Engine* vEngine, View* vView, Scene* vScene) {
        app._pEngine = vEngine;
        initVbIb(vEngine, app);

        auto vp = vView->getViewport();
        app.offscreenView = vEngine->createView();
        app.offscreenScene = vEngine->createScene();
        app.offscreenCamera = vEngine->createCamera(utils::EntityManager::get().create());
        app.offscreenView->setScene(app.offscreenScene);
        app.offscreenColorTexture =Texture::Builder()
                        .width(vp.width).height(vp.height).levels(1)
                        .usage(Texture::Usage::COLOR_ATTACHMENT | Texture::Usage::SAMPLEABLE)
                        .format(Texture::InternalFormat::RGBA16F)
                        .build(*vEngine);
        app.offscreenDepthTexture = Texture::Builder()
                        .width(vp.width).height(vp.height)
                        .usage(Texture::Usage::DEPTH_ATTACHMENT | Texture::Usage::SAMPLEABLE)
                        .sampler(Texture::Sampler::SAMPLER_2D)
                        .format(Texture::InternalFormat::DEPTH24)
                        .build(*vEngine);
        app.depthRenderTarget = RenderTarget::Builder()
                        .texture(RenderTarget::AttachmentPoint::DEPTH, app.offscreenDepthTexture)
                        .build(*vEngine);
        app.offscreenView->setRenderTarget(app.depthRenderTarget);
        createSurfaceSplat(vEngine, vScene, app);
        //viewtest 
        //app.offscreenView->setRenderTarget(app.depthRenderTarget); 
        app.offscreenView->setViewport({ 0, 0, vp.width, vp.height });
        //app.offscreenView->setCamera(app.offscreenCamera);

        app.offscreenView->setCamera(&vView->getCamera());
        //vView->setBlendMode(BlendMode::a)
        vView->setPostProcessingEnabled(false);
        app.offscreenView->setPostProcessingEnabled(false);
        //vView->setCamera(app.offscreenCamera);
        app.offscreenView->setScene(app.offscreenScene);

        
        app.offscreenScene->addEntity(app._Renderable[1]);

        FilamentApp::get().addOffscreenView(app.offscreenView);
        
        app._pSkybox = Skybox::Builder().color({ 0.1, 0.125, 0.25, 1.0 }).build(*vEngine);
        vScene->setSkybox(app._pSkybox);
        app.offscreenScene->setSkybox(app._pSkybox);//
    };
    auto preRender = [&app](Engine*, View*, Scene*, Renderer* renderer) {
        static bool once = true;
        if (once) {
            once = false;
            std::cout << "depthTex   =" << app.offscreenDepthTexture << std::endl;
            std::cout << "depthRT    ="
                      << app.depthRenderTarget->getTexture(RenderTarget::AttachmentPoint::DEPTH)<< std::endl;
        }
    };

    auto cleanup = [&app](Engine* vEngine, View* view, Scene* vScene) {
        auto& em = utils::EntityManager::get();
        auto camera = app.offscreenCamera->getEntity();
        vEngine->destroyCameraComponent(camera);
        em.destroy(camera);

        vEngine->destroy(app._pSkybox);
        vEngine->destroy(app._Renderable[0]);
        vEngine->destroy(app._Renderable[1]);
        vEngine->destroy(app._pMatInstance);
        vEngine->destroy(app._pMat);
        vEngine->destroy(app._PDepthMatInstance);
        vEngine->destroy(app._pDepthMat);
        vEngine->destroy(app._pVb);
        vEngine->destroy(app._pIb);

        vEngine->destroy(app.offscreenColorTexture);
        vEngine->destroy(app.offscreenDepthTexture);
        vEngine->destroy(app.offscreenScene);
        vEngine->destroy(app.offscreenView);
    };

    FilamentApp& filamentApp = FilamentApp::get();
    filamentApp.run(config, setup, cleanup, FilamentApp::ImGuiCallback(),preRender);
    return 0;
}

constexpr uint32_t TEXTURE_SIZE = 128;

void loadtestFile(std::vector<SSurfel>& voSurfels) {
    voSurfels.resize(3);
    for (size_t i = 0; i < voSurfels.size(); ++i) {
        voSurfels[i]._Position = float3(0+i, i+i*i, 0+i*i);
        voSurfels[i]._Radius =2.0f;
        voSurfels[i]._Normal = float3(0, 0, -1);
        voSurfels[i]._Color = float4(1, 0, 0, 1);
    }
}

static void initVbIb(Engine * vEngine, SApp & vApp) {
    // 加载Rsf文件
    std::cout << "isloadRsfSuccess : " << loadRsfFile(FilamentApp::getRootAssetsPath() + pRsfPath, vApp._Surfels) << std::endl;
    //loadtestFile(vApp._Surfels);
    std::cout << "Point number: " << vApp._Surfels.size() << std::endl;
    // VBO
    vApp._Vertices.reserve(vApp._Surfels.size() * 4);
    static const math::float2 quad[4] = { { -1, -1 }, { -1, 1 }, { 1, -1 }, { 1, 1 } };
    for (const auto& s: vApp._Surfels) {
        for (int i = 0; i < 4; ++i)
            vApp._Vertices.push_back({ s._Position, s._Normal, s._Radius, s._Color, quad[i]});
    }
    const uint32_t pointNumber = vApp._Vertices.size();
    std::cout << "Vertex number: " << pointNumber << std::endl;
    // VI
    static constexpr uint32_t quadIndices[6] = { 0, 1, 2, 3, 2, 1 };
    vApp._KIndices.reserve(vApp._Surfels.size()*6);
    for (uint32_t surfId = 0; surfId < vApp._Surfels.size(); ++surfId) {
        uint32_t base = surfId * 4; // 每个 surfel 占 4 个顶点
        for (uint32_t k = 0; k < 6; ++k) vApp._KIndices.push_back(base + quadIndices[k]);
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
                     .indexCount(vApp._KIndices.size())
                     .bufferType(IndexBuffer::IndexType::UINT)
                     .build(*vEngine);

    size_t byteCount = vApp._KIndices.size() * sizeof(uint32_t);
    vApp._pIb->setBuffer(*vEngine, IndexBuffer::BufferDescriptor(vApp._KIndices.data(), byteCount, nullptr));
}
static void createSurfaceSplat(Engine* vEngine, Scene* vScene, SApp& vApp) {
    // 创建材质
    vApp._pDepthMat = Material::Builder()
                              .package(RESOURCES_SPLATDEPTH_DATA, RESOURCES_SPLATDEPTH_SIZE)
                              .build(*vEngine);
    vApp._Renderable[1] = EntityManager::get().create();
    vApp._PDepthMatInstance = vApp._pDepthMat->createInstance();
    vApp._PDepthMatInstance->setParameter("radiusScale", 0.15f);
    vApp._PDepthMatInstance->setParameter("forwardFactor", 0.1f);
    vApp._PDepthMatInstance->setParameter("depthPrepass", true);

    RenderableManager::Builder(1)
            .boundingBox({ vApp._MinBounds, vApp._MaxBounds })
            .material(0, vApp._PDepthMatInstance)
            .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vApp._pVb, vApp._pIb, 0,
                    vApp._Surfels.size() * 6)
            .culling(false)
            .receiveShadows(false)
            .castShadows(false)
            .build(*vEngine, vApp._Renderable[1]);
    // vScene->addEntity(vApp._Renderable[1]);
    vApp._pMat = Material::Builder().package(RESOURCES_SPLAT_DATA, RESOURCES_SPLAT_SIZE)
                      .build(*vEngine);
    vApp._Renderable[0] = EntityManager::get().create();
    vApp._pMatInstance = vApp._pMat->createInstance();

    filament::TextureSampler sampler = {};
    sampler.setMinFilter(filament::TextureSampler::MinFilter::NEAREST);
    sampler.setMagFilter(filament::TextureSampler::MagFilter::NEAREST);
    sampler.setWrapModeR(filament::TextureSampler::WrapMode::CLAMP_TO_EDGE);

    vApp._pMatInstance->setParameter("radiusScale", 0.15f);
    vApp._pMatInstance->setParameter("forwardFactor", 0.1f);
    vApp._pMatInstance->setParameter("depthPrepass", false);
    vApp._pMatInstance->setParameter("depthTex", vApp.offscreenDepthTexture, sampler);

    RenderableManager::Builder(1)
            .boundingBox({ vApp._MinBounds, vApp._MaxBounds })
            .material(0, vApp._pMatInstance)
            .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vApp._pVb, vApp._pIb, 0,
                    vApp._Surfels.size()*6)
            .culling(false)
            .receiveShadows(false)
            .castShadows(false)
            .build(*vEngine, vApp._Renderable[0]);
            
    vScene->addEntity(vApp._Renderable[0]);
    //
    Entity blendEntity = EntityManager::get().create();
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
