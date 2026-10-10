#include "SandboxLayer.hpp"
#include <Axle/Application.hpp>
#include <Axle/Input.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/Components.hpp>
#include <Axle/Log.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>

SandboxLayer::SandboxLayer()
    : Layer("SandboxLayer") {
}

void SandboxLayer::OnAttach() {
    AXLE_INFO("SandboxLayer attached - Initializing Axle Editor Environment");

    auto& window = Axle::Application::Get().GetWindow();

    // 1. Perspective Camera
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

    // Static ground plane: normal (0, 1, 0) at Y = 0.0m
    m_FloorBodyID = m_PhysicsScene->CreatePlane(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        100.0f, // HalfExtent
        0.6f,   // Friction
        0.2f    // Restitution
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

    // Dynamic cylinder: radius 0.5m, half-height 0.75m at (-7.5, 4.0, 14.0)
    m_CylinderBodyID = m_PhysicsScene->CreateCylinder(
        glm::vec3(-7.5f, 4.0f, 14.0f),
        0.75f, // HalfHeight
        0.5f,  // Radius
        true,  // Dynamic
        0.5f,  // Friction
        0.3f   // Restitution
    );

    // Dynamic cone: radius 0.6m, half-height 0.75m at (7.5, 4.0, 14.0)
    m_ConeBodyID = m_PhysicsScene->CreateCone(
        glm::vec3(7.5f, 4.0f, 14.0f),
        0.75f, // HalfHeight
        0.6f,  // Radius
        true,  // Dynamic
        0.5f,  // Friction
        0.2f   // Restitution
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

                normal = vec3(0.0, 1.0, 0.0);
            } else if (u_PatternMode == 1) {
                // Local 3D checkerboard (4x4 tiles per face on the 1x1x1 box)
                vec3 coord = floor((v_LocalPos + vec3(0.5)) * u_TileScale + vec3(0.0001));
                pattern = mod(coord.x + coord.y + coord.z, 2.0);

                vec3 dx = dFdx(v_WorldPos);
                vec3 dy = dFdy(v_WorldPos);
                normal = normalize(cross(dx, dy));
                if (dot(normal, normalize(-v_WorldPos)) < 0.0) normal = -normal;
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

    // 4. Primitive Mesh Data (Cube, Sphere, Cylinder, Cone, Plane)
    m_CubeMesh = Axle::PrimitiveMesh::CreateCube();
    m_SphereMesh = Axle::PrimitiveMesh::CreateSphere(24, 32);
    m_CylinderMesh = Axle::PrimitiveMesh::CreateCylinder(32);
    m_ConeMesh = Axle::PrimitiveMesh::CreateCone(32);
    m_PlaneMesh = Axle::PrimitiveMesh::CreatePlane();

    // 5. Offscreen Framebuffer for Viewport
    Axle::FramebufferSpecification fbSpec;
    fbSpec.Width = window.GetWidth();
    fbSpec.Height = window.GetHeight();
    m_Framebuffer = Axle::Framebuffer::Create(fbSpec);
    m_ViewportWidth = fbSpec.Width;
    m_ViewportHeight = fbSpec.Height;

    // 6. Physics Debug Wireframe Renderer (JPH::DebugRenderer bridge)
    m_PhysicsDebugRenderer = std::make_unique<Axle::PhysicsDebugRenderer>();

    // 7. ECS Scene & Entities Initialization
    m_Scene = std::make_unique<Axle::Scene>();

    // Entity 1: Static Ground Plane
    m_FloorEntity = m_Scene->CreateEntity("Static Floor Plane");
    m_FloorEntity.AddComponent<Axle::TransformComponent>(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(200.0f, 1.0f, 200.0f)
    );
    m_FloorEntity.AddComponent<Axle::MeshComponent>(
        m_PlaneMesh,
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
        m_CubeMesh,
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
        m_SphereMesh,
        2,       // Spherical Caro
        16.0f,   // 16 sectors x 8 rings
        glm::vec4(0.0f, 0.85f, 0.95f, 1.0f),  // Electric cyan
        glm::vec4(0.05f, 0.12f, 0.24f, 1.0f)  // Deep navy
    );
    m_SphereEntity.AddComponent<Axle::RigidBodyComponent>(m_SphereBodyID, true);

    // Entity 4: Dynamic Cylinder
    m_CylinderEntity = m_Scene->CreateEntity("Dynamic Cylinder");
    m_CylinderEntity.AddComponent<Axle::TransformComponent>(
        glm::vec3(-7.5f, 4.0f, 14.0f),
        glm::vec3(0.5f, 0.75f, 0.5f)
    );
    m_CylinderEntity.AddComponent<Axle::MeshComponent>(
        m_CylinderMesh,
        1,       // Local 3D caro
        4.0f,
        glm::vec4(0.12f, 0.88f, 0.38f, 1.0f), // Neon Emerald Green
        glm::vec4(0.06f, 0.22f, 0.12f, 1.0f)  // Deep Forest Green
    );
    m_CylinderEntity.AddComponent<Axle::RigidBodyComponent>(m_CylinderBodyID, true);

    // Entity 5: Dynamic Cone
    m_ConeEntity = m_Scene->CreateEntity("Dynamic Cone");
    m_ConeEntity.AddComponent<Axle::TransformComponent>(
        glm::vec3(7.5f, 4.0f, 14.0f),
        glm::vec3(0.6f, 0.75f, 0.6f)
    );
    m_ConeEntity.AddComponent<Axle::MeshComponent>(
        m_ConeMesh,
        1,       // Local 3D caro
        5.0f,
        glm::vec4(0.98f, 0.76f, 0.10f, 1.0f), // Vivid Gold
        glm::vec4(0.52f, 0.10f, 0.82f, 1.0f)  // Royal Velvet Purple
    );
    m_ConeEntity.AddComponent<Axle::RigidBodyComponent>(m_ConeBodyID, true);

    // 8. Vehicle Mechanics (Phase 4 Automotive Simulation)
    m_Vehicle = std::make_unique<Axle::Vehicle>(*m_PhysicsScene);

    // Entity 6: Vehicle Chassis
    m_ChassisEntity = m_Scene->CreateEntity("Vehicle Chassis");
    m_ChassisEntity.AddComponent<Axle::TransformComponent>(
        m_Vehicle->GetPosition(),
        glm::vec3(1.8f, 0.5f, 4.0f)
    );
    m_ChassisEntity.AddComponent<Axle::MeshComponent>(
        m_CubeMesh,
        1,      // Local 3D caro
        4.0f,   // 4x4 squares per face
        glm::vec4(0.88f, 0.12f, 0.15f, 1.0f), // Racing crimson red
        glm::vec4(0.10f, 0.10f, 0.12f, 1.0f)  // Carbon fiber dark
    );
    m_ChassisEntity.AddComponent<Axle::RigidBodyComponent>(m_Vehicle->GetChassisBodyID(), true);

    // Entities 7-10: Four Vehicle Wheels (Cylindrical Wheels)
    const char* wheelNames[4] = { "Wheel FL", "Wheel FR", "Wheel RL", "Wheel RR" };
    for (Axle::u32 i = 0; i < 4; ++i) {
        m_WheelEntities[i] = m_Scene->CreateEntity(wheelNames[i]);
        m_WheelEntities[i].AddComponent<Axle::TransformComponent>(
            glm::vec3(0.0f),
            glm::vec3(0.35f, 0.125f, 0.35f) // Radius 0.35m, half-width 0.125m (width 0.25m)
        );
        m_WheelEntities[i].AddComponent<Axle::MeshComponent>(
            m_CylinderMesh,
            1,      // Local 3D caro
            6.0f,
            glm::vec4(0.15f, 0.16f, 0.18f, 1.0f), // Tire rubber
            glm::vec4(0.85f, 0.85f, 0.90f, 1.0f)  // Alloy rim silver
        );
    }

    // Connect Editor Panels
    m_SceneHierarchyPanel.SetContext(m_Scene.get(), m_PhysicsScene.get());
    m_VehicleTunerPanel.SetContext(m_Vehicle.get(), m_PhysicsScene.get());

    AXLE_INFO("Axle Editor initialized successfully");
}

void SandboxLayer::OnDetach() {
    AXLE_INFO("SandboxLayer detached");
    m_Vehicle.reset();
    m_Scene.reset();
    m_PhysicsDebugRenderer.reset();
    m_PhysicsScene.reset();
    m_Framebuffer.reset();
    m_CubeMesh.reset();
    m_SphereMesh.reset();
    m_CylinderMesh.reset();
    m_ConeMesh.reset();
    m_PlaneMesh.reset();
}

void SandboxLayer::OnWindowResize(Axle::u32 width, Axle::u32 height) {
    if (m_CameraController) {
        m_CameraController->OnResize(width, height);
    }
}

void SandboxLayer::ResetSimulation() {
    AXLE_INFO("Resetting physics simulation and vehicle state");
    m_PhysicsScene->SetBodyPositionAndRotation(m_BoxBodyID, glm::vec3(-5.0f, 4.0f, 8.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
    m_PhysicsScene->SetLinearVelocity(m_BoxBodyID, glm::vec3(0.0f));

    m_PhysicsScene->SetBodyPositionAndRotation(m_SphereBodyID, glm::vec3(5.0f, 4.0f, 8.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
    m_PhysicsScene->SetLinearVelocity(m_SphereBodyID, glm::vec3(0.0f));

    m_PhysicsScene->SetBodyPositionAndRotation(m_CylinderBodyID, glm::vec3(-7.5f, 4.0f, 14.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
    m_PhysicsScene->SetLinearVelocity(m_CylinderBodyID, glm::vec3(0.0f));

    m_PhysicsScene->SetBodyPositionAndRotation(m_ConeBodyID, glm::vec3(7.5f, 4.0f, 14.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
    m_PhysicsScene->SetLinearVelocity(m_ConeBodyID, glm::vec3(0.0f));

    if (m_Vehicle) {
        m_Vehicle->Reset(glm::vec3(0.0f, 1.2f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    }
    m_OrbitYaw = 180.0f;
    m_OrbitPitch = 15.0f;
}

void SandboxLayer::OnUpdate(Axle::Timestep ts) {
    m_LastDeltaTime = ts.GetSeconds();

    // Hotkey Controls
    if (Axle::Input::IsKeyPressed(Axle::KeyCode::R)) {
        ResetSimulation();
    }

    if (Axle::Input::IsKeyPressed(Axle::KeyCode::C)) {
        if (!m_CKeyWasPressed) {
            m_ChaseCamera = !m_ChaseCamera;
            m_CKeyWasPressed = true;
        }
    } else {
        m_CKeyWasPressed = false;
    }

    if (Axle::Input::IsKeyPressed(Axle::KeyCode::F1)) {
        if (!m_F1KeyWasPressed) {
            m_ShowPhysicsDebug = !m_ShowPhysicsDebug;
            m_F1KeyWasPressed = true;
        }
    } else {
        m_F1KeyWasPressed = false;
    }

    // Vehicle Driving Input (Active only when Viewport is hovered or focused)
    if (m_Vehicle) {
        Axle::f32 forwardInput = 0.0f;
        Axle::f32 rightInput = 0.0f;
        Axle::f32 brakeInput = 0.0f;
        Axle::f32 handBrakeInput = 0.0f;

        if (m_ViewportHovered || m_ViewportFocused) {
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
        }

        m_Vehicle->SetDriverInput(forwardInput, rightInput, brakeInput, handBrakeInput);
    }

    // Camera Update
    if (m_ChaseCamera && m_Vehicle) {
        const auto [mouseX, mouseY] = Axle::Input::GetMousePosition();
        const glm::vec2 currentMousePos{ mouseX, mouseY };

        if (m_FirstMouseUpdate) {
            m_LastMousePos = currentMousePos;
            m_FirstMouseUpdate = false;
        }

        const glm::vec2 mouseDelta = currentMousePos - m_LastMousePos;
        m_LastMousePos = currentMousePos;

        const bool isDragging = (m_ViewportHovered || m_ViewportFocused) 
                             && (Axle::Input::IsMouseButtonPressed(Axle::MouseCode::ButtonRight)
                              || Axle::Input::IsMouseButtonPressed(Axle::MouseCode::ButtonLeft));

        if (isDragging) {
            m_OrbitYaw -= mouseDelta.x * 0.25f;
            m_OrbitPitch -= mouseDelta.y * 0.25f;
            m_OrbitPitch = std::clamp(m_OrbitPitch, -45.0f, 75.0f);
        }

        const glm::vec3 carPos = m_Vehicle->GetPosition();
        const glm::vec3 carFwd = m_Vehicle->GetForwardVector();

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
        if (m_ViewportHovered) {
            m_CameraController->OnUpdate(ts);
        }
    }

    // Physics Simulation Stepping
    if (m_IsSimulationRunning) {
        Axle::Timestep simTs = ts.GetSeconds() * m_SimulationSpeed;
        m_PhysicsScene->Step(simTs);
    } else if (m_StepSingleFrame) {
        m_PhysicsScene->Step(1.0f / 60.0f);
        m_StepSingleFrame = false;
    }

    // Synchronize 4 wheel entity transforms with physics vehicle constraint
    if (m_Vehicle) {
        for (Axle::u32 i = 0; i < 4; ++i) {
            const glm::mat4 wheelMat = m_Vehicle->GetWheelTransform(i);
            auto& tc = m_WheelEntities[i].GetComponent<Axle::TransformComponent>();
            tc.Translation = glm::vec3(wheelMat[3]);
            tc.Rotation = glm::quat_cast(wheelMat);
        }
    }

    // ==========================================
    // RENDER PASS TO OFFSCREEN FRAMEBUFFER
    // ==========================================
    if (m_Framebuffer) {
        m_Framebuffer->Bind();
        Axle::RenderCommand::SetViewport(0, 0, m_ViewportWidth, m_ViewportHeight);
        Axle::RenderCommand::SetClearColor({ 0.10f, 0.12f, 0.15f, 1.0f });
        Axle::RenderCommand::Clear();

        // 1. Render all ECS entities
        m_Scene->OnUpdate(ts, m_PhysicsScene.get(), m_CameraController->GetCamera(), *m_Shader);

        // 2. Render JPH::DebugRenderer physics collider wireframes when enabled
        if (m_ShowPhysicsDebug && m_PhysicsDebugRenderer) {
            m_PhysicsDebugRenderer->Begin(m_CameraController->GetCamera().GetPosition());
            m_PhysicsScene->DrawDebug(*m_PhysicsDebugRenderer, m_PhysicsDrawSettings);
            m_PhysicsDebugRenderer->End(m_CameraController->GetCamera().GetViewProjectionMatrix());
        }

        m_Framebuffer->Unbind();
    }
}

void SandboxLayer::RenderToolbar() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 6.0f));

    if (ImGui::Begin("##Toolbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        // Play / Pause button
        if (m_IsSimulationRunning) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.35f, 0.15f, 1.0f));
            if (ImGui::Button("Pause (Space)", ImVec2(100.0f, 26.0f))) {
                m_IsSimulationRunning = false;
            }
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.75f, 0.25f, 1.0f));
            if (ImGui::Button("Play (Space)", ImVec2(100.0f, 26.0f))) {
                m_IsSimulationRunning = true;
            }
            ImGui::PopStyleColor();
        }

        ImGui::SameLine();
        if (ImGui::Button("Step Frame", ImVec2(90.0f, 26.0f))) {
            m_IsSimulationRunning = false;
            m_StepSingleFrame = true;
        }

        ImGui::SameLine();
        if (ImGui::Button("Reset Scene", ImVec2(90.0f, 26.0f))) {
            ResetSimulation();
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::SliderFloat("Speed", &m_SimulationSpeed, 0.1f, 2.0f, "%.1fx");

        ImGui::SameLine();
        ImGui::Spacing();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "| Camera: %s", 
                           m_ChaseCamera ? "Chase Cam [C]" : "Free-Fly [C]");

        ImGui::SameLine();
        ImGui::Checkbox("Wireframe [F1]", &m_ShowPhysicsDebug);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void SandboxLayer::OnImGuiRender() {
    // 1. Fullscreen DockSpace Container
    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(mainViewport->WorkPos);
    ImGui::SetNextWindowSize(mainViewport->WorkSize);
    ImGui::SetNextWindowViewport(mainViewport->ID);

    ImGuiWindowFlags dockspaceWindowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking
                                         | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse
                                         | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                                         | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("AxleEngineDockSpace", nullptr, dockspaceWindowFlags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspaceID = ImGui::GetID("AxleMainDockspace");
    ImGui::DockSpace(dockspaceID, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    // 2. Main Menu Bar
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                // Window will close
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Simulation")) {
            if (ImGui::MenuItem(m_IsSimulationRunning ? "Pause" : "Play", "Space")) {
                m_IsSimulationRunning = !m_IsSimulationRunning;
            }
            if (ImGui::MenuItem("Step 1 Frame", "O")) {
                m_IsSimulationRunning = false;
                m_StepSingleFrame = true;
            }
            if (ImGui::MenuItem("Reset All", "R")) {
                ResetSimulation();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Scene Hierarchy", nullptr, &m_ShowSceneHierarchy);
            ImGui::MenuItem("Properties / Inspector", nullptr, &m_ShowProperties);
            ImGui::MenuItem("Vehicle Tuner", nullptr, &m_ShowVehicleTuner);
            ImGui::MenuItem("Console Log", nullptr, &m_ShowConsole);
            ImGui::MenuItem("Telemetry & Performance", nullptr, &m_ShowTelemetry);
            ImGui::MenuItem("Physics Wireframe", "F1", &m_ShowPhysicsDebug);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Camera")) {
            if (ImGui::MenuItem("Vehicle Chase Camera", "C", m_ChaseCamera)) {
                m_ChaseCamera = true;
            }
            if (ImGui::MenuItem("Free-Fly Camera", "C", !m_ChaseCamera)) {
                m_ChaseCamera = false;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
    ImGui::End(); // End AxleEngineDockSpace

    // 3. Render Top Simulation Toolbar
    RenderToolbar();

    // 4. Render 3D Viewport Window
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("Viewport");

    m_ViewportFocused = ImGui::IsWindowFocused();
    m_ViewportHovered = ImGui::IsWindowHovered();

    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    if (viewportSize.x > 0.0f && viewportSize.y > 0.0f &&
        (m_ViewportWidth != static_cast<Axle::u32>(viewportSize.x) || 
         m_ViewportHeight != static_cast<Axle::u32>(viewportSize.y))) {
        m_ViewportWidth = static_cast<Axle::u32>(viewportSize.x);
        m_ViewportHeight = static_cast<Axle::u32>(viewportSize.y);
        m_Framebuffer->Resize(m_ViewportWidth, m_ViewportHeight);
        m_CameraController->OnResize(m_ViewportWidth, m_ViewportHeight);
    }

    if (m_Framebuffer) {
        uint32_t textureID = m_Framebuffer->GetColorAttachmentRendererID();
        // Flip UV coordinates vertically for OpenGL
        ImGui::Image(reinterpret_cast<void*>(static_cast<intptr_t>(textureID)), 
                     viewportSize, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    }

    // Viewport Overlay Badges
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 winPos = ImGui::GetWindowPos();
    ImVec2 badgePos = ImVec2(winPos.x + 12.0f, winPos.y + 12.0f);

    const char* statusText = m_IsSimulationRunning ? "SIMULATION ACTIVE" : "SIMULATION PAUSED";
    ImU32 statusBg = m_IsSimulationRunning ? IM_COL32(30, 150, 60, 200) : IM_COL32(200, 100, 20, 200);
    drawList->AddRectFilled(badgePos, ImVec2(badgePos.x + 160.0f, badgePos.y + 24.0f), statusBg, 4.0f);
    drawList->AddText(ImVec2(badgePos.x + 8.0f, badgePos.y + 4.0f), IM_COL32(255, 255, 255, 255), statusText);

    if (m_Vehicle) {
        const auto vTele = m_Vehicle->GetTelemetry();
        char speedStr[32];
        snprintf(speedStr, sizeof(speedStr), "%.1f KM/H", vTele.SpeedKmh);
        ImVec2 speedPos = ImVec2(badgePos.x + 170.0f, badgePos.y);
        drawList->AddRectFilled(speedPos, ImVec2(speedPos.x + 100.0f, speedPos.y + 24.0f), IM_COL32(20, 80, 160, 200), 4.0f);
        drawList->AddText(ImVec2(speedPos.x + 8.0f, speedPos.y + 4.0f), IM_COL32(255, 255, 255, 255), speedStr);
    }

    ImGui::End();
    ImGui::PopStyleVar();

    // 5. Scene Hierarchy & Inspector Panels
    m_SceneHierarchyPanel.OnImGuiRender(&m_ShowSceneHierarchy, &m_ShowProperties);

    // 6. Vehicle Tuner Panel
    m_VehicleTunerPanel.OnImGuiRender(&m_ShowVehicleTuner);

    // 7. Console Log Panel
    m_ConsolePanel.OnImGuiRender(&m_ShowConsole);

    // 8. Performance & Telemetry Panel
    if (m_ShowTelemetry) {
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
}
