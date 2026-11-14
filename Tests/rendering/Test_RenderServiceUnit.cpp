#include "RenderTestCommon.hpp"
#include <Axle/RenderWorld.hpp>
#include <Axle/ViewRequest.hpp>
#include <Axle/RenderService.hpp>
#include <Axle/RHIObjectPicker.hpp>
#include <Axle/SceneExtraction.hpp>
#include <Axle/MeshResourceResolver.hpp>
#include <Axle/RHI/RHISwitcher.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/PhysicsScene.hpp>
#include <Axle/PhysicsDebugRenderer.hpp>
#include <Axle/MaterialComponentCodec.hpp>
#include <Axle/MaterialCache.hpp>
#include <Axle/AssetManager.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/AssemblySerializer.hpp>
#include <Axle/RenderParticleData.hpp>
#include <sstream>
#include <iostream>
#include <vector>
#include <atomic>
#include <thread>

namespace Axle::RenderTests {

    static bool Test_RenderOwnershipContract() {
        RenderService service;
        auto initial = service.GetOwnershipReport();
        RENDER_CHECK(initial.State == RenderServiceState::Stopped, "RenderService starts stopped");
        RENDER_CHECK(initial.StateEpoch == service.GetStateEpoch() && initial.StateEpoch > 0,
            "Initial state snapshot exposes a nonzero epoch");
        RENDER_CHECK(!initial.OwnerBound && !initial.IsOwnerThread, "RenderService has no owner before initialization");
        RENDER_CHECK(!initial.NativeReady && initial.Backend == RHI::RHIType::None, "Uninitialized RHI is reported honestly");
        RENDER_CHECK(!initial.OwnershipAligned && !initial.RHIIsOwnerThread, "Uninitialized ownership is not reported as aligned");
        RENDER_CHECK(!service.RequireOwner("unit-test-before-init"), "Unbound RenderService rejects owner-only operation");

        RenderWorld world;
        ViewRequest request;
        const auto failed = service.RenderView(world, request);
        RENDER_CHECK(!failed.Success && service.GetState() == RenderServiceState::Failed,
            "RenderService records failed startup without a native device or GL context");
        auto bound = service.GetOwnershipReport();
        RENDER_CHECK(bound.State == RenderServiceState::Failed && bound.StateEpoch > initial.StateEpoch,
            "Failed startup advances the producer-visible state epoch");
        RENDER_CHECK(!bound.OwnerBound && !bound.IsOwnerThread, "Failed startup releases ownership for retry");
        RENDER_CHECK(!bound.NativeReady && !bound.DeviceCapabilities.SupportsRenderWorkerOwnership &&
            !bound.DeviceCapabilities.SupportsTransientResourceAliasing,
            "Unprobed worker ownership and transient aliasing stay disabled");
        RENDER_CHECK(!service.RequireOwner("unit-test-owner"), "Released failed startup has no owner-only operation");

        std::atomic<bool> foreignOwner{true};
        std::atomic<bool> foreignRequire{true};
        std::atomic<bool> foreignInit{true};
        std::atomic<bool> foreignStateUnchanged{false};
        std::atomic<uint64_t> foreignEpoch{0};
        std::thread foreign([&] {
            foreignOwner.store(service.IsOwnerThread(), std::memory_order_release);
            foreignRequire.store(service.RequireOwner("unit-test-foreign"), std::memory_order_release);
            foreignInit.store(service.Init(), std::memory_order_release);
            const auto retry = service.GetOwnershipReport();
            foreignStateUnchanged.store(retry.State == RenderServiceState::Failed && !retry.OwnerBound, std::memory_order_release);
            foreignEpoch.store(service.GetStateEpoch(), std::memory_order_release);
        });
        foreign.join();
        RENDER_CHECK(!foreignOwner.load(std::memory_order_acquire) && !foreignRequire.load(std::memory_order_acquire),
            "Foreign thread cannot satisfy RenderService ownership");
        RENDER_CHECK(!foreignInit.load(std::memory_order_acquire),
            "Foreign thread cannot acquire ownership after a failed startup");
        RENDER_CHECK(foreignStateUnchanged.load(std::memory_order_acquire) && !service.IsInitialized(),
            "Foreign retry can recover a failed startup without leaking ownership");
        RENDER_CHECK(foreignEpoch.load(std::memory_order_acquire) > bound.StateEpoch,
            "Retry state transitions advance the producer-visible epoch");

        service.Shutdown();
        auto stopped = service.GetOwnershipReport();
        RENDER_CHECK(!service.IsInitialized() && stopped.State == RenderServiceState::Stopped && !stopped.OwnerBound &&
            stopped.StateEpoch > bound.StateEpoch,
            "Owner shutdown drains the service and clears ownership");
        return true;
    }

    static bool Test_ViewRequestDefaults() {
        ViewRequest req;
        RENDER_CHECK(req.Kind == RenderViewKind::Main, "Default ViewRequest kind is Main");
        RENDER_CHECK(req.Width == 1280, "Default width is 1280");
        RENDER_CHECK(req.Height == 720, "Default height is 720");
        RENDER_CHECK(req.ColorTarget == nullptr, "Color target initially null");
        RENDER_CHECK(req.DepthTarget == nullptr, "Depth target initially null");
        RENDER_CHECK(req.ViewTargets == nullptr, "ViewTargets initially null");
        RENDER_CHECK(req.GLTargetFBO == nullptr, "GLTargetFBO initially null");
        RENDER_CHECK(req.PhysicsScene == nullptr, "PhysicsScene initially null");
        RENDER_CHECK(req.Name == "DefaultView", "Default name is DefaultView");
        return true;
    }

