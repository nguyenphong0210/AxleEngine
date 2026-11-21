#include "RenderTestCommon.hpp"
#include <Axle/RenderContracts.hpp>
#include <Axle/RenderGraph.hpp>
#include <Axle/ResolvedSurface.hpp>
#include <Axle/Scene.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/SceneExtraction.hpp>
#include <Axle/RHISceneRenderer.hpp>
#include <Axle/RenderBackendPolicy.hpp>
#include <Axle/ViewContext.hpp>
#include <Axle/RHI/RHIViewTargets.hpp>
#include <Axle/RHI/RHIMaterial.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
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

#ifndef AXLE_CUBEMAP_PREFILTER_SHADER_PATH
#define AXLE_CUBEMAP_PREFILTER_SHADER_PATH "Engine/Resources/Shaders/CubemapPrefilter.hlsl"
#endif

#ifndef AXLE_FORWARD_PBR_SHADER_PATH
#define AXLE_FORWARD_PBR_SHADER_PATH "Engine/Resources/Shaders/ForwardPBR.hlsl"
#endif

#ifndef AXLE_MATERIAL_SURFACE_SHADER_PATH
#define AXLE_MATERIAL_SURFACE_SHADER_PATH "Engine/Resources/Shaders/MaterialSurface.hlsl"
#endif

#ifndef AXLE_SSR_SHADER_PATH
#define AXLE_SSR_SHADER_PATH "Engine/Resources/Shaders/SSR.hlsl"
#endif

#ifndef AXLE_REFLECTION_PROBE_COMMON_SHADER_PATH
#define AXLE_REFLECTION_PROBE_COMMON_SHADER_PATH "Engine/Resources/Shaders/ReflectionProbeCommon.hlsli"
#endif

