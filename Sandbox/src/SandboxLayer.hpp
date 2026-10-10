#pragma once
#include <Axle/Layer.hpp>
#include <Axle/PerspectiveCameraController.hpp>
#include <Axle/Shader.hpp>
#include <Axle/Buffer.hpp>
#include <Axle/VertexArray.hpp>
#include <Axle/DebugPanel.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/PhysicsDebugRenderer.hpp>
#include <memory>

class SandboxLayer : public Axle::Layer {
public:
    SandboxLayer();
    ~SandboxLayer() override = default;

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(Axle::Timestep ts) override;
    void OnImGuiRender() override;
    void OnWindowResize(Axle::u32 width, Axle::u32 height) override;

private:
    std::unique_ptr<Axle::PerspectiveCameraController> m_CameraController;
    std::unique_ptr<Axle::PhysicsScene> m_PhysicsScene;
    std::unique_ptr<Axle::PhysicsDebugRenderer> m_PhysicsDebugRenderer;
    Axle::PhysicsDrawSettings m_PhysicsDrawSettings;
    bool m_ShowPhysicsDebug = false;
    bool m_F1KeyWasPressed = false;

    Axle::BodyID m_FloorBodyID;
    Axle::BodyID m_BoxBodyID;
    Axle::BodyID m_SphereBodyID;

    std::shared_ptr<Axle::VertexArray> m_VertexArray;
    std::shared_ptr<Axle::VertexArray> m_SphereVertexArray;
    std::unique_ptr<Axle::Shader> m_Shader;

    Axle::DebugPanel m_DebugPanel;
    Axle::f32 m_LastDeltaTime = 0.0f;
};
