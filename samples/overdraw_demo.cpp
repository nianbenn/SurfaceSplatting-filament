#include <filamentapp/Config.h>
#include <filamentapp/FilamentApp.h>

#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>
#include <filament/Renderer.h>

#include <viewer/AutomationEngine.h>
#include <viewer/AutomationSpec.h>
#include <viewer/ViewerGui.h>

#include <utils/EntityManager.h>        
#include <utils/Log.h>
#include <imgui.h>
#include <filagui/ImGuiExtensions.h>

#include <getopt/getopt.h>

#include <cmath>
#include <iostream>
#include <string>
#include <array>
#include <vector>

#include "generated/resources/resources.h"

using namespace filament;
using namespace filament::math;
using namespace utils;
using namespace filament::viewer;

using utils::Entity;
using utils::EntityManager;

struct App {
    Engine* engine;
    Config config;
    Camera* camera;
    ViewerGui* viewer;
    Skybox* skybox;

    struct Scene {
        // 添加overdraw相关成员
        static constexpr bool visualizeOverdraw = true;
        static constexpr size_t OVERDRAW_LAYERS = 4;            // overdraw可视化层数
        static constexpr uint8_t OVERDRAW_VISIBILITY_LAYER = 7; // Overdraw可视化层
        Material* overdrawMaterial;
        std::array<MaterialInstance*, OVERDRAW_LAYERS> overdrawMaterialInstances;
        VertexBuffer* fullScreenTriangleVertexBuffer;
        IndexBuffer* fullScreenTriangleIndexBuffer;
        std::array<Entity, OVERDRAW_LAYERS> overdrawVisualizer;
    }scene;

    // 普通三角形
    VertexBuffer* vb;
    IndexBuffer* ib;
    Material* mat;
    size_t numTriangles = 2;
    std::vector<Entity> renderables; 
    std::vector<MaterialInstance*> triangleMaterialInstances;
    std::vector<TransformManager::Instance> triangleXforms;

    std::function<void(ImGuiKey key)> onKeyPressed;
};

static void createOverdrawVisualizerEntities(Engine* engine, Scene* scene, App& app);
static void createTriangle(Engine* engine, Scene* scene, App& app);
void addTriangle(Engine* engine, Scene* scene, App& app);
void destroyTriangle(Engine* engine, Scene* scene, App& app);

int main(int argc, char** argv) {
    App app{};
    app.config.title = "overdraw_demo";
    // Filament定义了多个功能级别(4个)
    // FEATURE_LEVEL_0: 基础功能，支持最基本的渲染
    // overdraw_demo需要使用更高的功能级别，不要使用FEATURE_LEVEL_0
    app.config.featureLevel = backend::FeatureLevel::FEATURE_LEVEL_1;

    auto setup = [&app](Engine* engine, View* view, Scene* scene) {
        app.skybox = Skybox::Builder().color({ 0.0, 0.0, 0.0, 0.0 }).build(*engine);
        scene->setSkybox(app.skybox);
        app.viewer = new ViewerGui(engine, scene, view, 410);
        //app.viewer->getSettings().viewer.autoScaleEnabled = !app.actualSize;
        app.engine = engine;
        app.camera = &view->getCamera();
        view->setCamera(app.camera);

        createTriangle(engine, scene, app);
        createOverdrawVisualizerEntities(engine, scene, app);

        const auto overdrawVisibilityBit = (1u << App::Scene::OVERDRAW_VISIBILITY_LAYER);
        view->setVisibleLayers(overdrawVisibilityBit,
                (uint8_t) App::Scene::visualizeOverdraw << App::Scene::OVERDRAW_VISIBILITY_LAYER);
        view->setStencilBufferEnabled(App::Scene::visualizeOverdraw);

        // 写io事
        app.onKeyPressed = [engine, scene, &app](ImGuiKey key) {
            // addTriangle(engine, scene, app);
            if (key == ImGuiKey_Space) {
                std::cout << "SPACE" << std::endl;
                addTriangle(engine, scene, app);
            } else if (key == ImGuiKey_UpArrow) {
                std::cout << "UP" << std::endl;
                addTriangle(engine, scene, app);
            } else if (key == ImGuiKey_DownArrow) {
                std::cout << "DOWN" << std::endl;
                destroyTriangle(engine, scene, app);
            };
        };
    };
    FilamentApp::get().animate([&app](Engine* engine, View* view, double now) {
        auto& tcm = engine->getTransformManager();

        for (size_t i = 0; i < app.numTriangles; ++i) {
            float phase = float(i) * 0.2f;                 // 相位差
            float x = std::sin(float(now) + phase) * 0.5f; // 横向摆动
            mat4f model = mat4f::translation(float3(x, 0.0f, 0.0f));
            tcm.setTransform(app.triangleXforms[i], model);
        }
    });
    auto gui = [&app](Engine* engine, View* view) {
        if (ImGui::IsKeyPressed(ImGuiKey_Space))
        {
            if (app.onKeyPressed) app.onKeyPressed(ImGuiKey_Space);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            if (app.onKeyPressed) app.onKeyPressed(ImGuiKey_UpArrow);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            if (app.onKeyPressed) app.onKeyPressed(ImGuiKey_DownArrow);  
        }
    };
    auto cleanup = [&app](Engine* engine, View*, Scene*) {
        // 清理overdraw资源
        for (size_t i = 0; i < App::Scene::OVERDRAW_LAYERS; i++) {
            engine->destroy(app.scene.overdrawVisualizer[i]);
            engine->destroy(app.scene.overdrawMaterialInstances[i]);
        }
        engine->destroy(app.scene.overdrawMaterial);
        engine->destroy(app.scene.fullScreenTriangleVertexBuffer);
        engine->destroy(app.scene.fullScreenTriangleIndexBuffer);

        // 清理普通资源
        for (size_t i = 0; i < app.numTriangles; ++i) {
            engine->destroy(app.renderables[i]);
            engine->destroy(app.triangleMaterialInstances[i]);
        }
        engine->destroy(app.mat);
        engine->destroy(app.vb);
        engine->destroy(app.ib);
        engine->destroy(app.skybox);
    };

    FilamentApp& filamentApp = FilamentApp::get();
    //filamentApp.run(app.config, setup, cleanup);
    filamentApp.run(app.config, setup, cleanup, gui);
    return 0;
}