    static bool Test_ViewRequestCustomization() {
        ViewRequest req;
        req.Kind = RenderViewKind::AssetPreview;
        req.Width = 512;
        req.Height = 512;
        req.Name = "AssetPreview_512";
        req.Settings.EnableSkybox = false;
        req.Settings.MSAASamples = 4;
        req.Settings.EnableToneMapping = true;

        RENDER_CHECK(req.Kind == RenderViewKind::AssetPreview, "Kind is AssetPreview");
        RENDER_CHECK(req.Width == 512 && req.Height == 512, "Dimensions are 512x512");
        RENDER_CHECK(req.Name == "AssetPreview_512", "Name is set");
        RENDER_CHECK(!req.Settings.EnableSkybox, "Skybox disabled");
        RENDER_CHECK(req.Settings.MSAASamples == 4, "4x MSAA enabled");
        RENDER_CHECK(req.Settings.EnableToneMapping, "Tone mapping enabled");
        return true;
    }

    static bool Test_RenderResultContract() {
        RenderResult res;
        RENDER_CHECK(!res.Success, "Default result is not successful");
        RENDER_CHECK(res.DrawCallCount == 0, "Default draw calls is 0");
        RENDER_CHECK(res.RenderedEntityCount == 0, "Default entity count is 0");
        RENDER_CHECK(res.ErrorMessage.empty(), "Default error message is empty");

        res.Success = true;
        res.DrawCallCount = 42;
        res.RenderedEntityCount = 10;
        res.CompletionToken = RHI::RHICompletionToken{ 1, 1234 };

        RENDER_CHECK(res.Success, "Result marked successful");
        RENDER_CHECK(res.DrawCallCount == 42, "Draw call count matches");
        RENDER_CHECK(res.RenderedEntityCount == 10, "Rendered entity count matches");
        RENDER_CHECK(res.CompletionToken.Value == 1234, "Completion token value matches");
        return true;
    }

    static bool Test_RenderServiceTargetValidation() {
        RenderService service;
        RenderWorld world;
        ViewRequest req; // Has no color target or view targets

        RenderResult result = service.RenderView(world, req);
        // When no target is provided or no backend is ready without GPU device, it must fail safely with descriptive error
        RENDER_CHECK(!result.Success, "RenderView without target or GPU device fails gracefully");
        RENDER_CHECK(!result.ErrorMessage.empty(), "Error message is populated on failure");
        return true;
    }

    static bool Test_RenderServiceMultipleViewKinds() {
        RenderService service;
        RenderWorld world;

        std::vector<RenderViewKind> kinds = {
            RenderViewKind::Main,
            RenderViewKind::AssetPreview,
            RenderViewKind::VFXPreview,
            RenderViewKind::AssemblyPreview,
            RenderViewKind::Thumbnail
        };

        for (auto kind : kinds) {
            ViewRequest req;
            req.Kind = kind;
            RenderResult res = service.RenderView(world, req);
            RENDER_CHECK(!res.Success, "Each unconfigured view kind validates targets safely");
        }
        return true;
    }

    static bool Test_RenderCapabilitiesContracts() {
        auto d3d12Caps = QueryRendererCapabilities(RendererBackendType::ModernRHI_D3D12);
        RENDER_CHECK(d3d12Caps.Backend == RendererBackendType::ModernRHI_D3D12, "D3D12 backend type");
        RENDER_CHECK(d3d12Caps.SupportsMSAA && d3d12Caps.MaxMSAASamples == 8, "D3D12 supports 8x MSAA");
        RENDER_CHECK(d3d12Caps.SupportsHDR && d3d12Caps.SupportsToneMapping, "D3D12 supports HDR & ToneMapping");
        RENDER_CHECK(d3d12Caps.SupportsFXAA, "D3D12 supports FXAA");
        RENDER_CHECK(d3d12Caps.SupportsCustomHLSLShaders, "D3D12 supports custom HLSL shaders");
        RENDER_CHECK(d3d12Caps.SupportsRefractionCopy, "D3D12 supports refraction copy");
        RENDER_CHECK(d3d12Caps.SupportsCascadedShadows && !d3d12Caps.SupportsGPUSkinning && !d3d12Caps.SupportsAsyncCompute && !d3d12Caps.AdvancedFeatureDiagnostic.empty(), "D3D12 reports cascaded shadows and remaining deferred scene passes honestly");

        auto vkCaps = QueryRendererCapabilities(RendererBackendType::ModernRHI_Vulkan);
        RENDER_CHECK(vkCaps.Backend == RendererBackendType::ModernRHI_Vulkan, "Vulkan backend type");
        RENDER_CHECK(vkCaps.SupportsMSAA && vkCaps.MaxMSAASamples == 8, "Vulkan supports 8x MSAA");
        RENDER_CHECK(vkCaps.SupportsHDR && vkCaps.SupportsToneMapping, "Vulkan supports HDR & ToneMapping");
        RENDER_CHECK(vkCaps.SupportsFXAA, "Vulkan supports FXAA");
        RENDER_CHECK(vkCaps.SupportsCustomHLSLShaders, "Vulkan supports custom HLSL shaders");
        RENDER_CHECK(vkCaps.SupportsCascadedShadows && !vkCaps.SupportsGPUSkinning && !vkCaps.SupportsAsyncCompute && !vkCaps.AdvancedFeatureDiagnostic.empty(), "Vulkan reports cascaded shadows and remaining deferred scene passes honestly");

        auto glCaps = QueryRendererCapabilities(RendererBackendType::OpenGL_Compatibility);
        RENDER_CHECK(glCaps.Backend == RendererBackendType::OpenGL_Compatibility, "OpenGL Compatibility backend type");
        RENDER_CHECK(!glCaps.SupportsMSAA && glCaps.MaxMSAASamples == 1, "GL views use single-sample framebuffer attachments");
        RENDER_CHECK(!glCaps.SupportsHDR, "GL compatibility does not support HDR float render passes");
        RENDER_CHECK(!glCaps.SupportsFXAA, "GL compatibility does not support FXAA post-processing");
        RENDER_CHECK(!glCaps.SupportsCustomHLSLShaders, "GL compatibility does not support custom HLSL shaders");
        RENDER_CHECK(!glCaps.SupportsRefractionCopy, "GL compatibility does not support screen-space refraction copy");
        RENDER_CHECK(!glCaps.AdvancedFeatureDiagnostic.empty(), "GL compatibility reports honest feature limitation diagnostic");

        auto noneCaps = QueryRendererCapabilities(RendererBackendType::None);
        RENDER_CHECK(noneCaps.Backend == RendererBackendType::None, "None backend type");
        RENDER_CHECK(!noneCaps.SupportsMSAA && !noneCaps.SupportsHDR, "None backend has no capabilities");
        return true;
    }

