#include "RenderTestCommon.hpp"
#include <Axle/Components.hpp>
#include <Axle/DDGIShaderConstants.hpp>
#include <Axle/GIService.hpp>
#include <Axle/RenderBackendPolicy.hpp>
#include <Axle/MaterialSurfaceShader.hpp>
#include <Axle/RenderContracts.hpp>
#include <Axle/RHI/RHIShader.hpp>
#include <Axle/RenderWorld.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <Axle/RHI/RHIViewTargets.hpp>
#include <Axle/Scene.hpp>
#include <Axle/SceneExtraction.hpp>
#include <Axle/SceneRenderSettings.hpp>
#include <Axle/SceneSerializer.hpp>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace Axle;

#ifndef AXLE_DDGI_POLICY_DOC_PATH
#define AXLE_DDGI_POLICY_DOC_PATH "docs/milestones/MILESTONE_15_LIGHTING_MATERIALS/DDGI_ACQUISITION_POLICY.md"
#endif

#ifndef AXLE_RHI_VIEW_TARGETS_SOURCE_PATH
#define AXLE_RHI_VIEW_TARGETS_SOURCE_PATH "Engine/RHI/src/RHIViewTargets.cpp"
#endif

#ifndef AXLE_RHI_VIEWPORT_DESCRIPTOR_SOURCE_PATH
#define AXLE_RHI_VIEWPORT_DESCRIPTOR_SOURCE_PATH "Engine/UI/src/RHIViewportDescriptor.cpp"
#endif

#ifndef AXLE_SCENE_RENDERER_SOURCE_PATH
#define AXLE_SCENE_RENDERER_SOURCE_PATH "Engine/Scene/src/RHISceneRenderer.cpp"
#endif

#ifndef AXLE_RENDER_SERVICE_SOURCE_PATH
#define AXLE_RENDER_SERVICE_SOURCE_PATH "Engine/Scene/src/RenderService.cpp"
#endif

#ifndef AXLE_RENDER_CONTRACTS_SOURCE_PATH
#define AXLE_RENDER_CONTRACTS_SOURCE_PATH "Engine/Renderer/src/RenderContracts.cpp"
#endif

#ifndef AXLE_ENTITY_INSPECTOR_SOURCE_PATH
#define AXLE_ENTITY_INSPECTOR_SOURCE_PATH "Engine/UI/src/EntityInspector.cpp"
#endif

#ifndef AXLE_EDITOR_LAYER_SOURCE_PATH
#define AXLE_EDITOR_LAYER_SOURCE_PATH "Editor/src/EditorLayer.cpp"
#endif

#ifndef AXLE_EDITOR_LAYER_HEADER_PATH
#define AXLE_EDITOR_LAYER_HEADER_PATH "Editor/src/EditorLayer.hpp"
#endif

#ifndef AXLE_DDGI_SHADER_PATH
#define AXLE_DDGI_SHADER_PATH "Engine/Resources/Shaders/DDGI.hlsl"
#endif

#ifndef AXLE_DDGI_COMMON_SHADER_PATH
#define AXLE_DDGI_COMMON_SHADER_PATH "Engine/Resources/Shaders/DDGICommon.hlsli"
#endif

#ifndef AXLE_DDGI_PASS_SOURCE_PATH
#define AXLE_DDGI_PASS_SOURCE_PATH "Engine/Scene/src/Passes/DDGIPass.cpp"
#endif

#ifndef AXLE_FORWARD_PBR_SHADER_PATH
#define AXLE_FORWARD_PBR_SHADER_PATH "Engine/Resources/Shaders/ForwardPBR.hlsl"
#endif

#ifndef AXLE_MATERIAL_SURFACE_SHADER_PATH
#define AXLE_MATERIAL_SURFACE_SHADER_PATH "Engine/Resources/Shaders/MaterialSurface.hlsl"
#endif

#ifndef AXLE_SCENE_PASSES_SOURCE_PATH
#define AXLE_SCENE_PASSES_SOURCE_PATH "Engine/Scene/src/RHISceneRendererPasses.cpp"
#endif

namespace {
    std::string ReadText(const char* path) {
        std::ifstream file(path);
        if (!file.is_open()) return {};
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }
}

