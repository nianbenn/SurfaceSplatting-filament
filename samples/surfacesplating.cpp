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
    float3 position;
    float3 normal;
    float radius;
    float4 color;
};

struct Vertex {
    math::float3 position;    
    math::float3 normal; 
    float radius;  
    math::float4 color;        
    math::float2 quad;   
};

struct App {
    Engine* engine;
    Camera* camera;
    // Entity camera;
    Skybox* skybox;

    VertexBuffer* vb;
    IndexBuffer* ib;
    Material* mat;
    MaterialInstance* matInstance;

    Texture* tex;
    Entity renderable;

    std::vector<SSurfel> mSurfels;
    std::vector<Vertex> vertices;
    float3 minBounds;
    float3 maxBounds;
};

static const char* RsfPath = "MyAssets/painted_santa_kd.rsf";

static void InitVb(Engine* engine, App& app);
static void setupCamera(View* view, App& app);
static void createSplatTexture(Engine* engine, App& app);
static void createPointRender(Engine* engine, Scene* scene, App& app);
static void createSurfaceSplat(Engine* engine, Scene* scene, App& app);

static bool readBinFile(const std::string& vFileName, std::vector<char>& voBuffer);
static bool loadRsfFile(const std::string& vFileName, std::vector<SSurfel>& voSurfels);

static std::vector<uint32_t> kIndices;

int main(int argc, char* argv[]) {
    Config config;
    config.title = "surfacesplating";
    //config.cameraMode = camutils::Mode::FREE_FLIGHT;
    App app;
    auto setup = [&app](Engine* engine, View* view, Scene* scene) {
        app.engine = engine;

        createSplatTexture(engine, app);
        // 加载rsf文件并初始化Vb,Ib
        InitVb(engine, app);
        createSurfaceSplat(engine, scene, app);

        app.camera = &view->getCamera();
        view->setCamera(app.camera);
        //setupCamera(view, app);

        app.skybox = Skybox::Builder().color({ 0.1, 0.125, 0.25, 1.0 }).build(*engine);
        scene->setSkybox(app.skybox);
    };
    auto cleanup = [&app](Engine* engine, View* view, Scene* scene) {
        engine->destroy(app.skybox);
        engine->destroy(app.renderable);
        engine->destroy(app.matInstance);
        engine->destroy(app.mat);
        engine->destroy(app.vb);
        engine->destroy(app.ib);
    };

    FilamentApp& filamentApp = FilamentApp::get();
    filamentApp.setCameraFocalLength(10.0f);
    //filamentApp.setCameraNearFar(0.01f, 100.0f);
    filamentApp.run(config, setup, cleanup);
    return 0;
}

