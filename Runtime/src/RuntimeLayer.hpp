#pragma once
#include <Axle/Layer.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Vehicle.hpp>
#include <Axle/SceneRenderer.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/PerspectiveCamera.hpp>
#include <Axle/Project.hpp>
#include <Axle/ScriptHotReloadManager.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Log.hpp>
#include <Axle/Input.hpp>
#include <Axle/KeyCodes.hpp>

#include <memory>
#include <filesystem>

namespace Axle {

    class RuntimeLayer : public Layer {
    public:
        RuntimeLayer(const std::filesystem::path& projectPath = {});
        ~RuntimeLayer() override = default;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(Timestep ts) override;
        void OnWindowResize(u32 width, u32 height) override;

    private:
        void LoadProjectAndStartScene();

    private:
        std::filesystem::path m_ProjectPath;
        std::shared_ptr<Scene> m_Scene;
        std::unique_ptr<PhysicsScene> m_PhysicsScene;
        std::unique_ptr<SceneRenderer> m_SceneRenderer;

        PerspectiveCamera m_Camera;
        bool m_IsPaused = false;
        bool m_EscapeKeyWasPressed = false;
        bool m_F11KeyWasPressed = false;
        u32 m_WindowWidth = 1280;
        u32 m_WindowHeight = 720;
    };

}
