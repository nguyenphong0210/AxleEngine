#include "SandboxLayer.hpp"
#include "CarPlayerScript.hpp"
#include <Axle/Application.hpp>
#include <Axle/Input.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/Components.hpp>
#include <Axle/ScriptableEntity.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/PlatformUtils.hpp>
#include <Axle/Project.hpp>
#include <Axle/Log.hpp>
#include <filesystem>

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

    // 3. Shaders loaded from Project Asset Directory (Sandbox/assets/shaders/)
    auto vertPath = Axle::FileSystem::ResolveAssetPath("shaders/Checkerboard.vert");
    auto fragPath = Axle::FileSystem::ResolveAssetPath("shaders/Checkerboard.frag");
    std::string vertexSrc = Axle::FileSystem::ReadTextFile(vertPath);
    std::string fragmentSrc = Axle::FileSystem::ReadTextFile(fragPath);

    if (vertexSrc.empty() || fragmentSrc.empty()) {
        AXLE_WARN("FileSystem: Shader files not found at '{0}'. Using fallback shaders.", vertPath.string());
        vertexSrc = R"(
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
        fragmentSrc = R"(
            #version 450 core
            layout(location = 0) out vec4 o_Color;
            in vec3 v_WorldPos;
            in vec3 v_LocalPos;
            uniform int u_PatternMode;
            uniform float u_TileScale;
            uniform vec4 u_Color1;
            uniform vec4 u_Color2;
            uniform vec3 u_LightDir;
            uniform mat4 u_Transform;
            void main() {
                float pattern = 0.0;
                vec3 normal = vec3(0.0, 1.0, 0.0);
                if (u_PatternMode == 0) {
                    vec2 coord = floor(v_WorldPos.xz * u_TileScale);
                    pattern = mod(coord.x + coord.y, 2.0);
                } else if (u_PatternMode == 1) {
                    vec3 coord = floor((v_LocalPos + vec3(0.5)) * u_TileScale + vec3(0.0001));
                    pattern = mod(coord.x + coord.y + coord.z, 2.0);
                    vec3 dx = dFdx(v_WorldPos);
                    vec3 dy = dFdy(v_WorldPos);
                    normal = normalize(cross(dx, dy));
                    if (dot(normal, normalize(-v_WorldPos)) < 0.0) normal = -normal;
                } else {
                    vec3 nLocal = normalize(v_LocalPos);
                    float u = atan(nLocal.z, nLocal.x) / (2.0 * 3.14159265359) + 0.5;
                    float v = asin(clamp(nLocal.y, -1.0, 1.0)) / 3.14159265359 + 0.5;
                    vec2 coord = floor(vec2(u * u_TileScale, v * (u_TileScale * 0.5)));
                    pattern = mod(coord.x + coord.y, 2.0);
                    normal = normalize((u_Transform * vec4(v_LocalPos, 0.0)).xyz);
                }
                vec4 baseColor = mix(u_Color1, u_Color2, pattern);
                vec3 lightDir = normalize(u_LightDir);
                float NdotL = max(dot(normal, lightDir), 0.0);
                float lighting = 0.38 + 0.62 * NdotL;
                vec3 viewDir = normalize(-v_WorldPos);
                vec3 halfDir = normalize(lightDir + viewDir);
                float spec = pow(max(dot(normal, halfDir), 0.0), 32.0) * 0.18;
                o_Color = vec4(baseColor.rgb * lighting + vec3(spec), baseColor.a);
            }
        )";
    } else {
        AXLE_INFO("Loaded shaders successfully from '{0}' and '{1}'", vertPath.string(), fragPath.string());
    }

    m_Shader = std::make_unique<Axle::Shader>(vertexSrc, fragmentSrc);

    // 4. Primitive Mesh Data (Cube, Sphere, Cylinder, Cone, Plane)
    m_CubeMesh = Axle::PrimitiveMesh::CreateCube();
    m_SphereMesh = Axle::PrimitiveMesh::CreateSphere(24, 32);
    m_CylinderMesh = Axle::PrimitiveMesh::CreateCylinder(32);
    m_ConeMesh = Axle::PrimitiveMesh::CreateCone(32);
    m_PlaneMesh = Axle::PrimitiveMesh::CreatePlane();

    // 4.1 Asset Pipeline Validation: Load SportsCar_Body.axle
    auto carAxlePath = Axle::FileSystem::ResolveAssetPath("models/SportsCar_Body.axle");
    if (std::filesystem::exists(carAxlePath)) {
        auto loaded = Axle::AssetSerializer::LoadMesh(carAxlePath);
        if (loaded.Success) {
            AXLE_INFO("Asset Pipeline: Loaded '{0}' [Cache: {1}] ({2} vertices, {3} indices)", 
                carAxlePath.filename().string(), 
                loaded.LoadedFromCache ? "HIT" : "MISS (Auto-baked cache)",
                loaded.SourceData.Vertices.size(), loaded.SourceData.Indices.size());
        }
    }

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
    m_InitialDynamicStates[m_BoxBodyID.GetIndexAndSequenceNumber()] = { glm::vec3(-5.0f, 4.0f, 8.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f) };

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
    m_InitialDynamicStates[m_SphereBodyID.GetIndexAndSequenceNumber()] = { glm::vec3(5.0f, 4.0f, 8.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f) };

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
    m_InitialDynamicStates[m_CylinderBodyID.GetIndexAndSequenceNumber()] = { glm::vec3(-7.5f, 4.0f, 14.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f) };

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
    m_InitialDynamicStates[m_ConeBodyID.GetIndexAndSequenceNumber()] = { glm::vec3(7.5f, 4.0f, 14.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f) };

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
    m_ChassisEntity.AddComponent<Axle::VehicleComponent>(m_Vehicle.get());
    m_ChassisEntity.AddComponent<Axle::NativeScriptComponent>().Bind<CarPlayerScript>("CarPlayerScript");
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
        m_WheelEntities[i].AddComponent<Axle::HierarchyComponent>(static_cast<entt::entity>(m_ChassisEntity), false);
        chassisHierarchy.Children.push_back(static_cast<entt::entity>(m_WheelEntities[i]));

        const glm::mat4 wheelMat = m_Vehicle->GetWheelTransform(i);
        auto& tc = m_WheelEntities[i].GetComponent<Axle::TransformComponent>();
        tc.Translation = glm::vec3(wheelMat[3]);
        tc.Rotation = glm::quat_cast(wheelMat);
    }

    // 9. Automotive Test Playground & Obstacle Course (Phase 7)
    CreatePlayground();

    // Connect Editor Panels
    m_SceneHierarchyPanel.SetContext(m_Scene.get(), m_PhysicsScene.get());
    m_SceneHierarchyPanel.SetMeshes(m_CubeMesh, m_SphereMesh, m_CylinderMesh, m_ConeMesh, m_PlaneMesh);
    m_VehicleTunerPanel.SetContext(m_Vehicle.get(), m_PhysicsScene.get());
    m_ContentBrowserPanel.SetRootPath(Axle::FileSystem::GetAssetDirectory());
    m_AssetPreviewPanel.SetSceneContext(m_Scene.get(), m_PhysicsScene.get(), m_CubeMesh);
    m_ContentBrowserPanel.SetAssetPreviewPanel(&m_AssetPreviewPanel);
    m_ContentBrowserPanel.SetCompileAssetsCallback([this]() {
        CompileAndHotReloadAssets(false);
    });
    m_ContentBrowserPanel.SetOpenSceneCallback([this](const std::filesystem::path& path) {
        OpenScene(path);
    });
    m_ContentBrowserPanel.SetOpenProjectCallback([this](const std::filesystem::path& path) {
        OpenProject(path);
    });

    // Initialize Scene Serialization (Phase 10: Step 1 & 2)
    m_CurrentScenePath = Axle::FileSystem::ResolveAssetPath("scenes/Playground.axlescene");
    if (!std::filesystem::exists(m_CurrentScenePath)) {
        SaveScene(m_CurrentScenePath);
    }

    // Initialize Project Configuration System (Phase 10: Step 4)
    std::filesystem::path projFile = "AxleSandbox.axleproj";
    if (!std::filesystem::exists(projFile)) {
        auto checkAssetParent = Axle::FileSystem::GetAssetDirectory().parent_path() / "AxleSandbox.axleproj";
        if (std::filesystem::exists(checkAssetParent)) {
            projFile = checkAssetParent;
        }
    }

    if (std::filesystem::exists(projFile)) {
        OpenProject(projFile);
    } else {
        auto project = Axle::Project::New();
        project->GetConfig().Name = "Axle Sandbox";
        project->GetConfig().AssetDirectory = "assets";
        project->GetConfig().StartScene = "scenes/Playground.axlescene";
        Axle::Project::SetActive(project);
        Axle::Project::SaveActive(projFile);
        m_CurrentProjectPath = std::filesystem::absolute(projFile);
    }

    UpdateWindowTitle();

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

