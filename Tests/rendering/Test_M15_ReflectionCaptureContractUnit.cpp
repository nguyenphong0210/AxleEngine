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

        const std::string ssrShader = ReadText(AXLE_SSR_SHADER_PATH);
        RENDER_CHECK(!ssrShader.empty() &&
                     ssrShader.find("#include \"ReflectionProbeCommon.hlsli\"") != std::string::npos &&
                     ssrShader.find("EvaluateMultiProbeBlendHLSL") != std::string::npos &&
                     ssrShader.find("envReplacementDelta") != std::string::npos &&
                     ssrShader.find("(ssrColor - fallbackEnv)") != std::string::npos &&
                     ssrShader.find("u_ProbeCubemap") == std::string::npos,
            "M15-U5-30.1 / 30.3 / 31.5: SSR.hlsl replaces fallbackEnv proportionally via (ssrColor - fallbackEnv) under one energy policy");
    }

    std::cout << "[M15ReflectionCapture] All dynamic reflection capture contract tests passed!\n";
    return true;
}
