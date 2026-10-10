#include "SandboxLayer.hpp"
#include <Axle/Application.hpp>
#include <Axle/Log.hpp>
#include <Axle/Input.hpp>
#include <Axle/KeyCodes.hpp>
#include <Axle/RenderCommand.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

SandboxLayer::SandboxLayer()
    : Layer("SandboxLayer") {
}

void SandboxLayer::OnAttach() {
    AXLE_INFO("SandboxLayer attached");

    const auto& window = Axle::Application::Get().GetWindow();

    // 1. Perspective Camera Controller
    m_CameraController = std::make_unique<Axle::PerspectiveCameraController>(
        45.0f,
        static_cast<Axle::f32>(window.GetWidth()) / static_cast<Axle::f32>(window.GetHeight()),
        0.1f,
        1000.0f
    );
    m_CameraController->SetPosition(glm::vec3(0.0f, 6.0f, 18.0f));
    m_CameraController->SetRotation(glm::vec3(-12.0f, 0.0f, 0.0f));

    // 2. Jolt Physics Scene
    m_PhysicsScene = std::make_unique<Axle::PhysicsScene>();

    // Sàn tĩnh 100m x 1m x 100m tại Y = -0.5m
    m_FloorBodyID = m_PhysicsScene->CreateBox(
        glm::vec3(0.0f, -0.5f, 0.0f),
        glm::vec3(50.0f, 0.5f, 50.0f),
        false, // Static
        0.6f,  // Friction
        0.2f   // Restitution
    );

    // Khối hộp rơi tự do 1m x 1m x 1m tại Y = 10.0m
    m_BoxBodyID = m_PhysicsScene->CreateBox(
        glm::vec3(0.0f, 10.0f, 0.0f),
        glm::vec3(0.5f, 0.5f, 0.5f),
        true,  // Dynamic
        0.5f,  // Friction
        0.4f   // Restitution
    );

    m_PhysicsScene->OptimizeBroadPhase();

    // 3. Shaders
    const std::string vertexSrc = R"(
        #version 450 core
        layout(location = 0) in vec3 a_Position;
        layout(location = 1) in vec4 a_Color;

        uniform mat4 u_ViewProjection;
        uniform mat4 u_Transform;
        uniform vec4 u_ColorTint;

        out vec4 v_Color;

        void main() {
            v_Color = a_Color * u_ColorTint;
            gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);
        }
    )";

    const std::string fragmentSrc = R"(
        #version 450 core
        layout(location = 0) out vec4 o_Color;

        in vec4 v_Color;

        void main() {
            o_Color = v_Color;
        }
    )";

    m_Shader = std::make_unique<Axle::Shader>(vertexSrc, fragmentSrc);

    // 4. Cube Mesh Data
    Axle::f32 vertices[8 * 7] = {
        -0.5f, -0.5f, -0.5f,   0.9f, 0.2f, 0.2f, 1.0f,
         0.5f, -0.5f, -0.5f,   0.2f, 0.9f, 0.2f, 1.0f,
         0.5f,  0.5f, -0.5f,   0.2f, 0.2f, 0.9f, 1.0f,
        -0.5f,  0.5f, -0.5f,   0.9f, 0.9f, 0.2f, 1.0f,
        -0.5f, -0.5f,  0.5f,   0.9f, 0.2f, 0.9f, 1.0f,
         0.5f, -0.5f,  0.5f,   0.2f, 0.9f, 0.9f, 1.0f,
         0.5f,  0.5f,  0.5f,   1.0f, 1.0f, 1.0f, 1.0f,
        -0.5f,  0.5f,  0.5f,   0.3f, 0.3f, 0.3f, 1.0f
    };

    Axle::u32 indices[36] = {
        4, 5, 6,  6, 7, 4,
        1, 0, 3,  3, 2, 1,
        0, 4, 7,  7, 3, 0,
        5, 1, 2,  2, 6, 5,
        7, 6, 2,  2, 3, 7,
        0, 1, 5,  5, 4, 0
    };

    m_VertexArray = std::make_shared<Axle::VertexArray>();
    auto vertexBuffer = std::make_shared<Axle::VertexBuffer>(vertices, static_cast<Axle::u32>(sizeof(vertices)));
    vertexBuffer->SetLayout({
        { Axle::ShaderDataType::Float3, "a_Position" },
        { Axle::ShaderDataType::Float4, "a_Color" }
    });
    m_VertexArray->AddVertexBuffer(vertexBuffer);

    auto indexBuffer = std::make_shared<Axle::IndexBuffer>(indices, 36);
    m_VertexArray->SetIndexBuffer(indexBuffer);

    Axle::RenderCommand::SetClearColor({ 0.10f, 0.12f, 0.15f, 1.0f });
}

