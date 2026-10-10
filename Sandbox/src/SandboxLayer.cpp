#include "SandboxLayer.hpp"
#include <Axle/Application.hpp>
#include <Axle/Log.hpp>
#include <Axle/Input.hpp>
#include <Axle/KeyCodes.hpp>
#include <Axle/RenderCommand.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <vector>

namespace {

    // Helper: generate procedural UV sphere mesh
    std::shared_ptr<Axle::VertexArray> CreateSphereMesh(Axle::u32 stacks = 24, Axle::u32 sectors = 32) {
        std::vector<Axle::f32> vertices;
        std::vector<Axle::u32> indices;

        constexpr Axle::f32 PI = 3.14159265358979323846f;

        for (Axle::u32 i = 0; i <= stacks; ++i) {
            const Axle::f32 stackAngle = (PI * 0.5f) - static_cast<Axle::f32>(i) * (PI / static_cast<Axle::f32>(stacks));
            const Axle::f32 y = std::sin(stackAngle);
            const Axle::f32 xz = std::cos(stackAngle);

            for (Axle::u32 j = 0; j <= sectors; ++j) {
                const Axle::f32 sectorAngle = static_cast<Axle::f32>(j) * (2.0f * PI / static_cast<Axle::f32>(sectors));
                const Axle::f32 x = xz * std::cos(sectorAngle);
                const Axle::f32 z = xz * std::sin(sectorAngle);

                vertices.push_back(x);
                vertices.push_back(y);
                vertices.push_back(z);
            }
        }

        for (Axle::u32 i = 0; i < stacks; ++i) {
            const Axle::u32 k1 = i * (sectors + 1);
            const Axle::u32 k2 = k1 + sectors + 1;

            for (Axle::u32 j = 0; j < sectors; ++j) {
                if (i != 0) {
                    indices.push_back(k1 + j);
                    indices.push_back(k2 + j);
                    indices.push_back(k1 + j + 1);
                }

                if (i != (stacks - 1)) {
                    indices.push_back(k1 + j + 1);
                    indices.push_back(k2 + j);
                    indices.push_back(k2 + j + 1);
                }
            }
        }

        auto va = std::make_shared<Axle::VertexArray>();
        auto vb = std::make_shared<Axle::VertexBuffer>(vertices.data(), static_cast<Axle::u32>(vertices.size() * sizeof(Axle::f32)));
        vb->SetLayout({
            { Axle::ShaderDataType::Float3, "a_Position" }
        });
        va->AddVertexBuffer(vb);

        auto ib = std::make_shared<Axle::IndexBuffer>(indices.data(), static_cast<Axle::u32>(indices.size()));
        va->SetIndexBuffer(ib);

        return va;
    }

}

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

    // Static floor: 100m x 1m x 100m at Y = -0.5m (top surface at Y = 0.0m)
    m_FloorBodyID = m_PhysicsScene->CreateBox(
        glm::vec3(0.0f, -0.5f, 0.0f),
        glm::vec3(50.0f, 0.5f, 50.0f),
        false, // Static
        0.6f,  // Friction
        0.2f   // Restitution
    );

    // Dynamic box: 1m x 1m x 1m starting at elevation Y = 10.0m
    m_BoxBodyID = m_PhysicsScene->CreateBox(
        glm::vec3(0.0f, 10.0f, 0.0f),
        glm::vec3(0.5f, 0.5f, 0.5f),
        true,  // Dynamic
        0.5f,  // Friction
        0.4f   // Restitution
    );

    // Dynamic bouncy sphere: radius 0.5m starting at elevation Y = 14.0m, offset at X = 2.0m
    m_SphereBodyID = m_PhysicsScene->CreateSphere(
        glm::vec3(2.0f, 14.0f, 0.0f),
        0.5f,  // Radius
        true,  // Dynamic
        0.3f,  // Friction
        0.75f  // Bouncy restitution
    );

    // Optimize broadphase after adding static environment
    m_PhysicsScene->OptimizeBroadPhase();

    // 3. Shaders with procedural Caro (Checkerboard) pattern and directional lighting
    const std::string vertexSrc = R"(
        #version 450 core
        layout(location = 0) in vec3 a_Position;

        uniform mat4 u_ViewProjection;
        uniform mat4 u_Transform;

        out vec3 v_WorldPos;
        out vec3 v_LocalPos;

        void main() {
            v_LocalPos = a_Position;
            vec4 worldPos = u_Transform * vec4(a_Position, 1.0);
            v_WorldPos = worldPos.xyz;
            gl_Position = u_ViewProjection * worldPos;
        }
    )";

    const std::string fragmentSrc = R"(
        #version 450 core
        layout(location = 0) out vec4 o_Color;

        in vec3 v_WorldPos;
        in vec3 v_LocalPos;

        uniform int u_PatternMode;      // 0: World-space XZ caro (Floor), 1: Local-space 3D caro (Box), 2: Spherical Caro (Sphere)
        uniform float u_TileScale;      // Frequency of the checkerboard tiles
        uniform vec4 u_Color1;          // Primary checker tile color
        uniform vec4 u_Color2;          // Secondary checker tile color
        uniform vec3 u_LightDir;        // Directional sun light vector
        uniform mat4 u_Transform;

        void main() {
            float pattern = 0.0;
            vec3 normal = vec3(0.0, 1.0, 0.0);

            if (u_PatternMode == 0) {
                // World-space XZ checkerboard (ideal for floor grids)
                vec2 coord = floor(v_WorldPos.xz * u_TileScale);
                pattern = mod(coord.x + coord.y, 2.0);

                vec3 dx = dFdx(v_WorldPos);
                vec3 dy = dFdy(v_WorldPos);
                normal = normalize(cross(dx, dy));
            } else if (u_PatternMode == 1) {
                // Local 3D checkerboard (4x4 tiles per face on the 1x1x1 box)
                vec3 coord = floor((v_LocalPos + vec3(0.5)) * u_TileScale + vec3(0.0001));
                pattern = mod(coord.x + coord.y + coord.z, 2.0);

                vec3 dx = dFdx(v_WorldPos);
                vec3 dy = dFdy(v_WorldPos);
                normal = normalize(cross(dx, dy));
            } else {
                // Spherical Caro (Latitude & Longitude checkered pattern)
                vec3 nLocal = normalize(v_LocalPos);
                float u = atan(nLocal.z, nLocal.x) / (2.0 * 3.14159265359) + 0.5;
                float v = asin(clamp(nLocal.y, -1.0, 1.0)) / 3.14159265359 + 0.5;
                vec2 coord = floor(vec2(u * u_TileScale, v * (u_TileScale * 0.5)));
                pattern = mod(coord.x + coord.y, 2.0);

                // Smooth analytic normal for sphere
                normal = normalize((u_Transform * vec4(v_LocalPos, 0.0)).xyz);
            }

            vec4 baseColor = mix(u_Color1, u_Color2, pattern);

            // Directional diffuse + ambient light
            vec3 lightDir = normalize(u_LightDir);
            float NdotL = max(dot(normal, lightDir), 0.0);
            float lighting = 0.38 + 0.62 * NdotL;

            o_Color = vec4(baseColor.rgb * lighting, baseColor.a);
        }
    )";

    m_Shader = std::make_unique<Axle::Shader>(vertexSrc, fragmentSrc);

    // 4. Cube Mesh Data (Unit cube from -0.5 to 0.5)
    Axle::f32 vertices[8 * 3] = {
        -0.5f, -0.5f, -0.5f,
         0.5f, -0.5f, -0.5f,
         0.5f,  0.5f, -0.5f,
        -0.5f,  0.5f, -0.5f,
        -0.5f, -0.5f,  0.5f,
         0.5f, -0.5f,  0.5f,
         0.5f,  0.5f,  0.5f,
        -0.5f,  0.5f,  0.5f
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
        { Axle::ShaderDataType::Float3, "a_Position" }
    });
    m_VertexArray->AddVertexBuffer(vertexBuffer);

    auto indexBuffer = std::make_shared<Axle::IndexBuffer>(indices, 36);
    m_VertexArray->SetIndexBuffer(indexBuffer);

    // 5. UV Sphere Mesh Data
    m_SphereVertexArray = CreateSphereMesh(24, 32);

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

    // Press 'R' to reset and drop the box and sphere again from elevation
    if (Axle::Input::IsKeyPressed(Axle::KeyCode::R)) {
        m_PhysicsScene->SetBodyPositionAndRotation(m_BoxBodyID, glm::vec3(0.0f, 10.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
        m_PhysicsScene->SetLinearVelocity(m_BoxBodyID, glm::vec3(0.0f));

        m_PhysicsScene->SetBodyPositionAndRotation(m_SphereBodyID, glm::vec3(2.0f, 14.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
        m_PhysicsScene->SetLinearVelocity(m_SphereBodyID, glm::vec3(0.0f));
    }

    m_LastDeltaTime = ts.GetSeconds();
    m_CameraController->OnUpdate(ts);
    m_PhysicsScene->Step(ts);

    // Render pass
    Axle::RenderCommand::Clear();

    m_Shader->Bind();
    m_Shader->SetMat4("u_ViewProjection", m_CameraController->GetCamera().GetViewProjectionMatrix());
    m_Shader->SetFloat3("u_LightDir", glm::vec3(0.45f, 1.0f, 0.65f));

    // 1. Render static floor with world-space 1m x 1m Caro (Checkerboard) pattern
    const glm::mat4 floorTransform = m_PhysicsScene->GetBodyTransform(m_FloorBodyID) * glm::scale(glm::mat4(1.0f), glm::vec3(100.0f, 1.0f, 100.0f));
    m_Shader->SetMat4("u_Transform", floorTransform);
    m_Shader->SetInt("u_PatternMode", 0);       // World-space XZ caro
    m_Shader->SetFloat("u_TileScale", 1.0f);    // 1-meter squares
    m_Shader->SetFloat4("u_Color1", glm::vec4(0.20f, 0.22f, 0.25f, 1.0f)); // Dark slate
    m_Shader->SetFloat4("u_Color2", glm::vec4(0.32f, 0.35f, 0.40f, 1.0f)); // Cool medium gray
    Axle::RenderCommand::DrawIndexed(m_VertexArray);

    // 2. Render dynamic falling box with 4x4 Caro (Checkerboard) pattern on each face
    const glm::mat4 boxTransform = m_PhysicsScene->GetBodyTransform(m_BoxBodyID);
    m_Shader->SetMat4("u_Transform", boxTransform);
    m_Shader->SetInt("u_PatternMode", 1);       // Local-space 3D caro
    m_Shader->SetFloat("u_TileScale", 4.0f);    // 4x4 squares per face
    m_Shader->SetFloat4("u_Color1", glm::vec4(0.96f, 0.52f, 0.12f, 1.0f)); // Vibrant motorsport orange
    m_Shader->SetFloat4("u_Color2", glm::vec4(0.12f, 0.13f, 0.15f, 1.0f)); // Dark graphite
    Axle::RenderCommand::DrawIndexed(m_VertexArray);

    // 3. Render dynamic bouncy sphere with spherical Caro (Checkerboard) pattern
    const glm::mat4 sphereTransform = m_PhysicsScene->GetBodyTransform(m_SphereBodyID) * glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));
    m_Shader->SetMat4("u_Transform", sphereTransform);
    m_Shader->SetInt("u_PatternMode", 2);       // Spherical Caro
    m_Shader->SetFloat("u_TileScale", 16.0f);   // 16 longitude sectors x 8 latitude rings
    m_Shader->SetFloat4("u_Color1", glm::vec4(0.0f, 0.85f, 0.95f, 1.0f));  // Electric cyan
    m_Shader->SetFloat4("u_Color2", glm::vec4(0.05f, 0.12f, 0.24f, 1.0f)); // Deep navy
    Axle::RenderCommand::DrawIndexed(m_SphereVertexArray);
}

void SandboxLayer::OnImGuiRender() {
    // Gather runtime telemetry
    Axle::TelemetryData telemetry{
        .DeltaTime = m_LastDeltaTime,
        .FPS = m_LastDeltaTime > 0.0f ? 1.0f / m_LastDeltaTime : 0.0f,
        .CameraPosition = m_CameraController->GetCamera().GetPosition(),
        .CameraRotation = m_CameraController->GetCamera().GetRotation(),
        .HasPhysics = true,
        .BoxPosition = m_PhysicsScene->GetBodyPosition(m_BoxBodyID),
        .BoxVelocity = m_PhysicsScene->GetLinearVelocity(m_BoxBodyID),
        .IsBoxActive = m_PhysicsScene->IsActive(m_BoxBodyID),
        .SpherePosition = m_PhysicsScene->GetBodyPosition(m_SphereBodyID),
        .SphereVelocity = m_PhysicsScene->GetLinearVelocity(m_SphereBodyID),
        .IsSphereActive = m_PhysicsScene->IsActive(m_SphereBodyID),
        .TotalBodies = m_PhysicsScene->GetNumBodies(),
        .ActiveBodies = m_PhysicsScene->GetNumActiveBodies()
    };

    m_DebugPanel.OnImGuiRender(telemetry);
}