void SandboxLayer::OnFileDrop(const std::vector<std::string>& filepaths) {
    if (filepaths.empty()) return;

    // 1. Determine destination directory based on UI hover state
    std::filesystem::path destFolder;
    if (m_ContentBrowserPanel.IsWindowHovered()) {
        destFolder = m_ContentBrowserPanel.GetCurrentPath();
    } else {
        destFolder = Axle::FileSystem::ResolveAssetPath("models");
    }

    std::error_code ec;
    if (!std::filesystem::exists(destFolder, ec)) {
        std::filesystem::create_directories(destFolder, ec);
    }

    for (const auto& filepathStr : filepaths) {
        std::filesystem::path srcPath(filepathStr);
        if (!std::filesystem::exists(srcPath, ec)) continue;

        std::string ext = srcPath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });
        std::string stem = srcPath.stem().string();

        if (ext == ".axlescene" || ext == ".scene") {
            OpenScene(srcPath);
        } else if (ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx") {
            // Queue 3D asset for conversion confirmation modal
            m_PendingImportQueue.push_back(srcPath);
        } else if (ext == ".axle") {
            std::filesystem::path destAxlePath = destFolder / srcPath.filename();
            if (srcPath != destAxlePath) {
                std::filesystem::copy_file(srcPath, destAxlePath, std::filesystem::copy_options::overwrite_existing, ec);
            }
            auto loaded = Axle::AssetSerializer::LoadMesh(destAxlePath);
            AXLE_INFO("Asset Pipeline: Ingested .axle asset '{0}' (Cache: {1})", 
                destAxlePath.filename().string(), loaded.LoadedFromCache ? "HIT" : "MISS");

            if (m_ViewportHovered && loaded.Success) {
                glm::mat4 invVP = glm::inverse(m_CameraController->GetCamera().GetViewProjectionMatrix());
                glm::vec4 nearPointNDC(0.0f, 0.0f, -1.0f, 1.0f);
                glm::vec4 farPointNDC(0.0f, 0.0f, 1.0f, 1.0f);

                glm::vec4 nearW = invVP * nearPointNDC;
                glm::vec4 farW = invVP * farPointNDC;
                if (std::abs(nearW.w) > 1e-6f) nearW /= nearW.w;
                if (std::abs(farW.w) > 1e-6f) farW /= farW.w;

                glm::vec3 rayOrigin = glm::vec3(nearW);
                glm::vec3 rayDir = glm::normalize(glm::vec3(farW - nearW));

                glm::vec3 spawnPos(0.0f, loaded.SourceData.HalfExtent.y, 0.0f);
                Axle::RayCastHit hit;
                if (m_PhysicsScene && m_PhysicsScene->CastRay(rayOrigin, rayDir, 500.0f, hit)) {
                    spawnPos = hit.Position + glm::vec3(0.0f, loaded.SourceData.HalfExtent.y, 0.0f);
                } else {
                    if (std::abs(rayDir.y) > 1e-4f) {
                        float t = -rayOrigin.y / rayDir.y;
                        if (t > 0.0f && t < 150.0f) {
                            glm::vec3 groundHit = rayOrigin + rayDir * t;
                            spawnPos = groundHit + glm::vec3(0.0f, loaded.SourceData.HalfExtent.y, 0.0f);
                        } else {
                            spawnPos = rayOrigin + rayDir * 10.0f;
                        }
                    } else {
                        spawnPos = rayOrigin + rayDir * 10.0f;
                    }
                }

                auto entity = m_Scene->CreateEntity(stem);
                entity.AddComponent<Axle::TransformComponent>(spawnPos, glm::vec3(1.0f));
                entity.AddComponent<Axle::MeshComponent>(
                    loaded.VAO, destAxlePath.string(),
                    loaded.SourceData.PatternMode, loaded.SourceData.TileScale,
                    loaded.SourceData.ColorPrimary, loaded.SourceData.ColorSecondary
                );

                if (m_PhysicsScene) {
                    Axle::BoxBodyProps bProps;
                    bProps.Position = spawnPos;
                    bProps.HalfExtent = loaded.SourceData.HalfExtent;
                    bProps.IsDynamic = true;
                    bProps.Friction = 0.70f;
                    bProps.Restitution = 0.15f;
                    auto bodyId = m_PhysicsScene->CreateBox(bProps);
                    entity.AddComponent<Axle::RigidBodyComponent>(bodyId, true);
                }

                m_SceneHierarchyPanel.SetSelectedEntity(entity);
            }
        } else {
            // General asset file copy (textures, shaders, scenes, documents)
            std::filesystem::path destPath = destFolder / srcPath.filename();
            if (srcPath != destPath) {
                std::filesystem::copy_file(srcPath, destPath, std::filesystem::copy_options::overwrite_existing, ec);
                AXLE_INFO("Asset Pipeline: Ingested file '{0}' into '{1}'", 
                    srcPath.filename().string(), destFolder.string());
            }
        }
    }

    // Trigger conversion confirmation modal if 3D assets were queued
    if (!m_PendingImportQueue.empty() && !m_ShowImportModal) {
        SetupNextImportModal();
    }
}

void SandboxLayer::OnScenePlay() {
    CompileAndHotReloadAssets(false);
    m_SceneState = SceneState::Play;
    m_Scene->OnRuntimeStart();
    m_ChaseCamera = true;
    AXLE_INFO("Simulation started (Play Mode). Chase Camera locked to Vehicle. Drive with WASD/Arrows (Space: Handbrake).");
}

void SandboxLayer::CompileAndHotReloadAssets(bool forceRebakeAll) {
    auto assetDir = Axle::FileSystem::GetAssetDirectory();
    auto result = Axle::AssetSerializer::CompileAssetDirectory(assetDir, forceRebakeAll);

    // Hot-reload all active entities in the Scene with MeshComponent
    size_t entitiesUpdated = 0;
    auto view = m_Scene->GetRegistry().view<Axle::MeshComponent>();
    for (auto entity : view) {
        auto& mc = view.get<Axle::MeshComponent>(entity);
        if (!mc.AssetPath.empty() && std::filesystem::exists(mc.AssetPath)) {
            auto loaded = Axle::AssetSerializer::LoadMesh(mc.AssetPath);
            if (loaded.Success && loaded.VAO) {
                mc.VAO = loaded.VAO;
                mc.PatternMode = loaded.SourceData.PatternMode;
                mc.TileScale = loaded.SourceData.TileScale;
                mc.Color1 = loaded.SourceData.ColorPrimary;
                mc.Color2 = loaded.SourceData.ColorSecondary;
                entitiesUpdated++;
            }
        }
    }

    // Refresh Content Browser Thumbnails and Asset Preview Panel
    m_ContentBrowserPanel.RefreshThumbnails();
    if (!m_AssetPreviewPanel.GetCurrentAsset().empty()) {
        m_AssetPreviewPanel.SetAsset(m_AssetPreviewPanel.GetCurrentAsset());
    }

    m_CompileNotificationTimer = 3.5f;
    m_CompileNotificationMessage = "Compiled " + std::to_string(result.RecompiledCount) + " modified (" +
                                   std::to_string(result.UpToDateCount) + " up-to-date, " +
                                   std::to_string(entitiesUpdated) + " scene meshes reloaded)";
    m_ContentBrowserPanel.SetStatusNotification(m_CompileNotificationMessage);

    AXLE_INFO("Asset Compiler: Finished! Recompiled {0} assets ({1} up-to-date). Hot-reloaded {2} active scene entities.",
        result.RecompiledCount, result.UpToDateCount, entitiesUpdated);
}

void SandboxLayer::NewScene() {
    if (m_SceneState != SceneState::Edit) {
        OnSceneStop();
    }

    // Release Vehicle before PhysicsScene is destroyed
    m_Vehicle.reset();

    // Clear cached entity and body handles
    m_ChassisEntity = {};
    for (int i = 0; i < 4; ++i) {
        m_WheelEntities[i] = {};
    }
    m_FloorEntity = {};
    m_BoxEntity = {};
    m_SphereEntity = {};
    m_CylinderEntity = {};
    m_ConeEntity = {};
    m_BoxBodyID = Axle::BodyID();
    m_SphereBodyID = Axle::BodyID();
    m_CylinderBodyID = Axle::BodyID();
    m_ConeBodyID = Axle::BodyID();
    m_FloorBodyID = Axle::BodyID();
    m_InitialDynamicStates.clear();

    m_PhysicsScene = std::make_unique<Axle::PhysicsScene>();
    m_PhysicsDebugRenderer = std::make_unique<Axle::PhysicsDebugRenderer>();
    m_Scene->Clear();
    m_CurrentScenePath.clear();

    // Create default floor plane
    m_FloorEntity = m_Scene->CreateEntity("Ground Floor");
    m_FloorEntity.AddComponent<Axle::TransformComponent>(glm::vec3(0.0f), glm::vec3(150.0f, 1.0f, 150.0f));
    m_FloorEntity.AddComponent<Axle::MeshComponent>(m_PlaneMesh, "builtin://plane", 0, 2.0f, glm::vec4(0.35f, 0.38f, 0.42f, 1.0f), glm::vec4(0.18f, 0.20f, 0.24f, 1.0f));
    m_FloorBodyID = m_PhysicsScene->CreatePlane(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 75.0f, 0.6f, 0.0f);
    m_FloorEntity.AddComponent<Axle::RigidBodyComponent>(m_FloorBodyID, false, Axle::BodyShapeType::Plane, glm::vec3(75.0f), 0.6f, 0.0f);

    m_SceneHierarchyPanel.SetContext(m_Scene.get(), m_PhysicsScene.get());
    m_VehicleTunerPanel.SetContext(nullptr, m_PhysicsScene.get());
    m_AssetPreviewPanel.SetSceneContext(m_Scene.get(), m_PhysicsScene.get(), m_CubeMesh);
    m_CompileNotificationMessage = "Created new empty scene";
    m_CompileNotificationTimer = 3.5f;
    UpdateWindowTitle();
    AXLE_INFO("Sandbox: Created new empty scene");
}

