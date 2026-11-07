#include "RenderTestCommon.hpp"
#include <Axle/RenderWorld.hpp>
#include <Axle/ViewRequest.hpp>
#include <Axle/RenderService.hpp>
#include <iostream>
#include <vector>

namespace Axle::RenderTests {

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
        RENDER_CHECK(d3d12Caps.AdvancedFeatureDiagnostic.empty(), "D3D12 has no feature limitation diagnostic");

        auto vkCaps = QueryRendererCapabilities(RendererBackendType::ModernRHI_Vulkan);
        RENDER_CHECK(vkCaps.Backend == RendererBackendType::ModernRHI_Vulkan, "Vulkan backend type");
        RENDER_CHECK(vkCaps.SupportsMSAA && vkCaps.MaxMSAASamples == 8, "Vulkan supports 8x MSAA");
        RENDER_CHECK(vkCaps.SupportsHDR && vkCaps.SupportsToneMapping, "Vulkan supports HDR & ToneMapping");
        RENDER_CHECK(vkCaps.SupportsFXAA, "Vulkan supports FXAA");
        RENDER_CHECK(vkCaps.SupportsCustomHLSLShaders, "Vulkan supports custom HLSL shaders");
        RENDER_CHECK(vkCaps.AdvancedFeatureDiagnostic.empty(), "Vulkan has no feature limitation diagnostic");

        auto glCaps = QueryRendererCapabilities(RendererBackendType::OpenGL_Compatibility);
        RENDER_CHECK(glCaps.Backend == RendererBackendType::OpenGL_Compatibility, "OpenGL Compatibility backend type");
        RENDER_CHECK(glCaps.SupportsMSAA && glCaps.MaxMSAASamples == 4, "GL supports 4x MSAA");
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

} // namespace Axle::RenderTests

bool RunRenderServiceUnitTests() {
    std::cout << "  [RenderService] Running RenderService & ViewRequest unit tests...\n";
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
    std::cout << "  [RenderService] All RenderService & ViewRequest unit tests passed.\n";
    return true;
}
