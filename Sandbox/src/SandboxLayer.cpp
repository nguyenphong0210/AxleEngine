#include "SandboxLayer.hpp"
#include <Axle/Application.hpp>
#include <Axle/Log.hpp>
#include <Axle/Input.hpp>
#include <Axle/KeyCodes.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/Components.hpp>
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

    // Dynamic box: 1m x 1m x 1m offset to side of track at (-5.0, 4.0, 8.0)
    m_BoxBodyID = m_PhysicsScene->CreateBox(
        glm::vec3(-5.0f, 4.0f, 8.0f),
        glm::vec3(0.5f, 0.5f, 0.5f),
        true,  // Dynamic
        0.5f,  // Friction
        0.4f   // Restitution
    );

    // Dynamic bouncy sphere: radius 0.5m offset to other side at (5.0, 4.0, 8.0)
    m_SphereBodyID = m_PhysicsScene->CreateSphere(
        glm::vec3(5.0f, 4.0f, 8.0f),
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

    // 6. Physics Debug Wireframe Renderer (JPH::DebugRenderer bridge)
    m_PhysicsDebugRenderer = std::make_unique<Axle::PhysicsDebugRenderer>();

    // 7. ECS Scene & Entities Initialization
    m_Scene = std::make_unique<Axle::Scene>();

    // Entity 1: Static Ground Floor
    m_FloorEntity = m_Scene->CreateEntity("Static Floor");
    m_FloorEntity.AddComponent<Axle::TransformComponent>(
        glm::vec3(0.0f, -0.5f, 0.0f),
        glm::vec3(100.0f, 1.0f, 100.0f)
    );
    m_FloorEntity.AddComponent<Axle::MeshComponent>(
        m_VertexArray,
        0,       // World-space XZ caro
        1.0f,    // 1-meter squares
        glm::vec4(0.20f, 0.22f, 0.25f, 1.0f), // Dark slate
        glm::vec4(0.32f, 0.35f, 0.40f, 1.0f)  // Cool medium gray
    );
    m_FloorEntity.AddComponent<Axle::RigidBodyComponent>(m_FloorBodyID, false);

    // Entity 2: Dynamic Falling Box
    m_BoxEntity = m_Scene->CreateEntity("Dynamic Box");
    m_BoxEntity.AddComponent<Axle::TransformComponent>(
        glm::vec3(-5.0f, 4.0f, 8.0f),
        glm::vec3(1.0f, 1.0f, 1.0f)
    );
    m_BoxEntity.AddComponent<Axle::MeshComponent>(
        m_VertexArray,
        1,       // Local-space 3D caro
        4.0f,    // 4x4 squares per face
        glm::vec4(0.96f, 0.52f, 0.12f, 1.0f), // Vibrant motorsport orange
        glm::vec4(0.12f, 0.13f, 0.15f, 1.0f)  // Dark graphite
    );
    m_BoxEntity.AddComponent<Axle::RigidBodyComponent>(m_BoxBodyID, true);

    // Entity 3: Dynamic Bouncy Sphere
    m_SphereEntity = m_Scene->CreateEntity("Bouncy Sphere");
    m_SphereEntity.AddComponent<Axle::TransformComponent>(
        glm::vec3(5.0f, 4.0f, 8.0f),
        glm::vec3(0.5f, 0.5f, 0.5f)
    );
    m_SphereEntity.AddComponent<Axle::MeshComponent>(
        m_SphereVertexArray,
        2,       // Spherical Caro
        16.0f,   // 16 sectors x 8 rings
        glm::vec4(0.0f, 0.85f, 0.95f, 1.0f),  // Electric cyan
        glm::vec4(0.05f, 0.12f, 0.24f, 1.0f)  // Deep navy
    );
    m_SphereEntity.AddComponent<Axle::RigidBodyComponent>(m_SphereBodyID, true);

    // 8. Vehicle Mechanics (Phase 4 Automotive Simulation)
    m_Vehicle = std::make_unique<Axle::Vehicle>(*m_PhysicsScene);

    // Entity 4: Vehicle Chassis
    m_ChassisEntity = m_Scene->CreateEntity("Vehicle Chassis");
    m_ChassisEntity.AddComponent<Axle::TransformComponent>(
        m_Vehicle->GetPosition(),
        glm::vec3(1.8f, 0.5f, 4.0f)
    );
    m_ChassisEntity.AddComponent<Axle::MeshComponent>(
        m_VertexArray,
        1,      // Local 3D caro
        4.0f,   // 4x4 squares per face
        glm::vec4(0.88f, 0.12f, 0.15f, 1.0f), // Racing crimson red
        glm::vec4(0.10f, 0.10f, 0.12f, 1.0f)  // Carbon fiber dark
    );
    m_ChassisEntity.AddComponent<Axle::RigidBodyComponent>(m_Vehicle->GetChassisBodyID(), true);

    // Entities 5-8: Four Vehicle Wheels
    const char* wheelNames[4] = { "Wheel FL", "Wheel FR", "Wheel RL", "Wheel RR" };
    for (Axle::u32 i = 0; i < 4; ++i) {
        m_WheelEntities[i] = m_Scene->CreateEntity(wheelNames[i]);
        m_WheelEntities[i].AddComponent<Axle::TransformComponent>(
            glm::vec3(0.0f),
            glm::vec3(0.20f, 0.35f, 0.35f)
        );
        m_WheelEntities[i].AddComponent<Axle::MeshComponent>(
            m_SphereVertexArray,
            2,      // Spherical Caro
            16.0f,
            glm::vec4(0.15f, 0.16f, 0.18f, 1.0f), // Tire rubber
            glm::vec4(0.85f, 0.85f, 0.90f, 1.0f)  // Alloy rim silver
        );
    }

    Axle::RenderCommand::SetClearColor({ 0.10f, 0.12f, 0.15f, 1.0f });
}

