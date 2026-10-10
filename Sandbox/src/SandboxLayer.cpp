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
    auto& chassisHierarchy = m_ChassisEntity.AddComponent<Axle::HierarchyComponent>();

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
        m_WheelEntities[i].AddComponent<Axle::HierarchyComponent>(static_cast<entt::entity>(m_ChassisEntity));
        chassisHierarchy.Children.push_back(static_cast<entt::entity>(m_WheelEntities[i]));
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

void SandboxLayer::OnScenePlay() {
    m_SceneState = SceneState::Play;
    m_ChaseCamera = true;
    AXLE_INFO("Simulation started (Play Mode). Chase Camera locked to Vehicle. Drive with WASD/Arrows (Space: Handbrake).");
}

void SandboxLayer::OnScenePause() {
    m_SceneState = SceneState::Pause;
    AXLE_INFO("Simulation paused. Viewport Free-Fly inspection enabled.");
}

void SandboxLayer::OnSceneStop() {
    m_SceneState = SceneState::Edit;
    ResetSimulation();
    m_ChaseCamera = false;
    AXLE_INFO("Simulation stopped (Edit Mode). Editor Free-Fly Camera active: Hold RMB to look, WASD to fly (Shift: Sprint, Q/E: Elevate).");
}

void SandboxLayer::ResetSimulation() {
    AXLE_INFO("Resetting physics simulation and vehicle state");
    m_PhysicsScene->SetBodyPositionAndRotation(m_BoxBodyID, glm::vec3(-5.0f, 4.0f, 8.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
    m_PhysicsScene->SetLinearVelocity(m_BoxBodyID, glm::vec3(0.0f));
    m_PhysicsScene->GetBodyInterface().SetAngularVelocity(m_BoxBodyID, JPH::Vec3::sZero());

    m_PhysicsScene->SetBodyPositionAndRotation(m_SphereBodyID, glm::vec3(5.0f, 4.0f, 8.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
    m_PhysicsScene->SetLinearVelocity(m_SphereBodyID, glm::vec3(0.0f));
    m_PhysicsScene->GetBodyInterface().SetAngularVelocity(m_SphereBodyID, JPH::Vec3::sZero());

    m_PhysicsScene->SetBodyPositionAndRotation(m_CylinderBodyID, glm::vec3(-7.5f, 4.0f, 14.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
    m_PhysicsScene->SetLinearVelocity(m_CylinderBodyID, glm::vec3(0.0f));
    m_PhysicsScene->GetBodyInterface().SetAngularVelocity(m_CylinderBodyID, JPH::Vec3::sZero());

    m_PhysicsScene->SetBodyPositionAndRotation(m_ConeBodyID, glm::vec3(7.5f, 4.0f, 14.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
    m_PhysicsScene->SetLinearVelocity(m_ConeBodyID, glm::vec3(0.0f));
    m_PhysicsScene->GetBodyInterface().SetAngularVelocity(m_ConeBodyID, JPH::Vec3::sZero());

    if (m_Vehicle) {
        m_Vehicle->Reset(glm::vec3(0.0f, 1.2f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        m_Vehicle->SetDriverInput(0.0f, 0.0f, 0.0f, 0.0f);
        auto& chassisTC = m_ChassisEntity.GetComponent<Axle::TransformComponent>();
        chassisTC.Translation = m_Vehicle->GetPosition();
        chassisTC.Rotation = m_PhysicsScene->GetBodyRotation(m_Vehicle->GetChassisBodyID());

        for (Axle::u32 i = 0; i < 4; ++i) {
            const glm::mat4 wheelMat = m_Vehicle->GetWheelTransform(i);
            auto& tc = m_WheelEntities[i].GetComponent<Axle::TransformComponent>();
            tc.Translation = glm::vec3(wheelMat[3]);
            tc.Rotation = glm::quat_cast(wheelMat);
        }
    }
    m_OrbitYaw = 180.0f;
    m_OrbitPitch = 15.0f;
}

void SandboxLayer::OnUpdate(Axle::Timestep ts) {
    m_LastDeltaTime = ts.GetSeconds();

    // Hotkey Controls (debounced)
    if (Axle::Input::IsKeyPressed(Axle::KeyCode::F5)) {
        if (!m_F5KeyWasPressed) {
            if (m_SceneState != SceneState::Play) {
                OnScenePlay();
            }
            m_F5KeyWasPressed = true;
        }
    } else {
        m_F5KeyWasPressed = false;
    }

    if (Axle::Input::IsKeyPressed(Axle::KeyCode::F6)) {
        if (!m_F6KeyWasPressed) {
            if (m_SceneState == SceneState::Play) {
                OnScenePause();
            } else if (m_SceneState == SceneState::Pause) {
                OnScenePlay();
            }
            m_F6KeyWasPressed = true;
        }
    } else {
        m_F6KeyWasPressed = false;
    }

    if (Axle::Input::IsKeyPressed(Axle::KeyCode::F7) || Axle::Input::IsKeyPressed(Axle::KeyCode::Escape)) {
        if (!m_F7KeyWasPressed && !m_EscKeyWasPressed) {
            if (m_SceneState != SceneState::Edit) {
                OnSceneStop();
            }
            m_F7KeyWasPressed = true;
            m_EscKeyWasPressed = true;
        }
    } else {
        m_F7KeyWasPressed = false;
        m_EscKeyWasPressed = false;
    }

    if (Axle::Input::IsKeyPressed(Axle::KeyCode::O)) {
        if (!m_OKeyWasPressed) {
            if (m_SceneState == SceneState::Pause) {
                m_StepSingleFrame = true;
            }
            m_OKeyWasPressed = true;
        }
    } else {
        m_OKeyWasPressed = false;
    }

    const bool isHoldingRMB = Axle::Input::IsMouseButtonPressed(Axle::MouseCode::ButtonRight);
    const bool isCtrlDown = Axle::Input::IsKeyPressed(Axle::KeyCode::LeftControl) || Axle::Input::IsKeyPressed(Axle::KeyCode::RightControl);

    if (Axle::Input::IsKeyPressed(Axle::KeyCode::R)) {
        if (!m_RKeyWasPressed) {
            if (isCtrlDown || m_SceneState == SceneState::Play) {
                ResetSimulation();
            } else if (!isHoldingRMB && !ImGui::GetIO().WantTextInput) {
                m_GizmoType = ImGuizmo::OPERATION::SCALE;
            }
            m_RKeyWasPressed = true;
        }
    } else {
        m_RKeyWasPressed = false;
    }

    // Gizmo Hotkeys (Q: Select, W: Move, E: Rotate) when not holding RMB, not typing in UI, and not playing
    if (!isHoldingRMB && !ImGui::GetIO().WantTextInput && m_SceneState != SceneState::Play) {
        if (Axle::Input::IsKeyPressed(Axle::KeyCode::Q)) {
            m_GizmoType = -1;
        } else if (Axle::Input::IsKeyPressed(Axle::KeyCode::W)) {
            m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
        } else if (Axle::Input::IsKeyPressed(Axle::KeyCode::E)) {
            m_GizmoType = ImGuizmo::OPERATION::ROTATE;
        }
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

    // Vehicle Driving Input (Active ONLY in Play Mode when Viewport is hovered or focused)
    if (m_Vehicle) {
        Axle::f32 forwardInput = 0.0f;
        Axle::f32 rightInput = 0.0f;
        Axle::f32 brakeInput = 0.0f;
        Axle::f32 handBrakeInput = 0.0f;

        if (m_SceneState == SceneState::Play && (m_ViewportHovered || m_ViewportFocused) && !ImGui::GetIO().WantTextInput) {
            if (Axle::Input::IsKeyPressed(Axle::KeyCode::Up) || Axle::Input::IsKeyPressed(Axle::KeyCode::W)) {
                forwardInput += 1.0f;
            }
            if (Axle::Input::IsKeyPressed(Axle::KeyCode::Down) || Axle::Input::IsKeyPressed(Axle::KeyCode::S)) {
                forwardInput -= 1.0f;
            }
            if (Axle::Input::IsKeyPressed(Axle::KeyCode::Left) || Axle::Input::IsKeyPressed(Axle::KeyCode::A)) {
                rightInput -= 1.0f;
            }
            if (Axle::Input::IsKeyPressed(Axle::KeyCode::Right) || Axle::Input::IsKeyPressed(Axle::KeyCode::D)) {
                rightInput += 1.0f;
            }
            if (Axle::Input::IsKeyPressed(Axle::KeyCode::Space)) {
                handBrakeInput = 1.0f;
            }
        }

        m_Vehicle->SetDriverInput(forwardInput, rightInput, brakeInput, handBrakeInput);
    }

    // Camera Update
    if (m_SceneState == SceneState::Play && m_ChaseCamera && m_Vehicle) {
        // Vehicle Chase Camera
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
        // Editor Free-Fly Camera (Active in Edit mode, Pause mode, or Free-Fly mode)
        if ((m_ViewportHovered || m_ViewportFocused) && !ImGui::GetIO().WantTextInput) {
            const bool isManipulatingGizmo = (m_SceneState == SceneState::Edit) && ImGuizmo::IsUsing();
            if (!isManipulatingGizmo) {
                m_CameraController->OnUpdate(ts);
            }
        }
    }

    // Physics Simulation Stepping
    if (m_SceneState == SceneState::Play) {
        Axle::Timestep simTs = ts.GetSeconds() * m_SimulationSpeed;
        m_PhysicsScene->Step(simTs);
    } else if (m_SceneState == SceneState::Pause && m_StepSingleFrame) {
        m_PhysicsScene->Step(1.0f / 60.0f);
        m_StepSingleFrame = false;
    }

    // Synchronize vehicle chassis and 4 wheel entities with physics simulation
    if (m_Vehicle) {
        if (m_SceneState == SceneState::Edit) {
            auto& chassisTC = m_ChassisEntity.GetComponent<Axle::TransformComponent>();
            m_Vehicle->Reset(chassisTC.Translation, chassisTC.Rotation);
        } else {
            auto& chassisTC = m_ChassisEntity.GetComponent<Axle::TransformComponent>();
            chassisTC.Translation = m_Vehicle->GetPosition();
            chassisTC.Rotation = m_PhysicsScene->GetBodyRotation(m_Vehicle->GetChassisBodyID());
        }

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
    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    const float toolbarHeight = 36.0f;

    ImGui::SetNextWindowPos(mainViewport->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(mainViewport->WorkSize.x, toolbarHeight));
    ImGui::SetNextWindowViewport(mainViewport->ID);

    ImGuiWindowFlags toolbarFlags = ImGuiWindowFlags_NoDocking 
                                  | ImGuiWindowFlags_NoTitleBar 
                                  | ImGuiWindowFlags_NoResize 
                                  | ImGuiWindowFlags_NoMove 
                                  | ImGuiWindowFlags_NoScrollbar 
                                  | ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 4.0f));

    if (ImGui::Begin("##FixedTopToolbar", nullptr, toolbarFlags)) {
        auto DrawToolButton = [](const char* label, bool active, ImVec2 size) -> bool {
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.85f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.55f, 0.95f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.35f, 0.75f, 1.0f));
            }
            bool clicked = ImGui::Button(label, size);
            if (active) {
                ImGui::PopStyleColor(3);
            }
            return clicked;
        };

        // --- GIZMO TRANSFORMATION TOOLS ---
        if (DrawToolButton("Select (Q)", m_GizmoType == -1, ImVec2(76.0f, 26.0f))) {
            m_GizmoType = -1;
        }
        ImGui::SameLine();
        if (DrawToolButton("Move (W)", m_GizmoType == ImGuizmo::OPERATION::TRANSLATE, ImVec2(70.0f, 26.0f))) {
            m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
        }
        ImGui::SameLine();
        if (DrawToolButton("Rotate (E)", m_GizmoType == ImGuizmo::OPERATION::ROTATE, ImVec2(76.0f, 26.0f))) {
            m_GizmoType = ImGuizmo::OPERATION::ROTATE;
        }
        ImGui::SameLine();
        if (DrawToolButton("Scale (R)", m_GizmoType == ImGuizmo::OPERATION::SCALE, ImVec2(70.0f, 26.0f))) {
            m_GizmoType = ImGuizmo::OPERATION::SCALE;
        }
        ImGui::SameLine();
        const char* modeLabel = m_GizmoModeLocal ? "Coord: Local" : "Coord: World";
        if (ImGui::Button(modeLabel, ImVec2(96.0f, 26.0f))) {
            m_GizmoModeLocal = !m_GizmoModeLocal;
        }
        ImGui::SameLine();
        ImGui::Checkbox("Snap", &m_SnapEnabled);
        if (m_SnapEnabled) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(65.0f);
            if (m_GizmoType == ImGuizmo::OPERATION::ROTATE) {
                ImGui::DragFloat("##SnapRot", &m_SnapRotation, 1.0f, 1.0f, 90.0f, "%.0f deg");
            } else if (m_GizmoType == ImGuizmo::OPERATION::SCALE) {
                ImGui::DragFloat("##SnapScale", &m_SnapScale, 0.05f, 0.05f, 5.0f, "%.2f");
            } else {
                ImGui::DragFloat("##SnapTrans", &m_SnapTranslation, 0.05f, 0.05f, 5.0f, "%.2f m");
            }
        }

        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "|");
        ImGui::SameLine();

        // --- 1. PLAY BUTTON ---
        if (m_SceneState == SceneState::Play) {
            ImGui::BeginDisabled(true);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.50f, 0.25f, 0.5f));
            ImGui::Button("Playing (F5)", ImVec2(100.0f, 26.0f));
            ImGui::PopStyleColor();
            ImGui::EndDisabled();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.72f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.85f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.60f, 0.22f, 1.0f));
            if (ImGui::Button("Play (F5)", ImVec2(100.0f, 26.0f))) {
                OnScenePlay();
            }
            ImGui::PopStyleColor(3);
        }

        // --- 2. PAUSE BUTTON ---
        ImGui::SameLine();
        if (m_SceneState == SceneState::Play) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.88f, 0.48f, 0.12f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.98f, 0.58f, 0.18f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.40f, 0.10f, 1.0f));
            if (ImGui::Button("Pause (F6)", ImVec2(100.0f, 26.0f))) {
                OnScenePause();
            }
            ImGui::PopStyleColor(3);
        } else if (m_SceneState == SceneState::Pause) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.72f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.85f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.60f, 0.22f, 1.0f));
            if (ImGui::Button("Resume (F6)", ImVec2(100.0f, 26.0f))) {
                OnScenePlay();
            }
            ImGui::PopStyleColor(3);
        } else {
            ImGui::BeginDisabled(true);
            ImGui::Button("Pause", ImVec2(100.0f, 26.0f));
            ImGui::EndDisabled();
        }

        // --- 3. STOP BUTTON ---
        ImGui::SameLine();
        if (m_SceneState != SceneState::Edit) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.18f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.25f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.14f, 0.16f, 1.0f));
            if (ImGui::Button("Stop (F7)", ImVec2(90.0f, 26.0f))) {
                OnSceneStop();
            }
            ImGui::PopStyleColor(3);
        } else {
            ImGui::BeginDisabled(true);
            ImGui::Button("Stop", ImVec2(90.0f, 26.0f));
            ImGui::EndDisabled();
        }

        // --- 4. STEP 1 FRAME BUTTON ---
        ImGui::SameLine();
        if (m_SceneState == SceneState::Pause) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.52f, 0.88f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.62f, 0.98f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14f, 0.42f, 0.75f, 1.0f));
            if (ImGui::Button("Step (O)", ImVec2(80.0f, 26.0f))) {
                m_StepSingleFrame = true;
            }
            ImGui::PopStyleColor(3);
        } else {
            ImGui::BeginDisabled(true);
            ImGui::Button("Step", ImVec2(80.0f, 26.0f));
            ImGui::EndDisabled();
        }

        // --- 5. RESET SCENE BUTTON ---
        ImGui::SameLine();
        if (ImGui::Button("Reset (Ctrl+R)", ImVec2(98.0f, 26.0f))) {
            ResetSimulation();
        }

        // --- 6. SIMULATION SPEED SLIDER ---
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::SliderFloat("Speed", &m_SimulationSpeed, 0.1f, 2.0f, "%.1fx");

        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "|");

        // --- 7. CAMERA MODE TOGGLE ---
        ImGui::SameLine();
        const char* camBtnText = m_ChaseCamera ? "Cam: Chase [C]" : "Cam: Free-Fly [C]";
        if (ImGui::Button(camBtnText, ImVec2(125.0f, 26.0f))) {
            m_ChaseCamera = !m_ChaseCamera;
        }

        // --- 8. WIREFRAME TOGGLE ---
        ImGui::SameLine();
        ImGui::Checkbox("Wireframe [F1]", &m_ShowPhysicsDebug);

        // --- 9. STATUS / NAVIGATION HINT ---
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "|");

        ImGui::SameLine();
        if (m_SceneState == SceneState::Edit) {
            ImGui::TextColored(ImVec4(0.35f, 0.70f, 1.0f, 1.0f), "EDIT MODE: Q:Select W:Move E:Rotate R:Scale | Click to select | RMB+WASD Fly");
        } else if (m_SceneState == SceneState::Play) {
            ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.45f, 1.0f), "PLAYING: Drive with WASD/Arrows (Space: Handbrake)");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "PAUSED: Physics frozen. Hold RMB+WASD or manipulate entities");
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