// 全屏三角形的顶点和索引数据
static constexpr filament::math::float4 sFullScreenTriangleVertices[3] = {
    { -1.0f, -1.0f, 1.0f, 1.0f }, { 3.0f, -1.0f, 1.0f, 1.0f }, { -1.0f, 3.0f, 1.0f, 1.0f }
    //{ -1.0f, -1.0f, 1.0f, 1.0f }, { -0.5f, -0.5f, 1.0f, 1.0f }, { 0.0f, -1.0f, 1.0f, 1.0f }
};
static constexpr uint16_t sFullScreenTriangleIndices[3] = { 0, 1, 2 };
static void createOverdrawVisualizerEntities(Engine* engine, Scene* scene, App& app) {
    Material* material = Material::Builder()
                                 .package(RESOURCES_OVERDRAW_DATA, RESOURCES_OVERDRAW_SIZE)
                                 .build(*engine);
    const float3 overdrawColors[App::Scene::OVERDRAW_LAYERS] = {
        { 0.0f, 0.0f, 1.0f }, // blue         (overdrawn 1 time)
        { 0.0f, 1.0f, 0.0f }, // green        (overdrawn 2 times)
        { 1.0f, 0.0f, 1.0f }, // magenta      (overdrawn 3 times)
        { 1.0f, 0.0f, 0.0f }  // red          (overdrawn 4+ times)
    };
    for (auto i = 0; i < App::Scene::OVERDRAW_LAYERS; i++) {
        MaterialInstance* matInstance = material->createInstance();
        matInstance->setStencilCompareFunction(MaterialInstance::StencilCompareFunc::E);
        matInstance->setStencilReferenceValue(i + 2);
        matInstance->setParameter("color", overdrawColors[i]);
        app.scene.overdrawMaterialInstances[i] = matInstance;
    }
    auto& lastMi = app.scene.overdrawMaterialInstances[App::Scene::OVERDRAW_LAYERS - 1];
    lastMi->setStencilCompareFunction(MaterialInstance::StencilCompareFunc::LE);

    VertexBuffer* vertexBuffer = VertexBuffer::Builder()
                    .vertexCount(3)
                    .bufferCount(1)
                    .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT4, 0)
                    .build(*engine);
    vertexBuffer->setBufferAt(*engine, 0,
            { sFullScreenTriangleVertices, sizeof(sFullScreenTriangleVertices) });
    IndexBuffer* indexBuffer = IndexBuffer::Builder()
                                       .indexCount(3)
                                       .bufferType(IndexBuffer::IndexType::USHORT)
                                       .build(*engine);
    indexBuffer->setBuffer(*engine,
            { sFullScreenTriangleIndices, sizeof(sFullScreenTriangleIndices) });

    auto& em = EntityManager::get();
    const auto& matInstances = app.scene.overdrawMaterialInstances;
    for (auto i = 0; i < App::Scene::OVERDRAW_LAYERS; i++) {
        Entity overdrawEntity = em.create();
        RenderableManager::Builder(1)
                .boundingBox({ {}, { 1.0f, 1.0f, 1.0f } })
                .material(0, matInstances[i])
                .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vertexBuffer, indexBuffer, 0, 3)
                .culling(false)
                .priority(7u)
                .layerMask(0xFF, 1u << App::Scene::OVERDRAW_VISIBILITY_LAYER)
                .build(*engine, overdrawEntity);
        scene->addEntity(overdrawEntity);
        app.scene.overdrawVisualizer[i] = overdrawEntity;
    }
    app.scene.overdrawMaterial = material;
    app.scene.fullScreenTriangleVertexBuffer = vertexBuffer;
    app.scene.fullScreenTriangleIndexBuffer = indexBuffer;
}

