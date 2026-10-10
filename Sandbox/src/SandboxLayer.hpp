#pragma once
#include <Axle/Layer.hpp>
#include <Axle/PerspectiveCameraController.hpp>
#include <Axle/Shader.hpp>
#include <Axle/Buffer.hpp>
#include <Axle/VertexArray.hpp>
#include <Axle/Framebuffer.hpp>
#include <Axle/DebugPanel.hpp>
#include <Axle/SceneHierarchyPanel.hpp>
#include <Axle/VehicleTunerPanel.hpp>
#include <Axle/ConsolePanel.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/PhysicsDebugRenderer.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Vehicle.hpp>
#include <Axle/PrimitiveMesh.hpp>
#include <imgui.h>
#include <ImGuizmo.h>
#include <memory>

class SandboxLayer : public Axle::Layer {
public:
    enum class SceneState {
        Edit = 0,
        Play = 1,
        Pause = 2
    };

    SandboxLayer();
    ~SandboxLayer() override = default;

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(Axle::Timestep ts) override;
    void OnImGuiRender() override;
    void OnWindowResize(Axle::u32 width, Axle::u32 height) override;

    void OnScenePlay();
    void OnScenePause();
    void OnSceneStop();

private:
    void ResetSimulation();
    void RenderToolbar();

private:
    std::unique_ptr<Axle::PerspectiveCameraController> m_CameraController;
    std::unique_ptr<Axle::PhysicsScene> m_PhysicsScene;
    std::unique_ptr<Axle::PhysicsDebugRenderer> m_PhysicsDebugRenderer;
    Axle::PhysicsDrawSettings m_PhysicsDrawSettings;
    bool m_ShowPhysicsDebug = false;
    bool m_F1KeyWasPressed = false;

    // Viewport & Offscreen Framebuffer
    std::shared_ptr<Axle::Framebuffer> m_Framebuffer;
    Axle::u32 m_ViewportWidth = 1280;
    Axle::u32 m_ViewportHeight = 720;
    bool m_ViewportFocused = false;
    bool m_ViewportHovered = false;

    // Simulation Execution Control
    SceneState m_SceneState = SceneState::Edit;
    bool m_StepSingleFrame = false;
    Axle::f32 m_SimulationSpeed = 1.0f;
    bool m_F5KeyWasPressed = false;
    bool m_F6KeyWasPressed = false;
    bool m_F7KeyWasPressed = false;
    bool m_OKeyWasPressed = false;
    bool m_EscKeyWasPressed = false;
    bool m_RKeyWasPressed = false;

    // 3D Transform Gizmo & Viewport Interaction (Phase 6)
    int m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
    bool m_GizmoModeLocal = false;
    bool m_SnapEnabled = false;
    float m_SnapTranslation = 0.5f;
    float m_SnapRotation = 45.0f;
    float m_SnapScale = 0.5f;

    // Vehicle and chase camera
    std::unique_ptr<Axle::Vehicle> m_Vehicle;
    bool m_ChaseCamera = false;
    bool m_CKeyWasPressed = false;
    Axle::f32 m_OrbitYaw = 180.0f;
    Axle::f32 m_OrbitPitch = 15.0f;
    glm::vec2 m_LastMousePos{ 0.0f, 0.0f };
    bool m_FirstMouseUpdate = true;

    // Scene & Entities
    std::unique_ptr<Axle::Scene> m_Scene;
    Axle::Entity m_FloorEntity;
    Axle::Entity m_BoxEntity;
    Axle::Entity m_SphereEntity;
    Axle::Entity m_CylinderEntity;
    Axle::Entity m_ConeEntity;
    Axle::Entity m_ChassisEntity;
    Axle::Entity m_WheelEntities[4];

    Axle::BodyID m_FloorBodyID;
    Axle::BodyID m_BoxBodyID;
    Axle::BodyID m_SphereBodyID;
    Axle::BodyID m_CylinderBodyID;
    Axle::BodyID m_ConeBodyID;

    // Primitive Meshes & Shaders
    std::shared_ptr<Axle::VertexArray> m_CubeMesh;
    std::shared_ptr<Axle::VertexArray> m_SphereMesh;
    std::shared_ptr<Axle::VertexArray> m_CylinderMesh;
    std::shared_ptr<Axle::VertexArray> m_ConeMesh;
    std::shared_ptr<Axle::VertexArray> m_PlaneMesh;
    std::unique_ptr<Axle::Shader> m_Shader;

    // Editor Panels
    Axle::SceneHierarchyPanel m_SceneHierarchyPanel;
    Axle::VehicleTunerPanel m_VehicleTunerPanel;
    Axle::ConsolePanel m_ConsolePanel;
    Axle::DebugPanel m_DebugPanel;

    // Panel Visibility Flags
    bool m_ShowSceneHierarchy = true;
    bool m_ShowProperties = true;
    bool m_ShowVehicleTuner = true;
    bool m_ShowConsole = true;
    bool m_ShowTelemetry = true;
    bool m_ResetLayout = false;

    Axle::f32 m_LastDeltaTime = 0.0f;
};
