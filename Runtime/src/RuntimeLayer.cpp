#include "RuntimeLayer.hpp"
#include <Axle/Application.hpp>
#include <Axle/Package.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/PrimitiveMesh.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/Components.hpp>
#include <Axle/RenderCommand.hpp>
#include <GLFW/glfw3.h>

namespace Axle {

    RuntimeLayer::RuntimeLayer(const std::filesystem::path& projectPath)
        : Layer("RuntimeLayer"), m_ProjectPath(projectPath), m_Camera(45.0f, 16.0f / 9.0f, 0.1f, 1000.0f) {
    }

    void RuntimeLayer::OnAttach() {
        AXLE_INFO("RuntimeLayer attached - Initializing Axle Standalone Game Player");

        auto& window = Application::Get().GetWindow();
        m_WindowWidth = window.GetWidth();
        m_WindowHeight = window.GetHeight();

        // 1. Sync GLFW input context and initialize gameplay scripts module
        ScriptHotReloadManager::Get().SyncInputContext(static_cast<GLFWwindow*>(window.GetNativeWindow()));
        ScriptHotReloadManager::Get().Initialize("AxleScripts.dll");

        // 2. Initialize Physics Scene
        m_PhysicsScene = std::make_unique<PhysicsScene>();

        // 3. Initialize Decoupled Scene Renderer (Direct Screen Backbuffer)
        m_SceneRenderer = std::make_unique<SceneRenderer>();
        m_SceneRenderer->SetViewportSize(m_WindowWidth, m_WindowHeight);

        // 4. Initial Perspective Camera
        const float aspect = (m_WindowHeight > 0) ? (static_cast<float>(m_WindowWidth) / static_cast<float>(m_WindowHeight)) : (16.0f / 9.0f);
        m_Camera.SetProjection(45.0f, aspect, 0.1f, 1000.0f);
        m_Camera.SetPosition(glm::vec3(0.0f, 6.0f, 18.0f));
        m_Camera.SetRotation(glm::vec3(-12.0f, 0.0f, 0.0f));

        // 5. Load Project and Startup Scene
        LoadProjectAndStartScene();
    }