void SandboxLayer::OpenScene(const std::filesystem::path& filepath) {
    std::filesystem::path targetPath = filepath;
    if (targetPath.empty()) {
        auto scenesDir = Axle::FileSystem::ResolveAssetPath("scenes");
        if (!std::filesystem::exists(scenesDir)) {
            std::filesystem::create_directories(scenesDir);
        }
        std::string defaultPathStr = scenesDir.string();

        std::string selected = Axle::FileDialogs::OpenFile("Axle Scene (*.axlescene)\0*.axlescene\0All Files (*.*)\0*.*\0", defaultPathStr.c_str());
        if (selected.empty()) {
            return;
        }
        targetPath = selected;
    }

    if (!std::filesystem::exists(targetPath)) {
        AXLE_WARN("Sandbox: Scene file does not exist: '{0}'", targetPath.string());
        return;
    }

    if (m_SceneState != SceneState::Edit) {
        OnSceneStop();
    }

    // 1. MUST destroy m_Vehicle BEFORE destroying m_PhysicsScene!
    m_Vehicle.reset();

    // 2. Clear cached entity and body handles
    m_ChassisEntity = {};
    for (int i = 0; i < 4; ++i) {
        m_WheelEntities[i] = {};
    }
    m_FloorEntity = {};
    m_BoxEntity = {};
    m_SphereEntity = {};
    m_CylinderEntity = {};
    m_ConeEntity = {};
    m_BoxBodyID = Axle::BodyID();
    m_SphereBodyID = Axle::BodyID();
    m_CylinderBodyID = Axle::BodyID();
    m_ConeBodyID = Axle::BodyID();
    m_FloorBodyID = Axle::BodyID();
    m_InitialDynamicStates.clear();

    // 3. Create fresh PhysicsScene and PhysicsDebugRenderer
    m_PhysicsScene = std::make_unique<Axle::PhysicsScene>();
    m_PhysicsDebugRenderer = std::make_unique<Axle::PhysicsDebugRenderer>();

    Axle::SceneSerializer serializer(m_Scene.get(), m_PhysicsScene.get());
    Axle::SceneSerializerContext ctx;
    ctx.CubeMesh = m_CubeMesh;
    ctx.SphereMesh = m_SphereMesh;
    ctx.CylinderMesh = m_CylinderMesh;
    ctx.ConeMesh = m_ConeMesh;
    ctx.PlaneMesh = m_PlaneMesh;
    ctx.ScriptBinder = [](Axle::Entity entity, const std::string& scriptName) {
        if (scriptName == "CarPlayerScript") {
            entity.AddComponent<Axle::NativeScriptComponent>().Bind<CarPlayerScript>("CarPlayerScript");
        }
    };
    serializer.SetContext(ctx);

    if (serializer.Deserialize(targetPath)) {
        m_CurrentScenePath = targetPath;

        // Rediscover vehicle entities and dynamic shapes from the deserialized scene
        auto view = m_Scene->GetRegistry().view<Axle::TagComponent>();
        for (auto entityID : view) {
            const auto& tag = view.get<Axle::TagComponent>(entityID).Tag;
            Axle::Entity e{ entityID, m_Scene.get() };
            if (tag == "Vehicle Chassis") {
                m_ChassisEntity = e;
            } else if (tag == "Wheel FL") {
                m_WheelEntities[0] = e;
            } else if (tag == "Wheel FR") {
                m_WheelEntities[1] = e;
            } else if (tag == "Wheel RL") {
                m_WheelEntities[2] = e;
            } else if (tag == "Wheel RR") {
                m_WheelEntities[3] = e;
            } else if (tag == "Dynamic Box") {
                m_BoxEntity = e;
                if (e.HasComponent<Axle::RigidBodyComponent>()) {
                    m_BoxBodyID = e.GetComponent<Axle::RigidBodyComponent>().RuntimeBodyID;
                }
            } else if (tag == "Bouncy Sphere") {
                m_SphereEntity = e;
                if (e.HasComponent<Axle::RigidBodyComponent>()) {
                    m_SphereBodyID = e.GetComponent<Axle::RigidBodyComponent>().RuntimeBodyID;
                }
            } else if (tag == "Dynamic Cylinder") {
                m_CylinderEntity = e;
                if (e.HasComponent<Axle::RigidBodyComponent>()) {
                    m_CylinderBodyID = e.GetComponent<Axle::RigidBodyComponent>().RuntimeBodyID;
                }
            } else if (tag == "Dynamic Cone") {
                m_ConeEntity = e;
                if (e.HasComponent<Axle::RigidBodyComponent>()) {
                    m_ConeBodyID = e.GetComponent<Axle::RigidBodyComponent>().RuntimeBodyID;
                }
            } else if (tag == "Static Floor Plane" || tag == "Ground Floor") {
                m_FloorEntity = e;
                if (e.HasComponent<Axle::RigidBodyComponent>()) {
                    m_FloorBodyID = e.GetComponent<Axle::RigidBodyComponent>().RuntimeBodyID;
                }
            }
        }

        // Cache initial dynamic states for ResetSimulation()
        auto rbView = m_Scene->GetRegistry().view<Axle::RigidBodyComponent, Axle::TransformComponent>();
        for (auto entityID : rbView) {
            const auto& rb = rbView.get<Axle::RigidBodyComponent>(entityID);
            const auto& tc = rbView.get<Axle::TransformComponent>(entityID);
            if (rb.IsDynamic && !rb.RuntimeBodyID.IsInvalid()) {
                m_InitialDynamicStates[rb.RuntimeBodyID.GetIndexAndSequenceNumber()] = { tc.Translation, tc.Rotation };
            }
        }

        // If the loaded scene contains a Vehicle Chassis, re-instantiate Vehicle physics and bind components
        if (m_ChassisEntity && m_ChassisEntity.HasComponent<Axle::TransformComponent>()) {
            m_Vehicle = std::make_unique<Axle::Vehicle>(*m_PhysicsScene);

            auto& chassisTC = m_ChassisEntity.GetComponent<Axle::TransformComponent>();
            m_Vehicle->Reset(chassisTC.Translation, chassisTC.Rotation);

            if (m_ChassisEntity.HasComponent<Axle::VehicleComponent>()) {
                m_ChassisEntity.GetComponent<Axle::VehicleComponent>().VehicleInstance = m_Vehicle.get();
            } else {
                m_ChassisEntity.AddComponent<Axle::VehicleComponent>(m_Vehicle.get());
            }

            if (m_ChassisEntity.HasComponent<Axle::RigidBodyComponent>()) {
                m_ChassisEntity.GetComponent<Axle::RigidBodyComponent>().RuntimeBodyID = m_Vehicle->GetChassisBodyID();
            } else {
                m_ChassisEntity.AddComponent<Axle::RigidBodyComponent>(m_Vehicle->GetChassisBodyID(), true);
            }
        }

        m_SceneHierarchyPanel.SetContext(m_Scene.get(), m_PhysicsScene.get());
        m_VehicleTunerPanel.SetContext(m_Vehicle.get(), m_PhysicsScene.get());
        m_AssetPreviewPanel.SetSceneContext(m_Scene.get(), m_PhysicsScene.get(), m_CubeMesh);

        m_CompileNotificationMessage = "Loaded scene: " + targetPath.filename().string();
        m_CompileNotificationTimer = 3.5f;
        UpdateWindowTitle();
        AXLE_INFO("Sandbox: Successfully loaded scene from '{0}'", targetPath.string());
    }
}

void SandboxLayer::SaveScene(const std::filesystem::path& filepath) {
    if (filepath.empty() && m_CurrentScenePath.empty()) {
        SaveSceneAs();
        return;
    }

    auto targetPath = filepath.empty() ? m_CurrentScenePath : filepath;

    Axle::SceneSerializer serializer(m_Scene.get(), m_PhysicsScene.get());
    Axle::SceneSerializerContext ctx;
    ctx.CubeMesh = m_CubeMesh;
    ctx.SphereMesh = m_SphereMesh;
    ctx.CylinderMesh = m_CylinderMesh;
    ctx.ConeMesh = m_ConeMesh;
    ctx.PlaneMesh = m_PlaneMesh;
    ctx.ScriptBinder = [](Axle::Entity entity, const std::string& scriptName) {
        if (scriptName == "CarPlayerScript") {
            entity.AddComponent<Axle::NativeScriptComponent>().Bind<CarPlayerScript>("CarPlayerScript");
        }
    };
    serializer.SetContext(ctx);

    if (serializer.Serialize(targetPath)) {
        m_CurrentScenePath = targetPath;
        m_CompileNotificationMessage = "Saved scene: " + targetPath.filename().string();
        m_CompileNotificationTimer = 3.5f;
        UpdateWindowTitle();
        AXLE_INFO("Sandbox: Successfully saved scene to '{0}'", targetPath.string());
    }
}

void SandboxLayer::SaveSceneAs() {
    auto scenesDir = Axle::FileSystem::ResolveAssetPath("scenes");
    if (!std::filesystem::exists(scenesDir)) {
        std::filesystem::create_directories(scenesDir);
    }
    std::string defaultPathStr = scenesDir.string();

    std::string filepath = Axle::FileDialogs::SaveFile("Axle Scene (*.axlescene)\0*.axlescene\0All Files (*.*)\0*.*\0", defaultPathStr.c_str(), "axlescene");
    if (!filepath.empty()) {
        std::filesystem::path target(filepath);
        if (target.extension() != ".axlescene") {
            target += ".axlescene";
        }
        SaveScene(target);
    }
}

void SandboxLayer::NewProject() {
    auto defaultDir = std::filesystem::current_path();
    std::string filepath = Axle::FileDialogs::SaveFile("Axle Project (*.axleproj)\0*.axleproj\0All Files (*.*)\0*.*\0", defaultDir.string().c_str(), "axleproj");
    if (filepath.empty()) return;

    std::filesystem::path target(filepath);
    if (target.extension() != ".axleproj") {
        target += ".axleproj";
    }

    auto projectDir = target.parent_path();
    auto assetDir = projectDir / "assets";
    std::filesystem::create_directories(assetDir / "scenes");
    std::filesystem::create_directories(assetDir / "models");
    std::filesystem::create_directories(assetDir / "textures");
    std::filesystem::create_directories(assetDir / "shaders");

    auto project = Axle::Project::New();
    project->GetConfig().Name = target.stem().string();
    project->GetConfig().AssetDirectory = "assets";
    project->GetConfig().StartScene = "scenes/Untitled.axlescene";

    Axle::Project::SetActive(project);
    Axle::Project::SaveActive(target);
    m_CurrentProjectPath = target;

    // Create new scene in project's scenes directory
    NewScene();
    SaveScene(assetDir / "scenes/Untitled.axlescene");

    m_ContentBrowserPanel.SetRootPath(Axle::Project::GetAssetDirectory());
    m_CompileNotificationMessage = "Created project: " + project->GetConfig().Name;
    m_CompileNotificationTimer = 4.0f;
    UpdateWindowTitle();
    AXLE_INFO("Sandbox: Created and activated new project '{0}' at '{1}'", project->GetConfig().Name, target.string());
}

void SandboxLayer::OpenProject(const std::filesystem::path& filepath) {
    std::filesystem::path target = filepath;
    if (target.empty()) {
        auto currentDir = m_CurrentProjectPath.empty() ? std::filesystem::current_path() : m_CurrentProjectPath.parent_path();
        std::string selected = Axle::FileDialogs::OpenFile("Axle Project (*.axleproj)\0*.axleproj\0All Files (*.*)\0*.*\0", currentDir.string().c_str());
        if (selected.empty()) return;
        target = selected;
    }

    if (!std::filesystem::exists(target)) {
        AXLE_WARN("Sandbox: Project file does not exist: '{0}'", target.string());
        return;
    }

    auto project = Axle::Project::Load(target);
    if (project) {
        m_CurrentProjectPath = target;
        m_ContentBrowserPanel.SetRootPath(Axle::Project::GetAssetDirectory());

        // Load project's StartScene if specified
        const auto& startSceneRel = project->GetConfig().StartScene;
        if (!startSceneRel.empty()) {
            auto startScenePath = Axle::Project::GetAssetDirectory() / startSceneRel;
            if (std::filesystem::exists(startScenePath)) {
                OpenScene(startScenePath);
            } else {
                NewScene();
            }
        } else {
            NewScene();
        }

        m_CompileNotificationMessage = "Loaded project: " + project->GetConfig().Name;
        m_CompileNotificationTimer = 4.0f;
        UpdateWindowTitle();
        AXLE_INFO("Sandbox: Successfully opened project '{0}'", target.string());
    } else {
        AXLE_ERROR("Sandbox: Failed to load project from '{0}'", target.string());
    }
}