#ifndef AXLE_PBR_COMMON_SHADER_PATH
#define AXLE_PBR_COMMON_SHADER_PATH "Engine/Resources/Shaders/PBRCommon.hlsli"
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
    RenderReflectionProbe anonymous;
    anonymous.Position.x = .0001f;
    const auto firstKey = RHISceneRenderer::ResolveDynamicProbeKey(anonymous);
    anonymous.Position.x = .0002f;
    RENDER_CHECK(firstKey != RHISceneRenderer::ResolveDynamicProbeKey(anonymous), "anonymous probe keys preserve exact finite coordinates");
    anonymous.Position.x = std::numeric_limits<float>::infinity();
    RENDER_CHECK(!RHISceneRenderer::ResolveDynamicProbeKey(anonymous).empty(), "invalid anonymous coordinates do not enter integer conversion");
    RENDER_CHECK(BuildCubeFaceCamera({},0,.1f,.105f).GetFarClip() == .105f,
        "valid close clip planes are preserved without silent widening");
    std::cout << "[M15ReflectionCapture] Running Milestone 15 Unit 5 Batch B (Dynamic Reflection Capture) Contract Tests...\n";

    {
        constexpr uint32_t size = 16;
        const auto projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
        const auto inverse = glm::inverse(projection);
        const float planeDepth = SSRRaymarchContract::ProjectViewToUVZ(glm::vec3(0, 0, -4), projection).z;
        SSRRaymarchContract::RaySetup ray;
        ray.StartUVZ = {0.2f, 0.5f, planeDepth - 0.01f};
        ray.EndUVZ = {0.8f, 0.5f, planeDepth + 0.01f};
        ray.StartView = SSRRaymarchContract::ReconstructViewPos(ray.StartUVZ, ray.StartUVZ.z, inverse);
        ray.EndView = SSRRaymarchContract::ReconstructViewPos(ray.EndUVZ, ray.EndUVZ.z, inverse);
        ray.OriginView = ray.StartView;
        ray.MaxDistance = 10.0f;
        ray.Valid = true;
        const std::vector<std::vector<float>> pyramid{std::vector<float>(size * size, planeDepth)};
        const auto hit = SSRRaymarchContract::TraceHiZDepthBuffer(pyramid, size, size, ray,
            projection, inverse, 0.05f, 64);
        RENDER_CHECK(hit.Hit && std::abs(hit.HitUV.x - 0.5f) < 0.03f && hit.Confidence > 0.0f,
            "M15-U5-39.7: SSR refines a continuous plane at minimum ray thickness");
    }

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

    // 6. M15-U5-C (S5.3): Cubemap roughness prefilter contracts & pure 3D cubemap reference
    {
        RENDER_CHECK(ComputePrefilterMipCount(0u) == 0u &&
                     ComputePrefilterMipCount(4u) == 3u &&
                     ComputePrefilterMipCount(8u) == 4u &&
                     ComputePrefilterMipCount(16u) == 5u &&
                     ComputePrefilterMipCount(128u) == 5u &&
                     ComputePrefilterMipCount(1024u) == 5u,
            "M15-U5-16.1: ComputePrefilterMipCount computes clamped [1, 5] power-of-two cube mip chain");

        RENDER_CHECK(ComputePrefilterRoughness(0u, 5u) == 0.0f &&
                     std::abs(ComputePrefilterRoughness(1u, 5u) - 0.25f) < 1e-6f &&
                     std::abs(ComputePrefilterRoughness(2u, 5u) - 0.50f) < 1e-6f &&
                     std::abs(ComputePrefilterRoughness(3u, 5u) - 0.75f) < 1e-6f &&
                     std::abs(ComputePrefilterRoughness(4u, 5u) - 1.00f) < 1e-6f,
            "M15-U5-17.1: ComputePrefilterRoughness maps mips linearly from 0.0 (mirror) to 1.0 (diffuse lobe)");

        ReflectionPrefilterPolicy validPrefilter;
        std::string reason;
        RENDER_CHECK(ValidatePrefilterPolicy(validPrefilter, &reason) && reason.empty(),
            "M15-U5-16.1: default RGBA16_FLOAT 5-mip prefilter policy validates");

        ReflectionPrefilterPolicy badFormat = validPrefilter;
        badFormat.ColorFormat = nri::Format::RGBA8_UNORM;
        RENDER_CHECK(!ValidatePrefilterPolicy(badFormat, &reason),
            "M15-U5-16.1: rejects SDR prefilter cubemap formats");

        ReflectionPrefilterPolicy badMips = validPrefilter;
        badMips.MipCount = 6u;
        RENDER_CHECK(!ValidatePrefilterPolicy(badMips, &reason),
            "M15-U5-16.1: rejects prefilter mip count exceeding max 5-mip chain");

        badMips.Resolution = 16u;
        badMips.MipCount = 6u;
        RENDER_CHECK(!ValidatePrefilterPolicy(badMips, &reason),
            "M15-U5-16.1: rejects mip count exceeding resolution chain depth");

        ReflectionPrefilterPolicy badSamples = validPrefilter;
        badSamples.SampleCount = 4u;
        RENDER_CHECK(!ValidatePrefilterPolicy(badSamples, &reason),
            "M15-U5-17.1: rejects insufficient prefilter sample budget");

        // M15-U5-17.2 / 17.3 / 20: Pure 3D cube direction seam continuity and bilinear 3D lookup without equirectangular projection.
        const glm::vec3 edgeRightPlusZ = ComputeCubeTexelDirection(4u, 1.0f, 0.5f); // (+X/+Z edge)
        const glm::vec3 edgeLeftPlusX = ComputeCubeTexelDirection(0u, 0.0f, 0.5f);  // (+X/+Z edge)
        RENDER_CHECK(glm::distance(edgeRightPlusZ, edgeLeftPlusX) < 1e-5f,
            "M15-U5-17.2 / 17.3: +Z right seam equals +X left seam in 3D direction space");

        const glm::vec3 edgeTopPlusZ = ComputeCubeTexelDirection(4u, 0.5f, 0.0f);   // (+Z/+Y edge)
        const glm::vec3 edgeBottomPlusY = ComputeCubeTexelDirection(2u, 0.5f, 1.0f);
        RENDER_CHECK(glm::distance(edgeTopPlusZ, edgeBottomPlusY) < 1e-5f,
            "M15-U5-17.2 / 17.3: +Z top seam equals +Y bottom seam in 3D direction space");

        constexpr uint32_t refRes = 16u;
        std::array<std::vector<glm::vec3>, FaceCount> constantCube;
        std::array<std::vector<glm::vec3>, FaceCount> poleSpotCube;
        for (uint32_t f = 0u; f < FaceCount; ++f) {
            constantCube[f].assign(static_cast<size_t>(refRes) * refRes, glm::vec3(2.0f, 4.0f, 8.0f));
            poleSpotCube[f].resize(static_cast<size_t>(refRes) * refRes);
            for (uint32_t y = 0u; y < refRes; ++y) {
                for (uint32_t x = 0u; x < refRes; ++x) {
                    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(refRes);
                    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(refRes);
                    const glm::vec3 dir = ComputeCubeTexelDirection(f, u, v);
                    const float poleLobe = 0.1f + 20.0f * std::pow(std::max(dir.y, 0.0f), 24.0f);
                    poleSpotCube[f][static_cast<size_t>(y) * refRes + x] = glm::vec3(poleLobe);
                }
            }
        }

        const glm::vec3 constOut = EvaluatePrefilterDirectionCPU(constantCube, refRes, {0.0f, 1.0f, 0.0f}, 0.75f, 64u);
        auto seamCube = constantCube;
        for (auto& face : seamCube) std::fill(face.begin(), face.end(), glm::vec3(0));
        std::fill(seamCube[0].begin(), seamCube[0].end(), glm::vec3(4));
        const auto seam = SampleCubemapBilinear3D(seamCube, refRes, {1,0,1});
        const auto left = SampleCubemapBilinear3D(seamCube, refRes, {1-.00001f,0,1});
        const auto right = SampleCubemapBilinear3D(seamCube, refRes, {1+.00001f,0,1});
        RENDER_CHECK(glm::distance(seam, glm::vec3(2)) < .001f && glm::distance(left, right) < .002f,
            "CPU cube bilinear footprint blends across face edges continuously");
        auto cornerCube = constantCube;
        for (uint32_t f = 0; f < FaceCount; ++f)
            std::fill(cornerCube[f].begin(), cornerCube[f].end(), glm::vec3(float(f + 1)));
        for (float x : {-1.0f, 1.0f}) for (float y : {-1.0f, 1.0f}) for (float z : {-1.0f, 1.0f}) {
            const glm::vec3 corner(x, y, z);
            const float expected = ((x > 0 ? 1.0f : 2.0f) + (y > 0 ? 3.0f : 4.0f) + (z > 0 ? 5.0f : 6.0f)) / 3.0f;
            RENDER_CHECK(glm::distance(SampleCubemapBilinear3D(cornerCube, refRes, corner), glm::vec3(expected)) < .001f,
                "CPU cube corner symmetrically averages its three meeting faces");
            for (int axis = 0; axis < 3; ++axis) {
                auto approach = corner;
                approach[axis] *= 1.00001f;
                RENDER_CHECK(glm::distance(SampleCubemapBilinear3D(cornerCube, refRes, approach), glm::vec3(expected)) < .002f,
                    "all eight cube corners stay continuous when approached from each major face");
            }
        }
        RENDER_CHECK(EvaluatePrefilterDirectionCPU(constantCube,refRes,{0,1,0},std::numeric_limits<float>::quiet_NaN(),64) == glm::vec3(0),
            "nonfinite roughness receives the explicit neutral reference fallback");
        RENDER_CHECK(glm::distance(constOut, glm::vec3(2.0f, 4.0f, 8.0f)) < 1e-4f,
            "M15-U5-19.1: constant HDR cubemap radiance is conserved at rough mips");

        const glm::vec3 sharpPole = EvaluatePrefilterDirectionCPU(poleSpotCube, refRes, {0.0f, 1.0f, 0.0f}, 0.0f, 64u);
        const glm::vec3 midPole = EvaluatePrefilterDirectionCPU(poleSpotCube, refRes, {0.0f, 1.0f, 0.0f}, 0.50f, 64u);
        const glm::vec3 roughPole = EvaluatePrefilterDirectionCPU(poleSpotCube, refRes, {0.0f, 1.0f, 0.0f}, 1.00f, 64u);
        const glm::vec3 poleRingX = EvaluatePrefilterDirectionCPU(poleSpotCube, refRes, glm::normalize(glm::vec3(0.45f, 1.0f, 0.0f)), 0.50f, 256u);
        const glm::vec3 poleRingZ = EvaluatePrefilterDirectionCPU(poleSpotCube, refRes, glm::normalize(glm::vec3(0.0f, 1.0f, 0.45f)), 0.50f, 256u);
        const float maxPoleRing = std::max({poleRingX.r, poleRingZ.r, 1e-4f});
        RENDER_CHECK(sharpPole.r > midPole.r && midPole.r > roughPole.r &&
                     std::abs(poleRingX.r - poleRingZ.r) <= maxPoleRing * 0.05f,
            "M15-U5-19.1 / 20: CPU 3D cubemap GGX prefilter decreases peak monotonically with isotropic +Y pole response");

        const std::string prefilterShader = ReadText(AXLE_CUBEMAP_PREFILTER_SHADER_PATH);
        RENDER_CHECK(!prefilterShader.empty() &&
                     prefilterShader.find("DownsampleCubeCS") != std::string::npos &&
                     prefilterShader.find("PrefilterCubeCS") != std::string::npos &&
                     prefilterShader.find("TextureCube<float4>") != std::string::npos &&
                     prefilterShader.find("RWTexture2DArray<float4>") != std::string::npos &&
                     prefilterShader.find("ImportanceSampleGGX") != std::string::npos &&
                     prefilterShader.find("atan2") == std::string::npos &&
                     prefilterShader.find("asin") == std::string::npos,
            "M15-U5-17.1 / 20: CubemapPrefilter.hlsl prefilters 3D TextureCube into RWTexture2DArray without equirectangular math");
    }

    // 6. M15-U5-D (S5.4): Manual/automatic modes, YAML round-trip, per-domain stamps,
    //    bounded scheduler allocation, and cache memory budget + LRU eviction plan.
    {
        Scene sceneA;
        auto probeEntityA = sceneA.CreateEntityWithUUID(100001ULL, "ProbeA");
        auto& rpc = probeEntityA.AddComponent<ReflectionProbeComponent>();
        rpc.DynamicCapture = true;
        rpc.CaptureResolution = 64u;
        rpc.CaptureMode = ReflectionProbeCaptureMode::Manual;
        rpc.BoxExtents = {12.0f, 12.0f, 12.0f};
        rpc.BoxOffset = {0.0f, 1.5f, 0.0f};

        const RenderWorld extractedA = ExtractRenderWorld(sceneA);
        RENDER_CHECK(extractedA.ReflectionProbes.size() == 1 &&
                     extractedA.ReflectionProbes[0].CaptureMode == ReflectionProbeCaptureMode::Manual &&
                     extractedA.ReflectionProbes[0].StableUUID == 100001ULL &&
                     extractedA.SceneInstanceID == sceneA.GetInstanceId() &&
                     extractedA.SceneLifetimeEpoch >= 1u,
            "M15-U5-22 / 26.4: SceneExtraction propagates CaptureMode, StableUUID, SceneInstanceID and SceneLifetimeEpoch");

        SceneSerializer srcSer(&sceneA);
        const std::string yamlText = srcSer.SerializeToString();
        RENDER_CHECK(yamlText.find("CaptureMode: Manual") != std::string::npos,
            "M15-U5-22: SceneSerializer writes CaptureMode: Manual");
        Scene sceneB;
        SceneSerializer dstSer(&sceneB);
        RENDER_CHECK(dstSer.DeserializeFromString(yamlText),
            "M15-U5-22: SceneSerializer deserializes ReflectionProbeComponent YAML");
        const RenderWorld extractedB = ExtractRenderWorld(sceneB);
        RENDER_CHECK(extractedB.ReflectionProbes.size() == 1 &&
                     extractedB.ReflectionProbes[0].CaptureMode == ReflectionProbeCaptureMode::Manual,
            "M15-U5-22: CaptureMode::Manual survives YAML serialize/deserialize round-trip");

        RenderReflectionProbe pA = extractedA.ReflectionProbes[0];
        RenderReflectionProbe pB = extractedB.ReflectionProbes[0];
        pA.EntityID = 1;
        pB.EntityID = 1;
        pB.StableUUID = 200002ULL;
        RENDER_CHECK(RHISceneRenderer::ResolveDynamicProbeKey(pA) != RHISceneRenderer::ResolveDynamicProbeKey(pB),
            "M15-U5-26.4: distinct StableUUIDs prevent probe key aliasing across scenes with identical entity IDs");

        // Per-domain stamp isolation (21.1 - 21.5, 26.2)
        RHISceneRenderer cpuStampRenderer;
        RenderWorld baseWorld;
        baseWorld.SceneRevision = 10;
        baseWorld.SceneInstanceID = 7;
        baseWorld.SceneLifetimeEpoch = 1;
        baseWorld.DirectionalLight = RenderDirectionalLight{};
        baseWorld.PointLights.push_back(RenderPointLight{});
        RenderMeshInstance mesh{};
        mesh.EntityID = 11;
        mesh.StableUUID = 777;
        mesh.WorldTransform = glm::mat4(1.0f);
        mesh.Material.Roughness = 0.5f;
        RenderSkinInstance skin{};
        skin.BoneMatrices.push_back(glm::mat4(1.0f));
        baseWorld.Meshes.push_back(mesh);
        baseWorld.Skins.push_back(skin);
        RenderReflectionProbe baseProbe{};
        baseProbe.EntityID = 100;
        baseProbe.CaptureResolution = 64u;
        baseProbe.DynamicCapture = true;
        SceneRenderSettings baseSettings;
        ReflectionService reflService;

        const auto baseStamps = cpuStampRenderer.ComputeDynamicCaptureDomainStamps(baseWorld, baseProbe, baseSettings);
        {
            RenderReflectionProbe movedProbe = baseProbe;
            movedProbe.Position.x += 2.0f;
            const auto inv = reflService.EvaluateInvalidation(
                baseStamps, cpuStampRenderer.ComputeDynamicCaptureDomainStamps(baseWorld, movedProbe, baseSettings));
            RENDER_CHECK(inv.TransformChanged && !inv.GeometryChanged && !inv.MaterialChanged && !inv.LightingChanged,
                "M15-U5-21.1: probe transform change sets TransformChanged only");
        }
        {
            RenderWorld revWorld = baseWorld;
            revWorld.SceneRevision = 11;
            const auto inv = reflService.EvaluateInvalidation(
                baseStamps, cpuStampRenderer.ComputeDynamicCaptureDomainStamps(revWorld, baseProbe, baseSettings));
            RENDER_CHECK(inv.SceneRevisionChanged && !inv.TransformChanged && !inv.GeometryChanged,
                "M15-U5-21.2: scene revision change sets SceneRevisionChanged");
        }
        {
            RenderWorld animWorld = baseWorld;
            animWorld.Skins[0].BoneMatrices[0] = glm::translate(glm::mat4(1.0f), {0.0f, 1.0f, 0.0f});
            const auto inv = reflService.EvaluateInvalidation(
                baseStamps, cpuStampRenderer.ComputeDynamicCaptureDomainStamps(animWorld, baseProbe, baseSettings));
            RENDER_CHECK(inv.GeometryChanged && !inv.TransformChanged && !inv.MaterialChanged && !inv.LightingChanged,
                "M15-U5-21.3: deformed/animated skin bone change triggers GeometryChanged");
        }
        {
            RenderWorld matWorld = baseWorld;
            matWorld.Meshes[0].Material.Roughness = 0.15f;
            const auto inv = reflService.EvaluateInvalidation(
                baseStamps, cpuStampRenderer.ComputeDynamicCaptureDomainStamps(matWorld, baseProbe, baseSettings));
            RENDER_CHECK(inv.MaterialChanged && !inv.TransformChanged && !inv.GeometryChanged && !inv.LightingChanged,
                "M15-U5-21.3: material property edit triggers MaterialChanged only");
        }
        {
            RenderWorld lightWorld = baseWorld;
            lightWorld.PointLights[0].Intensity = 25.0f;
            const auto inv = reflService.EvaluateInvalidation(
                baseStamps, cpuStampRenderer.ComputeDynamicCaptureDomainStamps(lightWorld, baseProbe, baseSettings));
            RENDER_CHECK(inv.LightingChanged && !inv.TransformChanged && !inv.GeometryChanged && !inv.MaterialChanged,
                "M15-U5-21.4: light intensity edit triggers LightingChanged only");
        }
        {
            RenderWorld assetWorld = baseWorld;
            assetWorld.Skybox.HDRPath = "missing_asset_path_check.hdr";
            const auto inv = reflService.EvaluateInvalidation(
                baseStamps, cpuStampRenderer.ComputeDynamicCaptureDomainStamps(assetWorld, baseProbe, baseSettings));
            RENDER_CHECK(inv.AssetChanged && inv.LightingChanged,
                "M15-U5-21.5: skybox HDR path update triggers AssetChanged");
        }

        // Cache byte footprint & LRU eviction planning (M15-U5-25)
        const uint64_t expectedBytes64 = 2ull * 6ull * 8ull * (4096ull + 1024ull + 256ull + 64ull + 16ull);
        RENDER_CHECK(EstimateDynamicProbeCacheBytes(64u, 5u, nri::Format::RGBA16_SFLOAT) == expectedBytes64,
            "M15-U5-25: EstimateDynamicProbeCacheBytes matches dual 6-face RGBA16_FLOAT mip-chain footprint");

        const std::vector<CaptureCacheSlotInfo> lruEntries{
            {"probeA", 500ull, 10ull, false},
            {"probeB", 500ull, 30ull, false},
            {"probeC", 500ull, 20ull, false}
        };
        std::vector<std::string> evictKeys;
        std::string budgetDiag;
        RENDER_CHECK(!ComputeLRUCacheEvictionPlan(lruEntries, 400ull, 500ull, &evictKeys, &budgetDiag),
            "M15-U5-25: single probe exceeding total cache budget fails deterministically");
        RENDER_CHECK(ComputeLRUCacheEvictionPlan(lruEntries, 1500ull, 700ull, &evictKeys, &budgetDiag) &&
                     evictKeys.size() == 2 && evictKeys[0] == "probeA" && evictKeys[1] == "probeC",
            "M15-U5-25: LRU eviction selects oldest sequence entries first (probeA then probeC, keeping probeB)");

        // Bounded per-frame capture schedule (M15-U5-23.1)
        const std::vector<ScheduledCaptureWork> queue{
            {"pHigh", 2u, 1000u},
            {"pLow", 6u, 500u}
        };
        const auto singleProbePlan = ComputeFrameCaptureSchedule(queue, 1u, 6u);
        RENDER_CHECK(singleProbePlan.size() == 1 && singleProbePlan[0].ProbeKey == "pHigh" && singleProbePlan[0].FacesToRun == 2u,
            "M15-U5-23.1: maxProbesPerFrame=1 bounds execution to highest priority probe");
        const auto sharedBudgetPlan = ComputeFrameCaptureSchedule(queue, 2u, 4u);
        RENDER_CHECK(sharedBudgetPlan.size() == 2 &&
                     sharedBudgetPlan[0].ProbeKey == "pHigh" && sharedBudgetPlan[0].FacesToRun == 2u &&
                     sharedBudgetPlan[1].ProbeKey == "pLow" && sharedBudgetPlan[1].FacesToRun == 2u,
            "M15-U5-23.1: maxFacesPerFrame=4 caps total faces across probes per frame");
    }

    // 7. M15-U5-E (S5.5): BlendDistance extraction/YAML round-trip, 512B GPU probe CBV contract,
    //    and unified ReflectionProbeCommon / ForwardPBR / MaterialSurface / SSR shader energy policy.
    {
        Scene blendScene;
        auto probeEntity = blendScene.CreateEntityWithUUID(555001ULL, "BlendProbe");
        auto& rpc = probeEntity.AddComponent<ReflectionProbeComponent>();
        rpc.Shape = ProbeShape::Sphere;
        rpc.Radius = 7.5f;
        rpc.BlendDistance = 2.25f;
        rpc.ParallaxCorrected = true;
        rpc.Intensity = 1.4f;

        const RenderWorld extractedBlend = ExtractRenderWorld(blendScene);
        RENDER_CHECK(extractedBlend.ReflectionProbes.size() == 1 &&
                     extractedBlend.ReflectionProbes[0].Shape == 1 &&
                     std::abs(extractedBlend.ReflectionProbes[0].Radius - 7.5f) < 1e-4f &&
                     std::abs(extractedBlend.ReflectionProbes[0].BlendDistance - 2.25f) < 1e-4f,
            "M15-U5-27.1 / 27.2: SceneExtraction propagates Shape, Radius and BlendDistance");

        SceneSerializer srcSer(&blendScene);
        const std::string yamlText = srcSer.SerializeToString();
        RENDER_CHECK(yamlText.find("BlendDistance: ") != std::string::npos,
            "M15-U5-27.1 / 27.2: SceneSerializer writes BlendDistance for reflection probes");

        Scene loadedScene;
        SceneSerializer dstSer(&loadedScene);
        RENDER_CHECK(dstSer.DeserializeFromString(yamlText),
            "M15-U5-27.1 / 27.2: SceneSerializer deserializes ReflectionProbe BlendDistance");
        const RenderWorld extractedLoaded = ExtractRenderWorld(loadedScene);
        RENDER_CHECK(extractedLoaded.ReflectionProbes.size() == 1 &&
                     extractedLoaded.ReflectionProbes[0].Shape == 1 &&
                     std::abs(extractedLoaded.ReflectionProbes[0].BlendDistance - 2.25f) < 1e-4f,
            "M15-U5-27.1 / 27.2: BlendDistance survives full YAML round-trip");

        // Legacy scenes omitting BlendDistance must deserialize cleanly with BlendDistance = 0
        std::string legacyYaml = yamlText;
        const size_t bdPos = legacyYaml.find("      BlendDistance:");
        RENDER_CHECK(bdPos != std::string::npos, "M15-U5-E legacy yaml fixture strips BlendDistance line");
        const size_t bdEnd = legacyYaml.find('\n', bdPos);
        legacyYaml.erase(bdPos, bdEnd != std::string::npos ? (bdEnd - bdPos + 1) : std::string::npos);
        Scene legacyScene;
        SceneSerializer legacySer(&legacyScene);
        RENDER_CHECK(legacySer.DeserializeFromString(legacyYaml),
            "M15-U5-E: legacy scene YAML without BlendDistance loads cleanly");
        const RenderWorld extractedLegacy = ExtractRenderWorld(legacyScene);
        RENDER_CHECK(extractedLegacy.ReflectionProbes.size() == 1 &&
                     extractedLegacy.ReflectionProbes[0].BlendDistance == 0.0f,
            "M15-U5-E: omitted BlendDistance defaults to 0.0f");

        static_assert(MaxActiveReflectionProbes == 4u, "Up to 4 reflection probes are bound per view");
        static_assert(sizeof(RHIGPUReflectionProbe) == 112u, "RHIGPUReflectionProbe matches HLSL float4 x 7 layout");
        static_assert(sizeof(RHIReflectionProbeBufferData) == 512u, "RHIReflectionProbeBufferData must match 512-byte CBV");
        RENDER_CHECK(sizeof(RHIReflectionProbeBufferData) == 512u,
            "M15-U5-28: RHIReflectionProbeBufferData maintains 512-byte GPU constant buffer layout");

        const std::string commonShader = ReadText(AXLE_REFLECTION_PROBE_COMMON_SHADER_PATH);
        RENDER_CHECK(!commonShader.empty() &&
                     commonShader.find("EvaluateProbeInfluenceHLSL") != std::string::npos &&
                     commonShader.find("ApplyProbeParallaxHLSL") != std::string::npos &&
                     commonShader.find("EvaluateMultiProbeBlendHLSL") != std::string::npos &&
                     commonShader.find("smoothstep") != std::string::npos &&
                     commonShader.find("u_ReflectionProbeCube0") != std::string::npos &&
                     commonShader.find("u_ReflectionProbeCube3") != std::string::npos,
            "M15-U5-27.1..29: ReflectionProbeCommon.hlsli provides shared C1 influence, per-probe parallax, and 4-slot blend");

        const std::string forwardShader = ReadText(AXLE_FORWARD_PBR_SHADER_PATH);
        const std::string surfaceShader = ReadText(AXLE_MATERIAL_SURFACE_SHADER_PATH);
        RENDER_CHECK(!forwardShader.empty() && !surfaceShader.empty() &&
                     forwardShader.find("#include \"ReflectionProbeCommon.hlsli\"") != std::string::npos &&
                     forwardShader.find("EvaluateMultiProbeBlendHLSL") != std::string::npos &&
                     surfaceShader.find("#include \"ReflectionProbeCommon.hlsli\"") != std::string::npos &&
                     surfaceShader.find("EvaluateMultiProbeBlendHLSL") != std::string::npos,
            "M15-U5-28 / 30.2: ForwardPBR.hlsl and MaterialSurface.hlsl share the canonical multi-probe + sky blend path");

        // SSR energy behavior is verified with actual forward terms and native
        // readbacks, rather than requiring a particular shader expression.
    }

    // 8. M15-U5-F (S5.6): Material-aware SSR cone footprint, roughness fade,
    //    dielectric vs conductor F0, and shader resolve bindings.
    {
        RENDER_CHECK(SSRRaymarchContract::EvaluateRoughnessFade(0.05f) == 1.0f &&
                     SSRRaymarchContract::EvaluateRoughnessFade(0.65f) == 1.0f &&
                     SSRRaymarchContract::EvaluateRoughnessFade(0.80f) > 0.1f &&
                     SSRRaymarchContract::EvaluateRoughnessFade(0.80f) < 0.9f &&
                     SSRRaymarchContract::EvaluateRoughnessFade(0.98f) == 0.0f,
            "M15-U5-34.1 / 38.1: EvaluateRoughnessFade smoothly tapers rough reflections down to 0");

        const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(45.0f), 16.0f / 9.0f, 0.1f, 100.0f);
        const float rLow = SSRRaymarchContract::ComputeRoughnessConePixelRadius(0.05f, 3.0f, -6.0f, proj, 1280u, 720u);
        const float rMid = SSRRaymarchContract::ComputeRoughnessConePixelRadius(0.30f, 3.0f, -6.0f, proj, 1280u, 720u);
        const float rHigh = SSRRaymarchContract::ComputeRoughnessConePixelRadius(0.65f, 3.0f, -6.0f, proj, 1280u, 720u);
        const float rFar = SSRRaymarchContract::ComputeRoughnessConePixelRadius(0.30f, 9.0f, -6.0f, proj, 1280u, 720u);
        RENDER_CHECK(rLow < rMid && rMid < rHigh && rMid < rFar && rHigh <= SSRRaymarchContract::MaxConeRadiusPixels,
            "M15-U5-34.1: Roughness cone radius expands monotonically with roughness and hit distance");

        const glm::vec3 blueDielectricF0 = SSRRaymarchContract::ComputeSpecularF0({0.02f, 0.12f, 0.95f}, 0.0f);
        const glm::vec3 goldConductorF0 = SSRRaymarchContract::ComputeSpecularF0({1.0f, 0.71f, 0.29f}, 1.0f);
        RENDER_CHECK(std::abs(blueDielectricF0.r - 0.04f) < 1e-5f &&
                     std::abs(blueDielectricF0.g - 0.04f) < 1e-5f &&
                     std::abs(blueDielectricF0.b - 0.04f) < 1e-5f &&
                     goldConductorF0.r > goldConductorF0.g && goldConductorF0.g > goldConductorF0.b * 2.0f,
            "M15-U5-36.1 / 36.2: Dielectric baseColor yields achromatic F0=0.04 while conductor preserves tinted F0");

        const std::string commonShader = ReadText(AXLE_REFLECTION_PROBE_COMMON_SHADER_PATH);
        const std::string ssrShader = ReadText(AXLE_SSR_SHADER_PATH);
        const std::string forwardShader = ReadText(AXLE_FORWARD_PBR_SHADER_PATH);
        const std::string surfaceShader = ReadText(AXLE_MATERIAL_SURFACE_SHADER_PATH);
        RENDER_CHECK(!commonShader.empty() && !ssrShader.empty() && !forwardShader.empty() && !surfaceShader.empty() &&
                     commonShader.find("MakeMaterialReflectionSurfaceOutput") != std::string::npos &&
                     ssrShader.find("DownsampleColorCS") != std::string::npos &&
                     ssrShader.find("u_SSRColor0") != std::string::npos &&
                     ssrShader.find("u_SSRColor3") != std::string::npos &&
                     forwardShader.find("MakeMaterialReflectionSurfaceOutput") != std::string::npos &&
                     surfaceShader.find("MakeMaterialReflectionSurfaceOutput") != std::string::npos &&
                     surfaceShader.find("reflectionResponse *= 0.02") == std::string::npos,
            "M15-U5-35.2 / 35.4: Shaders declare material reflection roughness/flag channel, SSRColor0..3, and calibrated resolve");
    }

    // 9. M15-U5-G / U5-H (S5.7): Validated LTC tables, rectangle/disk horizon-clipped
    //    contour integrals, units/orientation/sidedness/range, and numerical quadrature parity.
    {
        const auto ltcTables = AreaLightLTCContract::BuildValidatedLTCTables();
        std::string tableReason;
        RENDER_CHECK(AreaLightLTCContract::ValidateLTCTables(ltcTables, &tableReason),
            ("M15-U5-40: LTC tables satisfy determinant, range and finiteness invariants: " + tableReason).c_str());

        // Bilinear lookup continuity across roughness and view angle
        const glm::vec4 sampleLowR = AreaLightLTCContract::SampleLTC1(0.10f, 0.90f);
        const glm::vec4 sampleMidR = AreaLightLTCContract::SampleLTC1(0.45f, 0.60f);
        const glm::vec4 sampleDiff = AreaLightLTCContract::SampleLTC1(1.00f, 1.00f);
        const glm::vec4 ltc2Normal = AreaLightLTCContract::SampleLTC2(0.15f, 0.95f);
        const glm::vec4 ltc2Grazing = AreaLightLTCContract::SampleLTC2(0.15f, 0.10f);
        RENDER_CHECK(sampleDiff.x > 0.0f && sampleDiff.w > 0.0f &&
                     (sampleLowR.x * sampleLowR.w - sampleLowR.y * sampleLowR.z) > 0.0f &&
                     (sampleMidR.x * sampleMidR.w - sampleMidR.y * sampleMidR.z) > 0.0f &&
                     ltc2Grazing.y > ltc2Normal.y && ltc2Normal.x > 0.7f,
            "M15-U5-40: SampleLTC1/LTC2 interpolate smoothly and satisfy fitted GGX determinant/Fresnel limits");

        // Orthonormal basis resilience for non-unit or collinear Up vectors (M15-U5-41.3)
        glm::vec3 bRight, bUp, bDir;
        AreaLightLTCContract::ComputeOrthonormalAreaBasis({0.0f, -3.0f, 4.0f}, {0.0f, -3.0f, 4.0f}, bRight, bUp, bDir);
        RENDER_CHECK(std::abs(glm::length(bRight) - 1.0f) < 1e-4f &&
                     std::abs(glm::length(bUp) - 1.0f) < 1e-4f &&
                     std::abs(glm::length(bDir) - 1.0f) < 1e-4f &&
                     std::abs(glm::dot(bRight, bUp)) < 1e-4f &&
                     std::abs(glm::dot(bRight, bDir)) < 1e-4f &&
                     std::abs(glm::dot(bUp, bDir)) < 1e-4f,
            "M15-U5-41.3: ComputeOrthonormalAreaBasis handles collinear Up safely");

        auto makeFrame = [](AreaLightShape shape) {
            LightingRectFrame f{};
            f.Position = {0.0f, 2.5f, -3.0f};
            f.Direction = glm::normalize(glm::vec3(0.0f, -2.5f, 3.0f));
            f.Up = {0.0f, 1.0f, 0.0f};
            f.Width = 2.0f;
            f.Height = 2.0f;
            f.Range = 20.0f;
            f.Color = {1.0f, 1.0f, 1.0f};
            f.SignedIntensity = 20.0f;
            f.Shape = shape;
            f.ShadowPolicy = AreaLightShadowPolicy::UnshadowedAnalyticLTC;
            return f;
        };

        const glm::vec3 P(0.0f, 0.0f, 0.0f);
        const glm::vec3 N(0.0f, 1.0f, 0.0f);
        const glm::vec3 V = glm::normalize(glm::vec3(0.0f, 2.5f, 3.0f));

        // Rectangle vs Disk area ratio (~pi/4 for square W==H) & elliptical rotation test (M15-U5-41.1, 41.2)
        const auto rectEval = AreaLightLTCContract::EvaluateLTCAreaLightCPU(
            makeFrame(AreaLightShape::Rectangle), P, N, V, {0.8f, 0.8f, 0.8f}, 0.0f, 0.5f);
        const auto diskEval = AreaLightLTCContract::EvaluateLTCAreaLightCPU(
            makeFrame(AreaLightShape::Disk), P, N, V, {0.8f, 0.8f, 0.8f}, 0.0f, 0.5f);
        const float ffRatio = diskEval.DiffuseFormFactor / std::max(rectEval.DiffuseFormFactor, 1e-6f);
        RENDER_CHECK(rectEval.DiffuseFormFactor > 0.02f && diskEval.DiffuseFormFactor > 0.02f &&
                     ffRatio > 0.72f && ffRatio < 0.84f,
            "M15-U5-41.1 / 41.2: Circular disk form factor matches pi/4 area ratio relative to enclosing square");

        // Elliptical aspect vs circular disk symmetry
        LightingRectFrame circleA = makeFrame(AreaLightShape::Disk);
        LightingRectFrame circleB = makeFrame(AreaLightShape::Disk);
        circleB.Up = glm::normalize(glm::cross(circleB.Direction, glm::vec3(1.0f, 0.0f, 0.0f)));
        const auto cEvalA = AreaLightLTCContract::EvaluateLTCAreaLightCPU(circleA, P, N, V, {0.8f, 0.8f, 0.8f}, 0.0f, 0.5f);
        const auto cEvalB = AreaLightLTCContract::EvaluateLTCAreaLightCPU(circleB, P, N, V, {0.8f, 0.8f, 0.8f}, 0.0f, 0.5f);
        RENDER_CHECK(std::abs(cEvalA.DiffuseFormFactor - cEvalB.DiffuseFormFactor) < 0.002f,
            "M15-U5-41.2: Equal-diameter disk emitter is invariant under tangent basis rotation");

        // A circular disk must not acquire an eight-sided sharp specular outline when its tangent rotates.
        LightingRectFrame edgeDisk = makeFrame(AreaLightShape::Disk);
        edgeDisk.Position = {1.0f, 3.0f, 0.0f};
        edgeDisk.Direction = {0.0f, -1.0f, 0.0f};
        float minimumEdgeSpecular = 1000.0f, maximumEdgeSpecular = 0.0f;
        for (int phase = 0; phase < 5; ++phase) {
            const float angle = float(phase) * 3.14159265359f / 64.0f; // Include phases between contour vertices.
            edgeDisk.Up = {std::sin(angle), 0.0f, std::cos(angle)};
            const auto rotatedEdge = AreaLightLTCContract::EvaluateLTCAreaLightCPU(
                edgeDisk, P, N, N, glm::vec3(1.0f), 1.0f, 0.15f);
            minimumEdgeSpecular = std::min(minimumEdgeSpecular, rotatedEdge.SpecularRadiance.r);
            maximumEdgeSpecular = std::max(maximumEdgeSpecular, rotatedEdge.SpecularRadiance.r);
        }
        std::cout << "[U5-G Disk Edge Rotation] minimum=" << minimumEdgeSpecular
                  << " maximum=" << maximumEdgeSpecular << "\n";
        RENDER_CHECK(minimumEdgeSpecular > 0.01f &&
                     maximumEdgeSpecular - minimumEdgeSpecular < 0.03f * maximumEdgeSpecular,
            "M15-U5-41.2 / 45.1: circular disk sharp reflection is stable under emitter tangent rotation");

        // Horizon-straddling wall emitter intersecting floor y=0 (M15-U5-41.1, 42.1)
        LightingRectFrame straddling = makeFrame(AreaLightShape::Rectangle);
        straddling.Position = {0.0f, 0.0f, -2.0f};
        straddling.Direction = {0.0f, 0.0f, 1.0f};
        straddling.Up = {0.0f, 1.0f, 0.0f};
        straddling.Width = 2.0f;
        straddling.Height = 4.0f; // spans y in [-2, +2], so bottom half is below y=0 horizon
        const auto horizonEval = AreaLightLTCContract::EvaluateLTCAreaLightCPU(
            straddling, P, N, V, {0.9f, 0.9f, 0.9f}, 0.0f, 0.6f);
        const auto horizonRef = AreaLightLTCContract::EvaluateAreaLightNumericalReferenceCPU(
            straddling, P, N, V, {0.9f, 0.9f, 0.9f}, 0.0f, 0.6f, 32u);
        RENDER_CHECK(horizonEval.DiffuseFormFactor > 0.01f &&
                     std::abs(horizonEval.DiffuseFormFactor - horizonRef.DiffuseFormFactor) < 0.01f,
            "M15-U5-41.1 / 42.1 / 45.3: Horizon-straddling quad clips cleanly to z>=0 and matches 32x32 quadrature");

        // One-sided back-face culling vs TwoSided symmetry & finite Range cutoff (M15-U5-41.4)
        LightingRectFrame oneSidedBack = makeFrame(AreaLightShape::Rectangle);
        oneSidedBack.Direction = -oneSidedBack.Direction;
        const auto oneBackEval = AreaLightLTCContract::EvaluateLTCAreaLightCPU(
            oneSidedBack, P, N, V, {1.0f, 1.0f, 1.0f}, 0.0f, 0.5f);
        oneSidedBack.SignedIntensity = -oneSidedBack.SignedIntensity;
        const auto twoBackEval = AreaLightLTCContract::EvaluateLTCAreaLightCPU(
            oneSidedBack, P, N, V, {1.0f, 1.0f, 1.0f}, 0.0f, 0.5f);
        RENDER_CHECK(oneBackEval.TotalRadiance == glm::vec3(0.0f) &&
                     std::abs(twoBackEval.DiffuseFormFactor - rectEval.DiffuseFormFactor) < 1e-4f,
            "M15-U5-41.4: One-sided emitter yields 0 behind light plane while TwoSided is symmetric");

        LightingRectFrame outOfRange = makeFrame(AreaLightShape::Disk);
        outOfRange.Range = 2.5f; // emitter at (0, 2.5, -3) is ~3.9m away (> 2.5m)
        const auto rangedEval = AreaLightLTCContract::EvaluateLTCAreaLightCPU(
            outOfRange, P, N, V, {1.0f, 1.0f, 1.0f}, 0.0f, 0.5f);
        RENDER_CHECK(rangedEval.TotalRadiance == glm::vec3(0.0f) && rangedEval.RangeAttenuation == 0.0f,
            "M15-U5-41.4: Finite Range cuts off energy cleanly beyond emitter distance");

        LightingRectFrame ellipse = makeFrame(AreaLightShape::Disk);
        ellipse.Position = glm::vec3(0.0f);
        ellipse.Direction = {0.0f, 0.0f, -1.0f};
        ellipse.Up = {0.0f, 1.0f, 0.0f};
        ellipse.Width = 8.0f; ellipse.Height = 2.0f;
        const glm::vec2 boundary(4.0f / std::sqrt(2.0f), 1.0f / std::sqrt(2.0f));
        const glm::vec2 exterior = boundary + 2.0f * glm::normalize(glm::vec2(boundary.x / 16.0f, boundary.y));
        RENDER_CHECK(std::abs(AreaLightLTCContract::ComputeEmitterClosestDistance(
            ellipse, {exterior.x, exterior.y, -3.0f}) - std::sqrt(13.0f)) < 1e-4f,
            "M15-U5-41.4: elliptical disk range uses Euclidean closest point at off-axis positions");

        // Roughness, view-angle & metallic partition sweep (M15-U5-42.1, 42.2, 45.1)
        const auto metalLowR = AreaLightLTCContract::EvaluateLTCAreaLightCPU(
            makeFrame(AreaLightShape::Rectangle), P, N, V, {1.0f, 0.71f, 0.29f}, 1.0f, 0.08f);
        const auto metalMidR = AreaLightLTCContract::EvaluateLTCAreaLightCPU(
            makeFrame(AreaLightShape::Rectangle), P, N, V, {1.0f, 0.71f, 0.29f}, 1.0f, 0.35f);
        const auto metalHighR = AreaLightLTCContract::EvaluateLTCAreaLightCPU(
            makeFrame(AreaLightShape::Rectangle), P, N, V, {1.0f, 0.71f, 0.29f}, 1.0f, 0.80f);
        RENDER_CHECK(metalLowR.DiffuseRadiance == glm::vec3(0.0f) &&
                     metalLowR.SpecularRadiance.r > metalMidR.SpecularRadiance.r * 1.2f &&
                     metalMidR.SpecularRadiance.r > metalHighR.SpecularRadiance.r * 1.2f &&
                     metalLowR.SpecularRadiance.r > metalLowR.SpecularRadiance.b * 2.0f,
            "M15-U5-42.2 / 45.1: Metallic area light response has zero diffuse, tinted F0, and monotonic roughness falloff");

        // Emitter tilt & 32x32 numerical quadrature reference agreement (M15-U5-45.2, 45.3)
        LightingRectFrame facing = makeFrame(AreaLightShape::Disk);
        facing.Position = {0.0f, 2.0f, 0.0f};
        facing.Direction = {0.0f, -1.0f, 0.0f};
        facing.Up = {0.0f, 0.0f, 1.0f};
        LightingRectFrame tilted45 = facing;
        tilted45.Direction = glm::normalize(glm::vec3(0.7071f, -0.7071f, 0.0f));
        const auto facingLTC = AreaLightLTCContract::EvaluateLTCAreaLightCPU(
            facing, P, N, glm::normalize(glm::vec3(0.0f, 1.0f, 0.5f)), {0.8f, 0.8f, 0.8f}, 0.0f, 0.55f);
        const auto facingRef = AreaLightLTCContract::EvaluateAreaLightNumericalReferenceCPU(
            facing, P, N, glm::normalize(glm::vec3(0.0f, 1.0f, 0.5f)), {0.8f, 0.8f, 0.8f}, 0.0f, 0.55f, 32u);
        const auto tiltedLTC = AreaLightLTCContract::EvaluateLTCAreaLightCPU(
            tilted45, P, N, glm::normalize(glm::vec3(0.0f, 1.0f, 0.5f)), {0.8f, 0.8f, 0.8f}, 0.0f, 0.55f);
        const auto tiltedRef = AreaLightLTCContract::EvaluateAreaLightNumericalReferenceCPU(
            tilted45, P, N, glm::normalize(glm::vec3(0.0f, 1.0f, 0.5f)), {0.8f, 0.8f, 0.8f}, 0.0f, 0.55f, 32u);
        RENDER_CHECK(tiltedLTC.DiffuseFormFactor < facingLTC.DiffuseFormFactor * 0.90f &&
                     std::abs(facingLTC.DiffuseFormFactor - facingRef.DiffuseFormFactor) < 0.015f &&
                     std::abs(tiltedLTC.DiffuseFormFactor - tiltedRef.DiffuseFormFactor) < 0.015f,
            "M15-U5-45.2 / 45.3: Tilted emitter decreases irradiance and LTC matches 32x32 numerical quadrature");

        // Independent specular reference: table finiteness and diffuse parity do not validate a GGX fit.
        for (AreaLightShape shape : {AreaLightShape::Rectangle, AreaLightShape::Disk}) {
            for (float nv : {1.0f, 0.6f, 0.3f}) {
                for (float rough : {0.35f, 0.55f, 0.8f}) {
                    const glm::vec3 view(std::sqrt(1.0f - nv * nv), nv, 0.0f);
                    LightingRectFrame referenceLight = makeFrame(shape);
                    referenceLight.Position = glm::vec3(-view.x, view.y, 0.0f) * 3.0f;
                    referenceLight.Direction = -glm::normalize(referenceLight.Position);
                    referenceLight.Width = 1.5f;
                    referenceLight.Height = 1.5f;
                    const auto fitted = AreaLightLTCContract::EvaluateLTCAreaLightCPU(
                        referenceLight, P, N, view, glm::vec3(1.0f), 1.0f, rough);
                    const auto integrated = AreaLightLTCContract::EvaluateAreaLightNumericalReferenceCPU(
                        referenceLight, P, N, view, glm::vec3(1.0f), 1.0f, rough, 128u);
                    std::cout << "[U5-G Specular Reference] shape=" << int(shape) << " nv=" << nv
                              << " r=" << rough << " ltc=" << fitted.SpecularRadiance.r
                              << " ggx=" << integrated.SpecularRadiance.r << "\n";
                    RENDER_CHECK(std::abs(fitted.SpecularRadiance.r - integrated.SpecularRadiance.r) <
                                 0.20f * integrated.SpecularRadiance.r + 0.002f,
                        "M15-U5-40 / 42.2 / 45.3: specular LTC agrees with independent dense GGX integration");
                }
            }
        }

        // Explicit unshadowed shadow policy contract (M15-U5-43)
        RENDER_CHECK(RectLightComponent{}.ShadowPolicy == AreaLightShadowPolicy::UnshadowedAnalyticLTC &&
                     RenderRectLight{}.ShadowPolicy == AreaLightShadowPolicy::UnshadowedAnalyticLTC,
            "M15-U5-43: Area lights default to explicit UnshadowedAnalyticLTC policy");

        // Shader contract source verification (M15-U5-40..42)
        const std::string pbrCommon = ReadText(AXLE_PBR_COMMON_SHADER_PATH);
        const std::string forwardShader = ReadText(AXLE_FORWARD_PBR_SHADER_PATH);
        const std::string surfaceShader = ReadText(AXLE_MATERIAL_SURFACE_SHADER_PATH);
        RENDER_CHECK(!pbrCommon.empty() && !forwardShader.empty() && !surfaceShader.empty() &&
                     pbrCommon.find("EvaluateLTCAreaLightTerms") != std::string::npos &&
                     pbrCommon.find("LTC_ClipPolygonZ0") != std::string::npos &&
                     pbrCommon.find("SampleLTC1") != std::string::npos &&
                     pbrCommon.find("SampleLTC2") != std::string::npos &&
                     forwardShader.find("EvaluateLTCAreaLightTerms") != std::string::npos &&
                     surfaceShader.find("EvaluateLTCAreaLightTerms") != std::string::npos,
            "M15-U5-40..42: PBRCommon.hlsli, ForwardPBR.hlsl and MaterialSurface.hlsl share validated LTC area-light evaluation");
    }

    // 10. M15-U5-H1 (S5.8 Part 1): Planar surface contract, mirrored camera, ZO oblique clip projection,
    //     RHIViewTargets extent computation, policy filtering, and domain invalidation.
    {
        RenderPlanarReflector floorRefl{};
        floorRefl.EntityID = 500;
        floorRefl.StableUUID = 900500ULL;
        floorRefl.Position = {0.0f, 0.0f, -4.0f};
        floorRefl.Normal = {0.0f, 1.0f, 0.0f};
        floorRefl.Tangent = {1.0f, 0.0f, 0.0f};
        floorRefl.Bitangent = {0.0f, 0.0f, 1.0f};
        floorRefl.HalfExtents = {4.0f, 4.0f};
        floorRefl.ResolutionScale = 0.5f;
        floorRefl.EdgeFadeDistance = 0.25f;
        floorRefl.MaxRoughness = 0.45f;

        const PlanarReflectionContract::PlanarReflectionSurface floorSurface =
            PlanarReflectionContract::BuildSurfaceFromReflector(floorRefl);
        RENDER_CHECK(floorSurface.Valid &&
                     glm::length(floorSurface.Normal - glm::vec3(0.0f, 1.0f, 0.0f)) < 1e-5f,
            "M15-U5-46.1: BuildSurfaceFromReflector constructs orthonormal valid surface");

        const glm::vec3 samplePt(1.25f, 3.5f, -5.0f);
        const glm::vec3 reflectedPt = PlanarReflectionContract::ReflectPoint(floorSurface, samplePt);
        const glm::vec3 doubleReflected = PlanarReflectionContract::ReflectPoint(floorSurface, reflectedPt);
        RENDER_CHECK(glm::length(reflectedPt - glm::vec3(1.25f, -3.5f, -5.0f)) < 1e-4f &&
                     glm::length(doubleReflected - samplePt) < 1e-4f,
            "M15-U5-46.1: ReflectPoint reflects across plane and is an exact involution");

        const glm::mat4 reflMat = PlanarReflectionContract::BuildReflectionMatrix(floorSurface);
        const glm::mat4 reflSq = reflMat * reflMat;
        float maxIdentityErr = 0.0f;
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                maxIdentityErr = std::max(maxIdentityErr, std::abs(reflSq[c][r] - (c == r ? 1.0f : 0.0f)));
            }
        }
        RENDER_CHECK(std::abs(glm::determinant(glm::mat3(reflMat)) + 1.0f) < 1e-4f && maxIdentityErr < 1e-4f,
            "M15-U5-46.1 / 46.2: reflection matrix has det == -1 and R^2 == I");

        // Camera in front of plane: mirrored view, winding flip, and ZO oblique near-plane clip test
        PerspectiveCamera mainCam(60.0f, 16.0f / 9.0f, 0.1f, 200.0f);
        mainCam.SetViewMatrix(
            glm::lookAt(glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f, 0.0f, -4.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
            glm::vec3(0.0f, 2.0f, 0.0f));
        PlanarReflectionContract::PlanarReflectionPolicy capturePolicy;
        const auto mirrored = PlanarReflectionContract::BuildMirroredCamera(mainCam, floorSurface, capturePolicy);
        RENDER_CHECK(mirrored.Valid && mirrored.ReverseWinding && mirrored.ObliqueClippingApplied &&
                     glm::length(mirrored.Camera.GetPosition() - glm::vec3(0.0f, -2.0f, 0.0f)) < 1e-4f &&
                     glm::determinant(glm::mat3(mirrored.Camera.GetViewMatrix())) < 0.0f,
            "M15-U5-46.2: BuildMirroredCamera derives reflected position/view and sets ReverseWinding=true");

        // Virtual-image projection invariant: mirrored view * real point == main view * reflected point
        const glm::vec4 viewRealInMirrored = mirrored.Camera.GetViewMatrix() * glm::vec4(samplePt, 1.0f);
        const glm::vec4 viewReflInMain = mainCam.GetViewMatrix() * glm::vec4(reflectedPt, 1.0f);
        RENDER_CHECK(glm::length(glm::vec3(viewRealInMirrored) - glm::vec3(viewReflInMain)) < 1e-4f,
            "M15-U5-46.2: mirrored view maps real point identically to main view seeing virtual reflection");

        // Verify ZO oblique clip matrix places mirror plane at z_ndc == 0, clips behind (< 0), keeps in front [0, 1]
        const glm::mat4 mvpObliqueZO = mirrored.Camera.GetViewProjectionMatrixZO();
        auto evalZndc = [&](const glm::vec3& p) {
            const glm::vec4 clip = mvpObliqueZO * glm::vec4(p, 1.0f);
            return clip.z / clip.w;
        };
        const float zOnPlane = evalZndc({0.0f, 0.0f, -4.0f});
        const float zBehindPlane = evalZndc({0.0f, -1.0f, -2.0f});
        const float zInFrontNear = evalZndc({0.0f, 0.5f, -5.0f});
        const float zInFrontFar = evalZndc({0.0f, 20.0f, -50.0f});
        RENDER_CHECK(std::abs(zOnPlane) < 1e-4f && zBehindPlane < -0.05f &&
                     zInFrontNear > 0.0f && zInFrontNear <= 1.0f &&
                     zInFrontFar > zInFrontNear && zInFrontFar <= 1.0001f,
            "M15-U5-46.6: ZO oblique clip maps mirror plane to z_ndc=0, clips behind (<0), preserves in-front [0,1]");

        // Tilted mirror plane (35 deg X rotation) + identity limit check against standard perspectiveRH_ZO
        RenderPlanarReflector tiltedRefl = floorRefl;
        tiltedRefl.Position = {1.0f, 1.5f, -3.0f};
        const float tiltRad = glm::radians(35.0f);
        tiltedRefl.Normal = glm::normalize(glm::vec3(0.2f, std::cos(tiltRad), std::sin(tiltRad)));
        const auto tiltedSurface = PlanarReflectionContract::BuildSurfaceFromReflector(tiltedRefl);
        PerspectiveCamera tiltedCam(50.0f, 1.3333f, 0.1f, 150.0f);
        tiltedCam.SetViewMatrix(
            glm::lookAt(glm::vec3(1.5f, 3.0f, 2.0f), tiltedRefl.Position, glm::vec3(0.0f, 1.0f, 0.0f)),
            glm::vec3(1.5f, 3.0f, 2.0f));
        const auto tiltedMirrored = PlanarReflectionContract::BuildMirroredCamera(tiltedCam, tiltedSurface, capturePolicy);
        const glm::mat4 tiltedVP = tiltedMirrored.Camera.GetViewProjectionMatrixZO();
        const glm::vec3 tiltedOnPlane = tiltedRefl.Position + tiltedSurface.Tangent * 0.8f;
        const glm::vec3 tiltedBehind = tiltedRefl.Position - tiltedSurface.Normal * 0.75f;
        const glm::vec3 tiltedFront = tiltedRefl.Position + tiltedSurface.Normal * 2.0f;
        const glm::vec4 cOn = tiltedVP * glm::vec4(tiltedOnPlane, 1.0f);
        const glm::vec4 cBehind = tiltedVP * glm::vec4(tiltedBehind, 1.0f);
        const glm::vec4 cFront = tiltedVP * glm::vec4(tiltedFront, 1.0f);
        RENDER_CHECK(tiltedMirrored.Valid && tiltedMirrored.ObliqueClippingApplied &&
                     std::abs(cOn.z / cOn.w) < 1e-4f &&
                     (cBehind.z / cBehind.w) < -0.01f &&
                     (cFront.z / cFront.w) > 0.0f && (cFront.z / cFront.w) <= 1.0001f,
            "M15-U5-46.6: tilted oblique ZO projection clips behind-plane geometry and preserves in-front depth order");

        // Identity limit: near-plane C_view = (0, 0, -1, -near) reproduces glm::perspectiveRH_ZO
        const glm::mat4 stdZO = glm::perspectiveRH_ZO(glm::radians(60.0f), 1.5f, 0.25f, 100.0f);
        bool exactApplied = false;
        glm::mat4 exactGL{};
        const glm::mat4 exactOblique = PlanarReflectionContract::BuildObliqueClipProjectionZO(
            stdZO, glm::mat4(1.0f), glm::vec4(0.0f, 0.0f, -1.0f, -0.25f), nullptr, &exactApplied, &exactGL);
        float maxZOErr = 0.0f;
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                maxZOErr = std::max(maxZOErr, std::abs(exactOblique[c][r] - stdZO[c][r]));
            }
        }
        RENDER_CHECK(exactApplied && maxZOErr < 1e-5f,
            "M15-U5-46.6: ZO oblique clip on camera near plane equals standard perspectiveRH_ZO");

        auto authored = floorRefl;
        authored.ClipPlaneOffset = 0.03f;
        authored.CaptureNearPlane = 0.4f;
        authored.CaptureFarPlane = 20.0f;
        const auto authoredSurface = PlanarReflectionContract::BuildSurfaceFromReflector(authored);
        const auto authoredCamera = PlanarReflectionContract::BuildMirroredCamera(mainCam, authoredSurface, capturePolicy);
        RENDER_CHECK(authoredCamera.Valid && std::abs(authoredCamera.WorldPlane.w + 0.03f) < 1e-5f &&
            std::abs(authoredCamera.Camera.GetNearClip() - 0.4f) < 1e-5f &&
            std::abs(authoredCamera.Camera.GetFarClip() - 20.0f) < 1e-5f,
            "M15-U5-46.1 / 46.2: authored capture bias and depth range control the mirrored camera");
        const glm::vec3 obliqueNormal = glm::normalize(glm::vec3(std::sqrt(1.0f - 0.98f * 0.98f), 0.98f, 0.0f));
        const float smoothNormal = PlanarReflectionContract::EvaluatePlanarReflectionConfidence(
            floorSurface, floorSurface.Position, obliqueNormal, 0.05f);
        RENDER_CHECK(std::abs(smoothNormal - 0.7407407f) < 0.001f &&
            PlanarReflectionContract::EvaluatePlanarReflectionConfidence(
                floorSurface, floorSurface.Position + floorSurface.Normal * 0.03f, floorSurface.Normal, 0.05f) == 0.0f,
            "M15-U5-46.5: CPU normal and geometric-plane confidence match shader smoothstep thresholds");

        auto smallSurface = authoredSurface;
        smallSurface.HalfExtents = {0.1f, 0.1f};
        smallSurface.EdgeFadeDistance = 0.2f;
        RENDER_CHECK(std::abs(PlanarReflectionContract::EvaluatePlanarReflectionConfidence(
            smallSurface, smallSurface.Position, smallSurface.Normal, 0.0f) - 1.0f) < 0.001f &&
            PlanarReflectionContract::EvaluatePlanarReflectionConfidence(
                smallSurface, smallSurface.Position, smallSurface.Normal, 0.6f, 1.0f) == 0.0f,
            "M15-U5-46.5: oversized edge fade and global roughness cap match native resolve");

        // Camera behind plane must fail cleanly
        PerspectiveCamera behindCam = mainCam;
        behindCam.SetViewMatrix(glm::mat4(1.0f), {0.0f, -1.0f, 0.0f});
        const auto invalidMirrored = PlanarReflectionContract::BuildMirroredCamera(behindCam, floorSurface, capturePolicy);
        RENDER_CHECK(!invalidMirrored.Valid && !invalidMirrored.Diagnostic.empty(),
            "M15-U5-46.2: camera behind reflector plane is explicitly rejected with diagnostic");

        // Recursion prevention & entity self-exclusion
        SceneRenderSettings noisySettings;
        noisySettings.EnablePlanarReflections = true;
        noisySettings.EnableSSR = true;
        noisySettings.EnableTAA = true;
        noisySettings.MSAASamples = 4;
        const SceneRenderSettings cleanCapture = PlanarReflectionContract::SanitizeCaptureSettings(noisySettings, capturePolicy);
        RENDER_CHECK(!cleanCapture.EnablePlanarReflections && !cleanCapture.EnableSSR &&
                     !cleanCapture.EnableTAA && cleanCapture.MSAASamples == 1u,
            "M15-U5-46.2: SanitizeCaptureSettings disables planar recursion, SSR, TAA and MSAA");

        RenderWorld srcWorld;
        srcWorld.PlanarReflectors.push_back(floorRefl);
        RenderMeshInstance mirrorMesh{};
        mirrorMesh.EntityID = 500;
        RenderMeshInstance sceneMesh{};
        sceneMesh.EntityID = 501;
        srcWorld.Meshes.push_back(mirrorMesh);
        srcWorld.Meshes.push_back(sceneMesh);
        const RenderWorld capWorld = PlanarReflectionContract::FilterCaptureWorld(srcWorld, floorRefl, capturePolicy);
        RENDER_CHECK(capWorld.PlanarReflectors.empty() && capWorld.Meshes.size() == 1 && capWorld.Meshes[0].EntityID == 501u,
            "M15-U5-46.2 / 46.6: FilterCaptureWorld disables nested reflectors and excludes self-reflector entity");

        auto zeroIndexReflector = floorRefl;
        zeroIndexReflector.EntityID = 0;
        zeroIndexReflector.StableUUID = UUID(505);
        srcWorld.Meshes.front().EntityID = 0;
        srcWorld.Meshes.front().StableUUID = zeroIndexReflector.StableUUID;
        const auto zeroIndexCapture = PlanarReflectionContract::FilterCaptureWorld(srcWorld, zeroIndexReflector, {});
        RENDER_CHECK(zeroIndexCapture.Meshes.size() == 1 && zeroIndexCapture.Meshes.front().EntityID == 501,
            "M15-U5-46.6: stable UUID excludes a receiver at valid EnTT entity index zero");

        // Confidence, edge fade & roughness rolloff
        const float cCenterSmooth = PlanarReflectionContract::EvaluatePlanarReflectionConfidence(
            floorSurface, {0.0f, 0.0f, -4.0f}, {0.0f, 1.0f, 0.0f}, 0.05f);
        const float cEdgeFade = PlanarReflectionContract::EvaluatePlanarReflectionConfidence(
            floorSurface, {3.9f, 0.0f, -4.0f}, {0.0f, 1.0f, 0.0f}, 0.05f);
        const float cOutsideExtent = PlanarReflectionContract::EvaluatePlanarReflectionConfidence(
            floorSurface, {4.5f, 0.0f, -4.0f}, {0.0f, 1.0f, 0.0f}, 0.05f);
        const float cRoughCutoff = PlanarReflectionContract::EvaluatePlanarReflectionConfidence(
            floorSurface, {0.0f, 0.0f, -4.0f}, {0.0f, 1.0f, 0.0f}, 0.50f);
        RENDER_CHECK(cCenterSmooth > 0.99f && cEdgeFade > 0.0f && cEdgeFade < 0.6f &&
                     cOutsideExtent == 0.0f && cRoughCutoff == 0.0f,
            "M15-U5-46.1 / 46.5 / 46.6: planar confidence fades at extents and cuts off outside plane or high roughness");

        // Extent scaling, feature hash & invalidation flags
        const glm::uvec2 halfExtent = PlanarReflectionContract::ComputeScaledTargetExtent(1919u, 1079u, 0.5f);
        RENDER_CHECK(halfExtent.x == 960u && halfExtent.y == 540u &&
                     RHI::RHIViewTargets::ComputePlanarTargetDimension(100u, 0.1f) == 25u &&
                     RHI::RHIViewTargets::ComputePlanarTargetDimension(0u, 0.5f) == 0u,
            "M15-U5-46.3: planar target scaling clamps safely to [0.25, 1.0] and handles odd extents");

        SceneRenderSettings basePlanarSettings;
        SceneRenderSettings activePlanarSettings;
        activePlanarSettings.EnablePlanarReflections = true;
        RENDER_CHECK(HashViewFeaturePolicy(basePlanarSettings) != HashViewFeaturePolicy(activePlanarSettings),
            "M15-U5-46.3: HashViewFeaturePolicy tracks EnablePlanarReflections changes");

        PlanarReflectionContract::PlanarReflectionDomainStamps s0{};
        s0.PlaneStamp = 1; s0.CameraStamp = 1; s0.TargetWidth = 640; s0.TargetHeight = 360;
        auto sMoved = s0; sMoved.CameraStamp = 2;
        const auto invCam = PlanarReflectionContract::EvaluateInvalidation(s0, sMoved, false);
        RENDER_CHECK(invCam.AnyChange() && invCam.CameraChanged && !invCam.PlaneChanged && !invCam.TargetExtentChanged,
            "M15-U5-46.7: PlanarReflectionInvalidation isolates per-domain change signals");

        // H2 Resolve shader, RenderGraph pass, and policy parity inspection
        const auto readText = [](const char* path) {
            std::ifstream in(path);
            std::stringstream ss;
            if (in.is_open()) ss << in.rdbuf();
            return ss.str();
        };
        const std::string planarShader = readText(AXLE_PLANAR_REFLECTION_SHADER_PATH);
        const std::string planarPassSrc = readText(AXLE_PLANAR_PASS_SOURCE_PATH);
        const std::string scenePassesSrc = readText(AXLE_SCENE_PASSES_SOURCE_PATH);
        const std::string renderServiceSrc = readText(AXLE_RENDER_SERVICE_SOURCE_PATH);
        RENDER_CHECK(!planarShader.empty() &&
                     planarShader.find("VSMain") != std::string::npos &&
                     planarShader.find("PSMain") != std::string::npos &&
                     planarShader.find("u_ReflectedViewProj") != std::string::npos &&
                     planarShader.find("u_OpaqueColor") != std::string::npos &&
                     planarShader.find("u_EnvironmentSpecular") != std::string::npos &&
                     planarShader.find("u_ReflectionResponse") != std::string::npos &&
                     planarShader.find("u_PlanarColor") != std::string::npos &&
                     planarShader.find("SanitizeHDRColor") != std::string::npos &&
                     planarShader.find("baseLit + planarSpec") != std::string::npos,
            "M15-U5-46.5: PlanarReflection.hlsl implements sanitized unified convex resolve");
        RENDER_CHECK(!planarPassSrc.empty() &&
                     planarPassSrc.find("PlanarCopy") != std::string::npos &&
                     planarPassSrc.find("PlanarResolve") != std::string::npos &&
                     planarPassSrc.find("PlanarRelease") != std::string::npos &&
                     planarPassSrc.find("GetOrCreatePlanarApplyPipeline") != std::string::npos,
            "M15-U5-46.4: PlanarReflectionPass declares explicit copy/resolve/release DAG edges");
        RENDER_CHECK(scenePassesSrc.find("PlanarReflectionPass::Declare") != std::string::npos &&
                     scenePassesSrc.find("PlanarReflectionPass::Record") != std::string::npos &&
                     renderServiceSrc.find("EnablePlanarReflections") != std::string::npos,
            "M15-U5-46.4 / 46.5: RHISceneRendererPasses and RenderService wire PlanarReflectionPass and target policy parity");

        PlanarReflectionCaptureStatus st{};
        st.State = PlanarCaptureLifecycleState::Idle;
        RENDER_CHECK(st.State == PlanarCaptureLifecycleState::Idle && !st.HasCapture && !st.Failed,
            "M15-U5-46.7: PlanarReflectionCaptureStatus defaults cleanly to Idle");
    }

    std::cout << "[M15ReflectionCapture] All dynamic reflection capture contract tests passed!\n";
    return true;
}