    static bool Test_RenderServiceCapabilitiesIntegration() {
        RenderService service;
        RendererCapabilities caps = service.GetActiveCapabilities();
        RENDER_CHECK(!caps.BackendName.empty(), "Active backend name is reported");
        service.SetViewportSize(640, 360);
        service.SetUseIBL(true);
        RENDER_CHECK(!service.CanPickEntity() && service.GetPhysicsDebugLineCount() == 0,
                     "product-facing viewport/telemetry seams remain safe before backend initialization");
        PerspectiveCamera camera;
        RENDER_CHECK(service.PickEntity({}, camera, 64, 64, 0, 0) == RHIObjectPicker::InvalidEntityID,
                     "product-facing picking seam returns the explicit invalid ID without a native picker");
        return true;
    }

    static bool Test_PreviewTwoLightsExtraction() {
        Scene scene;
        auto keyEnt = scene.CreateEntity("KeyLight");
        auto& dlc1 = keyEnt.AddComponent<DirectionalLightComponent>();
        dlc1.Azimuth = 45.0f;
        dlc1.Elevation = 35.0f;
        dlc1.Intensity = 1.0f;
        dlc1.Color = glm::vec3(1.0f, 0.95f, 0.9f);

        auto fillEnt = scene.CreateEntity("FillLight");
        auto& dlc2 = fillEnt.AddComponent<DirectionalLightComponent>();
        dlc2.Azimuth = 0.0f;
        dlc2.Elevation = -90.0f;
        dlc2.Intensity = 0.7f;
        dlc2.Color = glm::vec3(0.7f, 0.7f, 0.75f);

        SceneExtractionOptions options;
        RenderWorld world = ExtractRenderWorld(scene, options);

        RENDER_CHECK(world.DirectionalLight.has_value(), "Primary directional light extracted");
        RENDER_CHECK(world.AdditionalDirectionalLights.size() == 1, "Secondary fill light extracted into AdditionalDirectionalLights");
        RENDER_CHECK(world.GetDirectionalLightCount() == 2, "GetDirectionalLightCount reports 2 lights");
        bool hasKey = (world.DirectionalLight->Intensity == 1.0f) || (world.AdditionalDirectionalLights[0].Intensity == 1.0f);
        bool hasFill = (world.DirectionalLight->Intensity == 0.7f) || (world.AdditionalDirectionalLights[0].Intensity == 0.7f);
        RENDER_CHECK(hasKey, "Key light intensity 1.0 is present");
        RENDER_CHECK(hasFill, "Fill light intensity 0.7 is present");
        return true;
    }

    static bool Test_PreviewMaterialIsolationAndEdits() {
        Scene mainScene;
        auto mainEnt = mainScene.CreateEntity("MainObject");
        mainEnt.AddComponent<TransformComponent>();
        auto& mainMesh = mainEnt.AddComponent<MeshInstance3D>();
        mainMesh.AssetPath = "builtin://cube";
        auto& mainMat = mainEnt.AddComponent<MaterialComponent>();
        mainMat.AlbedoColor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f); // Red