void SandboxLayer::SaveProject() {
    if (m_CurrentProjectPath.empty()) {
        SaveProjectAs();
        return;
    }
    if (Axle::Project::SaveActive(m_CurrentProjectPath)) {
        m_CompileNotificationMessage = "Saved project configuration";
        m_CompileNotificationTimer = 3.5f;
        AXLE_INFO("Sandbox: Saved project to '{0}'", m_CurrentProjectPath.string());
    }
}

void SandboxLayer::SaveProjectAs() {
    auto currentDir = m_CurrentProjectPath.empty() ? std::filesystem::current_path() : m_CurrentProjectPath.parent_path();
    std::string filepath = Axle::FileDialogs::SaveFile("Axle Project (*.axleproj)\0*.axleproj\0All Files (*.*)\0*.*\0", currentDir.string().c_str(), "axleproj");
    if (!filepath.empty()) {
        std::filesystem::path target(filepath);
        if (target.extension() != ".axleproj") {
            target += ".axleproj";
        }
        m_CurrentProjectPath = target;
        if (Axle::Project::GetActive()) {
            Axle::Project::GetActive()->GetConfig().Name = target.stem().string();
        }
        SaveProject();
        UpdateWindowTitle();
    }
}

void SandboxLayer::UpdateWindowTitle() {
    std::string projName = "No Project";
    if (auto active = Axle::Project::GetActive()) {
        projName = active->GetConfig().Name;
    }
    std::string sceneName = m_CurrentScenePath.empty() ? "Untitled" : m_CurrentScenePath.filename().string();
    Axle::Application::Get().GetWindow().SetTitle("Axle Engine - " + projName + " - [" + sceneName + "]");
}

void SandboxLayer::OnScenePause() {
    m_SceneState = SceneState::Pause;
    AXLE_INFO("Simulation paused. Viewport Free-Fly inspection enabled.");
}

void SandboxLayer::OnSceneStop() {
    m_SceneState = SceneState::Edit;
    m_Scene->OnRuntimeStop();
    ResetSimulation();
    m_ChaseCamera = false;
    AXLE_INFO("Simulation stopped (Edit Mode). Editor Free-Fly Camera active: Hold RMB to look, WASD to fly (Shift: Sprint, Q/E: Elevate).");
}

