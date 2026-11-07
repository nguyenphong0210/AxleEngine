#include <Axle/AssetManager.hpp>
#include "RuntimeLayer.hpp"
#include <Axle/Application.hpp>
#include <Axle/Package.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/PrimitiveMesh.hpp>
#include <Axle/AssetSerializer.hpp>
#include <Axle/Components.hpp>
#include <Axle/ScriptableEntity.hpp>
#include <stdexcept>
#include <Axle/RenderCommand.hpp>
#include <Axle/AudioEngine.hpp>
#include <AxleSpline/SplineComponent.hpp>
#include <AxleSpline/TrackGenerator.hpp>
#include <AxleSpline/SplineSerializer.hpp>
#include <AxleTerrain/TerrainComponent.hpp>
#include <AxleTerrain/TerrainMesh.hpp>
#include <AxleTerrain/TerrainCollider.hpp>
#include <AxleTerrain/TerrainSerializer.hpp>
#include <GLFW/glfw3.h>
#include <algorithm>

namespace Axle {

    RuntimeLayer::RuntimeLayer(const std::filesystem::path& projectPath)
        : Layer("RuntimeLayer"), m_ProjectPath(projectPath), m_Camera(45.0f, 16.0f / 9.0f, 0.1f, 1000.0f) {
    }

    void RuntimeLayer::OnAttach() {
        AXLE_INFO("RuntimeLayer attached - Initializing Axle Standalone Game Player (Zero-ImGui Shipping Runtime)");

        // Initialize Audio Subsystem
        AudioEngine::Init();

        auto& window = Application::Get().GetWindow();
        m_WindowWidth = window.GetWidth();
        m_WindowHeight = window.GetHeight();

        // 1. Sync GLFW input context and initialize gameplay scripts module
        ScriptHotReloadManager::Get().SyncInputContext(static_cast<GLFWwindow*>(window.GetNativeWindow()));
        ScriptHotReloadManager::Get().Initialize("AxleScripts.dll");
        ScriptHotReloadManager::Get().SyncImGuiContext(nullptr);

        // 2. Initialize Hardware 2D Canvas for In-Game UI & HUD
        m_Canvas.Init();

        // 3. Initialize Physics Scene
        m_PhysicsScene = std::make_unique<PhysicsScene>();

        // 4. Initialize unified RenderService
        if (!RenderService::Get().Init()) {
            AXLE_WARN("Runtime: RenderService initialized with fallback/partial backend");
        }

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

        AssetManager::Init(FileSystem::GetAssetDirectory());

        // Step B: Locate start scene
        std::filesystem::path startScenePath;
        if (project && !project->GetConfig().StartScene.empty()) {
            startScenePath = Project::GetAssetFileSystemPath(project->GetConfig().StartScene);
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
            ctx.CustomEntitySerializer = [](Entity entity, std::ostream& out) {
                Spline::SplineSerializer::Serialize(entity, out);
                Terrain::TerrainSerializer::Serialize(entity, out);
            };
            ctx.CustomSectionDeserializer = [](Entity entity, const std::string& sectionName, const std::vector<std::string>& lines) -> bool {
                if (Spline::SplineSerializer::Deserialize(entity, sectionName, lines)) return true;
                if (Terrain::TerrainSerializer::Deserialize(entity, sectionName, lines)) return true;
                return false;
            };
            serializer.SetContext(ctx);
            if (serializer.Deserialize(startScenePath)) {
                sceneLoaded = true;

                // Rebuild Track & Terrain runtime meshes/colliders for loaded scene
                auto splineView = m_Scene->GetRegistry().view<Spline::SplineComponent>();
                for (auto e : splineView) {
                    auto& sc = splineView.get<Spline::SplineComponent>(e);
                    auto mesh = Spline::TrackGenerator::GenerateTrackMesh(sc.Curve, sc);
                    sc.TrackVAO = Spline::TrackGenerator::BuildVAO(mesh);
                    sc.VertexCount = static_cast<u32>(mesh.GetVertexCount());
                    sc.TriangleCount = static_cast<u32>(mesh.GetTriangleCount());
                    sc.Dirty = false;
                    Entity ent{ e, m_Scene.get() };
                    if (!ent.HasComponent<MeshInstance3D>()) {
                        ent.AddComponent<MeshInstance3D>(sc.TrackVAO, 1, 4.0f, glm::vec4(0.2f, 0.2f, 0.2f, 1.0f), glm::vec4(0.3f, 0.3f, 0.3f, 1.0f));
                    } else {
                        ent.GetComponent<MeshInstance3D>().VAO = sc.TrackVAO;
                    }
                    if (m_PhysicsScene && sc.GeneratePhysics) {
                        sc.RuntimeBodyID = Spline::TrackGenerator::CreatePhysicsCollider(mesh, sc, *m_PhysicsScene);
                    }
                }

                auto terrainView = m_Scene->GetRegistry().view<Terrain::TerrainComponent>();
                for (auto e : terrainView) {
                    auto& tc = terrainView.get<Terrain::TerrainComponent>(e);
                    tc.CombinedVAO = Terrain::TerrainMesh::BuildCombinedVAO(tc.Data);
                    Entity ent{ e, m_Scene.get() };
                    if (!ent.HasComponent<MeshInstance3D>()) {
                        ent.AddComponent<MeshInstance3D>(tc.CombinedVAO, 0, 8.0f, glm::vec4(0.35f, 0.55f, 0.25f, 1.0f), glm::vec4(0.45f, 0.35f, 0.2f, 1.0f));
                    } else {
                        ent.GetComponent<MeshInstance3D>().VAO = tc.CombinedVAO;
                    }
                    if (m_PhysicsScene && tc.GeneratePhysics) {
                        tc.PhysicsBodyID = Terrain::TerrainCollider::CreateTerrainCollider(tc.Data, *m_PhysicsScene, tc.Friction, tc.Restitution);
                    }
                }

                Application::Get().GetWindow().SetTitle("Axle Game - " + startScenePath.filename().stem().string());
                AXLE_INFO("Runtime: Successfully loaded startup scene '{0}'", startScenePath.string());
            }
        }

        if (!sceneLoaded) {
            AXLE_INFO("Runtime: No authored startup scene loaded; keeping an empty scene.");
            Application::Get().GetWindow().SetTitle("Axle Game - Standalone Player");
        }

        // Step E: Start simulation runtime
        m_SimulationDriver.Reset();
        m_SimulationDriver.Resume();
        m_Scene->OnRuntimeStart(m_PhysicsScene.get());
        AXLE_INFO("Runtime: Simulation lifecycle started successfully.");
    }