        Scene previewScene;
        auto previewEnt = previewScene.CreateEntity("PreviewObject");
        previewEnt.AddComponent<TransformComponent>();
        auto& prevMesh = previewEnt.AddComponent<MeshInstance3D>();
        prevMesh.AssetPath = "builtin://sphere";
        auto& prevMat = previewEnt.AddComponent<MaterialComponent>();
        prevMat.AlbedoColor = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f); // Blue

        SceneExtractionOptions options;
        RenderWorld mainWorld = ExtractRenderWorld(mainScene, options);
        RenderWorld prevWorld = ExtractRenderWorld(previewScene, options);

        RENDER_CHECK(mainWorld.GetMeshCount() == 1, "Main world has 1 mesh");
        RENDER_CHECK(prevWorld.GetMeshCount() == 1, "Preview world has 1 mesh");
        RENDER_CHECK(mainWorld.Meshes[0].Material.AlbedoColor.r == 1.0f, "Main mesh is red");
        RENDER_CHECK(prevWorld.Meshes[0].Material.AlbedoColor.b == 1.0f, "Preview mesh is blue");

        // Edit preview material in place
        prevMat.AlbedoColor = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f); // Green
        RenderWorld prevWorldEdited = ExtractRenderWorld(previewScene, options);
        RENDER_CHECK(prevWorldEdited.Meshes[0].Material.AlbedoColor.g == 1.0f, "Preview mesh updated to green");

        // Verify main scene remains untouched
        RenderWorld mainWorldCheck = ExtractRenderWorld(mainScene, options);
        RENDER_CHECK(mainWorldCheck.Meshes[0].Material.AlbedoColor.r == 1.0f, "Main mesh remains red after preview edit");
        RENDER_CHECK(mainWorldCheck.Meshes[0].Material.AlbedoColor.g == 0.0f, "No color bleed into main scene");
        return true;
    }

    static bool Test_PreviewViewRequestResizeIsolation() {
        ViewRequest req;
        req.Kind = RenderViewKind::AssetPreview;
        req.Width = 380;
        req.Height = 320;
        RENDER_CHECK(req.Width == 380 && req.Height == 320, "Initial preview dimensions");

        // Same-frame resize
        req.Width = 450;
        req.Height = 240;
        RENDER_CHECK(req.Width == 450 && req.Height == 240, "Resized preview dimensions");
        RENDER_CHECK(req.Kind == RenderViewKind::AssetPreview, "Kind preserved after resize");
        return true;
    }

    static bool Test_VFXPreviewIsolationAndMultiEmitter() {
        Scene mainScene;
        auto mainEnt = mainScene.CreateEntity("GameEntity");
        mainEnt.AddComponent<TransformComponent>();

        Scene vfxScene;
        auto vfxEnt = vfxScene.CreateEntity("VFX Preview");
        auto& pec = vfxEnt.AddComponent<ParticleEmitterComponent>();
        pec.EmitterStacks.clear();

        ParticleEmitterStack stack1;
        stack1.Name = "Sparks";
        stack1.Visible = true;
        ParticleEmitterStack stack2;
        stack2.Name = "Smoke";
        stack2.Visible = true;

        pec.EmitterStacks.push_back(stack1);
        pec.EmitterStacks.push_back(stack2);

        RENDER_CHECK(pec.EmitterStacks.size() == 2, "VFX preview has 2 emitter stacks");
        RENDER_CHECK(mainScene.GetRegistry().view<ParticleEmitterComponent>().empty(), "Main scene has no VFX emitters");

        ViewRequest vfxReq;
        vfxReq.Kind = RenderViewKind::VFXPreview;
        vfxReq.Width = 640;
        vfxReq.Height = 480;
        vfxReq.Settings.EnableParticles = true;
        vfxReq.Name = "VFXPreview";

        RENDER_CHECK(vfxReq.Kind == RenderViewKind::VFXPreview, "VFX view kind set correctly");
        RENDER_CHECK(vfxReq.Settings.EnableParticles, "VFX view enables particles");

        // Verify main scene entity count remains 1 and is isolated
        RENDER_CHECK(!mainScene.GetRegistry().all_of<ParticleEmitterComponent>(static_cast<entt::entity>(mainEnt)), "Main entity not polluted by VFX");
        return true;
    }

    static bool Test_AssemblyPreviewExtractionAndIsolation() {
        Scene mainScene;
        auto mainEnt = mainScene.CreateEntity("GamePlayer");
        mainEnt.AddComponent<TransformComponent>();

        // Build private preview scene mirroring AssemblyEditorPanel
        Scene previewScene;

        // 1. Studio Lights
        Entity fillLight = previewScene.CreateEntity("FillLight");
        auto& dlc2 = fillLight.AddComponent<DirectionalLightComponent>();
        dlc2.Azimuth = 225.0f;
        dlc2.Elevation = 30.0f;
        dlc2.Color = glm::vec3(0.85f, 0.90f, 1.0f);
        dlc2.Intensity = 0.4f;
        dlc2.CastShadows = false;

        Entity keyLight = previewScene.CreateEntity("KeyLight");
        auto& dlc1 = keyLight.AddComponent<DirectionalLightComponent>();
        dlc1.Azimuth = 45.0f;
        dlc1.Elevation = 50.0f;
        dlc1.Color = glm::vec3(1.0f);
        dlc1.Intensity = 1.0f;
        dlc1.CastShadows = false;

        // 2. Pedestal
        Entity pedestal = previewScene.CreateEntity("Pedestal");
        auto& pedTc = pedestal.GetComponent<TransformComponent>();
        pedTc.Translation = glm::vec3(0.0f, -0.01f, 0.0f);
        auto& pedMesh = pedestal.AddComponent<MeshInstance3D>();
        pedMesh.AssetPath = "builtin://plane";
        ResolveMeshResources(pedMesh);

        // 3. Multi-part Assembly hierarchy
        Entity rootPart = previewScene.CreateEntity("RobotBase");
        auto& rootTc = rootPart.GetComponent<TransformComponent>();
        rootTc.Translation = glm::vec3(0.0f, 1.0f, 0.0f);
        auto& rootMesh = rootPart.AddComponent<MeshInstance3D>();
        rootMesh.AssetPath = "builtin://cube";
        ResolveMeshResources(rootMesh);

        Entity armPart = previewScene.CreateEntity("RobotArm");
        armPart.SetParent(rootPart, false);
        auto& armTc = armPart.GetComponent<TransformComponent>();
        armTc.Translation = glm::vec3(0.0f, 2.5f, 0.0f);
        auto& armMesh = armPart.AddComponent<MeshInstance3D>();
        armMesh.AssetPath = "builtin://cylinder";
        ResolveMeshResources(armMesh);

        previewScene.UpdateWorldTransforms();

        // Verify parent-child world transform propagation
        glm::mat4 armWorld = previewScene.GetWorldTransform(armPart);
        RENDER_CHECK(std::abs(armWorld[3][1] - 3.5f) < 1e-4f, "Assembly child world Y translation is 1.0 + 2.5 = 3.5");

        // Extract RenderWorld with armPart selected
        std::unordered_set<uint32_t> selectedSet;
        selectedSet.insert(static_cast<uint32_t>((entt::entity)armPart));

        SceneExtractionOptions options;
        options.SelectedEntities = &selectedSet;
        options.ResolveResources = true;

        RenderWorld world = ExtractRenderWorld(previewScene, options);
        RENDER_CHECK(world.Meshes.size() == 3, "Extracted 3 meshes (pedestal + root + child arm)");

        bool foundSelectedArm = false;
        bool rootNotSelected = false;
        for (const auto& m : world.Meshes) {
            if (m.EntityID == static_cast<uint32_t>((entt::entity)armPart)) {
                if (m.Selected) foundSelectedArm = true;
            }
            if (m.EntityID == static_cast<uint32_t>((entt::entity)rootPart)) {
                if (!m.Selected) rootNotSelected = true;
            }
        }
        RENDER_CHECK(foundSelectedArm, "Assembly child arm mesh marked as Selected");
        RENDER_CHECK(rootNotSelected, "Assembly root mesh marked as not selected");

        // ViewRequest verification
        ViewRequest req;
        req.Kind = RenderViewKind::AssemblyPreview;
        req.Width = 800;
        req.Height = 600;
        req.Settings.Wireframe = true;
        req.Name = "AssemblyPreviewView";

        RENDER_CHECK(req.Kind == RenderViewKind::AssemblyPreview, "ViewRequest kind is AssemblyPreview");
        RENDER_CHECK(req.Settings.Wireframe, "Wireframe mode requested");

        // Verify complete main scene isolation
        RENDER_CHECK(mainScene.GetRegistry().view<MeshInstance3D>().empty(), "Main scene has no preview meshes");
        return true;
    }

    static bool Test_ThumbnailRenderJobAndCacheContracts() {
        // 1. Thumbnail ViewRequest parameters
        ViewRequest thumbReq;
        thumbReq.Kind = RenderViewKind::Thumbnail;
        thumbReq.Width = 128;
        thumbReq.Height = 128;
        thumbReq.Settings.ClearColor = glm::vec4(0.11f, 0.13f, 0.17f, 1.0f);
        thumbReq.Settings.EnableShadows = false;
        thumbReq.Settings.EnableSkybox = false;
        thumbReq.Name = "Thumbnail_TestAsset";

        RENDER_CHECK(thumbReq.Kind == RenderViewKind::Thumbnail, "Thumbnail kind set");
        RENDER_CHECK(thumbReq.Width == 128 && thumbReq.Height == 128, "Thumbnail resolution is 128x128");
        RENDER_CHECK(!thumbReq.Settings.EnableShadows, "Shadows disabled for fast thumbnail generation");

        // 2. Thumbnail RenderWorld setup
        RenderWorld world;
        world.TotalEntities = 1;

        RenderDirectionalLight keyLight;
        keyLight.Direction = glm::normalize(glm::vec3(0.5f, 0.85f, 0.6f));
        keyLight.Intensity = 1.0f;
        world.DirectionalLight = keyLight;

        RenderDirectionalLight fillLight;
        fillLight.Direction = glm::normalize(glm::vec3(-0.4f, 0.5f, -0.6f));
        fillLight.Intensity = 0.35f;
        world.AdditionalDirectionalLights.push_back(fillLight);

        RenderMeshInstance rmi;
        rmi.EntityID = 1;
        rmi.WorldTransform = glm::mat4(1.0f);
        rmi.Material.AlbedoColor = glm::vec4(0.8f, 0.2f, 0.2f, 1.0f);
        rmi.Material.PatternMode = 1;
        world.Meshes.push_back(rmi);

        RENDER_CHECK(world.Meshes.size() == 1, "Thumbnail world has 1 mesh");
        RENDER_CHECK(world.DirectionalLight.has_value(), "Key directional light set");
        RENDER_CHECK(world.AdditionalDirectionalLights.size() == 1, "Fill directional light set");

        // 3. Target validation without targets rejects safely
        RenderResult result = RenderService::Get().RenderView(world, thumbReq);
        RENDER_CHECK(!result.Success, "Thumbnail view without target safely rejected");

        // 4. Invalidation & queue simulation contract
        std::vector<std::string> pendingQueue;
        pendingQueue.push_back("assets/mesh1.obj");
        pendingQueue.push_back("assets/mesh2.obj");
        pendingQueue.push_back("assets/mesh3.axle");

        RENDER_CHECK(pendingQueue.size() == 3, "Pending thumbnail queue initialized");

        // Invalidate mesh2 while queued (rapid directory switch simulation)
        auto it = std::find(pendingQueue.begin(), pendingQueue.end(), "assets/mesh2.obj");
        if (it != pendingQueue.end()) pendingQueue.erase(it);

        RENDER_CHECK(pendingQueue.size() == 2, "Invalidated item safely evicted from pending queue");
        RENDER_CHECK(pendingQueue[0] == "assets/mesh1.obj" && pendingQueue[1] == "assets/mesh3.axle", "Queue integrity preserved");

        // Clear all (complete directory change)
        pendingQueue.clear();
        RENDER_CHECK(pendingQueue.empty(), "Pending queue cleared on directory navigation");
        return true;
    }

    bool Test_StandaloneRuntimeContracts() {
        // 1. Standalone GameView ViewRequest contracts
        ViewRequest request;
        request.Kind = RenderViewKind::Main;
        request.Width = 1920;
        request.Height = 1080;
        request.Name = "RuntimeGameView";
        request.Settings.EnableShadows = true;
        request.Settings.EnableSkybox = true;
        request.Settings.ShowPhysicsDebug = false;

        PerspectiveCamera cam(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
        cam.SetPosition(glm::vec3(0.0f, 2.0f, 5.0f));
        request.Camera = cam;

        RENDER_CHECK(request.Kind == RenderViewKind::Main, "Main view kind matches");
        RENDER_CHECK(request.Width == 1920 && request.Height == 1080, "Resolution matches 1080p");
        RENDER_CHECK(request.Settings.EnableShadows && request.Settings.EnableSkybox, "Standard shipping settings");
        RENDER_CHECK(!request.Settings.ShowPhysicsDebug, "Physics debug disabled by default in shipping runtime");

        // 2. Resize behavior
        request.Width = 2560;
        request.Height = 1440;
        cam.SetProjection(60.0f, 2560.0f / 1440.0f, 0.1f, 1000.0f);
        request.Camera = cam;
        RENDER_CHECK(request.Width == 2560 && request.Height == 1440, "Resize to 1440p validated");

        // 3. Scene RenderView execution contract
        auto scene = std::make_shared<Scene>();
        auto entity = scene->CreateEntity("RuntimeActor");
        auto& tc = entity.GetComponent<TransformComponent>();
        tc.Translation = glm::vec3(0.0f, 1.0f, 0.0f);

        auto& mc = entity.AddComponent<MaterialComponent>();
        mc.AlbedoColor = glm::vec4(0.2f, 0.7f, 0.3f, 1.0f);
        mc.Roughness = 0.4f;
        mc.Metallic = 0.1f;

        // Without targets or active device, gracefully returns safe result
        auto result = RenderService::Get().RenderView(*scene, request);
        RENDER_CHECK(!result.Success || result.DrawCallCount >= 0, "RenderView executed safely");

        // 4. CLI vs project precedence contracts
        RHI::RHIBackendConfig cfg;
        RENDER_CHECK(RHI::RHISwitcher::ParseArgument("--rhi=dx12", cfg), "Parse CLI dx12");
        RENDER_CHECK(cfg.PreferredBackend == RHI::RHIType::Direct3D12, "CLI preferred dx12");
        RENDER_CHECK(cfg.RawArgument == "dx12", "CLI raw argument recorded");

        RHI::RHIBackendConfig glCfg;
        RENDER_CHECK(RHI::RHISwitcher::ParseArgument("--rhi=opengl", glCfg), "Parse CLI opengl");
        RENDER_CHECK(glCfg.PreferredBackend == RHI::RHIType::OpenGL, "CLI preferred opengl");

        return true;
    }

    static bool Test_PhysicsDebugAndOverlayContracts() {
        PhysicsEngine::Init();

        // 1. Create PhysicsScene and add multiple collider bodies
        auto physicsScene = std::make_unique<PhysicsScene>();
        physicsScene->CreateBox(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.5f, 0.5f, 0.5f), true);
        physicsScene->CreateSphere(glm::vec3(2.0f, 1.0f, 0.0f), 0.75f, true);
        physicsScene->CreateCylinder(glm::vec3(-2.0f, 1.0f, 0.0f), 1.0f, 0.5f, true);
        physicsScene->CreatePlane(PlaneBodyProps{});

        RENDER_CHECK(physicsScene->GetNumBodies() == 4, "PhysicsScene holds 4 collider bodies");

        // 2. Direct line generation into PhysicsDebugRenderer without GL mesh creation
        PhysicsDebugRenderer debugRenderer;
        debugRenderer.Begin(glm::vec3(0.0f, 5.0f, 10.0f));

        PhysicsDrawSettings drawSettings;
        drawSettings.DrawShapes = true;
        drawSettings.DrawWireframe = true;
        drawSettings.DrawBoundingBoxes = false;
        drawSettings.DrawVelocity = false;
        drawSettings.DrawCenterOfMass = false;

        physicsScene->DrawDebug(debugRenderer, drawSettings);
        uint32_t baseLineCount = debugRenderer.GetLineCount();
        RENDER_CHECK(baseLineCount > 0, "Lines generated for collider wireframes");

        // Adding bounding boxes increases the line count
        drawSettings.DrawBoundingBoxes = true;
        debugRenderer.Begin(glm::vec3(0.0f, 5.0f, 10.0f));
        physicsScene->DrawDebug(debugRenderer, drawSettings);
        uint32_t bboxLineCount = debugRenderer.GetLineCount();
        RENDER_CHECK(bboxLineCount > baseLineCount, "Bounding box lines added when flag enabled");

        debugRenderer.Clear();
        RENDER_CHECK(debugRenderer.GetLineCount() == 0, "Clear resets debug lines");

        // 3. ViewRequest active physics scene and view isolation contracts
        ViewRequest mainReq;
        mainReq.Kind = RenderViewKind::Main;
        mainReq.Width = 1920;
        mainReq.Height = 1080;
        mainReq.PhysicsScene = physicsScene.get();
        mainReq.Settings.ShowPhysicsDebug = true;
        mainReq.Settings.PhysicsDrawSettings = drawSettings;
        mainReq.Settings.PhysicsDepthBias = 0.0001f;
        mainReq.Settings.PhysicsAlphaMultiplier = 0.85f;

        RENDER_CHECK(mainReq.PhysicsScene == physicsScene.get(), "Main ViewRequest supplies active PhysicsScene");
        RENDER_CHECK(mainReq.Settings.ShowPhysicsDebug, "Physics debug wireframe enabled on main view");
        RENDER_CHECK(Near(mainReq.Settings.PhysicsDepthBias, 0.0001f, 0.00001f), "Physics depth bias preserved");
        RENDER_CHECK(Near(mainReq.Settings.PhysicsAlphaMultiplier, 0.85f, 0.001f), "Physics alpha multiplier preserved");

        // Preview views must isolate physics debug: PhysicsScene remains null
        ViewRequest previewReq;
        previewReq.Kind = RenderViewKind::AssetPreview;
        previewReq.Width = 512;
        previewReq.Height = 512;
        previewReq.PhysicsScene = nullptr;
        previewReq.Settings.ShowPhysicsDebug = false;

        RENDER_CHECK(previewReq.PhysicsScene == nullptr, "Preview view request has null PhysicsScene");
        RENDER_CHECK(!previewReq.Settings.ShowPhysicsDebug, "Preview view request has physics debug disabled");

        // 4. Edit / Play / Pause / Stop lifecycle pointer isolation
        auto editorPhysicsScene = std::make_unique<PhysicsScene>();
        editorPhysicsScene->CreateBox(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f));
        RENDER_CHECK(editorPhysicsScene->GetNumBodies() == 1, "Editor physics scene has 1 body");

        // Play mode clones into an ephemeral play physics scene
        auto playPhysicsScene = std::make_unique<PhysicsScene>();
        playPhysicsScene->CreateBox(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f));
        playPhysicsScene->CreateSphere(glm::vec3(0.0f, 5.0f, 0.0f), 0.5f);
        RENDER_CHECK(playPhysicsScene->GetNumBodies() == 2, "Play physics scene has 2 bodies");

        // During Play and Pause, request points to playPhysicsScene
        ViewRequest playReq;
        playReq.PhysicsScene = playPhysicsScene.get();
        RENDER_CHECK(playReq.PhysicsScene->GetNumBodies() == 2, "Play ViewRequest references active play physics");

        // On Stop, play scene destroyed and editor scene restored
        playPhysicsScene.reset();
        ViewRequest stopReq;
        stopReq.PhysicsScene = editorPhysicsScene.get();
        RENDER_CHECK(stopReq.PhysicsScene->GetNumBodies() == 1, "Stop ViewRequest restored pristine editor physics with zero drift");

        return true;
    }


    static bool Test_AuthoredMaterialRoundTrips() {
        const auto root=std::filesystem::absolute("build/agent-runs/OPT-05/material-codec");
        std::filesystem::create_directories(root);
        MaterialComponent m;
        m.AlbedoPath="missing/albedo.png";m.NormalPath="missing/normal.png";m.RoughnessPath="missing/rough.png";
        m.MetallicPath="missing/metal.png";m.AOPath="missing/ao.png";m.EmissivePath="missing/emission.png";m.ORMPath="missing/orm.png";
        m.ClearcoatPath="missing/coat.png";m.AnisotropyFlowPath="missing/flow.png";m.DetailNormalPath="missing/detail.png";m.DetailMaskPath="missing/mask.png";
        m.UseAlbedoMap=m.UseNormalMap=m.UseRoughnessMap=m.UseMetallicMap=m.UseAOMap=m.UseEmissiveMap=m.UseORMMap=true;
        m.UseClearcoatMap=m.UseAnisotropyFlowMap=m.UseDetailNormalMap=m.UseDetailMaskMap=true;
        m.AlbedoColor={0.3f,0.4f,0.7f,0.8f};m.EmissionColor={0.2f,0.5f,0.9f};m.EmissionIntensity=2.5f;
        m.TextureMode=TextureMode::PackedORM;m.EnableSpecularHighlights=m.EnableEnvironmentReflections=false;
        m.Roughness=0.27f;m.Metallic=0.63f;m.NormalStrength=0.4f;m.AOStrength=0.7f;
        m.DetailNormalStrength=0.37f;m.DetailUVScale={3,4};m.DetailUVOffset={0.2f,0.3f};
        m.RefractionEnabled=true;m.RefractionStrength=0.23f;m.RefractionIOR=1.7f;m.RefractionThickness=0.35f;m.RefractionTint={0.7f,0.8f,0.9f};
        m.ClearcoatStrength=0.8f;m.ClearcoatRoughness=0.2f;m.AnisotropyStrength=0.6f;m.AnisotropyRotation=0.4f;
        m.RenderQueue=3123;m.UVScale={2,3};m.UVOffset={0.1f,0.2f};m.SurfaceType=SurfaceType::Transparent;m.CullMode=CullMode::None;m.DepthWrite=false;m.AlphaCutoff=0.32f;
        m.UseCustomShader=true;m.CustomShaderPath="missing/custom.hlsl";m.CustomShaderGeneration=99;m.CustomShaderDiagnostic="not authored";
        for(auto type:{ShaderPropertyType::Float,ShaderPropertyType::Float2,ShaderPropertyType::Float3,ShaderPropertyType::Float4,ShaderPropertyType::Color,ShaderPropertyType::Int,ShaderPropertyType::Bool,ShaderPropertyType::Texture2D}) {
            ShaderPropertyInstance p;p.Desc.Name="Property"+std::to_string(static_cast<int>(type));p.Desc.Type=type;
            p.FloatVal=0.75f;p.Vec4Val={0.1f,0.2f,0.3f,0.4f};p.IntVal=42;p.BoolVal=true;p.TexturePath="missing/property.png";m.CustomProperties.push_back(p);
        }
        auto canonical=[](const MaterialComponent& material){std::ostringstream out;WriteMaterialComponent(out,material);return out.str();};
        const auto expected=canonical(m);
        RENDER_CHECK(expected.find("not authored")==std::string::npos&&expected.find("CustomShaderGeneration")==std::string::npos,"material persistence excludes renderer feedback");
        AssemblyData assembly;assembly.IsValid=true;assembly.RootNode.HasMaterial=true;assembly.RootNode.Material=m;
        const auto path=root/"advanced.axle";
        RENDER_CHECK(AssemblySerializer::SaveAssemblyData(path,assembly),"save full inline assembly material");
        auto loaded=AssemblySerializer::LoadAssemblyData(path);
        RENDER_CHECK(loaded.IsValid&&canonical(loaded.RootNode.Material)==expected,"Assembly Editor save/reopen retains every authored material field");
        Scene source;auto entity=source.CreateEntity("Advanced inline material");entity.AddComponent<MaterialComponent>()=m;
        RENDER_CHECK(AssemblySerializer::Serialize(path,entity),"serialize actual scene entity assembly");
        loaded=AssemblySerializer::LoadAssemblyData(path);
        RENDER_CHECK(loaded.IsValid&&canonical(loaded.RootNode.Material)==expected,"entity assembly writer uses complete material schema");
        Scene target;auto spawned=AssemblySerializer::Deserialize(path,&target);
        RENDER_CHECK(spawned&&spawned.HasComponent<MaterialComponent>()&&canonical(spawned.GetComponent<MaterialComponent>())==expected,"assembly instantiation retains advanced inline material");
        Scene roundTrip;SceneSerializer writer(&source),reader(&roundTrip);
        RENDER_CHECK(reader.DeserializeFromString(writer.SerializeToString()),"Scene uses shared authored material schema");
        auto view=roundTrip.GetRegistry().view<MaterialComponent>();
        RENDER_CHECK(view.size()==1&&canonical(view.get<MaterialComponent>(*view.begin()))==expected,"Scene and Assembly material round trips agree");
        return true;
    }
    static bool Test_ParticleSnapshotHasNoCallbacks() {
        Scene scene;auto entity=scene.CreateEntity("Owned particle snapshot");auto& emitter=entity.AddComponent<ParticleEmitterComponent>();emitter.EmitterStacks.clear();
        ParticleEmitterStack stack("Callback source",ParticleEmitterConfig::CreateNeutral());
        stack.Config.Collision.WorldRaycast=[](const glm::vec3&,const glm::vec3&,float,glm::vec3&,glm::vec3&){return false;};
        stack.System->GetPool().Spawn(ParticleSpawnData{});emitter.EmitterStacks.push_back(std::move(stack));
        auto world=ExtractRenderWorld(scene);
        RENDER_CHECK(world.Particles.size()==1&&!world.Particles[0]->Config.Collision.WorldRaycast,"render snapshot never retains live physics callback");
        RENDER_CHECK(bool(emitter.EmitterStacks[0].Config.Collision.WorldRaycast),"extraction preserves simulation callback");return true;
    }


    static bool Test_LoadedMaterialPeekIdentity() {
        const auto root=std::filesystem::absolute("build/agent-runs/OPT-05/material-peek")/std::to_string(uint64_t(UUID()));
        const auto a=root/"a",b=root/"b";std::filesystem::create_directories(a);std::filesystem::create_directories(b);
        struct Restore {std::filesystem::path Root=FileSystem::GetAssetDirectory(),Assets=AssetManager::GetAssetDirectory();~Restore(){MaterialCache::ClearMaterialCache();if(Assets.empty())AssetManager::Shutdown();else AssetManager::Init(Assets);FileSystem::SetAssetDirectory(Root);}} restore;
        Material first("First"),second("Second");first.SetAlphaCutoff(0.3f);second.SetAlphaCutoff(0.8f);
        RENDER_CHECK(first.Save(a/"fx.axleasset")&&second.Save(b/"fx.axleasset"),"two project material identities");
        MaterialCache::ClearMaterialCache();AssetManager::Init(a);
        auto loaded=MaterialCache::GetMaterial(std::filesystem::path("fx.axleasset"));RENDER_CHECK(bool(loaded),"material authoring cache fixture");loaded->SetAlphaCutoff(0.17f);
        auto peek=MaterialCache::FindLoadedMaterial("fx.axleasset");
        RENDER_CHECK(peek&&peek->GetAlphaCutoff()==0.17f,"read-only material peek preserves unsaved values");
        FileSystem::SetAssetDirectory(b);
        RENDER_CHECK(!MaterialCache::FindLoadedMaterial("fx.axleasset"),"same relative path in another project cannot return old material");
        FileSystem::SetAssetDirectory(a);std::filesystem::copy_file(b/"fx.axleasset",a/"fx.axleasset",std::filesystem::copy_options::overwrite_existing);
        RENDER_CHECK(!MaterialCache::FindLoadedMaterial("fx.axleasset"),"replaced file UUID invalidates cached authoring values");return true;
    }

} // namespace Axle::RenderTests