void SandboxLayer::OnDetach() {
    AXLE_INFO("SandboxLayer detached");
    m_Vehicle.reset();
    m_Scene.reset();
    m_PhysicsDebugRenderer.reset();
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

    // Press 'R' to reset and drop dynamic bodies and vehicle
    if (Axle::Input::IsKeyPressed(Axle::KeyCode::R)) {
        m_PhysicsScene->SetBodyPositionAndRotation(m_BoxBodyID, glm::vec3(-5.0f, 4.0f, 8.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
        m_PhysicsScene->SetLinearVelocity(m_BoxBodyID, glm::vec3(0.0f));

        m_PhysicsScene->SetBodyPositionAndRotation(m_SphereBodyID, glm::vec3(5.0f, 4.0f, 8.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
        m_PhysicsScene->SetLinearVelocity(m_SphereBodyID, glm::vec3(0.0f));

        if (m_Vehicle) {
            m_Vehicle->Reset(glm::vec3(0.0f, 1.2f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        }
        m_OrbitYaw = 180.0f;
        m_OrbitPitch = 15.0f;
    }

    // Press 'C' to toggle chase camera / free-fly camera
    if (Axle::Input::IsKeyPressed(Axle::KeyCode::C)) {
        if (!m_CKeyWasPressed) {
            m_ChaseCamera = !m_ChaseCamera;
            m_CKeyWasPressed = true;
        }
    } else {
        m_CKeyWasPressed = false;
    }

    // Press 'F1' to toggle physics wireframe debug visualization
    if (Axle::Input::IsKeyPressed(Axle::KeyCode::F1)) {
        if (!m_F1KeyWasPressed) {
            m_ShowPhysicsDebug = !m_ShowPhysicsDebug;
            m_F1KeyWasPressed = true;
        }
    } else {
        m_F1KeyWasPressed = false;
    }

    // Driver inputs for vehicle
    if (m_Vehicle) {
        Axle::f32 forwardInput = 0.0f;
        Axle::f32 rightInput = 0.0f;
        Axle::f32 brakeInput = 0.0f;
        Axle::f32 handBrakeInput = 0.0f;

        // Drive inputs: Arrow keys always work; WASD work when Chase Camera is active
        if (Axle::Input::IsKeyPressed(Axle::KeyCode::Up) || (m_ChaseCamera && Axle::Input::IsKeyPressed(Axle::KeyCode::W))) {
            forwardInput += 1.0f;
        }
        if (Axle::Input::IsKeyPressed(Axle::KeyCode::Down) || (m_ChaseCamera && Axle::Input::IsKeyPressed(Axle::KeyCode::S))) {
            forwardInput -= 1.0f;
        }
        if (Axle::Input::IsKeyPressed(Axle::KeyCode::Left) || (m_ChaseCamera && Axle::Input::IsKeyPressed(Axle::KeyCode::A))) {
            rightInput -= 1.0f;
        }
        if (Axle::Input::IsKeyPressed(Axle::KeyCode::Right) || (m_ChaseCamera && Axle::Input::IsKeyPressed(Axle::KeyCode::D))) {
            rightInput += 1.0f;
        }
        if (Axle::Input::IsKeyPressed(Axle::KeyCode::Space)) {
            handBrakeInput = 1.0f;
        }

        m_Vehicle->SetDriverInput(forwardInput, rightInput, brakeInput, handBrakeInput);
    }

    m_LastDeltaTime = ts.GetSeconds();

    // Camera update: Chase vehicle with mouse orbit, or Free-fly controller
    if (m_ChaseCamera && m_Vehicle) {
        const auto [mouseX, mouseY] = Axle::Input::GetMousePosition();
        const glm::vec2 currentMousePos{ mouseX, mouseY };

        if (m_FirstMouseUpdate) {
            m_LastMousePos = currentMousePos;
            m_FirstMouseUpdate = false;
        }

        const glm::vec2 mouseDelta = currentMousePos - m_LastMousePos;
        m_LastMousePos = currentMousePos;

        // Orbit camera around vehicle with mouse drag (Right or Left button)
        const bool isDragging = Axle::Input::IsMouseButtonPressed(Axle::MouseCode::ButtonRight)
                             || Axle::Input::IsMouseButtonPressed(Axle::MouseCode::ButtonLeft);

        if (isDragging) {
            m_OrbitYaw -= mouseDelta.x * 0.25f;
            m_OrbitPitch -= mouseDelta.y * 0.25f;
            m_OrbitPitch = std::clamp(m_OrbitPitch, -45.0f, 75.0f);
        }

        const glm::vec3 carPos = m_Vehicle->GetPosition();
        const glm::vec3 carFwd = m_Vehicle->GetForwardVector();

        // If driving and not dragging mouse, smoothly align orbit behind car heading
        if (!isDragging) {
            const Axle::f32 carSpeed = glm::length(m_PhysicsScene->GetLinearVelocity(m_Vehicle->GetChassisBodyID()));
            if (carSpeed > 0.5f) {
                const Axle::f32 targetYaw = glm::degrees(std::atan2(-carFwd.x, -carFwd.z));
                Axle::f32 diff = targetYaw - m_OrbitYaw;
                while (diff < -180.0f) diff += 360.0f;
                while (diff > 180.0f) diff -= 360.0f;
                m_OrbitYaw += diff * std::min(1.0f, ts.GetSeconds() * 3.5f);
            }
        }

        const Axle::f32 radYaw = glm::radians(m_OrbitYaw);
        const Axle::f32 radPitch = glm::radians(m_OrbitPitch);
        constexpr Axle::f32 dist = 7.5f;

        glm::vec3 offset;
        offset.x = -std::sin(radYaw) * std::cos(radPitch) * dist;
        offset.y = std::sin(radPitch) * dist + 1.2f;
        offset.z = -std::cos(radYaw) * std::cos(radPitch) * dist;

        const glm::vec3 lookTarget = carPos + glm::vec3(0.0f, 0.8f, 0.0f);
        const glm::vec3 camPos = lookTarget - offset;
        const glm::vec3 lookDir = glm::normalize(lookTarget - camPos);

        const Axle::f32 pitch = glm::degrees(std::asin(std::clamp(lookDir.y, -0.999f, 0.999f)));
        const Axle::f32 yaw = glm::degrees(std::atan2(-lookDir.x, -lookDir.z));

        m_CameraController->SetPosition(camPos);
        m_CameraController->SetRotation(glm::vec3(pitch, yaw, 0.0f));
    } else {
        m_CameraController->OnUpdate(ts);
    }

    // Step physics simulation
    m_PhysicsScene->Step(ts);

    // Synchronize 4 wheel entity transforms with physics vehicle constraint
    if (m_Vehicle) {
        for (Axle::u32 i = 0; i < 4; ++i) {
            const glm::mat4 wheelMat = m_Vehicle->GetWheelTransform(i);
            auto& tc = m_WheelEntities[i].GetComponent<Axle::TransformComponent>();
            tc.Translation = glm::vec3(wheelMat[3]);
            tc.Rotation = glm::quat_cast(wheelMat);
        }
    }

    // Render pass
    Axle::RenderCommand::Clear();

    // 1. Render all entities through ECS Scene (chassis position is updated via RigidBodyComponent)
    m_Scene->OnUpdate(ts, m_PhysicsScene.get(), m_CameraController->GetCamera(), *m_Shader);

    // 2. Render JPH::DebugRenderer physics collider wireframes when enabled
    if (m_ShowPhysicsDebug && m_PhysicsDebugRenderer) {
        m_PhysicsDebugRenderer->Begin(m_CameraController->GetCamera().GetPosition());
        m_PhysicsScene->DrawDebug(*m_PhysicsDebugRenderer, m_PhysicsDrawSettings);
        m_PhysicsDebugRenderer->End(m_CameraController->GetCamera().GetViewProjectionMatrix());
    }
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
        .ActiveBodies = m_PhysicsScene->GetNumActiveBodies(),
        .ShowPhysicsWireframe = m_ShowPhysicsDebug,
        .DrawBoundingBoxes = m_PhysicsDrawSettings.DrawBoundingBoxes,
        .DrawVelocity = m_PhysicsDrawSettings.DrawVelocity,
        .DrawCenterOfMass = m_PhysicsDrawSettings.DrawCenterOfMass,
        .DebugLineCount = m_PhysicsDebugRenderer ? m_PhysicsDebugRenderer->GetLineCount() : 0,
        .HasVehicle = (m_Vehicle != nullptr),
        .IsChaseCamera = m_ChaseCamera
    };

    if (m_Vehicle) {
        const auto vTele = m_Vehicle->GetTelemetry();
        telemetry.VehicleSpeedKmh = vTele.SpeedKmh;
        telemetry.VehicleRPM = vTele.EngineRPM;
        telemetry.VehicleGear = vTele.CurrentGear;
        for (Axle::u32 i = 0; i < 4; ++i) {
            telemetry.WheelContacts[i] = vTele.Wheels[i].HasContact;
            telemetry.WheelSuspension[i] = vTele.Wheels[i].SuspensionLength;
        }
    }

    m_DebugPanel.OnImGuiRender(telemetry);

    m_ShowPhysicsDebug = telemetry.ShowPhysicsWireframe;
    m_PhysicsDrawSettings.DrawBoundingBoxes = telemetry.DrawBoundingBoxes;
    m_PhysicsDrawSettings.DrawVelocity = telemetry.DrawVelocity;
    m_PhysicsDrawSettings.DrawCenterOfMass = telemetry.DrawCenterOfMass;
}