void SandboxLayer::OnDetach() {
    AXLE_INFO("SandboxLayer detached");
    m_PhysicsScene.reset();
}

void SandboxLayer::OnWindowResize(Axle::u32 width, Axle::u32 height) {
    if (m_CameraController) {
        m_CameraController->OnResize(width, height);
    }
}

void SandboxLayer::OnUpdate(Axle::Timestep ts) {
    if (Axle::Input::IsKeyPressed(Axle::KeyCode::Escape)) {
        Axle::Application::Get().Close();
        return;
    }

    // Nhấn phím 'R' để thả rơi lại khối hộp
    if (Axle::Input::IsKeyPressed(Axle::KeyCode::R)) {
        m_PhysicsScene->SetBodyPositionAndRotation(m_BoxBodyID, glm::vec3(0.0f, 10.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
        m_PhysicsScene->SetLinearVelocity(m_BoxBodyID, glm::vec3(0.0f));
    }

    m_LastDeltaTime = ts.GetSeconds();
    m_CameraController->OnUpdate(ts);
    m_PhysicsScene->Step(ts);

    // Render pass
    Axle::RenderCommand::Clear();

    m_Shader->Bind();
    m_Shader->SetMat4("u_ViewProjection", m_CameraController->GetCamera().GetViewProjectionMatrix());

    // 1. Render Sàn tĩnh
    const glm::mat4 floorTransform = m_PhysicsScene->GetBodyTransform(m_FloorBodyID) * glm::scale(glm::mat4(1.0f), glm::vec3(100.0f, 1.0f, 100.0f));
    m_Shader->SetMat4("u_Transform", floorTransform);
    m_Shader->SetFloat4("u_ColorTint", glm::vec4(0.25f, 0.28f, 0.32f, 1.0f));
    Axle::RenderCommand::DrawIndexed(m_VertexArray);

    // 2. Render Khối hộp rơi tự do
    const glm::mat4 boxTransform = m_PhysicsScene->GetBodyTransform(m_BoxBodyID);
    m_Shader->SetMat4("u_Transform", boxTransform);
    m_Shader->SetFloat4("u_ColorTint", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    Axle::RenderCommand::DrawIndexed(m_VertexArray);
}

void SandboxLayer::OnImGuiRender() {
    // Thu thập telemetry
    Axle::TelemetryData telemetry{
        .DeltaTime = m_LastDeltaTime,
        .FPS = m_LastDeltaTime > 0.0f ? 1.0f / m_LastDeltaTime : 0.0f,
        .CameraPosition = m_CameraController->GetCamera().GetPosition(),
        .CameraRotation = m_CameraController->GetCamera().GetRotation(),
        .HasPhysics = true,
        .BoxPosition = m_PhysicsScene->GetBodyPosition(m_BoxBodyID),
        .BoxVelocity = m_PhysicsScene->GetLinearVelocity(m_BoxBodyID),
        .IsBoxActive = m_PhysicsScene->IsActive(m_BoxBodyID),
        .TotalBodies = m_PhysicsScene->GetNumBodies(),
        .ActiveBodies = m_PhysicsScene->GetNumActiveBodies()
    };

    m_DebugPanel.OnImGuiRender(telemetry);
}