    void RuntimeLayer::LoadProjectAndStartScene() {
        // Step 0: Auto-mount monolithic asset package if available (e.g. Data.axlepak)
        static const std::vector<std::filesystem::path> packageCandidates = {
            "Data.axlepak",
            "Game.axlepak",
            "assets.axlepak",
            "Export/Data.axlepak"
        };
        for (const auto& pkg : packageCandidates) {
            if (std::filesystem::exists(pkg)) {
                if (FileSystem::MountPackage(pkg)) {
                    AXLE_INFO("Runtime: Mounted asset package '{0}' ({1} assets indexed)",
                        pkg.string(), FileSystem::GetMountedPackage()->GetEntryCount());
                    break;
                }
            }
        }

        // Step A: Locate project configuration
        std::filesystem::path projPath = m_ProjectPath;
        if (projPath.empty()) {
            if (FileSystem::Exists("Sandbox.axleproj")) {
                projPath = "Sandbox.axleproj";
            } else if (FileSystem::Exists("Project.axleproj")) {
                projPath = "Project.axleproj";
            } else {
                auto resolved = FileSystem::ResolveAssetPath("Sandbox.axleproj");
                if (FileSystem::Exists(resolved)) projPath = resolved;
            }
        }

        std::shared_ptr<Project> project = nullptr;
        if (!projPath.empty() && FileSystem::Exists(projPath)) {
            project = Project::Load(projPath);
            if (project) {
                AXLE_INFO("Runtime: Loaded project from '{0}'", projPath.string());
            }
        }

        // Step B: Locate start scene
        std::filesystem::path startScenePath;
        if (project) {
            startScenePath = Project::GetAssetFileSystemPath(project->GetConfig().StartScene);
            if (startScenePath.empty() || !FileSystem::Exists(startScenePath)) {
                if (FileSystem::Exists(project->GetConfig().StartScene)) {
                    startScenePath = project->GetConfig().StartScene;
                }
            }
        }

        if (startScenePath.empty() || !FileSystem::Exists(startScenePath)) {
            std::vector<std::filesystem::path> candidateScenes = {
                "scenes/CarTest.axlescene",
                "scenes/VehicleSandbox.axlescene",
                "scenes/SampleScene.axlescene",
                FileSystem::ResolveAssetPath("scenes/CarTest.axlescene"),
                FileSystem::ResolveAssetPath("scenes/VehicleSandbox.axlescene"),
                FileSystem::ResolveAssetPath("scenes/SampleScene.axlescene"),
                "assets/scenes/CarTest.axlescene",
                "assets/scenes/VehicleSandbox.axlescene",
                "assets/scenes/SampleScene.axlescene"
            };

            for (const auto& cand : candidateScenes) {
                if (FileSystem::Exists(cand)) {
                    startScenePath = cand;
                    break;
                }
            }

            // Fallback: search directory for first .axlescene file
            if (startScenePath.empty() || !FileSystem::Exists(startScenePath)) {
                auto scenesDir = FileSystem::ResolveAssetPath("scenes");
                if (std::filesystem::exists(scenesDir)) {
                    for (const auto& entry : std::filesystem::directory_iterator(scenesDir)) {
                        if (entry.path().extension() == ".axlescene") {
                            startScenePath = entry.path();
                            break;
                        }
                    }
                }
            }
        }

        m_Scene = std::make_shared<Scene>();

        // Step C: Deserialize scene if found
        bool sceneLoaded = false;
        if (!startScenePath.empty() && FileSystem::Exists(startScenePath)) {
            SceneSerializer serializer(m_Scene.get(), m_PhysicsScene.get());
            SceneSerializerContext ctx;
            ctx.CubeMesh = PrimitiveMesh::CreateCube();
            ctx.SphereMesh = PrimitiveMesh::CreateSphere(24, 32);
            ctx.CylinderMesh = PrimitiveMesh::CreateCylinder(32);
            ctx.ConeMesh = PrimitiveMesh::CreateCone(32);
            ctx.PlaneMesh = PrimitiveMesh::CreatePlane();
            ctx.ScriptBinder = [](Entity entity, const std::string& scriptName) {
                if (!entity.HasComponent<NativeScriptComponent>()) {
                    entity.AddComponent<NativeScriptComponent>().Bind(scriptName);
                } else {
                    entity.GetComponent<NativeScriptComponent>().Bind(scriptName);
                }
            };
            serializer.SetContext(ctx);
            if (serializer.Deserialize(startScenePath)) {
                sceneLoaded = true;
                Application::Get().GetWindow().SetTitle("Axle Game - " + startScenePath.filename().stem().string());
                AXLE_INFO("Runtime: Successfully loaded startup scene '{0}'", startScenePath.string());
            }
        }

        // Step D: Default fallback scene if no file could be loaded
        if (!sceneLoaded) {
            AXLE_WARN("Runtime: No startup scene found. Generating baseline simulation scene.");
            Application::Get().GetWindow().SetTitle("Axle Game - Standalone Player");

            auto sunEntity = m_Scene->CreateEntity("Directional Light");
            auto& tc = sunEntity.AddComponent<TransformComponent>();
            tc.Translation = glm::vec3(0.0f, 10.0f, 0.0f);
            tc.Rotation = glm::quat(glm::vec3(glm::radians(50.0f), glm::radians(-30.0f), 0.0f));
            sunEntity.AddComponent<DirectionalLightComponent>();

            auto skyEntity = m_Scene->CreateEntity("Skybox Environment");
            skyEntity.AddComponent<SkyboxComponent>();

            auto camEntity = m_Scene->CreateEntity("Main Camera");
            auto& camTc = camEntity.AddComponent<TransformComponent>();
            camTc.Translation = glm::vec3(0.0f, 6.0f, 18.0f);
            camTc.Rotation = glm::quat(glm::vec3(glm::radians(-12.0f), 0.0f, 0.0f));
            camEntity.AddComponent<CameraComponent>(45.0f, 0.1f, 1000.0f, true);
            camEntity.AddComponent<NativeScriptComponent>("VehicleChaseCamera");

            // Static Ground Floor
            auto floorEntity = m_Scene->CreateEntity("Ground Floor");
            auto& floorTc = floorEntity.AddComponent<TransformComponent>(glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(100.0f, 1.0f, 100.0f));
            floorEntity.AddComponent<MeshInstance3D>(PrimitiveMesh::CreatePlane(), "", 0, 8.0f, glm::vec4(0.3f, 0.32f, 0.35f, 1.0f), glm::vec4(0.2f, 0.22f, 0.25f, 1.0f));
            floorEntity.AddComponent<RigidBody3D>(BodyID{}, BodyMotionType::Static);
            floorEntity.AddComponent<CollisionShape3D>(CollisionShapeType::Box, glm::vec3(50.0f, 0.5f, 50.0f));
        }

        // Step E: Start simulation runtime
        m_Scene->OnRuntimeStart(m_PhysicsScene.get());
        AXLE_INFO("Runtime: Simulation lifecycle started successfully.");
    }