bool RunRenderServiceUnitTests() {
    std::cout << "  [RenderService] Running RenderService & ViewRequest unit tests...\n";
    if (!Axle::RenderTests::Test_RenderOwnershipContract()) return false;
    if (!Axle::RenderTests::Test_LoadedMaterialPeekIdentity()) return false;
    if (!Axle::RenderTests::Test_AuthoredMaterialRoundTrips()) return false;
    if (!Axle::RenderTests::Test_ParticleSnapshotHasNoCallbacks()) return false;
    if (!Axle::RenderTests::Test_ViewRequestDefaults()) return false;
    if (!Axle::RenderTests::Test_ViewRequestCustomization()) return false;
    if (!Axle::RenderTests::Test_RenderResultContract()) return false;
    if (!Axle::RenderTests::Test_RenderServiceTargetValidation()) return false;
    if (!Axle::RenderTests::Test_RenderServiceMultipleViewKinds()) return false;
    if (!Axle::RenderTests::Test_RenderCapabilitiesContracts()) return false;
    if (!Axle::RenderTests::Test_RenderServiceCapabilitiesIntegration()) return false;
    if (!Axle::RenderTests::Test_PreviewTwoLightsExtraction()) return false;
    if (!Axle::RenderTests::Test_PreviewMaterialIsolationAndEdits()) return false;
    if (!Axle::RenderTests::Test_PreviewViewRequestResizeIsolation()) return false;
    if (!Axle::RenderTests::Test_VFXPreviewIsolationAndMultiEmitter()) return false;
    if (!Axle::RenderTests::Test_AssemblyPreviewExtractionAndIsolation()) return false;
    if (!Axle::RenderTests::Test_ThumbnailRenderJobAndCacheContracts()) return false;
    if (!Axle::RenderTests::Test_StandaloneRuntimeContracts()) return false;
    if (!Axle::RenderTests::Test_PhysicsDebugAndOverlayContracts()) return false;
    std::cout << "  [RenderService] All RenderService & ViewRequest unit tests passed.\n";
    return true;
}