    void RuntimeLayer::OnUpdate(Timestep ts) {
        // 0. Update Audio Subsystem
        AudioEngine::Update();

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
                m_SimulationDriver.SetPaused(m_IsPaused);
                AXLE_INFO("Runtime: Simulation {0}", m_IsPaused ? "PAUSED" : "RESUMED");
                if (m_IsPaused) {
                    Input::SetCursorMode(CursorMode::Normal);
                }
                m_EscapeKeyWasPressed = true;
            }
        } else {
            m_EscapeKeyWasPressed = false;
        }

        // 2. Shared Simulation Driver Execution (EJ-14)
        m_SimulationDriver.SetPaused(m_IsPaused);
        if (m_Scene) {
            m_SimulationDriver.AdvanceFrame(ts.GetSeconds(), *m_Scene, m_PhysicsScene.get());
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

        // 4. Render Scene via unified RenderService
        if (m_Scene) {
            SceneRenderSettings settings;
            settings.EnableShadows = true;
            settings.EnableSkybox = true;
            settings.ShowPhysicsDebug = false;

            ViewRequest request;
            request.Kind = RenderViewKind::Main;
            request.Width = m_WindowWidth;
            request.Height = m_WindowHeight;
            request.Camera = activeCamera;
            request.Settings = settings;
            request.PhysicsScene = m_PhysicsScene.get();
            request.GLTargetFBO = nullptr;
            request.Name = "RuntimeGameView";

            RenderService::Get().RenderView(*m_Scene, request);
        }

        // 5. Render In-Game UI & HUD via Hardware Canvas2D (Zero-ImGui Shipping Runtime)
        RenderUI();
    }

    void RuntimeLayer::SetupPauseMenu() {
        if (!m_Scene) return;

        auto& uiCtx = m_Scene->GetUIDataContext();
        uiCtx.Set("Game.IsPaused", m_IsPaused);

        uiCtx.RegisterAction("OnResumeGame", [this]() {
            m_IsPaused = false;
            m_SimulationDriver.Resume();
            Input::SetCursorMode(CursorMode::Normal);
            AXLE_INFO("Runtime: Game resumed via UI button");
        });

        uiCtx.RegisterAction("OnQuitGame", []() {
            AXLE_INFO("Runtime: Quit requested via UI button");
            Application::Get().Close();
        });
    }

    void RuntimeLayer::RenderUI() {
        if (!m_Scene) return;

        auto& uiCtx = m_Scene->GetUIDataContext();
        uiCtx.Set("Game.IsPaused", m_IsPaused);

        const glm::vec2 viewportSize(static_cast<float>(m_WindowWidth), static_cast<float>(m_WindowHeight));
        if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) return;

        // Process mouse input for interactive UI (e.g. Pause Menu)
        auto [mx, my] = Input::GetMousePosition();
        bool isDown = Input::IsMouseButtonPressed(MouseCode::Button0);
        bool isClicked = isDown && !m_PrevMouseDown;
        m_PrevMouseDown = isDown;

        // Collect and sort canvas components
        auto canvasView = m_Scene->GetRegistry().view<UICanvasComponent>();
        struct CanvasItem {
            int32_t SortOrder;
            UICanvasComponent* Comp;
            entt::entity EntityHandle;
        };
        std::vector<CanvasItem> sortedCanvases;
        for (auto ent : canvasView) {
            auto& comp = canvasView.get<UICanvasComponent>(ent);
            if (comp.Visible && !comp.AssetPath.empty()) {
                sortedCanvases.push_back({ comp.SortOrder, &comp, ent });
            }
        }
        std::sort(sortedCanvases.begin(), sortedCanvases.end(), [](const CanvasItem& a, const CanvasItem& b) {
            return a.SortOrder < b.SortOrder;
        });

        // Reverse iterate for hit testing (topmost first)
        if (m_IsPaused) {
            for (auto it = sortedCanvases.rbegin(); it != sortedCanvases.rend(); ++it) {
                auto* comp = it->Comp;
                if (comp->Document) {
                    if (comp->Document->ProcessInput(glm::vec2(mx, my), isDown, isClicked, uiCtx)) {
                        break;
                    }
                }
            }
        }

        m_Canvas.BeginCanvas(viewportSize);

        for (auto& item : sortedCanvases) {
            auto* comp = item.Comp;
            if (!comp->Document) {
                comp->Document = std::make_shared<UIDocument>();
                comp->Document->LoadFromFile(comp->AssetPath);
            } else {
                comp->Document->CheckAndHotReload();
            }

            Axle::Entity canvasEntity{ item.EntityHandle, m_Scene.get() };
            Axle::Entity targetEntity = comp->GetDataSourceEntity(m_Scene.get(), canvasEntity);

            Axle::UIDataContext scopedCtx(&uiCtx);
            if (targetEntity) {
                if (targetEntity.HasComponent<Axle::NativeScriptComponent>()) {
                    auto& nsc = targetEntity.GetComponent<Axle::NativeScriptComponent>();
                    if (nsc.FloatFields.empty() && nsc.IntFields.empty() && !nsc.ScriptName.empty()) {
                        nsc.Bind(nsc.ScriptName);
                    }
                    nsc.SyncFieldsFromInstance();
                    for (const auto& [name, val] : nsc.FloatFields) scopedCtx.SetFloat(name, val);
                    for (const auto& [name, val] : nsc.IntFields) scopedCtx.SetInt(name, val);
                    for (const auto& [name, val] : nsc.BoolFields) scopedCtx.SetBool(name, val);
                    for (const auto& [name, val] : nsc.StringFields) scopedCtx.SetString(name, val);
                }
            }

            comp->Document->EvaluateBindings(scopedCtx);
            comp->Document->UpdateLayout(viewportSize);
            comp->Document->Render(m_Canvas);
        }

        m_Canvas.EndCanvas();
    }

    void RuntimeLayer::OnWindowResize(u32 width, u32 height) {
        if (width == 0 || height == 0) return;
        m_WindowWidth = width;
        m_WindowHeight = height;

        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        m_Camera.SetProjection(45.0f, aspect, 0.1f, 1000.0f);
    }

    void RuntimeLayer::OnDetach() {
        JobSystem::WaitForAll();
        AXLE_INFO("RuntimeLayer detached - Shutting down Game Player");
        Input::SetCursorMode(CursorMode::Normal);
        m_SimulationDriver.Reset();
        m_Canvas.Shutdown();
        if (m_Scene) {
            m_Scene->OnRuntimeStop(m_PhysicsScene.get());
        }
        m_Scene.reset();
        m_PhysicsScene.reset();
        ScriptHotReloadManager::Get().Shutdown();
        AudioEngine::Shutdown();
    }

}