void SandboxLayer::ResetSimulation() {
    AXLE_INFO("Resetting physics simulation and vehicle state");

    // Reset all registered dynamic bodies (pyramid crates, cones, tires, demo shapes) to initial poses
    for (const auto& [bodyIDRaw, state] : m_InitialDynamicStates) {
        Axle::BodyID bodyID(bodyIDRaw);
        if (!bodyID.IsInvalid() && m_PhysicsScene && m_PhysicsScene->IsBodyValid(bodyID)) {
            m_PhysicsScene->SetBodyPositionAndRotation(bodyID, state.Position, state.Rotation, true);
            m_PhysicsScene->SetLinearVelocity(bodyID, glm::vec3(0.0f));
            m_PhysicsScene->GetBodyInterface().SetAngularVelocity(bodyID, JPH::Vec3::sZero());
        }
    }

    if (m_Vehicle && m_ChassisEntity && m_ChassisEntity.HasComponent<Axle::TransformComponent>()) {
        m_Vehicle->Reset(glm::vec3(0.0f, 1.2f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        m_Vehicle->SetDriverInput(0.0f, 0.0f, 0.0f, 0.0f);
        auto& chassisTC = m_ChassisEntity.GetComponent<Axle::TransformComponent>();
        chassisTC.Translation = m_Vehicle->GetPosition();
        chassisTC.Rotation = m_PhysicsScene->GetBodyRotation(m_Vehicle->GetChassisBodyID());

        for (Axle::u32 i = 0; i < 4; ++i) {
            if (m_WheelEntities[i] && m_WheelEntities[i].HasComponent<Axle::TransformComponent>()) {
                const glm::mat4 wheelMat = m_Vehicle->GetWheelTransform(i);
                auto& tc = m_WheelEntities[i].GetComponent<Axle::TransformComponent>();
                tc.Translation = glm::vec3(wheelMat[3]);
                tc.Rotation = glm::quat_cast(wheelMat);
            }
        }
    }
    m_OrbitYaw = 180.0f;
    m_OrbitPitch = 15.0f;
}

void SandboxLayer::CreatePlayground() {
    AXLE_INFO("Generating Phase 7 Automotive Test Playground & Obstacle Course");

    auto registerDynamic = [&](Axle::BodyID bodyID, const glm::vec3& pos, const glm::quat& rot) {
        if (!bodyID.IsInvalid()) {
            m_InitialDynamicStates[bodyID.GetIndexAndSequenceNumber()] = { pos, rot };
        }
    };

    // Root Playground Entity
    auto playgroundRoot = m_Scene->CreateEntity("Automotive Playground");
    auto& playgroundHc = playgroundRoot.AddComponent<Axle::HierarchyComponent>();

    // =========================================================================
    // 1. JUMP RAMPS & INCLINES (Runway Center & Stunt Left Wing)
    // =========================================================================
    auto jumpGroup = m_Scene->CreateEntity("Jump Ramps & Inclines");
    auto& jumpHc = jumpGroup.AddComponent<Axle::HierarchyComponent>(static_cast<entt::entity>(playgroundRoot));
    playgroundHc.Children.push_back(static_cast<entt::entity>(jumpGroup));

    // Ramp 1: Runway Launch Ramp (16 deg incline, 2.2m rise)
    {
        glm::vec3 pos(0.0f, 1.0f, 22.0f);
        glm::quat rot = glm::angleAxis(glm::radians(-16.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        glm::vec3 halfExt(3.0f, 0.15f, 4.0f); // 6m wide, 8m long, 0.3m thick

        auto launchRamp = m_Scene->CreateEntity("Runway Launch Ramp");
        launchRamp.AddComponent<Axle::TransformComponent>(pos, rot, halfExt * 2.0f);
        launchRamp.AddComponent<Axle::MeshComponent>(
            m_CubeMesh, 1, 4.0f,
            glm::vec4(0.96f, 0.78f, 0.10f, 1.0f), // Hazard Vivid Yellow
            glm::vec4(0.12f, 0.13f, 0.16f, 1.0f)  // Dark Asphalt Carbon
        );

        Axle::BoxBodyProps props;
        props.Position = pos;
        props.Rotation = rot;
        props.HalfExtent = halfExt;
        props.IsDynamic = false;
        props.Friction = 0.85f;
        auto bodyID = m_PhysicsScene->CreateBox(props);
        launchRamp.AddComponent<Axle::RigidBodyComponent>(bodyID, false);
        launchRamp.AddComponent<Axle::HierarchyComponent>(static_cast<entt::entity>(jumpGroup));
        jumpHc.Children.push_back(static_cast<entt::entity>(launchRamp));
    }

    // Ramp 2: Runway Landing Pad (14 deg decline to cushion suspension)
    {
        glm::vec3 pos(0.0f, 1.0f, 35.0f);
        glm::quat rot = glm::angleAxis(glm::radians(14.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        glm::vec3 halfExt(3.0f, 0.15f, 4.0f);

        auto landingPad = m_Scene->CreateEntity("Runway Landing Pad");
        landingPad.AddComponent<Axle::TransformComponent>(pos, rot, halfExt * 2.0f);
        landingPad.AddComponent<Axle::MeshComponent>(
            m_CubeMesh, 1, 4.0f,
            glm::vec4(0.90f, 0.92f, 0.95f, 1.0f), // Racing White
            glm::vec4(0.12f, 0.13f, 0.16f, 1.0f)  // Dark Asphalt Carbon
        );

        Axle::BoxBodyProps props;
        props.Position = pos;
        props.Rotation = rot;
        props.HalfExtent = halfExt;
        props.IsDynamic = false;
        props.Friction = 0.85f;
        auto bodyID = m_PhysicsScene->CreateBox(props);
        landingPad.AddComponent<Axle::RigidBodyComponent>(bodyID, false);
        landingPad.AddComponent<Axle::HierarchyComponent>(static_cast<entt::entity>(jumpGroup));
        jumpHc.Children.push_back(static_cast<entt::entity>(landingPad));
    }

    // Ramp 3: Stunt Mega Ramp (Extreme 24 deg incline, 4.0m rise)
    {
        glm::vec3 pos(-18.0f, 1.9f, 25.0f);
        glm::quat rot = glm::angleAxis(glm::radians(-24.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        glm::vec3 halfExt(3.5f, 0.2f, 5.0f); // 7m wide, 10m long, 0.4m thick

        auto megaRamp = m_Scene->CreateEntity("Stunt Mega Ramp");
        megaRamp.AddComponent<Axle::TransformComponent>(pos, rot, halfExt * 2.0f);
        megaRamp.AddComponent<Axle::MeshComponent>(
            m_CubeMesh, 1, 5.0f,
            glm::vec4(0.72f, 0.12f, 0.92f, 1.0f), // Royal Stunt Violet
            glm::vec4(0.05f, 0.90f, 0.85f, 1.0f)  // Electric Cyan
        );

        Axle::BoxBodyProps props;
        props.Position = pos;
        props.Rotation = rot;
        props.HalfExtent = halfExt;
        props.IsDynamic = false;
        props.Friction = 0.85f;
        auto bodyID = m_PhysicsScene->CreateBox(props);
        megaRamp.AddComponent<Axle::RigidBodyComponent>(bodyID, false);
        megaRamp.AddComponent<Axle::HierarchyComponent>(static_cast<entt::entity>(jumpGroup));
        jumpHc.Children.push_back(static_cast<entt::entity>(megaRamp));
    }

    // =========================================================================
    // 2. WASHBOARD & BUMPS TRACK (Suspension Oscillation & Damping Lane)
    // =========================================================================
    auto washboardGroup = m_Scene->CreateEntity("Washboard Bumps Track");
    auto& washboardHc = washboardGroup.AddComponent<Axle::HierarchyComponent>(static_cast<entt::entity>(playgroundRoot));
    playgroundHc.Children.push_back(static_cast<entt::entity>(washboardGroup));

    for (int i = 0; i < 10; ++i) {
        float zPos = 10.0f + static_cast<float>(i) * 3.0f;
        auto bumpEntity = m_Scene->CreateEntity("Rumble Bump " + std::to_string(i + 1));
        glm::vec3 pos(14.0f, 0.10f, zPos);
        glm::vec3 halfExt(2.5f, 0.10f, 0.35f); // 5m wide across lane, 0.20m high, 0.70m thick
        bumpEntity.AddComponent<Axle::TransformComponent>(pos, halfExt * 2.0f);

        // Alternating red and white curb pattern
        glm::vec4 c1 = (i % 2 == 0) ? glm::vec4(0.92f, 0.15f, 0.15f, 1.0f) : glm::vec4(0.96f, 0.96f, 0.96f, 1.0f);
        glm::vec4 c2 = (i % 2 == 0) ? glm::vec4(0.96f, 0.96f, 0.96f, 1.0f) : glm::vec4(0.92f, 0.15f, 0.15f, 1.0f);
        bumpEntity.AddComponent<Axle::MeshComponent>(m_CubeMesh, 1, 6.0f, c1, c2);

        Axle::BoxBodyProps bProps;
        bProps.Position = pos;
        bProps.HalfExtent = halfExt;
        bProps.IsDynamic = false;
        bProps.Friction = 0.85f;
        auto bodyID = m_PhysicsScene->CreateBox(bProps);
        bumpEntity.AddComponent<Axle::RigidBodyComponent>(bodyID, false);
        bumpEntity.AddComponent<Axle::HierarchyComponent>(static_cast<entt::entity>(washboardGroup));
        washboardHc.Children.push_back(static_cast<entt::entity>(bumpEntity));
    }

    // =========================================================================
    // 3. BANKED CURVED TRACK (High-Speed Camber Turn)
    // =========================================================================
    auto bankedGroup = m_Scene->CreateEntity("Banked Camber Turn");
    auto& bankedHc = bankedGroup.AddComponent<Axle::HierarchyComponent>(static_cast<entt::entity>(playgroundRoot));
    playgroundHc.Children.push_back(static_cast<entt::entity>(bankedGroup));

    for (int i = 0; i < 6; ++i) {
        float t = static_cast<float>(i) / 5.0f;
        float angleDeg = 15.0f + t * 75.0f; // 15 to 90 degrees
        float angleRad = glm::radians(angleDeg);
        float radius = 22.0f;
        float x = 20.0f + radius * (1.0f - std::cos(angleRad));
        float z = 45.0f + radius * std::sin(angleRad);
        float y = 0.75f + t * 0.5f;

        // Yaw angle aligns with curve tangent, roll angle banks inward by -22 degrees
        glm::quat rot = glm::angleAxis(angleRad, glm::vec3(0.0f, 1.0f, 0.0f))
                      * glm::angleAxis(glm::radians(-22.0f), glm::vec3(0.0f, 0.0f, 1.0f));

        auto segmentEntity = m_Scene->CreateEntity("Bank Segment " + std::to_string(i + 1));
        glm::vec3 halfExt(3.0f, 0.15f, 3.5f); // 6m wide road, 0.3m thick, 7m long
        segmentEntity.AddComponent<Axle::TransformComponent>(glm::vec3(x, y, z), rot, halfExt * 2.0f);
        segmentEntity.AddComponent<Axle::MeshComponent>(
            m_CubeMesh, 1, 3.0f,
            glm::vec4(0.18f, 0.20f, 0.24f, 1.0f), // Asphalt Dark Slate
            glm::vec4(0.10f, 0.55f, 0.95f, 1.0f)  // Electric Blue Curbing
        );

        Axle::BoxBodyProps bProps;
        bProps.Position = glm::vec3(x, y, z);
        bProps.Rotation = rot;
        bProps.HalfExtent = halfExt;
        bProps.IsDynamic = false;
        bProps.Friction = 0.90f;
        auto bodyID = m_PhysicsScene->CreateBox(bProps);
        segmentEntity.AddComponent<Axle::RigidBodyComponent>(bodyID, false);
        segmentEntity.AddComponent<Axle::HierarchyComponent>(static_cast<entt::entity>(bankedGroup));
        bankedHc.Children.push_back(static_cast<entt::entity>(segmentEntity));
    }

    // =========================================================================
    // 4. DYNAMIC CRASH BARRIERS & DESTRUCTION COURSE
    // =========================================================================
    auto crashGroup = m_Scene->CreateEntity("Crash Course & Barriers");
    auto& crashHc = crashGroup.AddComponent<Axle::HierarchyComponent>(static_cast<entt::entity>(playgroundRoot));
    playgroundHc.Children.push_back(static_cast<entt::entity>(crashGroup));

    // A. Destructible 10-Crate Wooden Pyramid at Z = 52.0m on runway
    const struct CratePlacement {
        float x;
        float y;
    } pyramidCrates[10] = {
        // Base (4 crates)
        { -1.35f, 0.45f }, { -0.45f, 0.45f }, { 0.45f, 0.45f }, { 1.35f, 0.45f },
        // Tier 2 (3 crates)
        { -0.90f, 1.35f }, {  0.00f, 1.35f }, { 0.90f, 1.35f },
        // Tier 3 (2 crates)
        { -0.45f, 2.25f }, {  0.45f, 2.25f },
        // Apex (1 crate)
        {  0.00f, 3.15f }
    };

    for (int i = 0; i < 10; ++i) {
        glm::vec3 pos(pyramidCrates[i].x, pyramidCrates[i].y, 52.0f);
        glm::vec3 halfExt(0.45f, 0.45f, 0.45f);

        auto crateEntity = m_Scene->CreateEntity("Wooden Crate " + std::to_string(i + 1));
        crateEntity.AddComponent<Axle::TransformComponent>(pos, halfExt * 2.0f);
        crateEntity.AddComponent<Axle::MeshComponent>(
            m_CubeMesh, 1, 4.0f,
            glm::vec4(0.82f, 0.52f, 0.20f, 1.0f), // Wood Amber
            glm::vec4(0.38f, 0.22f, 0.08f, 1.0f)  // Dark Walnut
        );

        Axle::BoxBodyProps props;
        props.Position = pos;
        props.HalfExtent = halfExt;
        props.IsDynamic = true;
        props.Friction = 0.60f;
        props.Restitution = 0.15f;
        auto bodyID = m_PhysicsScene->CreateBox(props);
        crateEntity.AddComponent<Axle::RigidBodyComponent>(bodyID, true);
        crateEntity.AddComponent<Axle::HierarchyComponent>(static_cast<entt::entity>(crashGroup));
        crashHc.Children.push_back(static_cast<entt::entity>(crateEntity));

        registerDynamic(bodyID, pos, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    }

    // B. Slalom Cones (6 dynamic cones along Lane X ~ -8m)
    for (int i = 0; i < 6; ++i) {
        float xPos = (i % 2 == 0) ? -7.5f : -9.0f;
        float zPos = 8.0f + static_cast<float>(i) * 7.0f;
        glm::vec3 pos(xPos, 0.65f, zPos);

        auto coneEntity = m_Scene->CreateEntity("Slalom Cone " + std::to_string(i + 1));
        coneEntity.AddComponent<Axle::TransformComponent>(pos, glm::vec3(0.45f, 0.65f, 0.45f));
        coneEntity.AddComponent<Axle::MeshComponent>(
            m_ConeMesh, 1, 6.0f,
            glm::vec4(1.0f, 0.45f, 0.05f, 1.0f),  // Fluorescent Safety Orange
            glm::vec4(0.96f, 0.96f, 0.96f, 1.0f)  // Reflective White
        );

        Axle::ConeBodyProps props;
        props.Position = pos;
        props.Radius = 0.45f;
        props.HalfHeight = 0.65f;
        props.IsDynamic = true;
        props.Friction = 0.80f;
        props.Restitution = 0.10f;
        auto bodyID = m_PhysicsScene->CreateCone(props);
        coneEntity.AddComponent<Axle::RigidBodyComponent>(bodyID, true);
        coneEntity.AddComponent<Axle::HierarchyComponent>(static_cast<entt::entity>(crashGroup));
        crashHc.Children.push_back(static_cast<entt::entity>(coneEntity));

        registerDynamic(bodyID, pos, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    }

    // C. Tire Stack Crash Barrier (6 dynamic rubber cylinders at Z = 68.0m)
    for (int i = 0; i < 6; ++i) {
        float xPos = -2.5f + static_cast<float>(i) * 1.0f;
        glm::vec3 pos(xPos, 0.50f, 68.0f);

        auto tireEntity = m_Scene->CreateEntity("Tire Barrier " + std::to_string(i + 1));
        tireEntity.AddComponent<Axle::TransformComponent>(pos, glm::vec3(0.45f, 0.50f, 0.45f));
        tireEntity.AddComponent<Axle::MeshComponent>(
            m_CylinderMesh, 1, 4.0f,
            glm::vec4(0.12f, 0.12f, 0.14f, 1.0f), // Tire Rubber Black
            glm::vec4(0.22f, 0.22f, 0.25f, 1.0f)  // Weathered Rubber
        );

        Axle::CylinderBodyProps props;
        props.Position = pos;
        props.Radius = 0.45f;
        props.HalfHeight = 0.50f;
        props.IsDynamic = true;
        props.Friction = 0.85f;
        props.Restitution = 0.25f;
        auto bodyID = m_PhysicsScene->CreateCylinder(props);
        tireEntity.AddComponent<Axle::RigidBodyComponent>(bodyID, true);
        tireEntity.AddComponent<Axle::HierarchyComponent>(static_cast<entt::entity>(crashGroup));
        crashHc.Children.push_back(static_cast<entt::entity>(tireEntity));

        registerDynamic(bodyID, pos, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    }

    AXLE_INFO("Phase 7 Automotive Playground generated successfully (41 physical obstacles)");
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

    if (m_CompileNotificationTimer > 0.0f) {
        m_CompileNotificationTimer -= ts.GetSeconds();
    }

    if (Axle::Input::IsKeyPressed(Axle::KeyCode::F9)) {
        if (!m_F9KeyWasPressed) {
            CompileAndHotReloadAssets(false);
            m_F9KeyWasPressed = true;
        }
    } else {
        m_F9KeyWasPressed = false;
    }

    const bool isHoldingRMB = Axle::Input::IsMouseButtonPressed(Axle::MouseCode::ButtonRight);
    const bool isCtrlDown = Axle::Input::IsKeyPressed(Axle::KeyCode::LeftControl) || Axle::Input::IsKeyPressed(Axle::KeyCode::RightControl);
    const bool isShiftDown = Axle::Input::IsKeyPressed(Axle::KeyCode::LeftShift) || Axle::Input::IsKeyPressed(Axle::KeyCode::RightShift);

    // File Shortcut Hotkeys (Ctrl+S, Ctrl+Shift+S, Ctrl+O, Ctrl+N)
    if (isCtrlDown && !ImGui::GetIO().WantTextInput) {
        if (Axle::Input::IsKeyPressed(Axle::KeyCode::S)) {
            if (!m_SKeyWasPressed) {
                if (isShiftDown) {
                    SaveSceneAs();
                } else {
                    SaveScene();
                }
                m_SKeyWasPressed = true;
            }
        } else {
            m_SKeyWasPressed = false;
        }

        if (Axle::Input::IsKeyPressed(Axle::KeyCode::N)) {
            if (!m_NKeyWasPressed) {
                NewScene();
                m_NKeyWasPressed = true;
            }
        } else {
            m_NKeyWasPressed = false;
        }

        if (Axle::Input::IsKeyPressed(Axle::KeyCode::O)) {
            if (!m_OKeyWasPressed) {
                if (isShiftDown) {
                    OpenProject();
                } else {
                    OpenScene();
                }
                m_OKeyWasPressed = true;
            }
        } else {
            m_OKeyWasPressed = false;
        }
    }

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

    if (Axle::Input::IsKeyPressed(Axle::KeyCode::F11)) {
        if (!m_F11KeyWasPressed) {
            auto& window = Axle::Application::Get().GetWindow();
            if (window.IsMaximized()) {
                window.Restore();
            } else {
                window.Maximize();
            }
            m_F11KeyWasPressed = true;
        }
    } else {
        m_F1KeyWasPressed = false;
    }

    // C++ Native Scripting Runtime Execution (Phase 8: CarPlayerScript, Camera, Hazard scripts)
    if (m_SceneState == SceneState::Play) {
        m_Scene->OnUpdateRuntime(ts);
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
    if (m_SceneState == SceneState::Play && m_PhysicsScene) {
        Axle::Timestep simTs = ts.GetSeconds() * m_SimulationSpeed;
        m_PhysicsScene->Step(simTs);
    } else if (m_SceneState == SceneState::Pause && m_StepSingleFrame && m_PhysicsScene) {
        m_PhysicsScene->Step(1.0f / 60.0f);
        m_StepSingleFrame = false;
    }

    // Synchronize vehicle chassis and 4 wheel entities with physics simulation
    if (m_Vehicle && m_ChassisEntity && m_ChassisEntity.HasComponent<Axle::TransformComponent>()) {
        if (m_SceneState == SceneState::Edit) {
            auto& chassisTC = m_ChassisEntity.GetComponent<Axle::TransformComponent>();
            m_Vehicle->Reset(chassisTC.Translation, chassisTC.Rotation);
        } else {
            auto& chassisTC = m_ChassisEntity.GetComponent<Axle::TransformComponent>();
            chassisTC.Translation = m_Vehicle->GetPosition();
            chassisTC.Rotation = m_PhysicsScene->GetBodyRotation(m_Vehicle->GetChassisBodyID());
        }

        for (Axle::u32 i = 0; i < 4; ++i) {
            if (m_WheelEntities[i] && m_WheelEntities[i].HasComponent<Axle::TransformComponent>()) {
                const glm::mat4 wheelMat = m_Vehicle->GetWheelTransform(i);
                auto& tc = m_WheelEntities[i].GetComponent<Axle::TransformComponent>();
                tc.Translation = glm::vec3(wheelMat[3]);
                tc.Rotation = glm::quat_cast(wheelMat);
            }
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

        // --- 1. QUICK SCENE SAVE (Unreal Engine Style) ---
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.44f, 0.74f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.54f, 0.88f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14f, 0.36f, 0.62f, 1.0f));
        if (ImGui::Button("Save", ImVec2(56.0f, 26.0f))) {
            SaveScene();
        }
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Save active scene to file (Ctrl+S)");
        }

        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.35f, 0.38f, 0.44f, 1.0f), "|");
        ImGui::SameLine();

        // --- 2. GIZMO TRANSFORMATION TOOLS ---
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
        if (m_CompileNotificationTimer > 0.0f) {
            ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.55f, 1.0f), "[OK] %s", m_CompileNotificationMessage.c_str());
        } else if (m_SceneState == SceneState::Edit) {
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
            // Project Management
            if (ImGui::MenuItem("New Project...")) {
                NewProject();
            }
            if (ImGui::MenuItem("Open Project...", "Ctrl+Shift+O")) {
                OpenProject();
            }
            if (ImGui::MenuItem("Save Project")) {
                SaveProject();
            }
            if (ImGui::MenuItem("Save Project As...")) {
                SaveProjectAs();
            }
            ImGui::Separator();

            // Scene Operations
            if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
                NewScene();
            }
            if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {
                OpenScene();
            }
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
                SaveScene();
            }
            if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) {
                SaveSceneAs();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Simulation", "Ctrl+R")) {
                ResetSimulation();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                Axle::Application::Get().Close();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Asset")) {
            if (ImGui::MenuItem("Compile Modified Assets", "F9")) {
                CompileAndHotReloadAssets(false);
            }
            if (ImGui::MenuItem("Rebuild All Binary Caches", "Shift+F9")) {
                CompileAndHotReloadAssets(true);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Entity")) {
            if (ImGui::MenuItem("Create Empty Entity")) {
                auto entity = m_Scene->CreateEntity("Empty Entity");
                entity.AddComponent<Axle::TransformComponent>();
                m_SceneHierarchyPanel.SetSelectedEntity(entity);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("3D Cube (Dynamic Box)")) {
                m_SceneHierarchyPanel.CreatePrimitive(0);
            }
            if (ImGui::MenuItem("3D Sphere (Dynamic)")) {
                m_SceneHierarchyPanel.CreatePrimitive(1);
            }
            if (ImGui::MenuItem("3D Cylinder (Dynamic)")) {
                m_SceneHierarchyPanel.CreatePrimitive(2);
            }
            if (ImGui::MenuItem("3D Cone (Dynamic)")) {
                m_SceneHierarchyPanel.CreatePrimitive(3);
            }
            if (ImGui::MenuItem("Static Floor Plane")) {
                m_SceneHierarchyPanel.CreatePrimitive(4);
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
            ImGui::MenuItem("Content Browser", nullptr, &m_ShowContentBrowser);
            ImGui::MenuItem("Asset Preview", nullptr, &m_ShowAssetPreview);
            ImGui::MenuItem("Telemetry & Performance", nullptr, &m_ShowTelemetry);
            ImGui::MenuItem("Physics Wireframe", "F1", &m_ShowPhysicsDebug);
            ImGui::Separator();
            bool isMaximized = Axle::Application::Get().GetWindow().IsMaximized();
            if (ImGui::MenuItem(isMaximized ? "Restore Window" : "Maximize Window", "F11")) {
                if (isMaximized) {
                    Axle::Application::Get().GetWindow().Restore();
                } else {
                    Axle::Application::Get().GetWindow().Maximize();
                }
            }
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
        ImGuiID dock_right = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.28f, nullptr, &dock_main);
        ImGuiID dock_bottom = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.30f, nullptr, &dock_main);

        ImGui::DockBuilderDockWindow("Scene Hierarchy", dock_left);
        ImGui::DockBuilderDockWindow("Viewport", dock_main);
        ImGui::DockBuilderDockWindow("Content Browser", dock_bottom);
        ImGui::DockBuilderDockWindow("Console", dock_bottom);
        ImGui::DockBuilderDockWindow("Properties", dock_right);
        ImGui::DockBuilderDockWindow("Asset Preview", dock_right);
        ImGui::DockBuilderDockWindow("Vehicle Tuner", dock_right);
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

    // Drag & Drop Target: Drag 3D models (.obj) from Content Browser into Viewport to instantiate
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
            const char* pathStr = static_cast<const char*>(payload->Data);
            std::filesystem::path assetPath(pathStr);
            std::string ext = assetPath.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });

            if (ext == ".axlescene" || ext == ".scene") {
                OpenScene(assetPath);
            } else if (ext == ".axle" || ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx") {
                if (ext != ".axle") {
                    std::filesystem::path axlePath = assetPath.parent_path() / (assetPath.stem().string() + ".axle");
                    if (!std::filesystem::exists(axlePath)) {
                        Axle::AssetSerializer::AutoConvertModel(assetPath, axlePath, true);
                    }
                    assetPath = axlePath;
                }

                auto pkg = Axle::AssetSerializer::LoadPackage(assetPath);
                if (pkg.Success) {
                    glm::vec3 spawnPos(0.0f, pkg.HalfExtent.y, 0.0f);
                    ImVec2 mousePos = ImGui::GetMousePos();
                    float mouseX = mousePos.x - viewportMin.x;
                    float mouseY = mousePos.y - viewportMin.y;

                    if (mouseX >= 0.0f && mouseX < viewportBounds.x && mouseY >= 0.0f && mouseY < viewportBounds.y) {
                        float ndcX = (2.0f * (mouseX / viewportBounds.x)) - 1.0f;
                        float ndcY = 1.0f - (2.0f * (mouseY / viewportBounds.y));

                        glm::mat4 invVP = glm::inverse(m_CameraController->GetCamera().GetViewProjectionMatrix());
                        glm::vec4 nearPointNDC(ndcX, ndcY, -1.0f, 1.0f);
                        glm::vec4 farPointNDC(ndcX, ndcY, 1.0f, 1.0f);

                        glm::vec4 nearW = invVP * nearPointNDC;
                        glm::vec4 farW = invVP * farPointNDC;
                        if (std::abs(nearW.w) > 1e-6f) nearW /= nearW.w;
                        if (std::abs(farW.w) > 1e-6f) farW /= farW.w;

                        glm::vec3 rayOrigin = glm::vec3(nearW);
                        glm::vec3 rayDir = glm::normalize(glm::vec3(farW - nearW));

                        Axle::RayCastHit hit;
                        if (m_PhysicsScene && m_PhysicsScene->CastRay(rayOrigin, rayDir, 500.0f, hit)) {
                            spawnPos = hit.Position + glm::vec3(0.0f, pkg.HalfExtent.y, 0.0f);
                        } else {
                            if (std::abs(rayDir.y) > 1e-4f) {
                                float t = -rayOrigin.y / rayDir.y;
                                if (t > 0.0f && t < 150.0f) {
                                    glm::vec3 groundHit = rayOrigin + rayDir * t;
                                    spawnPos = groundHit + glm::vec3(0.0f, pkg.HalfExtent.y, 0.0f);
                                } else {
                                    spawnPos = rayOrigin + rayDir * 10.0f;
                                }
                            } else {
                                spawnPos = rayOrigin + rayDir * 10.0f;
                            }
                        }
                    }

                    if (pkg.IsPackage) {
                        std::string rootTag = pkg.Name.empty() ? assetPath.stem().string() : pkg.Name;
                        auto rootEntity = m_Scene->CreateEntity(rootTag);
                        rootEntity.AddComponent<Axle::TransformComponent>(spawnPos, glm::vec3(1.0f));
                        auto& rootHierarchy = rootEntity.AddComponent<Axle::HierarchyComponent>();

                        if (m_PhysicsScene) {
                            Axle::BoxBodyProps bProps;
                            bProps.Position = spawnPos;
                            bProps.HalfExtent = pkg.HalfExtent;
                            bProps.IsDynamic = true;
                            bProps.Friction = 0.70f;
                            bProps.Restitution = 0.15f;
                            auto bodyId = m_PhysicsScene->CreateBox(bProps);
                            rootEntity.AddComponent<Axle::RigidBodyComponent>(bodyId, true);
                        }

                        for (const auto& part : pkg.Parts) {
                            auto childEntity = m_Scene->CreateEntity(part.Name);
                            auto& ct = childEntity.AddComponent<Axle::TransformComponent>(part.LocalTranslation, part.LocalScale);
                            ct.Rotation = part.LocalRotation;

                            auto partFullPath = assetPath.parent_path() / part.RelativePath;
                            childEntity.AddComponent<Axle::MeshComponent>(
                                part.LoadedMesh.VAO ? part.LoadedMesh.VAO : m_CubeMesh,
                                partFullPath.string(),
                                part.LoadedMesh.SourceData.PatternMode,
                                part.LoadedMesh.SourceData.TileScale,
                                part.LoadedMesh.SourceData.ColorPrimary,
                                part.LoadedMesh.SourceData.ColorSecondary
                            );

                            childEntity.AddComponent<Axle::HierarchyComponent>((entt::entity)rootEntity);
                            rootHierarchy.Children.push_back((entt::entity)childEntity);
                        }

                        m_SceneHierarchyPanel.SetSelectedEntity(rootEntity);
                        AXLE_INFO("Instantiated Model Package '{0}' ({1} submeshes) into Viewport at ({2:.2f}, {3:.2f}, {4:.2f})", 
                            rootTag, pkg.Parts.size(), spawnPos.x, spawnPos.y, spawnPos.z);
                    } else if (!pkg.Parts.empty()) {
                        const auto& part = pkg.Parts[0];
                        std::string entityTag = pkg.Name.empty() ? assetPath.stem().string() : pkg.Name;
                        auto entity = m_Scene->CreateEntity(entityTag);
                        entity.AddComponent<Axle::TransformComponent>(spawnPos, glm::vec3(1.0f));

                        entity.AddComponent<Axle::MeshComponent>(
                            part.LoadedMesh.VAO ? part.LoadedMesh.VAO : m_CubeMesh,
                            assetPath.string(),
                            part.LoadedMesh.SourceData.PatternMode,
                            part.LoadedMesh.SourceData.TileScale,
                            part.LoadedMesh.SourceData.ColorPrimary,
                            part.LoadedMesh.SourceData.ColorSecondary
                        );

                        if (m_PhysicsScene) {
                            Axle::BoxBodyProps bProps;
                            bProps.Position = spawnPos;
                            bProps.HalfExtent = pkg.HalfExtent;
                            bProps.IsDynamic = true;
                            bProps.Friction = 0.70f;
                            bProps.Restitution = 0.15f;
                            auto bodyId = m_PhysicsScene->CreateBox(bProps);
                            entity.AddComponent<Axle::RigidBodyComponent>(bodyId, true);
                        }

                        m_SceneHierarchyPanel.SetSelectedEntity(entity);
                        AXLE_INFO("Instantiated 3D model '{0}' into Viewport at ({1:.2f}, {2:.2f}, {3:.2f})", 
                            entityTag, spawnPos.x, spawnPos.y, spawnPos.z);
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

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
            glm::mat4 worldTransform = m_Scene->GetWorldTransform(selectedEntity);
            glm::mat4 transform = worldTransform;

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
                if (selectedEntity.HasComponent<Axle::HierarchyComponent>() &&
                    selectedEntity.GetComponent<Axle::HierarchyComponent>().InheritTransform &&
                    selectedEntity.GetComponent<Axle::HierarchyComponent>().Parent != entt::null &&
                    m_Scene->GetRegistry().valid(selectedEntity.GetComponent<Axle::HierarchyComponent>().Parent)) {

                    entt::entity parentID = selectedEntity.GetComponent<Axle::HierarchyComponent>().Parent;
                    glm::mat4 parentWorld = m_Scene->GetWorldTransform(parentID);
                    glm::mat4 localMat = glm::inverse(parentWorld) * transform;

                    float translation[3], rotation[3], scale[3];
                    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(localMat), translation, rotation, scale);
                    tc.Translation = glm::vec3(translation[0], translation[1], translation[2]);
                    tc.Rotation = glm::quat(glm::radians(glm::vec3(rotation[0], rotation[1], rotation[2])));
                    tc.Scale = glm::vec3(scale[0], scale[1], scale[2]);
                } else {
                    float translation[3], rotation[3], scale[3];
                    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(transform), translation, rotation, scale);
                    tc.Translation = glm::vec3(translation[0], translation[1], translation[2]);
                    tc.Rotation = glm::quat(glm::radians(glm::vec3(rotation[0], rotation[1], rotation[2])));
                    tc.Scale = glm::vec3(scale[0], scale[1], scale[2]);
                }

                // Real-time synchronization with Jolt Physics bodies
                if (selectedEntity == m_ChassisEntity && m_Vehicle) {
                    m_Vehicle->Reset(tc.Translation, tc.Rotation);
                    for (Axle::u32 i = 0; i < 4; ++i) {
                        if (m_WheelEntities[i] && m_WheelEntities[i].HasComponent<Axle::TransformComponent>()) {
                            const glm::mat4 wheelMat = m_Vehicle->GetWheelTransform(i);
                            auto& wtc = m_WheelEntities[i].GetComponent<Axle::TransformComponent>();
                            wtc.Translation = glm::vec3(wheelMat[3]);
                            wtc.Rotation = glm::quat_cast(wheelMat);
                        }
                    }
                } else if (selectedEntity.HasComponent<Axle::RigidBodyComponent>() && m_PhysicsScene) {
                    auto& rb = selectedEntity.GetComponent<Axle::RigidBodyComponent>();
                    if (!rb.RuntimeBodyID.IsInvalid() && m_PhysicsScene->IsBodyValid(rb.RuntimeBodyID)) {
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

    // 8. Content Browser Panel
    m_ContentBrowserPanel.OnImGuiRender(&m_ShowContentBrowser);

    // 9. Asset Preview Panel
    m_AssetPreviewPanel.OnImGuiRender(&m_ShowAssetPreview);

    // 10. Performance & Telemetry Panel
    if (m_ShowTelemetry) {
        Axle::TelemetryData telemetry{
            .DeltaTime = m_LastDeltaTime,
            .FPS = m_LastDeltaTime > 0.0f ? 1.0f / m_LastDeltaTime : 0.0f,
            .CameraPosition = m_CameraController->GetCamera().GetPosition(),
            .CameraRotation = m_CameraController->GetCamera().GetRotation(),
            .HasPhysics = (m_PhysicsScene != nullptr),
            .BoxPosition = (m_PhysicsScene && !m_BoxBodyID.IsInvalid() && m_PhysicsScene->IsBodyValid(m_BoxBodyID)) ? m_PhysicsScene->GetBodyPosition(m_BoxBodyID) : glm::vec3(0.0f),
            .BoxVelocity = (m_PhysicsScene && !m_BoxBodyID.IsInvalid() && m_PhysicsScene->IsBodyValid(m_BoxBodyID)) ? m_PhysicsScene->GetLinearVelocity(m_BoxBodyID) : glm::vec3(0.0f),
            .IsBoxActive = (m_PhysicsScene && !m_BoxBodyID.IsInvalid() && m_PhysicsScene->IsBodyValid(m_BoxBodyID)) ? m_PhysicsScene->IsActive(m_BoxBodyID) : false,
            .SpherePosition = (m_PhysicsScene && !m_SphereBodyID.IsInvalid() && m_PhysicsScene->IsBodyValid(m_SphereBodyID)) ? m_PhysicsScene->GetBodyPosition(m_SphereBodyID) : glm::vec3(0.0f),
            .SphereVelocity = (m_PhysicsScene && !m_SphereBodyID.IsInvalid() && m_PhysicsScene->IsBodyValid(m_SphereBodyID)) ? m_PhysicsScene->GetLinearVelocity(m_SphereBodyID) : glm::vec3(0.0f),
            .IsSphereActive = (m_PhysicsScene && !m_SphereBodyID.IsInvalid() && m_PhysicsScene->IsBodyValid(m_SphereBodyID)) ? m_PhysicsScene->IsActive(m_SphereBodyID) : false,
            .TotalBodies = m_PhysicsScene ? m_PhysicsScene->GetNumBodies() : 0,
            .ActiveBodies = m_PhysicsScene ? m_PhysicsScene->GetNumActiveBodies() : 0,
            .ShowPhysicsWireframe = m_ShowPhysicsDebug,
            .DrawBoundingBoxes = m_PhysicsDrawSettings.DrawBoundingBoxes,
            .DrawVelocity = m_PhysicsDrawSettings.DrawVelocity,
            .DrawCenterOfMass = m_PhysicsDrawSettings.DrawCenterOfMass,
            .DebugLineCount = m_PhysicsDebugRenderer ? m_PhysicsDebugRenderer->GetLineCount() : 0,
            .HasVehicle = (m_Vehicle != nullptr),
            .IsChaseCamera = m_ChaseCamera
        };

        if (m_Vehicle && m_ChassisEntity && m_ChassisEntity.HasComponent<Axle::TransformComponent>()) {
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

    // 10. 3D Model Asset Ingestion Confirmation Modal
    RenderImportModal();
}

void SandboxLayer::SetupNextImportModal() {
    if (m_PendingImportQueue.empty()) {
        m_ShowImportModal = false;
        return;
    }

    m_ImportModalData.SourcePath = m_PendingImportQueue.front();
    m_PendingImportQueue.erase(m_PendingImportQueue.begin());

    if (m_ContentBrowserPanel.IsWindowHovered()) {
        m_ImportModalData.TargetFolder = m_ContentBrowserPanel.GetCurrentPath();
    } else {
        m_ImportModalData.TargetFolder = Axle::FileSystem::ResolveAssetPath("models");
    }

    std::string stem = m_ImportModalData.SourcePath.stem().string();
    std::string outName = stem + ".axle";
    strncpy_s(m_ImportModalData.OutputFileName, sizeof(m_ImportModalData.OutputFileName), outName.c_str(), _TRUNCATE);

    m_ImportModalData.SplitSubmeshes = true;
    m_ImportModalData.PatternMode = 1;
    m_ImportModalData.TileScale = 8.0f;
    m_ImportModalData.ColorPrimary = glm::vec4(0.85f, 0.88f, 0.92f, 1.0f);
    m_ImportModalData.ColorSecondary = glm::vec4(0.65f, 0.70f, 0.76f, 1.0f);
    m_ImportModalData.ErrorMessage.clear();

    m_ShowImportModal = true;
    m_OpenImportModalPopup = true;
}

void SandboxLayer::RenderImportModal() {
    if (m_OpenImportModalPopup) {
        ImGui::OpenPopup("Import 3D Asset##Modal");
        m_OpenImportModalPopup = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(540.0f, 0.0f), ImGuiCond_Appearing);

    bool modalOpen = m_ShowImportModal;
    if (ImGui::BeginPopupModal("Import 3D Asset##Modal", &modalOpen, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        m_ShowImportModal = modalOpen;

        // Detect model extension and format badge
        std::string ext = m_ImportModalData.SourcePath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return std::tolower(c); });

        const char* formatName = "3D Asset";
        ImVec4 badgeColor(0.2f, 0.6f, 1.0f, 1.0f);
        if (ext == ".obj") {
            formatName = "Wavefront OBJ (.obj)";
            badgeColor = ImVec4(0.95f, 0.6f, 0.2f, 1.0f);
        } else if (ext == ".gltf") {
            formatName = "glTF 2.0 Scene (.gltf)";
            badgeColor = ImVec4(0.2f, 0.8f, 0.4f, 1.0f);
        } else if (ext == ".glb") {
            formatName = "glTF 2.0 Binary (.glb)";
            badgeColor = ImVec4(0.2f, 0.75f, 0.9f, 1.0f);
        } else if (ext == ".fbx") {
            formatName = "Autodesk FBX (.fbx)";
            badgeColor = ImVec4(0.85f, 0.4f, 0.95f, 1.0f);
        }

        ImGui::TextColored(badgeColor, "[ %s ]", formatName);
        ImGui::SameLine();
        ImGui::TextDisabled("Model Ingestion Pipeline");
        ImGui::Separator();
        ImGui::Spacing();

        // Source details table
        if (ImGui::BeginTable("##ImportInfoTable", 2, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("Source File:");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(m_ImportModalData.SourcePath.filename().string().c_str());

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("Source Path:");
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", m_ImportModalData.SourcePath.string().c_str());

            std::error_code ec;
            auto sz = std::filesystem::file_size(m_ImportModalData.SourcePath, ec);
            if (!ec) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("File Size:");
                ImGui::TableNextColumn();
                if (sz < 1024) {
                    ImGui::Text("%llu Bytes", sz);
                } else if (sz < 1024 * 1024) {
                    ImGui::Text("%.1f KB", sz / 1024.0f);
                } else {
                    ImGui::Text("%.2f MB", sz / (1024.0f * 1024.0f));
                }
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("Target Folder:");
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", m_ImportModalData.TargetFolder.string().c_str());

            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Target filename
        ImGui::TextUnformatted("Output Asset Name (.axle):");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##OutputFileName", m_ImportModalData.OutputFileName, sizeof(m_ImportModalData.OutputFileName));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Submesh splitting / hierarchy
        ImGui::Checkbox("Split into Separate Parts / Hierarchy", &m_ImportModalData.SplitSubmeshes);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("When enabled, decomposes distinct submesh parts (doors, wheels, body) into separate .axle files organized in a dedicated folder, creating a hierarchical scene package.");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Material Presets
        ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.35f, 1.0f), "Default Material & Procedural Surface Appearance");
        ImGui::Spacing();

        ImGui::TextDisabled("Quick Palettes:");
        ImGui::SameLine();
        if (ImGui::SmallButton("Titanium Silver")) {
            m_ImportModalData.ColorPrimary = glm::vec4(0.85f, 0.88f, 0.92f, 1.0f);
            m_ImportModalData.ColorSecondary = glm::vec4(0.65f, 0.70f, 0.76f, 1.0f);
            m_ImportModalData.TileScale = 8.0f;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Slate Graphite")) {
            m_ImportModalData.ColorPrimary = glm::vec4(0.35f, 0.38f, 0.42f, 1.0f);
            m_ImportModalData.ColorSecondary = glm::vec4(0.20f, 0.22f, 0.26f, 1.0f);
            m_ImportModalData.TileScale = 8.0f;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Arctic Navy")) {
            m_ImportModalData.ColorPrimary = glm::vec4(0.22f, 0.28f, 0.38f, 1.0f);
            m_ImportModalData.ColorSecondary = glm::vec4(0.14f, 0.18f, 0.25f, 1.0f);
            m_ImportModalData.TileScale = 8.0f;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Ceramic White")) {
            m_ImportModalData.ColorPrimary = glm::vec4(0.92f, 0.92f, 0.94f, 1.0f);
            m_ImportModalData.ColorSecondary = glm::vec4(0.80f, 0.82f, 0.85f, 1.0f);
            m_ImportModalData.TileScale = 8.0f;
        }

        ImGui::ColorEdit4("Primary Color", glm::value_ptr(m_ImportModalData.ColorPrimary));
        ImGui::ColorEdit4("Secondary Color", glm::value_ptr(m_ImportModalData.ColorSecondary));

        const char* patternNames[] = {
            "0: Solid Pure",
            "1: Checkerboard",
            "2: Cyber Grid",
            "3: Warning Stripes",
            "4: Carbon Fiber",
            "5: Hexagon Dots"
        };
        ImGui::Combo("Pattern Preset", &m_ImportModalData.PatternMode, patternNames, IM_ARRAYSIZE(patternNames));
        ImGui::SliderFloat("Pattern Scale", &m_ImportModalData.TileScale, 0.5f, 32.0f, "%.1fx");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Pipeline info notice (Auto-Bake is always active under the hood, and immediate instantiate is removed as requested)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.75f, 1.0f, 1.0f));
        ImGui::TextWrapped("The model geometry will be converted into Axle's structured text format (.axle) and pre-baked into high-speed binary cache (.axlebin) inside the Content Browser.");
        ImGui::PopStyleColor();

        if (!m_ImportModalData.ErrorMessage.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", m_ImportModalData.ErrorMessage.c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Action Buttons
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.55f, 0.34f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.68f, 0.42f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14f, 0.45f, 0.28f, 1.0f));

        if (ImGui::Button("Convert & Import", ImVec2(160, 32))) {
            std::string outNameStr = m_ImportModalData.OutputFileName;
            if (outNameStr.empty()) {
                outNameStr = m_ImportModalData.SourcePath.stem().string() + ".axle";
            }
            if (outNameStr.length() < 5 || outNameStr.substr(outNameStr.length() - 5) != ".axle") {
                outNameStr += ".axle";
            }

            std::filesystem::path destAxlePath = m_ImportModalData.TargetFolder / outNameStr;

            bool success = Axle::AssetSerializer::AutoConvertModel(
                m_ImportModalData.SourcePath, destAxlePath,
                m_ImportModalData.SplitSubmeshes,
                m_ImportModalData.PatternMode,
                m_ImportModalData.TileScale,
                m_ImportModalData.ColorPrimary,
                m_ImportModalData.ColorSecondary
            );

            if (success) {
                AXLE_INFO("Asset Pipeline: Successfully converted '{0}' -> '{1}' (with binary cache)", 
                    m_ImportModalData.SourcePath.filename().string(), destAxlePath.filename().string());

                ImGui::CloseCurrentPopup();
                if (!m_PendingImportQueue.empty()) {
                    SetupNextImportModal();
                } else {
                    m_ShowImportModal = false;
                }
            } else {
                m_ImportModalData.ErrorMessage = "Conversion failed! Check console logs for error details.";
                AXLE_ERROR("Asset Pipeline: Failed to convert model '{0}'", m_ImportModalData.SourcePath.string());
            }
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(100, 32))) {
            ImGui::CloseCurrentPopup();
            m_PendingImportQueue.clear();
            m_ShowImportModal = false;
        }

        ImGui::EndPopup();
    } else {
        if (!modalOpen) {
            m_PendingImportQueue.clear();
            m_ShowImportModal = false;
        }
    }
}