constexpr uint32_t TEXTURE_SIZE = 128;
static void createSplatTexture(Engine* engine, App& app) {
    static image::LinearImage splat(3, 3, 1);
    splat.getPixelRef(1, 1)[0] = 0.25f;
    splat = image::resampleImage(splat, TEXTURE_SIZE, TEXTURE_SIZE,
            image::Filter::GAUSSIAN_SCALARS);
    // 创建纹理缓冲区
    Texture::PixelBufferDescriptor buffer(splat.getPixelRef(),
            size_t(TEXTURE_SIZE * TEXTURE_SIZE * sizeof(float)), Texture::Format::R,
            Texture::Type::FLOAT);
    app.tex = Texture::Builder()
                      .width(TEXTURE_SIZE)
                      .height(TEXTURE_SIZE)
                      .levels(1)
                      .sampler(Texture::Sampler::SAMPLER_2D)
                      .format(Texture::InternalFormat::R32F)
                      .build(*engine);
    app.tex->setImage(*engine, 0, std::move(buffer));
}
void loadtestFile(std::vector<SSurfel>& voSurfels) {
    voSurfels.resize(3);
    for (size_t i = 0; i < voSurfels.size(); ++i) {
        voSurfels[i].position = float3(0+i, 0+i*i, 0+i*i);
        voSurfels[i].radius =5.0f;
        voSurfels[i].normal = float3(1, 1, 1);
        voSurfels[i].color = float4(1, 0, 0, 1);
    }
}
static void InitVb(Engine* engine, App& app) {
    // 加载Rsf文件
    std::cout << "isloadRsfSuccess : " << loadRsfFile(FilamentApp::getRootAssetsPath() + RsfPath, app.mSurfels) << std::endl;
    //loadtestFile(app.mSurfels);
    std::cout << "Point number: " << app.mSurfels.size() << std::endl;
    // VBO
    app.vertices.reserve(app.mSurfels.size() * 4);
    static const math::float2 quad[4] = { { -1, -1 }, { -1, 1 }, { 1, -1 }, { 1, 1 } };
    for (const auto& s: app.mSurfels) {
        for (int i = 0; i < 4; ++i)
            app.vertices.push_back({ s.position, s.normal, s.radius, s.color, quad[i]});
    }
    const uint32_t pointNumber = app.vertices.size();
    // VI
    static constexpr uint32_t quadIndices[6] = { 0, 1, 2, 1, 3, 2 };
    kIndices.reserve(app.mSurfels.size()*6);
    for (uint32_t surfId = 0; surfId < app.mSurfels.size(); ++surfId) {
        uint32_t base = surfId * 4; // 每个 surfel 占 4 个顶点
        for (uint32_t k = 0; k < 6; ++k) kIndices.push_back(base + quadIndices[k]);
    }
    //for (size_t i = 0; i < pointNumber; i++) kIndices[i] = static_cast<uint32_t>(i);

    float minx = 1e9, miny = 1e9, minz = 1e9;
    float maxx = -1e9, maxy = -1e9, maxz = -1e9;
    for (size_t i = 0; i < pointNumber; i++) {
        minx = std::min(minx, app.vertices[i].position.x);
        miny = std::min(miny, app.vertices[i].position.y);
        minz = std::min(minz, app.vertices[i].position.z);
        maxx = std::max(maxx, app.vertices[i].position.x);
        maxy = std::max(maxy, app.vertices[i].position.y);
        maxz = std::max(maxz, app.vertices[i].position.z);
    }
    app.minBounds = { minx, miny, minz };
    app.maxBounds = { maxx, maxy, maxz };
    std::cout << minx << " " << miny << " " << minz << " " << maxx << " " << maxy << " " << maxz
              << std::endl;

    for (size_t i = 0; i < pointNumber; i++) {
        app.vertices[i].position.x =
                2.0f * (app.vertices[i].position.x - minx) / (maxx - minx) - 1.0f;
        app.vertices[i].position.y =
                2.0f * (app.vertices[i].position.y - miny) / (maxy - miny) - 1.0f;
        app.vertices[i].position.z =
                2.0f * (app.vertices[i].position.z - minz) / (maxz - minz) - 1.0f;
    }


    app.vb = VertexBuffer::Builder()
                     .vertexCount(static_cast<uint32_t>(app.vertices.size()))
                     .bufferCount(1)
                     .attribute(VertexAttribute::POSITION, 0, AttributeType::FLOAT3,
                             offsetof(Vertex, position), sizeof(Vertex))
                     .attribute(VertexAttribute::CUSTOM1, 0, AttributeType::FLOAT3,
                             offsetof(Vertex, normal), sizeof(Vertex))
                     .attribute(VertexAttribute::CUSTOM0, 0, AttributeType::FLOAT,
                             offsetof(Vertex, radius), sizeof(Vertex))
                     .attribute(VertexAttribute::COLOR, 0, AttributeType::FLOAT4,
                             offsetof(Vertex, color), sizeof(Vertex))
                     .attribute(VertexAttribute::UV0, 0, AttributeType::FLOAT2, 
                             offsetof(Vertex, quad), sizeof(Vertex))
                     .build(*engine);
    app.vb->setBufferAt(*engine, 0,
            VertexBuffer::BufferDescriptor(app.vertices.data(),
                    app.vertices.size() * sizeof(Vertex), nullptr));

    app.ib = IndexBuffer::Builder()
                     .indexCount(kIndices.size())
                     .bufferType(IndexBuffer::IndexType::UINT)
                     .build(*engine);

    size_t byteCount = kIndices.size() * sizeof(uint32_t);
    app.ib->setBuffer(*engine, IndexBuffer::BufferDescriptor(kIndices.data(), byteCount, nullptr));
}
static void createSurfaceSplat(Engine* engine, Scene* scene, App& app) {
    // 创建材质
    app.mat = Material::Builder().package(RESOURCES_SPLAT_DATA, RESOURCES_SPLAT_SIZE)
                      .build(*engine);
    app.renderable = EntityManager::get().create();
    app.matInstance = app.mat->createInstance();

    app.matInstance->setParameter("radiusScale", 0.5f);
    app.matInstance->setParameter("forwardFactor", 0.0f);
    app.matInstance->setParameter("depthPrepass", false);
    //app.matInstance->setParameter("depthPrepass", true);

    RenderableManager::Builder(1)
            .boundingBox({ app.minBounds, app.maxBounds })
            .material(0, app.matInstance)
            .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, app.vb, app.ib, 0,
                    app.vertices.size())
            .culling(false)
            .receiveShadows(false)
            .castShadows(false)
            .build(*engine, app.renderable);
    scene->addEntity(app.renderable);
}
// 摄像机设置
static void setupCamera(View* view, App& app) {
    // 计算点云中心和合适的距离
    float centerX = (app.minBounds.x + app.maxBounds.x) / 2.0f;
    float centerY = (app.minBounds.y + app.maxBounds.y) / 2.0f;
    float centerZ = (app.minBounds.z + app.maxBounds.z) / 2.0f;
    float sizeX = app.maxBounds.x - app.minBounds.x;
    float sizeY = app.maxBounds.y - app.minBounds.y;
    float sizeZ = app.maxBounds.z - app.minBounds.z;
    float radius = std::sqrt(sizeX * sizeX + sizeY * sizeY + sizeZ * sizeZ) * 0.5f;
    float cameraDistance = radius * 2.5f;

    filament::math::float3 eye(centerX + cameraDistance, centerY, centerZ + cameraDistance);
    filament::math::float3 lookAt(centerX, centerY, centerZ);
    filament::math::float3 up(0, 1, 0);
    app.camera->lookAt(eye, lookAt, up);
    std::cout << app.camera->getPosition().x << " " << app.camera->getPosition().y << " "
              << app.camera->getPosition().z << std::endl;
}
// 加载Rsf文件
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
        voSurfels[i].position = float3(PosRadiusNormal[i * 8 + 0], PosRadiusNormal[i * 8 + 1],
                PosRadiusNormal[i * 8 + 2]);
        voSurfels[i].radius = PosRadiusNormal[i * 8 + 3];
        voSurfels[i].normal = float3(PosRadiusNormal[i * 8 + 4], PosRadiusNormal[i * 8 + 5],
                PosRadiusNormal[i * 8 + 6]);
        voSurfels[i].color = float4(static_cast<float>(Colors[i * 4 + 0]) / 255.0f,
                static_cast<float>(Colors[i * 4 + 1]) / 255.0f,
                static_cast<float>(Colors[i * 4 + 2]) / 255.0f,
                static_cast<float>(Colors[i * 4 + 3]) / 255.0f);
    }
    return true;
}