void SandboxLayer::OnImGuiRender() {
    ImGuizmo::BeginFrame();

    // 1. Global Main Menu Bar (At the absolute top of the application window)
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Reset Scene", "Ctrl+R")) {
                ResetSimulation();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                // Application exit
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Simulation")) {
            if (ImGui::MenuItem("Play", "F5", m_SceneState == SceneState::Play, m_SceneState != SceneState::Play)) {
                OnScenePlay();
            }
            if (ImGui::MenuItem("Pause", "F6", m_SceneState == SceneState::Pause, m_SceneState == SceneState::Play)) {
                OnScenePause();
            }
            if (ImGui::MenuItem("Stop", "F7", m_SceneState == SceneState::Edit, m_SceneState != SceneState::Edit)) {
                OnSceneStop();
            }
            if (ImGui::MenuItem("Step 1 Frame", "O", false, m_SceneState == SceneState::Pause)) {
                m_StepSingleFrame = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Scene", "Ctrl+R")) {
                ResetSimulation();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Gizmo")) {
            if (ImGui::MenuItem("Select Tool", "Q", m_GizmoType == -1)) {
                m_GizmoType = -1;
            }
            if (ImGui::MenuItem("Translate", "W", m_GizmoType == ImGuizmo::OPERATION::TRANSLATE)) {
                m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
            }
            if (ImGui::MenuItem("Rotate", "E", m_GizmoType == ImGuizmo::OPERATION::ROTATE)) {
                m_GizmoType = ImGuizmo::OPERATION::ROTATE;
            }
            if (ImGui::MenuItem("Scale", "R", m_GizmoType == ImGuizmo::OPERATION::SCALE)) {
                m_GizmoType = ImGuizmo::OPERATION::SCALE;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Local Coordinates", nullptr, m_GizmoModeLocal)) {
                m_GizmoModeLocal = !m_GizmoModeLocal;
            }
            if (ImGui::MenuItem("Grid Snapping", nullptr, m_SnapEnabled)) {
                m_SnapEnabled = !m_SnapEnabled;
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
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Default Layout")) {
                m_ResetLayout = true;
            }
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
        ImGui::EndMainMenuBar();
    }

    // 2. Render Fixed Toolbar directly beneath Main Menu Bar
    RenderToolbar();

    // 3. Fullscreen DockSpace Container (Below Toolbar)
    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    const float toolbarHeight = 36.0f;
    const ImVec2 dockspacePos = ImVec2(mainViewport->WorkPos.x, mainViewport->WorkPos.y + toolbarHeight);
    const ImVec2 dockspaceSize = ImVec2(mainViewport->WorkSize.x, mainViewport->WorkSize.y - toolbarHeight);

    ImGui::SetNextWindowPos(dockspacePos);
    ImGui::SetNextWindowSize(dockspaceSize);
    ImGui::SetNextWindowViewport(mainViewport->ID);

    ImGuiWindowFlags dockspaceWindowFlags = ImGuiWindowFlags_NoDocking
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

    // Build default docked layout if not yet configured or if reset requested
    ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspaceID);
    if (node == nullptr || node->IsEmpty() || !node->IsSplitNode() || m_ResetLayout) {
        m_ResetLayout = false;

        ImGui::DockBuilderRemoveNode(dockspaceID);
        ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceID, dockspaceSize);

        ImGuiID dock_main = dockspaceID;
        ImGuiID dock_left = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.20f, nullptr, &dock_main);
        ImGuiID dock_right = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.26f, nullptr, &dock_main);
        ImGuiID dock_bottom = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.32f, nullptr, &dock_main);

        ImGui::DockBuilderDockWindow("Scene Hierarchy", dock_left);
        ImGui::DockBuilderDockWindow("Viewport", dock_main);
        ImGui::DockBuilderDockWindow("Vehicle Tuner", dock_bottom);
        ImGui::DockBuilderDockWindow("Console", dock_bottom);
        ImGui::DockBuilderDockWindow("Properties", dock_right);
        ImGui::DockBuilderDockWindow("Axle Engine - Telemetry", dock_right);

        ImGui::DockBuilderFinish(dockspaceID);
    }
    ImGui::End(); // End AxleEngineDockSpace

    // 3. Render 3D Viewport Window
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

    ImVec2 viewportMin = ImGui::GetItemRectMin();
    ImVec2 viewportMax = ImGui::GetItemRectMax();
    ImVec2 viewportBounds = ImGui::GetItemRectSize();

    // Configure ImGuizmo rendering bounds
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(viewportMin.x, viewportMin.y, viewportBounds.x, viewportBounds.y);

    const glm::mat4& cameraView = m_CameraController->GetCamera().GetViewMatrix();
    const glm::mat4& cameraProj = m_CameraController->GetCamera().GetProjectionMatrix();

    // 1. 3D Transform Gizmo Manipulation for Selected Entity (Active in Edit & Pause modes)
    if (m_SceneState != SceneState::Play) {
        auto selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
        if (selectedEntity && selectedEntity.HasComponent<Axle::TransformComponent>() && m_GizmoType != -1) {
            auto& tc = selectedEntity.GetComponent<Axle::TransformComponent>();
            glm::mat4 transform = tc.GetTransform();

            ImGuizmo::MODE mode = m_GizmoModeLocal ? ImGuizmo::MODE::LOCAL : ImGuizmo::MODE::WORLD;
            if (m_GizmoType == ImGuizmo::OPERATION::SCALE) {
                mode = ImGuizmo::MODE::LOCAL;
            }

            float snapValues[3] = { 0.0f, 0.0f, 0.0f };
            if (m_SnapEnabled) {
                if (m_GizmoType == ImGuizmo::OPERATION::TRANSLATE) {
                    snapValues[0] = snapValues[1] = snapValues[2] = m_SnapTranslation;
                } else if (m_GizmoType == ImGuizmo::OPERATION::ROTATE) {
                    snapValues[0] = snapValues[1] = snapValues[2] = m_SnapRotation;
                } else if (m_GizmoType == ImGuizmo::OPERATION::SCALE) {
                    snapValues[0] = snapValues[1] = snapValues[2] = m_SnapScale;
                }
            }

            if (ImGuizmo::Manipulate(
                    glm::value_ptr(cameraView),
                    glm::value_ptr(cameraProj),
                    static_cast<ImGuizmo::OPERATION>(m_GizmoType),
                    mode,
                    glm::value_ptr(transform),
                    nullptr,
                    m_SnapEnabled ? snapValues : nullptr
                )) {
                float translation[3], rotation[3], scale[3];
                ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(transform), translation, rotation, scale);

                tc.Translation = glm::vec3(translation[0], translation[1], translation[2]);
                tc.Rotation = glm::quat(glm::radians(glm::vec3(rotation[0], rotation[1], rotation[2])));
                tc.Scale = glm::vec3(scale[0], scale[1], scale[2]);

                // Real-time synchronization with Jolt Physics bodies
                if (selectedEntity == m_ChassisEntity && m_Vehicle) {
                    m_Vehicle->Reset(tc.Translation, tc.Rotation);
                    for (Axle::u32 i = 0; i < 4; ++i) {
                        const glm::mat4 wheelMat = m_Vehicle->GetWheelTransform(i);
                        auto& wtc = m_WheelEntities[i].GetComponent<Axle::TransformComponent>();
                        wtc.Translation = glm::vec3(wheelMat[3]);
                        wtc.Rotation = glm::quat_cast(wheelMat);
                    }
                } else if (selectedEntity.HasComponent<Axle::RigidBodyComponent>() && m_PhysicsScene) {
                    auto& rb = selectedEntity.GetComponent<Axle::RigidBodyComponent>();
                    if (!rb.RuntimeBodyID.IsInvalid()) {
                        m_PhysicsScene->SetBodyPositionAndRotation(rb.RuntimeBodyID, tc.Translation, tc.Rotation, true);
                        m_PhysicsScene->SetLinearVelocity(rb.RuntimeBodyID, glm::vec3(0.0f));
                        m_PhysicsScene->GetBodyInterface().SetAngularVelocity(rb.RuntimeBodyID, JPH::Vec3::sZero());
                    }
                }
            }
        }
    }

    // 3. Viewport Mouse Picking (Click on 3D meshes to select entity)
    if (m_ViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (!ImGuizmo::IsOver() && !ImGuizmo::IsUsing() && !Axle::Input::IsMouseButtonPressed(Axle::MouseCode::ButtonRight)) {
            ImVec2 mousePos = ImGui::GetMousePos();
            float mouseX = mousePos.x - viewportMin.x;
            float mouseY = mousePos.y - viewportMin.y;

            if (mouseX >= 0.0f && mouseX < viewportBounds.x && mouseY >= 0.0f && mouseY < viewportBounds.y) {
                float ndcX = (2.0f * (mouseX / viewportBounds.x)) - 1.0f;
                float ndcY = 1.0f - (2.0f * (mouseY / viewportBounds.y));

                glm::mat4 invVP = glm::inverse(m_CameraController->GetCamera().GetViewProjectionMatrix());
                glm::vec4 nearPointNDC(ndcX, ndcY, -1.0f, 1.0f);
                glm::vec4 farPointNDC(ndcX, ndcY, 1.0f, 1.0f);

                glm::vec4 nearWorld = invVP * nearPointNDC;
                if (std::abs(nearWorld.w) > 1e-6f) nearWorld /= nearWorld.w;

                glm::vec4 farWorld = invVP * farPointNDC;
                if (std::abs(farWorld.w) > 1e-6f) farWorld /= farWorld.w;

                glm::vec3 rayOrigin = glm::vec3(nearWorld);
                glm::vec3 rayDir = glm::normalize(glm::vec3(farWorld - nearWorld));

                Axle::RayCastHit hit;
                if (m_PhysicsScene && m_PhysicsScene->CastRay(rayOrigin, rayDir, 1000.0f, hit)) {
                    Axle::Entity hitEntity = m_Scene->FindEntityByBodyID(hit.HitBodyID);
                    if (hitEntity) {
                        m_SceneHierarchyPanel.SetSelectedEntity(hitEntity);
                    } else {
                        m_SceneHierarchyPanel.SetSelectedEntity({});
                    }
                } else {
                    // Clicked empty space in viewport -> deselect
                    m_SceneHierarchyPanel.SetSelectedEntity({});
                }
            }
        }
    }

    // Viewport Overlay Badges
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 winPos = ImGui::GetWindowPos();
    ImVec2 badgePos = ImVec2(winPos.x + 12.0f, winPos.y + 12.0f);

    const char* statusText = (m_SceneState == SceneState::Edit) ? "EDIT MODE (STOPPED)"
                           : (m_SceneState == SceneState::Play) ? "SIMULATION ACTIVE"
                           : "SIMULATION PAUSED";
    ImU32 statusBg = (m_SceneState == SceneState::Edit) ? IM_COL32(35, 75, 150, 220)
                   : (m_SceneState == SceneState::Play) ? IM_COL32(25, 140, 55, 220)
                   : IM_COL32(190, 95, 15, 220);

    drawList->AddRectFilled(badgePos, ImVec2(badgePos.x + 175.0f, badgePos.y + 24.0f), statusBg, 4.0f);
    drawList->AddText(ImVec2(badgePos.x + 8.0f, badgePos.y + 4.0f), IM_COL32(255, 255, 255, 255), statusText);

    if (m_SceneState == SceneState::Play && m_Vehicle) {
        const auto vTele = m_Vehicle->GetTelemetry();
        char speedStr[48];
        snprintf(speedStr, sizeof(speedStr), "%.1f KM/H | GEAR %d", vTele.SpeedKmh, vTele.CurrentGear);
        ImVec2 speedPos = ImVec2(badgePos.x + 185.0f, badgePos.y);
        drawList->AddRectFilled(speedPos, ImVec2(speedPos.x + 155.0f, speedPos.y + 24.0f), IM_COL32(20, 30, 45, 220), 4.0f);
        drawList->AddText(ImVec2(speedPos.x + 8.0f, speedPos.y + 4.0f), IM_COL32(0, 230, 180, 255), speedStr);
    } else if (m_SceneState == SceneState::Edit) {
        ImVec2 hintPos = ImVec2(badgePos.x + 185.0f, badgePos.y);
        drawList->AddRectFilled(hintPos, ImVec2(hintPos.x + 440.0f, hintPos.y + 24.0f), IM_COL32(20, 25, 35, 200), 4.0f);
        drawList->AddText(ImVec2(hintPos.x + 8.0f, hintPos.y + 4.0f), IM_COL32(200, 210, 225, 255), "Gizmo: W/E/R | Q: Select | Click Mesh | RMB+WASD Fly");
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
