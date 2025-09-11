#include "common/arguments.h"

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

#include <utils/EntityManager.h>

#include <filamentapp/Config.h>
#include <filamentapp/FilamentApp.h>

#include <getopt/getopt.h>

#include <cmath>
#include <iostream>
#include <string> 

#include "generated/resources/resources.h"

using namespace filament;
using utils::Entity;
using utils::EntityManager;

struct App {
    Config config;
    VertexBuffer* vb;
    IndexBuffer* ib;
    Material* mat;
    Camera* cam;
    //Entity camera;
    Skybox* skybox;
    Entity renderable;
};

struct Vertex {
    filament::math::float3 position;
    uint32_t color;
};

static const Vertex TRIANGLE_VERTICES[3] = {
    { { 1, 0, -1 }, 0xffff0000u },
    { { cos(M_PI * 2 / 3), sin(M_PI * 2 / 3), -1 }, 0xff00ff00u },
    { { cos(M_PI * 4 / 3), sin(M_PI * 4 / 3), -1 }, 0xff0000ffu },
};
static constexpr uint16_t TRIANGLE_INDICES[3] = { 0, 1, 2 };

int main(int argc, char** argv) {
    App app{};
    app.config.title = "hellotriangle";
    app.config.featureLevel = backend::FeatureLevel::FEATURE_LEVEL_1;

    auto setup = [&app](Engine* engine, View* view, Scene* scene) {
        app.skybox = Skybox::Builder().color({ 0.1, 0.125, 0.25, 1.0 }).build(*engine);
        scene->setSkybox(app.skybox);
        view->setPostProcessingEnabled(false);
        static_assert(sizeof(Vertex) == 16, "Strange vertex size.");
        app.vb = VertexBuffer::Builder()
                         .vertexCount(3)
                         .bufferCount(1)
                         .attribute(VertexAttribute::POSITION, 0,
                                 VertexBuffer::AttributeType::FLOAT3, 0, 16)
                         .attribute(VertexAttribute::COLOR, 0, VertexBuffer::AttributeType::UBYTE4,
                                 12, 16)
                         .normalized(VertexAttribute::COLOR)
                         .build(*engine);
        app.vb->setBufferAt(*engine, 0,
                VertexBuffer::BufferDescriptor(TRIANGLE_VERTICES, 48, nullptr));
        app.ib = IndexBuffer::Builder()
                         .indexCount(3)
                         .bufferType(IndexBuffer::IndexType::USHORT)
                         .build(*engine);
        app.ib->setBuffer(*engine, IndexBuffer::BufferDescriptor(TRIANGLE_INDICES, 6, nullptr));
        app.mat = Material::Builder()
                          .package(RESOURCES_BAKEDCOLOR_DATA, RESOURCES_BAKEDCOLOR_SIZE)
                          .build(*engine);
        app.renderable = EntityManager::get().create();
        RenderableManager::Builder(1)
                .boundingBox({ { -1, -1, -1 }, { 1, 1, 1 } })
                .material(0, app.mat->getDefaultInstance())
                .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, app.vb, app.ib, 0, 3)
                .culling(false)
                .receiveShadows(false)
                .castShadows(false)
                .build(*engine, app.renderable);
        auto& tcm = engine->getTransformManager();
        tcm.setTransform(tcm.getInstance(app.renderable), math::mat4f::scaling(2.0f));
        scene->addEntity(app.renderable);
        //app.camera = utils::EntityManager::get().create();
        //app.cam = engine->createCamera(app.camera);

        //app.cam = &view->getCamera();
        //view->setCamera(app.cam);
        //const uint32_t w = view->getViewport().width;
        //const uint32_t h = view->getViewport().height;
        //const float aspect = (float) w / h;
        //app.cam->setProjection(60.0f, aspect, 0.1f, 100.0f);
        //app.cam->lookAt(math::float3(0, 0, 5), math::float3(0, 0, 0));
       
    };

    auto cleanup = [&app](Engine* engine, View*, Scene*) {
        engine->destroy(app.skybox);
        engine->destroy(app.renderable);
        engine->destroy(app.mat);
        engine->destroy(app.vb);
        engine->destroy(app.ib);
        //engine->destroyCameraComponent(app.camera);
        //utils::EntityManager::get().destroy(app.camera);
    };

    //bool initialCameraSet = false;
    //FilamentApp::get().animate([&app,&initialCameraSet](Engine* engine, View* view, double now) {
    //    if (!initialCameraSet) {
    //        app.cam->lookAt(math::float3(0, 0, 100), math::float3(0, 0, 0));
    //        initialCameraSet = true;
    //    }
    //    const uint32_t w = view->getViewport().width;
    //    const uint32_t h = view->getViewport().height;
    //    const float aspect = (float) w / h;
    //    app.cam->setProjection(60.0f, aspect, 0.1f, 100.0f);
    //    //app.cam->lookAt(math::float3(0, 0, 2), math::float3(0, 0, 0));
    //});
    
    FilamentApp::get().run(app.config, setup, cleanup);

    return 0;
}