bool RunM15DDGIContractUnitTests() {
    std::cout << "[M15DDGIContract] Running Milestone 15 Unit 7 Batch A (DDGI Design Gate & Contracts) Tests...\n";

    // 1. M15-U7-01.1: Formal acquisition policy, shader constant alignment & coordinate bijections
    {
        const std::string policyDoc = ReadText(AXLE_DDGI_POLICY_DOC_PATH);
        RENDER_CHECK(!policyDoc.empty() &&
                     policyDoc.find("Amortized Multi-Face Tile-Atlas Raster Probe Capture") != std::string::npos &&
                     policyDoc.find("BLAS/TLAS") != std::string::npos &&
                     policyDoc.find("Irradiance Atlas") != std::string::npos &&
                     policyDoc.find("Distance & Visibility Moments Atlas") != std::string::npos,
            "M15-U7-01.1: DDGI acquisition policy freezes raster probe capture + compute update and forbids M16 RT / camera-copy hacks");

        static_assert(sizeof(DDGIContract::DDGIShaderConstants) == 256,
            "DDGIShaderConstants must match 256-byte GPU constant buffer layout");
        DDGIContract::DDGIShaderConstants cb;
        RENDER_CHECK(cb.Flags.x == 0.0f && cb.Flags.y == 0.0f &&
                     DDGIContract::IrradianceTileSize == 8u &&
                     DDGIContract::DistanceTileSize == 16u &&
                     DDGIContract::MaxProbeCount == 16384u,
            "M15-U7-01.1: DDGIShaderConstants defaults to disabled with matching atlas tile constants");

        const glm::ivec3 dims(5, 3, 7);
        const uint32_t total = static_cast<uint32_t>(dims.x * dims.y * dims.z);
        RenderDDGIProbeGrid grid;
        grid.Enabled = true;
        grid.GridDimensions = dims;
        grid.GridOrigin = glm::vec3(-4.0f, 1.0f, 2.0f);
        grid.GridSpacing = glm::vec3(1.5f, 2.0f, 0.75f);

        std::set<uint32_t> seen;
        for (uint32_t i = 0; i < total; ++i) {
            const glm::ivec3 coord = DDGIContract::ProbeIndexToCoord(i, dims);
            const uint32_t roundTrip = DDGIContract::ProbeCoordToIndex(coord, dims);
            RENDER_CHECK(roundTrip == i &&
                         coord.x >= 0 && coord.x < dims.x &&
                         coord.y >= 0 && coord.y < dims.y &&
                         coord.z >= 0 && coord.z < dims.z,
                "M15-U7-02: probe index <-> coordinate conversion is bijective");
            seen.insert(roundTrip);
            const glm::vec3 worldPos = DDGIContract::ProbeIndexToWorldPos(i, grid);
            const glm::vec3 expectedPos = grid.GridOrigin + glm::vec3(coord) * grid.GridSpacing;
            RENDER_CHECK(Axle::RenderTests::Near(worldPos.x, expectedPos.x, 1e-6f) &&
                         Axle::RenderTests::Near(worldPos.y, expectedPos.y, 1e-6f) &&
                         Axle::RenderTests::Near(worldPos.z, expectedPos.z, 1e-6f),
                "M15-U7-02: probe world position matches grid origin + coord * spacing");
        }
        RENDER_CHECK(seen.size() == total, "M15-U7-02: probe coordinate bijection covers all probes uniquely");

        for (uint32_t maxProbes : {1u, 4u, 8u, 16u, DDGIContract::HardMaxProbesPerFrame}) {
            uint32_t cols = 0, rows = 0, width = 0, height = 0;
            DDGIContract::ComputeCaptureScratchLayout(maxProbes, cols, rows, width, height);
            RENDER_CHECK(width == cols * DDGIContract::CaptureFaceResolution &&
                         height == rows * DDGIContract::CaptureFaceResolution &&
                         cols * rows >= maxProbes * DDGIContract::FacesPerProbe,
                "M15-U7-01.1: capture scratch layout fits all 6 faces per staggered probe");

            std::set<std::pair<int, int>> usedTiles;
            for (uint32_t p = 0; p < maxProbes; ++p) {
                for (uint32_t f = 0; f < DDGIContract::FacesPerProbe; ++f) {
                    const glm::ivec2 tile = DDGIContract::CaptureSlotToTile(p, f, cols);
                    RENDER_CHECK(tile.x >= 0 && tile.x < static_cast<int>(cols) &&
                                 tile.y >= 0 && tile.y < static_cast<int>(rows) &&
                                 usedTiles.emplace(tile.x, tile.y).second,
                        "M15-U7-01.1: capture scratch tiles are non-overlapping across probe slots and cube faces");
                }
            }
        }
    }

    // 2. M15-U7-01.2: Staggered update budget clamping & bounded VRAM calculation
    {
        // Reference 32x16x32 = 16,384 probes: naive 25% = 4,096 probes must clamp to <= MaxProbesPerFrame!
        const uint32_t bigProbes = 32u * 16u * 32u;
        RENDER_CHECK(DDGIContract::ComputeStaggeredUpdateCount(bigProbes, 0.25f, 8u, false) == 8u &&
                     DDGIContract::ComputeStaggeredUpdateCount(bigProbes, 0.25f, 8u, true) == 8u &&
                     DDGIContract::ComputeStaggeredUpdateCount(bigProbes, 1.0f, 64u, true) == DDGIContract::HardMaxProbesPerFrame,
            "M15-U7-01.2: staggered probe count clamps large grids cleanly to MaxProbesPerFrame / HardMaxProbesPerFrame");

        // Small 4x2x4 = 32 probe grid: steady-state 25% = 8, reset seed <= cap warms all 32 probes
        RENDER_CHECK(DDGIContract::ComputeStaggeredUpdateCount(32u, 0.25f, 32u, false) == 8u &&
                     DDGIContract::ComputeStaggeredUpdateCount(32u, 0.25f, 32u, true) == 32u &&
                     DDGIContract::ComputeStaggeredUpdateCount(10u, 0.25f, 8u, false) == 3u,
            "M15-U7-01.2: staggered probe count supports steady-state rotation and bounded seed warm-up");

        const auto bigBudget = DDGIContract::EstimateMemoryBudget({32, 16, 32}, DDGIContract::HardMaxProbesPerFrame, false);
        RENDER_CHECK(bigBudget.Extent.Valid &&
                     bigBudget.Extent.ProbeCount == 16384u &&
                     bigBudget.Extent.AtlasCols == 128u && bigBudget.Extent.AtlasRows == 128u &&
                     bigBudget.Extent.IrradianceWidth == 1024u && bigBudget.Extent.IrradianceHeight == 1024u &&
                     bigBudget.Extent.DistanceWidth == 2048u && bigBudget.Extent.DistanceHeight == 2048u &&
                     bigBudget.IrradianceBytes == 1024ull * 1024ull * 8ull &&
                     bigBudget.DistanceBytes == 2048ull * 2048ull * 8ull &&
                     (bigBudget.IrradianceBytes + bigBudget.DistanceBytes) == 40ull * 1024ull * 1024ull &&
                     (bigBudget.IrradianceBytes + bigBudget.DistanceBytes) <= DDGIContract::MaxAtlasBudgetBytes &&
                     bigBudget.CaptureWidth == 384u && bigBudget.CaptureHeight == 128u &&
                     bigBudget.CaptureScratchBytes == 384ull * 128ull * 12ull,
            "M15-U7-01.2: 32x16x32 atlas memory budget matches 8 MiB irradiance + 32 MiB distance + 576 KiB capture scratch");
    }

    // 3. M15-U7-02: Dual atlas contracts, grid validation, extraction & serialization round-trip
    {
        // The product of these three dimensions is 2^64 + 4.
        constexpr glm::ivec3 overflowingGrid{968973220, 49477, 384773};
        RENDER_CHECK(!DDGIContract::ComputeAtlasExtent({0, 4, 4}).Valid &&
                     !DDGIContract::ComputeAtlasExtent({-2, 4, 4}).Valid &&
                     !DDGIContract::ComputeAtlasExtent({33, 16, 32}).Valid &&
                     !DDGIContract::ComputeAtlasExtent(overflowingGrid).Valid,
            "M15-U7-02: invalid or oversized probe grids fail atlas extent validation");

        const auto rhiSmall = RHI::RHIViewTargets::ComputeDDGIAtlasExtent(4u, 2u, 4u);
        const auto cSmall = DDGIContract::ComputeAtlasExtent({4, 2, 4});
        RENDER_CHECK(rhiSmall.Valid && cSmall.Valid &&
                     rhiSmall.ProbeCount == 32u &&
                     rhiSmall.IrradianceWidth == cSmall.IrradianceWidth &&
                     rhiSmall.IrradianceHeight == cSmall.IrradianceHeight &&
                     rhiSmall.DistanceWidth == cSmall.DistanceWidth &&
                     rhiSmall.DistanceHeight == cSmall.DistanceHeight &&
                     rhiSmall.IrradianceWidth == rhiSmall.AtlasCols * RHI::RHIViewTargets::DDGIIrradianceTileSize &&
                     rhiSmall.DistanceWidth == rhiSmall.AtlasCols * RHI::RHIViewTargets::DDGIDistanceTileSize,
            "M15-U7-02: RHIViewTargets and DDGIContract produce identical 8x8 irradiance & 16x16 distance extents");

        RenderDDGIProbeGrid grid;
        grid.Enabled = true;
        std::string reason;
        RENDER_CHECK(DDGIContract::ValidateGrid(grid, &reason) && reason.empty(),
            "M15-U7-02: default RenderDDGIProbeGrid validates cleanly");

        auto checkBad = [&](auto mutator, const char* label) -> bool {
            auto bad = grid;
            mutator(bad);
            std::string why;
            RENDER_CHECK(!DDGIContract::ValidateGrid(bad, &why) && !why.empty(), label);
            return true;
        };
        if (!checkBad([](auto& g) { g.GridDimensions = {33, 16, 32}; }, "M15-U7-02: oversize dimensions fail validation")) return false;
        if (!checkBad([](auto& g) { g.GridSpacing.y = 0.0f; }, "M15-U7-02: non-positive spacing fails validation")) return false;
        if (!checkBad([](auto& g) { g.EnergyMultiplier = -0.1f; }, "M15-U7-02: negative energy fails validation")) return false;
        if (!checkBad([](auto& g) { g.Hysteresis = 1.0f; }, "M15-U7-02: hysteresis == 1.0 fails validation")) return false;
        if (!checkBad([](auto& g) { g.ViewBias = -0.01f; }, "M15-U7-02: negative view bias fails validation")) return false;
        if (!checkBad([](auto& g) { g.NormalBias = std::numeric_limits<float>::quiet_NaN(); }, "M15-U7-02: non-finite normal bias fails validation")) return false;
        if (!checkBad([](auto& g) { g.UpdateFraction = 0.0f; }, "M15-U7-02: zero update fraction fails validation")) return false;
        if (!checkBad([](auto& g) { g.MaxProbesPerFrame = 0; }, "M15-U7-02: zero max probes per frame fails validation")) return false;

        Scene scene;
        auto ent = scene.CreateEntity("DDGIProbeVolume");
        auto& xform = ent.GetComponent<TransformComponent>();
        xform.Translation = glm::vec3(1.0f, 2.0f, 3.0f);
        scene.UpdateWorldTransforms();
        auto& comp = ent.AddComponent<DDGIProbeGridComponent>();
        comp.Enabled = true;
        comp.GridDimensions = glm::ivec3(6, 4, 8);
        comp.GridSpacing = glm::vec3(1.25f, 1.5f, 2.0f);
        comp.GridOrigin = glm::vec3(-2.0f, 0.5f, -3.0f);
        comp.EnergyMultiplier = 1.4f;
        comp.UpdateFraction = 0.5f;
        comp.MaxProbesPerFrame = 12;
        comp.Hysteresis = 0.85f;
        comp.ViewBias = 0.22f;
        comp.NormalBias = 0.11f;

        const auto extracted = ExtractDDGIProbeGrid(scene);
        RENDER_CHECK(extracted.has_value() &&
                     extracted->Enabled &&
                     extracted->GridDimensions == glm::ivec3(6, 4, 8) &&
                     Axle::RenderTests::Near(extracted->GridOrigin.x, -1.0f, 1e-5f) &&
                     Axle::RenderTests::Near(extracted->GridOrigin.y, 2.5f, 1e-5f) &&
                     Axle::RenderTests::Near(extracted->GridOrigin.z, 0.0f, 1e-5f) &&
                     Axle::RenderTests::Near(extracted->UpdateFraction, 0.5f, 1e-5f) &&
                     extracted->MaxProbesPerFrame == 12 &&
                     Axle::RenderTests::Near(extracted->Hysteresis, 0.85f, 1e-5f) &&
                     Axle::RenderTests::Near(extracted->ViewBias, 0.22f, 1e-5f) &&
                     Axle::RenderTests::Near(extracted->NormalBias, 0.11f, 1e-5f),
            "M15-U7-02: ExtractDDGIProbeGrid composes entity world translation and preserves new DDGI parameters");

        comp.Enabled = false;
        RENDER_CHECK(!ExtractDDGIProbeGrid(scene).has_value(),
            "M15-U7-02: ExtractDDGIProbeGrid omits disabled probe grid components");
        comp.Enabled = true;

        SceneSerializer serializer(&scene);
        const std::string yaml = serializer.SerializeToString();
        Scene restored;
        SceneSerializer deserializer(&restored);
        RENDER_CHECK(deserializer.DeserializeFromString(yaml), "M15-U7-02: scene YAML with DDGIProbeGridComponent deserializes cleanly");
        restored.UpdateWorldTransforms();
        const auto restoredGrid = ExtractDDGIProbeGrid(restored);
        RENDER_CHECK(restoredGrid.has_value() &&
                     restoredGrid->GridDimensions == comp.GridDimensions &&
                     Axle::RenderTests::Near(restoredGrid->UpdateFraction, comp.UpdateFraction, 1e-4f) &&
                     restoredGrid->MaxProbesPerFrame == comp.MaxProbesPerFrame &&
                     Axle::RenderTests::Near(restoredGrid->Hysteresis, comp.Hysteresis, 1e-4f) &&
                     Axle::RenderTests::Near(restoredGrid->ViewBias, comp.ViewBias, 1e-4f) &&
                     Axle::RenderTests::Near(restoredGrid->NormalBias, comp.NormalBias, 1e-4f),
            "M15-U7-02: SceneSerializer round-trips all DDGIProbeGridComponent fields");
    }

    // 4. M15-U7-07.3: Explicit experimental opt-in default & unified ViewTarget resolution
    {
        SceneRenderSettings defaultSettings;
        RHI::RHIViewTargetDesc defaultTargetDesc;
        RENDER_CHECK(!defaultSettings.EnableDDGI && !defaultTargetDesc.EnableDDGI,
            "M15-U7-07.3: EnableDDGI defaults to false on SceneRenderSettings and RHIViewTargetDesc");

        RenderDDGIProbeGrid activeGrid;
        activeGrid.Enabled = true;
        activeGrid.GridDimensions = {8, 4, 8};

        bool enableOut = true;
        const std::array<uint32_t, 3> zeroDims{0u, 0u, 0u};
        const std::array<uint32_t, 3> expectedDims{8u, 4u, 8u};
        std::array<uint32_t, 3> dimsOut{9, 9, 9};
        DDGIContract::ResolveViewTargetOptions(defaultSettings, false, &activeGrid, enableOut, dimsOut);
        RENDER_CHECK(!enableOut && dimsOut == zeroDims,
            "M15-U7-07.3: disabled EnableDDGI never enables targets even when scene has an active grid");

        SceneRenderSettings enabledSettings;
        enabledSettings.EnableDDGI = true;
        DDGIContract::ResolveViewTargetOptions(enabledSettings, false, &activeGrid, enableOut, dimsOut);
        RENDER_CHECK(enableOut && dimsOut == expectedDims,
            "M15-U7-07.3: active opt-in single-sample main view resolves authored grid dimensions");

        DDGIContract::ResolveViewTargetOptions(enabledSettings, true, &activeGrid, enableOut, dimsOut);
        RENDER_CHECK(!enableOut, "M15-U7-07.3: sub-captures never recursively allocate/run DDGI");

        enabledSettings.MSAASamples = 4;
        DDGIContract::ResolveViewTargetOptions(enabledSettings, false, &activeGrid, enableOut, dimsOut);
        RENDER_CHECK(!enableOut, "M15-U7-07.3: MSAA resolves DDGI targets off safely");

        enabledSettings.MSAASamples = 1;
        DDGIContract::ResolveViewTargetOptions(enabledSettings, false, nullptr, enableOut, dimsOut);
        RENDER_CHECK(!enableOut && dimsOut == zeroDims,
            "M15-U7-07.3: a view without an authored grid does not allocate DDGI atlases");

        // Backend capability gating: without Compute or StorageTextures, EnableDDGI turns off with diagnostic
        RHI::RHIDeviceCapabilities noComputeCaps;
        noComputeCaps.ActiveBackend = RHI::RHIType::Vulkan;
        noComputeCaps.SupportsComputeShaders = false;
        noComputeCaps.SupportsStorageTextures = true;
        noComputeCaps.SupportsDescriptorSets = true;
        noComputeCaps.SupportsHDRFormats = true;
        const auto noComputePolicy = RenderBackendPolicy::FromDevice(noComputeCaps);
        const auto resolvedNoCompute = noComputePolicy.ResolveSettings(enabledSettings);
        const auto fallbacks = noComputePolicy.CollectFallbacks(enabledSettings);
        RENDER_CHECK(!resolvedNoCompute.EnableDDGI && !fallbacks.empty(),
            "M15-U7-07.3: backend without compute support forces EnableDDGI off with fallback diagnostic");

        // Verify source wiring consistency across all owners and Editor/Inspector UI
        const std::string viewTargetsSrc = ReadText(AXLE_RHI_VIEW_TARGETS_SOURCE_PATH);
        const std::string renderContractsSrc = ReadText(AXLE_RENDER_CONTRACTS_SOURCE_PATH);
        const std::string viewportDescSrc = ReadText(AXLE_RHI_VIEWPORT_DESCRIPTOR_SOURCE_PATH);
        const std::string sceneRendererSrc = ReadText(AXLE_SCENE_RENDERER_SOURCE_PATH);
        const std::string renderServiceSrc = ReadText(AXLE_RENDER_SERVICE_SOURCE_PATH);
        const std::string inspectorSrc = ReadText(AXLE_ENTITY_INSPECTOR_SOURCE_PATH);
        const std::string editorHeaderSrc = ReadText(AXLE_EDITOR_LAYER_HEADER_PATH);
        const std::string editorSrc = ReadText(AXLE_EDITOR_LAYER_SOURCE_PATH);

        RENDER_CHECK(viewTargetsSrc.find("ViewTarget_DDGI_Distance") != std::string::npos &&
                     renderContractsSrc.find("DDGIContract::ResolveViewTargetOptions") != std::string::npos &&
                     viewportDescSrc.find("ViewTargetContract::BuildViewTargetDesc") != std::string::npos &&
                     sceneRendererSrc.find("ViewTargetContract::BuildViewTargetDesc") != std::string::npos &&
                     renderServiceSrc.find("ViewTargetContract::BuildViewTargetDesc") != std::string::npos &&
                     inspectorSrc.find("DDGIProbeGridComponent") != std::string::npos &&
                     editorHeaderSrc.find("std::optional<bool> m_ViewportDDGIOverride") != std::string::npos &&
                     editorSrc.find("Experimental DDGI") != std::string::npos,
            "M15-U7-02 / 07.3 / U10-03.5: ViewTargets, ViewTargetContract, Viewport, RenderService, RHISceneRenderer, Inspector, and Editor UI agree on DDGI contracts");

        // U7-B: Verify legacy camera-screen-copy hack is gone and real capture/octahedral/Chebyshev compute kernels exist
        const std::string ddgiShader = ReadText(AXLE_DDGI_SHADER_PATH);
        const std::string ddgiCommon = ReadText(AXLE_DDGI_COMMON_SHADER_PATH);
        const std::string ddgiPassSrc = ReadText(AXLE_DDGI_PASS_SOURCE_PATH);
        const std::string forwardSrc = ReadText(AXLE_FORWARD_PBR_SHADER_PATH);
        RENDER_CHECK(!ddgiShader.empty() && !ddgiCommon.empty() && !ddgiPassSrc.empty() && !forwardSrc.empty() &&
                     ddgiShader.find("u_Scene") == std::string::npos &&
                     ddgiShader.find("CSSeedClear") != std::string::npos &&
                     ddgiShader.find("CSUpdateIrradiance") != std::string::npos &&
                     ddgiShader.find("CSUpdateDistanceMoments") != std::string::npos &&
                     ddgiCommon.find("DDGIMirrorInteriorCoord") != std::string::npos &&
                     ddgiCommon.find("DDGIEvaluateChebyshev") != std::string::npos &&
                     ddgiCommon.find("DDGISampleVolumeIrradiance") != std::string::npos &&
                      ddgiPassSrc.find("DDGICapture") != std::string::npos &&
                      forwardSrc.find("PSDDGICapture") != std::string::npos,
            "M15-U7-03..05: DDGI retires camera u_Scene copy in favor of world-space DDGICapture + octahedral irradiance & Chebyshev distance kernels");

        // U7-C: Verify ForwardPBR, MaterialSurface, MaterialSurfaceShader, DDGIPass, and RHISceneRendererPasses diffuse GI wiring
        const std::string materialSurfaceSrc = ReadText(AXLE_MATERIAL_SURFACE_SHADER_PATH);
        const std::string scenePassesSrc = ReadText(AXLE_SCENE_PASSES_SOURCE_PATH);
        const std::string assembled = AssembleMaterialSurfaceShader("", "MaterialSurface AxleEvaluateMaterial(float2 uv) { MaterialSurface s = (MaterialSurface)0; return s; }");
        RENDER_CHECK(!materialSurfaceSrc.empty() && !scenePassesSrc.empty() &&
                     forwardSrc.find("AXLE_DDGI_CBUFFER_REGISTER b8") != std::string::npos &&
                     forwardSrc.find("u_DDGIIrradiance") != std::string::npos &&
                     forwardSrc.find("u_DDGIDistance") != std::string::npos &&
                     forwardSrc.find("DDGISampleVolumeIrradiance") != std::string::npos &&
                     materialSurfaceSrc.find("AXLE_DDGI_CBUFFER_REGISTER b8") != std::string::npos &&
                     materialSurfaceSrc.find("DDGISampleVolumeIrradiance") != std::string::npos &&
                     assembled.find("#include \"DDGICommon.hlsli\"") == std::string::npos &&
                     assembled.find("DDGISampleVolumeIrradiance") != std::string::npos &&
                     ddgiPassSrc.find("CaptureConstantsCB") != std::string::npos &&
                     scenePassesSrc.find("m_DDGICaptureOcclusionSet") != std::string::npos &&
                     scenePassesSrc.find("m_LastGIFrame.SamplingEnabled = true") != std::string::npos,
            "M15-U7-06 / 07.2: ForwardPBR, MaterialSurface, and render passes wire DDGI volume irradiance, capture multi-bounce, and SamplingEnabled");

        RHI::RHIShaderConstantBuffer ddgiCb{"DDGIConstants", 8u, 0u, sizeof(DDGIContract::DDGIShaderConstants), {}};
        RHI::RHIShaderConstantBuffer badDdgiCb{"DDGIConstants", 8u, 0u, 128u, {}};
        RHI::RHIShaderResourceBinding irradBind{"u_DDGIIrradiance", RHI::RHIResourceType::Texture, 7u, 1u, 4u};
        RHI::RHIShaderResourceBinding distBind{"u_DDGIDistance", RHI::RHIResourceType::Texture, 8u, 1u, 4u};
        RHI::RHIShaderReflection ddgiRefl;
        ddgiRefl.ConstantBuffers = {ddgiCb};
        ddgiRefl.Resources = {irradBind, distBind};
        std::string diag;
        RENDER_CHECK(IsMaterialSurfaceEngineBuffer(ddgiCb) &&
                     IsMaterialSurfaceEngineResource(irradBind) &&
                     IsMaterialSurfaceEngineResource(distBind) &&
                     ValidateMaterialSurfaceEngineBuffers(ddgiRefl, diag),
            "M15-U7-06: MaterialSurfaceShader recognizes and validates DDGIConstants b8 and t7/t8 atlases");
        StripMaterialSurfaceEngineBindings(ddgiRefl);
        RENDER_CHECK(ddgiRefl.ConstantBuffers.empty() && ddgiRefl.Resources.empty(),
            "M15-U7-06: StripMaterialSurfaceEngineBindings strips DDGI engine resources from material properties");
        ddgiRefl.ConstantBuffers = {badDdgiCb};
        RENDER_CHECK(!ValidateMaterialSurfaceEngineBuffers(ddgiRefl, diag),
            "M15-U7-06: ValidateMaterialSurfaceEngineBuffers rejects invalid DDGIConstants size");
    }

    std::cout << "[M15DDGIContract] All M15 U7 Batch A DDGI contract tests passed.\n";
    return true;
}