// 三角形的顶点索引
static const float4 TRIANGLE_VERTICES[3] = { 
    { 0.0f, 0.5f, -0.5f, 1.0f },
    { -0.5f, -0.5f, -0.5f, 1.0f }, 
    { 0.5f, -0.5f, -0.5f, 1.0f } };
static constexpr uint16_t TRIANGLE_INDICES[3] = { 0, 1, 2 };
void createTriangle(Engine* engine, Scene* scene, App& app) {
    app.vb = VertexBuffer::Builder()
                     .vertexCount(3)
                     .bufferCount(1)
                     .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT4,
                             0, 16)
                     .build(*engine);
    app.vb->setBufferAt(*engine, 0,
            VertexBuffer::BufferDescriptor(TRIANGLE_VERTICES, sizeof(TRIANGLE_VERTICES)));

    app.ib = IndexBuffer::Builder()
                     .indexCount(3)
                     .bufferType(IndexBuffer::IndexType::USHORT)
                     .build(*engine);
    app.ib->setBuffer(*engine,
            IndexBuffer::BufferDescriptor(TRIANGLE_INDICES, sizeof(TRIANGLE_INDICES)));

    app.mat = Material::Builder()
                      .package(RESOURCES_BAKEDCOLOR_DATA, RESOURCES_BAKEDCOLOR_SIZE)
                      .build(*engine);
    // 创建6个三角形
    app.numTriangles = 0;
    for (int i = 0; i < 6; i++) addTriangle(engine, scene, app);
}
void addTriangle(Engine* engine, Scene* scene, App& app)
{
    int i = app.numTriangles++;
    app.renderables.resize(app.numTriangles);
    app.triangleMaterialInstances.resize(app.numTriangles);
    app.triangleXforms.resize(app.numTriangles);

    app.renderables[i] = EntityManager::get().create();
    app.triangleMaterialInstances[i] = app.mat->createInstance();
    auto& mi = app.triangleMaterialInstances[i];
    mi->setStencilWrite(true);
    mi->setDepthFunc(MaterialInstance::DepthFunc::A);
    mi->setStencilOpDepthStencilPass(MaterialInstance::StencilOperation::INCR);

    RenderableManager::Builder(1)
            .boundingBox({ { -1, -1, -1 }, { 1, 1, 1 } })
            .material(0, mi)
            .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, app.vb, app.ib, 0, 3)
            .culling(false)
            .receiveShadows(false)
            .castShadows(false)
            .build(*engine, app.renderables[i]);
    // 只创建 TransformManager 实例句柄
    auto& tcm = engine->getTransformManager();
    app.triangleXforms[i] = tcm.getInstance(app.renderables[i]);

    scene->addEntity(app.renderables[i]);
}
void destroyTriangle(Engine* engine, Scene* scene, App& app)
{
    int i = --app.numTriangles;
    if (i < 0) return; // 没有三角形可删除
    engine->destroy(app.renderables[i]);
    engine->destroy(app.triangleMaterialInstances[i]);

    app.renderables.resize(app.numTriangles);
    app.triangleMaterialInstances.resize(app.numTriangles);
    app.triangleXforms.resize(app.numTriangles);
}