namespace Axle {
void RuntimeLayer::VerifySimulationContract() {
    struct Probe final : Axle::ScriptableEntity {
        int Frames = 0, Ticks = 0, Late = 0, Held = 0;
        void OnUpdate(Axle::Timestep) override { ++Frames; }
        void OnFixedUpdate(Axle::Timestep) override {
            ++Ticks;
            const bool held = Axle::Input::IsKeyPressed(Axle::KeyCode::W);
            Held += held;
            auto& body = GetComponent<Axle::RigidBody3D>();
            GetEntity().GetScene()->GetPhysicsScene()->SetLinearVelocity(body.RuntimeBodyID, glm::vec3(held ? 2.0f : 0.0f, 0, 0));
        }
        void OnLateUpdate(Axle::Timestep) override { ++Late; }
    };
    const auto require = [](bool condition) { if (!condition) throw std::runtime_error("Product simulation contract failed"); };
    const auto* previousInput = Axle::Input::GetInputContext();
    Axle::InputContext hardware = *previousInput;
    struct RestoreInput {
        const Axle::InputContext* Previous;
        ~RestoreInput() { Axle::Input::SetInputContext(Previous); Axle::ScriptHotReloadManager::Get().SyncInputContext(); }
    } restoreInput{previousInput};
    hardware.IsKeyPressedFn = [](Axle::KeyCode key) { return key == Axle::KeyCode::W; };
    hardware.IsMouseButtonPressedFn = [](Axle::MouseCode) { return false; };
    Axle::Input::SetInputContext(&hardware);
    for (int rate : {30, 60, 144}) {
        m_Scene->OnRuntimeStop(m_PhysicsScene.get());
        m_Scene->Clear();
        m_PhysicsScene = std::make_unique<PhysicsScene>();
        m_Scene = std::make_shared<Scene>();
        auto actor = m_Scene->CreateEntity("SimulationAcceptance");
        actor.GetComponent<TransformComponent>().Translation.y = 20.0f;
        actor.AddComponent<CollisionShape3D>();
        actor.AddComponent<RigidBody3D>().GravityFactor = 0.0f;
        actor.AddComponent<NativeScriptComponent>().Bind<Probe>();
        m_Scene->OnRuntimeStart(m_PhysicsScene.get());
        m_SimulationDriver.Reset();
        m_IsPaused = false;
        auto* instance = static_cast<Probe*>(actor.GetComponent<NativeScriptComponent>().Instance);
        require(instance != nullptr);
        for (int frame = 0; frame < rate; ++frame) OnUpdate(1.0f / rate);
        const float x = actor.GetWorldTransform()[3].x;
        require(m_SimulationDriver.GetClock().GetDiagnostics().TotalTicksExecuted == 60);
        require(instance->Ticks == 60 && instance->Held == 60 && instance->Frames == rate && instance->Late == rate);
        require(std::abs(x - 2.0f) < 0.01f);
        m_IsPaused = true;
        OnUpdate(1.0f);
        require(instance->Ticks == 60);
        m_SimulationDriver.RequestSingleStep();
        OnUpdate(0.0f);
        require(instance->Ticks == 61 && m_IsPaused);
        m_IsPaused = false;
        m_SimulationDriver.SetSpeed(0.0f);
        const auto framesBeforeZero = instance->Frames;
        OnUpdate(1.0f);
        require(instance->Ticks == 61 && instance->Frames == framesBeforeZero);
        m_SimulationDriver.SetSpeed(1.0f);
        AXLE_INFO("PRODUCT_SIM_PASS Runtime rate={0} ticks=60 held=60 x={1}", rate, x);
    }
    m_Scene->Clear();
    Axle::Input::SetInputContext(previousInput);
}
}
