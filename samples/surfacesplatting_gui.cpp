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

#include <viewer/AutomationEngine.h>
#include <viewer/AutomationSpec.h>
#include <viewer/ViewerGui.h>

#include <utils/EntityManager.h>

#include <filamentapp/Config.h>
#include <filamentapp/FilamentApp.h>

#include <getopt/getopt.h>

#include <imgui.h>

#include <cmath>
#include <iostream>
#include <string>

#include "generated/resources/resources.h"

using namespace filament;
using utils::Entity;
using utils::EntityManager;

using namespace filament::viewer;

class CRenderAlgorithm {
public:
    virtual ~CRenderAlgorithm() = default;
    virtual std::string getName() const = 0;
    virtual void gui(MaterialInstance* vMatInstance) = 0;
};

class CPointRendering final : public CRenderAlgorithm {
public:
    std::string getName() const override { return "Point Rendering"; }
    void gui(MaterialInstance* vMatInstance) {};
};

class CSurfaceSplatting final : public CRenderAlgorithm {
public:
    [[nodiscard]] std::string getName() const override { return "Surface Splatting"; }
    void gui(MaterialInstance* vMatInstance) {
        static float RadiusScale = 2.5f;
        static float ForwardFactor = 0.5f;
        static float4 BackgroundColor = float4(0.1f, 0.2f, 0.3f, 1.0f);
        static float3 LightDir1 = float3(0.5f, 0.5f, 1.0f);
        static float3 LightDir2 = float3(-0.5f, 0.25f, -0.5f);
        ImGui::DragFloat("Radius Scale", &RadiusScale, 0.01f, 0.01f, 10.0f);
        ImGui::DragFloat("Forward Factor", &ForwardFactor, 0.01f, 0.01f, 10.0f);
        ImGui::ColorEdit4("Background Color", &BackgroundColor.r);
        ImGui::DragFloat3("Light Dir1", &LightDir1.x, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat3("Light Dir2", &LightDir2.x, 0.01f, 0.0f, 1.0f);

        //vMatInstance->setParameter("radiusScale", RadiusScale);
        //vMatInstance->setParameter("forwardFactor", ForwardFactor);
    };
};

struct App {
    Config _config;
    Material* _pMat;
    MaterialInstance* _pMatInstance;

    std::vector<std::shared_ptr<CRenderAlgorithm>> _RenderAlgorithms{};
    size_t _ActiveAlgorithmIdx = 0;
};

int main(int argc, char** argv) {
    App app{};
    app._config.title = "hellogui";
    app._config.featureLevel = backend::FeatureLevel::FEATURE_LEVEL_1;
    app._RenderAlgorithms.push_back(std::make_shared<CSurfaceSplatting>());
    app._RenderAlgorithms.push_back(std::make_shared<CPointRendering>());

    auto setup = [&app](Engine* vEngine, View* vView, Scene* vScene) {
    };

    auto cleanup = [&app](Engine* vEngine, View*, Scene*) {
    };

    auto gui = [&app](Engine*, View*) {
        for (size_t i = 0; i < app._RenderAlgorithms.size(); ++i) {
            if (ImGui::RadioButton(app._RenderAlgorithms[i]->getName().c_str(),
                        i == app._ActiveAlgorithmIdx)) {
                app._ActiveAlgorithmIdx = i;
            }
        }
        app._RenderAlgorithms[app._ActiveAlgorithmIdx]->gui(app._pMatInstance);
    };

    FilamentApp::get().run(app._config, setup, cleanup, gui);

    return 0;
}
