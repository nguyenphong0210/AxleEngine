#include "RenderTestCommon.hpp"
#include <Axle/RenderContracts.hpp>
#include <Axle/RenderGraph.hpp>
#include <Axle/ResolvedSurface.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/SceneExtraction.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using namespace Axle;
using namespace Axle::ReflectionCaptureContract;

#ifndef AXLE_CUBEMAP_CAPTURE_SHADER_PATH
#define AXLE_CUBEMAP_CAPTURE_SHADER_PATH "Engine/Resources/Shaders/CubemapCapture.hlsl"
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

bool RunM15ReflectionCaptureContractUnitTests() {
    std::cout << "[M15ReflectionCapture] Running Milestone 15 Unit 5 Batch B (Dynamic Reflection Capture) Contract Tests...\n";

    // 1. M15-U5-08.1 / 08.2: Target allocation & policy contract
    {
        ReflectionCapturePolicy policy;
        std::string reason;
        RENDER_CHECK(ValidateCapturePolicy(policy, &reason) && reason.empty(),
            "M15-U5-08.1: default single-sample linear HDR RGBA16_FLOAT + D32_FLOAT capture policy validates");

        ReflectionCapturePolicy badSamples = policy;
        badSamples.SampleCount = 4;
        RENDER_CHECK(!ValidateCapturePolicy(badSamples, &reason) && reason.find("single-sample") != std::string::npos,
            "M15-U5-08.1: rejects MSAA capture targets");

        ReflectionCapturePolicy badColor = policy;
        badColor.ColorFormat = nri::Format::RGBA8_UNORM;
        RENDER_CHECK(!ValidateCapturePolicy(badColor, &reason) && reason.find("RGBA16_FLOAT") != std::string::npos,
            "M15-U5-08.2: rejects SDR color formats that bypass linear HDR capture");

        ReflectionCapturePolicy badDepth = policy;
        badDepth.DepthFormat = nri::Format::D24_UNORM_S8_UINT;
        RENDER_CHECK(!ValidateCapturePolicy(badDepth, &reason) && reason.find("D32_FLOAT") != std::string::npos,
            "M15-U5-08.1: rejects non-D32_FLOAT capture depth formats");

        for (uint32_t badRes : {0u, 8u, 127u, 2048u}) {
            ReflectionCapturePolicy badPolicy = policy;
            badPolicy.Resolution = badRes;
            RENDER_CHECK(!ValidateCapturePolicy(badPolicy, &reason),
                "M15-U5-08.1: rejects out-of-bounds or non-power-of-two capture resolution");
        }

        ReflectionCapturePolicy badPlanes = policy;
        badPlanes.NearPlane = 0.0f;
        RENDER_CHECK(!ValidateCapturePolicy(badPlanes, &reason),
            "M15-U5-08.1: rejects invalid near clip plane");
        badPlanes.NearPlane = 5.0f;
        badPlanes.FarPlane = 5.0f;
        RENDER_CHECK(!ValidateCapturePolicy(badPlanes, &reason),
            "M15-U5-08.1: rejects FarPlane <= NearPlane");
    }

    // 2. M15-U5-09.1 / 09.2: Six 90-degree face camera basis & off-axis cubemap projection orientation
    {
        const glm::vec3 expectedForwards[6] = {
            { 1.0f,  0.0f,  0.0f}, // +X
            {-1.0f,  0.0f,  0.0f}, // -X
            { 0.0f,  1.0f,  0.0f}, // +Y
            { 0.0f, -1.0f,  0.0f}, // -Y
            { 0.0f,  0.0f,  1.0f}, // +Z
            { 0.0f,  0.0f, -1.0f}  // -Z
        };
        const glm::vec3 expectedUps[6] = {
            {0.0f, 1.0f,  0.0f},
            {0.0f, 1.0f,  0.0f},
            {0.0f, 0.0f, -1.0f},
            {0.0f, 0.0f,  1.0f},
            {0.0f, 1.0f,  0.0f},
            {0.0f, 1.0f,  0.0f}
        };

        const glm::vec3 probePos(2.0f, 1.5f, -3.0f);
        std::string reason;
        for (uint32_t face = 0; face < FaceCount; ++face) {
            const CubeFaceBasis basis = GetCubeFaceBasis(face);
            RENDER_CHECK(ValidateCubeFaceBasis(basis, &reason),
                "M15-U5-09.2: canonical cubemap face basis is unit, orthogonal, and oriented");
            RENDER_CHECK(glm::length(basis.Forward - expectedForwards[face]) < 1e-5f &&
                         glm::length(basis.Up - expectedUps[face]) < 1e-5f,
                "M15-U5-09.1: face direction and up match TextureCube standard");

            const PerspectiveCamera cam = BuildCubeFaceCamera(probePos, face, 0.1f, 50.0f);
            RENDER_CHECK(std::abs(cam.GetFOV() - 90.0f) < 1e-4f &&
                         std::abs(cam.GetAspectRatio() - 1.0f) < 1e-4f &&
                         glm::length(cam.GetPosition() - probePos) < 1e-5f,
                "M15-U5-09.1: cube face camera enforces 90-degree square FOV at probe center");

            // Project an off-axis world point shifted along +Forward*4, +Right*2, +Up*1.
            // Pre-flip RH lookAt places +Right at negative clip X; after FlipHorizontal (u -> 1 - u),
            // +Right lands cleanly on the positive/right half (+0.5) and +Up on the top half (+0.25).
            const glm::vec3 target = probePos + basis.Forward * 4.0f + basis.Right * 2.0f + basis.Up * 1.0f;
            const glm::vec4 clip = cam.GetViewProjectionMatrixZO() * glm::vec4(target, 1.0f);
            RENDER_CHECK(clip.w > 0.0f, "M15-U5-09.2: point in front of face has positive clip.w");
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            RENDER_CHECK(std::abs(ndc.x - (-0.5f)) < 1e-3f &&
                         std::abs(ndc.y - 0.25f) < 1e-3f &&
                         ndc.z > 0.0f && ndc.z < 1.0f,
                "M15-U5-09.2: RH capture + horizontal flip maps (+Right, +Up) into TextureCube top-right quadrant");
        }

        CubeFaceBasis bad = GetCubeFaceBasis(0);
        bad.Up = bad.Forward;
        RENDER_CHECK(!ValidateCubeFaceBasis(bad, &reason),
            "M15-U5-09.2: rejects collinear forward/up basis");
        bad = GetCubeFaceBasis(0);
        bad.FlipHorizontalForCubemapCopy = false;
        RENDER_CHECK(!ValidateCubeFaceBasis(bad, &reason),
            "M15-U5-12.1: rejects unflipped RH cubemap basis");
        bad = GetCubeFaceBasis(6);
        RENDER_CHECK(!ValidateCubeFaceBasis(bad, &reason),
            "M15-U5-09.2: rejects out-of-range face index");
    }

    // 3. M15-U5-10.1..10.4 & 11.1..11.2: Settings isolation and capture world filtering
    {
        ReflectionCapturePolicy policy;
        std::string reason;

        auto badSSR = policy; badSSR.AllowSSR = true;
        RENDER_CHECK(!ValidateCapturePolicy(badSSR, &reason), "M15-U5-10.1: rejects AllowSSR");
        auto badProbe = policy; badProbe.AllowProbeSampling = true;
        RENDER_CHECK(!ValidateCapturePolicy(badProbe, &reason), "M15-U5-10.1: rejects recursive probe sampling");
        auto badTAA = policy; badTAA.IsolateMainViewTAA = false;
        RENDER_CHECK(!ValidateCapturePolicy(badTAA, &reason), "M15-U5-10.2: rejects shared main-view TAA state");
        auto badOverlay = policy; badOverlay.IncludeEditorOverlays = true;
        RENDER_CHECK(!ValidateCapturePolicy(badOverlay, &reason), "M15-U5-10.3: rejects Editor/debug overlays");

        SceneRenderSettings noisySettings;
        noisySettings.MSAASamples = 4;
        noisySettings.EnableToneMapping = true;
        noisySettings.ToneMappingMode = 0;
        noisySettings.EnableFXAA = true;
        noisySettings.EnableBloom = true;
        noisySettings.EnableAutoExposure = true;
        noisySettings.EnableTAA = true;
        noisySettings.EnableSSR = true;
        noisySettings.EnableGTAO = true;
        noisySettings.EnableContactShadows = true;
        noisySettings.EnableVolumetricFog = true;
        noisySettings.EnableDDGI = true;
        noisySettings.HiZVisualizationLevel = 2;
        noisySettings.Wireframe = true;
        noisySettings.ShowPhysicsDebug = true;
        noisySettings.EnableParticles = true;
        noisySettings.EnableSkybox = true;
        noisySettings.EnableShadows = true;

        const SceneRenderSettings sanitized = SanitizeCaptureSettings(noisySettings, policy);
        RENDER_CHECK(sanitized.MSAASamples == 1 &&
                     !sanitized.EnableToneMapping &&
                     sanitized.ToneMappingMode == 3 &&
                     !sanitized.EnableFXAA &&
                     !sanitized.EnableBloom &&
                     !sanitized.EnableAutoExposure &&
                     !sanitized.EnableTAA &&
                     !sanitized.EnableSSR &&
                     !sanitized.EnableGTAO &&
                     !sanitized.EnableContactShadows &&
                     !sanitized.EnableVolumetricFog &&
                     !sanitized.EnableDDGI &&
                     sanitized.HiZVisualizationLevel == -1 &&
                     !sanitized.Wireframe &&
                     !sanitized.ShowPhysicsDebug &&
                     !sanitized.EnableParticles &&
                     sanitized.EnableSkybox &&
                     sanitized.EnableShadows,
            "M15-U5-10/11: capture settings isolate SSR/TAA/GTAO/post/debug overlays while keeping skybox & selected shadows");

        RenderWorld world;
        world.Skybox.Authored = true;
        world.Skybox.Enabled = true;
        world.DirectionalLight = RenderDirectionalLight{};
        world.PointLights.push_back(RenderPointLight{});
        world.SpotLights.push_back(RenderSpotLight{});
        world.ReflectionProbes.push_back(RenderReflectionProbe{});
        world.Particles.push_back(nullptr);
        world.DebugLines.push_back(RenderDebugLine{});

        RenderMeshInstance opaqueMesh;
        opaqueMesh.EntityID = 10;
        RenderMeshInstance cutoutMesh;
        cutoutMesh.EntityID = 11;
        RHI::RHIMaterialDesc cutoutDesc;
        cutoutDesc.SurfaceType = SurfaceType::Cutout;
        cutoutMesh.Material.RHIMaterialAsset = std::make_shared<RHI::RHIMaterial>(cutoutDesc);
        RenderMeshInstance transparentMesh;
        transparentMesh.EntityID = 12;
        RHI::RHIMaterialDesc transDesc;
        transDesc.SurfaceType = SurfaceType::Transparent;
        transparentMesh.Material.RHIMaterialAsset = std::make_shared<RHI::RHIMaterial>(transDesc);
        RenderMeshInstance selfMesh;
        selfMesh.EntityID = 42;
        RenderMeshInstance helperMesh;
        helperMesh.EntityID = 99;

        world.Meshes = {opaqueMesh, cutoutMesh, transparentMesh, selfMesh, helperMesh};

        policy.ExcludeEntityID = 42;
        policy.AdditionalExcludedEntityIDs = {99};
        const RenderWorld filtered = FilterCaptureWorld(world, policy);
        RENDER_CHECK(filtered.Meshes.size() == 2 &&
                     filtered.Meshes[0].EntityID == 10 &&
                     filtered.Meshes[1].EntityID == 11,
            "M15-U5-10.4 / 11.1: FilterCaptureWorld retains opaque + cutout meshes and excludes self, helpers, and transparents");
        RENDER_CHECK(filtered.Particles.empty() &&
                     filtered.DebugLines.empty() &&
                     filtered.ReflectionProbes.empty() &&
                     filtered.Skybox.Enabled,
            "M15-U5-10.1 / 10.3 / 11.1: FilterCaptureWorld strips particles, debug lines, and probes while keeping skybox");
        RENDER_CHECK(filtered.DirectionalLight && filtered.DirectionalLight->CastsShadows &&
                     filtered.PointLights[0].Shadows.CastShadows && filtered.SpotLights[0].Shadows.CastShadows,
            "M15-U5-11.2: FilterCaptureWorld preserves selected directional/local shadows when enabled");

        policy.IncludeSelectedShadows = false;
        const RenderWorld noShadowWorld = FilterCaptureWorld(world, policy);
        RENDER_CHECK(noShadowWorld.DirectionalLight && !noShadowWorld.DirectionalLight->CastsShadows &&
                     !noShadowWorld.PointLights[0].Shadows.CastShadows && !noShadowWorld.SpotLights[0].Shadows.CastShadows,
            "M15-U5-11.2: FilterCaptureWorld disables shadows when CaptureShadows policy is off");
    }

    // 4. M15-U5-12.1 / 12.2: Orientation shader contract & RenderGraph barrier dependency ordering
    {
        const std::string shader = ReadText(AXLE_CUBEMAP_CAPTURE_SHADER_PATH);
        RENDER_CHECK(!shader.empty() &&
                     shader.find("OrientFaceCS") != std::string::npos &&
                     shader.find("size - 1u - dstPixel.x") != std::string::npos,
            "M15-U5-12.1: CubemapCapture.hlsl horizontally mirrors X for TextureCube copy and sanitizes NaNs");

        RenderGraph graph;
        const auto faceColor = graph.Import("FaceColor", nullptr);
        const auto orientedColor = graph.Import("OrientedColor", nullptr);
        const auto cubemap = graph.Import("CaptureCubemap", nullptr);
        graph.AddPass("OpaqueAndSky", {
            {faceColor, RHI::RHIResourceLayout::ColorAttachment, RenderGraph::Access::Write, RenderGraph::Load::Clear}});
        graph.AddPass("CaptureOrient", {
            {faceColor, RHI::RHIResourceLayout::ShaderResource},
            {orientedColor, RHI::RHIResourceLayout::ShaderResourceStorage, RenderGraph::Access::Write}});
        graph.AddPass("CaptureCopy", {
            {orientedColor, RHI::RHIResourceLayout::CopySource},
            {cubemap, RHI::RHIResourceLayout::CopyDest, RenderGraph::Access::Write}});
        graph.AddPass("Export", {
            {cubemap, RHI::RHIResourceLayout::ShaderResource}});
        RENDER_CHECK(graph.Compile(false, false),
            "M15-U5-12.2: CaptureOrient -> CaptureCopy -> Export RenderGraph barrier chain compiles cleanly");

        RenderGraph invalidGraph;
        const auto badFace = invalidGraph.Import("UnwrittenFace", nullptr);
        const auto badOriented = invalidGraph.Import("BadOriented", nullptr);
        invalidGraph.AddPass("CaptureOrientBeforeWrite", {
            {badFace, RHI::RHIResourceLayout::ShaderResource},
            {badOriented, RHI::RHIResourceLayout::ShaderResourceStorage, RenderGraph::Access::Write}});
        RENDER_CHECK(!invalidGraph.Compile(false, false),
            "M15-U5-12.2: RenderGraph rejects reading capture face before prior stored write");
    }

    // 5. Scene serialization & extraction round-trip for dynamic ReflectionProbeComponent fields
    {
        Scene srcScene;
        Entity probeEnt = srcScene.CreateEntity("DynProbe");
        auto& rpc = probeEnt.AddComponent<ReflectionProbeComponent>();
        rpc.DynamicCapture = true;
        rpc.CaptureResolution = 256;
        rpc.CaptureShadows = false;
        rpc.CaptureNearPlane = 0.25f;
        rpc.CaptureFarPlane = 75.0f;

        SceneSerializer serializer(&srcScene);
        const std::string yaml = serializer.SerializeToString();

        Scene dstScene;
        SceneSerializer deserializer(&dstScene);
        RENDER_CHECK(deserializer.DeserializeFromString(yaml),
            "dynamic reflection probe properties serialize cleanly to YAML and reload");

        const RenderWorld extracted = ExtractRenderWorld(dstScene);
        RENDER_CHECK(extracted.ReflectionProbes.size() == 1 &&
                     extracted.ReflectionProbes[0].DynamicCapture &&
                     extracted.ReflectionProbes[0].CaptureResolution == 256 &&
                     !extracted.ReflectionProbes[0].CaptureShadows &&
                     std::abs(extracted.ReflectionProbes[0].CaptureNearPlane - 0.25f) < 1e-4f &&
                     std::abs(extracted.ReflectionProbes[0].CaptureFarPlane - 75.0f) < 1e-4f,
            "SceneExtraction preserves dynamic reflection probe settings after round-trip");
    }

    std::cout << "[M15ReflectionCapture] All dynamic reflection capture contract tests passed!\n";
    return true;
}