    void RuntimeLayer::OnUpdate(Timestep ts) {
        // 1. Hotkey Controls
        if (Input::IsKeyPressed(KeyCode::F11)) {
            if (!m_F11KeyWasPressed) {
                auto& win = Application::Get().GetWindow();
                if (win.IsMaximized()) {
                    win.Restore();
                } else {
                    win.Maximize();
                }
                m_F11KeyWasPressed = true;
            }
        } else {
            m_F11KeyWasPressed = false;
        }

        if (Input::IsKeyPressed(KeyCode::Escape)) {
            if (!m_EscapeKeyWasPressed) {
                m_IsPaused = !m_IsPaused;
                AXLE_INFO("Runtime: Simulation {0}", m_IsPaused ? "PAUSED" : "RESUMED");
                m_EscapeKeyWasPressed = true;
            }
        } else {
            m_EscapeKeyWasPressed = false;
        }

        // 2. Physics Simulation Step & Script Lifecycle
        if (!m_IsPaused && m_PhysicsScene && m_Scene) {
            m_PhysicsScene->Step(ts);
            m_Scene->OnUpdateRuntime(ts, m_PhysicsScene.get());
        }

        // 3. Resolve Primary Game Camera
        PerspectiveCamera activeCamera = m_Camera;
        if (m_Scene) {
            Entity camEntity = m_Scene->GetPrimaryCameraEntity();
            if (camEntity && camEntity.HasComponent<CameraComponent>()) {
                const auto& camComp = camEntity.GetComponent<CameraComponent>();
                glm::mat4 worldMat = m_Scene->GetWorldTransform((entt::entity)camEntity);
                const float aspect = (m_WindowHeight > 0) ? (static_cast<float>(m_WindowWidth) / static_cast<float>(m_WindowHeight)) : (16.0f / 9.0f);
                activeCamera.SetProjection(camComp.FOV, aspect, camComp.NearClip, camComp.FarClip);
                activeCamera.SetWorldTransform(worldMat);
            }
        }

        // 4. Render directly to screen backbuffer (targetFBO == nullptr)
        if (m_SceneRenderer && m_Scene) {
            SceneRenderSettings settings;
            settings.EnableShadows = true;
            settings.EnableSkybox = true;
            settings.ShowPhysicsDebug = false;

            m_SceneRenderer->RenderScene(m_Scene, activeCamera, nullptr, m_PhysicsScene.get(), nullptr, settings);
        }
    }

    void RuntimeLayer::OnWindowResize(u32 width, u32 height) {
        if (width == 0 || height == 0) return;
        m_WindowWidth = width;
        m_WindowHeight = height;

        if (m_SceneRenderer) {
            m_SceneRenderer->SetViewportSize(width, height);
        }

        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        m_Camera.SetProjection(45.0f, aspect, 0.1f, 1000.0f);
    }

    void RuntimeLayer::OnDetach() {
        AXLE_INFO("RuntimeLayer detached - Shutting down Game Player");
        if (m_Scene) {
            m_Scene->OnRuntimeStop(m_PhysicsScene.get());
        }
        m_Scene.reset();
        m_PhysicsScene.reset();
        m_SceneRenderer.reset();
        ScriptHotReloadManager::Get().Shutdown();
    }

}
