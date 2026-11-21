#include "RenderTestCommon.hpp"
#include "RHIReadback.hpp"

#include <Axle/Components.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/Log.hpp>
#include <Axle/RHIMeshRegistry.hpp>
#include <Axle/RHISceneRenderer.hpp>
#include <Axle/RenderBackendPolicy.hpp>
#include <Axle/RenderContracts.hpp>
#include <Axle/RenderQualityPreset.hpp>
#include <Axle/RenderService.hpp>
#include <Axle/Scene.hpp>
#include <Axle/SceneExtraction.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/Window.hpp>
#include <Axle/RHI/RHIBuffer.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHIDiagnostics.hpp>
#include <Axle/RHI/RHIPresentationContext.hpp>
#include <Axle/RHI/RHIViewTargets.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace Axle;
using namespace Axle::RHI;

namespace {

    using Clock = std::chrono::steady_clock;

    double ElapsedMs(Clock::time_point start, Clock::time_point end) {
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    struct MetricStats {
        double Min = 0.0;
        double P50 = 0.0;
        double P95 = 0.0;
        double P99 = 0.0;
        double Max = 0.0;
        double Mean = 0.0;
        size_t Count = 0;
    };

    double PercentileSorted(const std::vector<double>& sorted, double fraction) {
        if (sorted.empty()) return 0.0;
        if (sorted.size() == 1) return sorted.front();
        const double pos = std::clamp(fraction, 0.0, 1.0) * static_cast<double>(sorted.size() - 1);
        const size_t lo = static_cast<size_t>(std::floor(pos));
        const size_t hi = std::min(lo + 1, sorted.size() - 1);
        const double t = pos - static_cast<double>(lo);
        return sorted[lo] * (1.0 - t) + sorted[hi] * t;
    }

    MetricStats Summarize(std::vector<double> samples) {
        MetricStats stats{};
        if (samples.empty()) return stats;
        std::sort(samples.begin(), samples.end());
        stats.Count = samples.size();
        stats.Min = samples.front();
        stats.P50 = PercentileSorted(samples, 0.50);
        stats.P95 = PercentileSorted(samples, 0.95);
        stats.P99 = PercentileSorted(samples, 0.99);
        stats.Max = samples.back();
        const double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
        stats.Mean = sum / static_cast<double>(samples.size());
        return stats;
    }

    enum class PassCategory {
        Shadows,
        AOAndNormals,
        Reflections,
        Fog,
        GI,
        SSS,
        GeometryAndOpaque,
        PostProcess,
        Export,
        Other
    };

    bool StartsWith(std::string_view value, std::string_view prefix) {
        return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
    }

    PassCategory ClassifyPass(std::string_view name) {
        if (name == "CascadedShadows" || name == "LocalShadows") return PassCategory::Shadows;
        if (name == "SurfaceNormal" || name == "SurfaceNormalPrepass" || name == "SurfaceNormals" ||
            StartsWith(name, "GTAO") || StartsWith(name, "ContactShadow")) {
            return PassCategory::AOAndNormals;
        }
        if (StartsWith(name, "HiZ") || StartsWith(name, "SSR") || StartsWith(name, "Planar") ||
            StartsWith(name, "Capture")) {
            return PassCategory::Reflections;
        }
        if (StartsWith(name, "Fog")) return PassCategory::Fog;
        if (StartsWith(name, "DDGI")) return PassCategory::GI;
        if (StartsWith(name, "SSS")) return PassCategory::SSS;
        if (StartsWith(name, "AutoExposure") || StartsWith(name, "Bloom") || StartsWith(name, "TAA") ||
            name == "ToneMapping" || name == "FXAA") {
            return PassCategory::PostProcess;
        }
        if (name == "OpaqueAndSky" || name == "MotionVectors" || name == "RefractionCopy" ||
            name == "Transparent" || name == "ParticleDepthCopy" || name == "ParticlesAndPhysics" ||
            name == "Resolve" || name == "ReactiveMask" || name == "PhysicsDebugOverlay") {
            return PassCategory::GeometryAndOpaque;
        }
        if (name == "Export") return PassCategory::Export;
        return PassCategory::Other;
    }

    bool IsLightingSubsetCategory(PassCategory cat) {
        return cat == PassCategory::Shadows ||
               cat == PassCategory::AOAndNormals ||
               cat == PassCategory::Reflections ||
               cat == PassCategory::Fog ||
               cat == PassCategory::GI ||
               cat == PassCategory::SSS;
    }

    const char* CategoryLabel(PassCategory cat) {
        switch (cat) {
            case PassCategory::Shadows: return "Shadows";
            case PassCategory::AOAndNormals: return "AOAndNormals";
            case PassCategory::Reflections: return "Reflections";
            case PassCategory::Fog: return "Fog";
            case PassCategory::GI: return "GI";
            case PassCategory::SSS: return "SSS";
            case PassCategory::GeometryAndOpaque: return "GeometryAndOpaque";
            case PassCategory::PostProcess: return "PostProcess";
            case PassCategory::Export: return "Export";
            default: return "Other";
        }
    }

    uint64_t FormatBytesPerPixel(RHITextureFormat fmt) {
        switch (fmt) {
            case RHITextureFormat::R8_UNORM: return 1ull;
            case RHITextureFormat::RG16_FLOAT:
            case RHITextureFormat::R32_FLOAT:
            case RHITextureFormat::R32_UINT:
            case RHITextureFormat::D32_FLOAT:
            case RHITextureFormat::D24_UNORM_S8_UINT:
            case RHITextureFormat::RGBA8_UNORM:
            case RHITextureFormat::RGBA8_SRGB:
            case RHITextureFormat::BGRA8_UNORM: return 4ull;
            case RHITextureFormat::RGBA16_FLOAT: return 8ull;
            case RHITextureFormat::RGBA32_FLOAT: return 16ull;
            default: return 4ull;
        }
    }

    uint64_t EstimateTextureBytes(const RHITexture& tex) {
        const auto& desc = tex.GetDesc();
        const uint64_t bpp = FormatBytesPerPixel(desc.Format);
        const uint64_t baseSlice = static_cast<uint64_t>(std::max(1u, desc.Width)) *
                                   static_cast<uint64_t>(std::max(1u, desc.Height)) *
                                   static_cast<uint64_t>(std::max(1u, desc.Depth)) *
                                   bpp *
                                   static_cast<uint64_t>(std::max(1u, desc.SampleCount)) *
                                   static_cast<uint64_t>(std::max(1u, desc.ArrayLayers)) *
                                   static_cast<uint64_t>(desc.IsCubeMap ? 6u : 1u);
        uint64_t total = baseSlice;
        uint32_t mw = std::max(1u, desc.Width);
        uint32_t mh = std::max(1u, desc.Height);
        uint32_t md = std::max(1u, desc.Depth);
        for (uint32_t m = 1; m < std::max(1u, desc.MipLevels); ++m) {
            mw = std::max(1u, mw / 2u);
            mh = std::max(1u, mh / 2u);
            md = std::max(1u, md / 2u);
            total += static_cast<uint64_t>(mw) * static_cast<uint64_t>(mh) * static_cast<uint64_t>(md) *
                     bpp *
                     static_cast<uint64_t>(std::max(1u, desc.ArrayLayers)) *
                     static_cast<uint64_t>(desc.IsCubeMap ? 6u : 1u);
        }
        return total;
    }

    struct ViewTargetsInventory {
        uint32_t ResidentTextures = 0;
        uint64_t TotalBytes = 0;
        uint64_t ColorDepthMSAABytes = 0;
        uint64_t HistoryVelocityBytes = 0;
        uint64_t ReflectionsHiZBytes = 0;
        uint64_t Fog3DBytes = 0;
        uint64_t AOAndSSSBytes = 0;
        uint64_t BloomAndExposureBytes = 0;
        uint64_t DDGIBytes = 0;
    };

    ViewTargetsInventory InspectViewTargets(const RHIViewTargets& targets) {
        ViewTargetsInventory inv{};
        auto accumulate = [&](RHIViewTarget slot, const RHITexture* tex) {
            if (!tex || !tex->IsValid()) return;
            const uint64_t bytes = EstimateTextureBytes(*tex);
            ++inv.ResidentTextures;
            inv.TotalBytes += bytes;
            switch (slot) {
                case RHIViewTarget::Color:
                case RHIViewTarget::Depth:
                case RHIViewTarget::MSAAColor:
                case RHIViewTarget::MSAADepth:
                case RHIViewTarget::HDR:
                case RHIViewTarget::FXAA:
                case RHIViewTarget::OpaqueColor:
                case RHIViewTarget::OpaqueDepth:
                case RHIViewTarget::ParticleDepth:
                case RHIViewTarget::ScreenNormal:
                case RHIViewTarget::ScreenStorage:
                    inv.ColorDepthMSAABytes += bytes;
                    break;
                case RHIViewTarget::History0:
                case RHIViewTarget::History1:
                case RHIViewTarget::Velocity:
                case RHIViewTarget::Reactive:
                case RHIViewTarget::TAAHistoryDepth:
                case RHIViewTarget::MotionPreviousDepth:
                    inv.HistoryVelocityBytes += bytes;
                    break;
                case RHIViewTarget::HiZ0:
                case RHIViewTarget::HiZ1:
                case RHIViewTarget::HiZ2:
                case RHIViewTarget::HiZ3:
                case RHIViewTarget::HiZ4:
                case RHIViewTarget::HiZ5:
                case RHIViewTarget::HiZ6:
                case RHIViewTarget::HiZ7:
                case RHIViewTarget::ReflectionEnvironment:
                case RHIViewTarget::ReflectionResponse:
                case RHIViewTarget::SSRColor0:
                case RHIViewTarget::SSRColor1:
                case RHIViewTarget::SSRColor2:
                case RHIViewTarget::SSRColor3:
                case RHIViewTarget::PlanarColor:
                case RHIViewTarget::PlanarDepth:
                    inv.ReflectionsHiZBytes += bytes;
                    break;
                case RHIViewTarget::FroxelScattering:
                case RHIViewTarget::FroxelIntegrated:
                case RHIViewTarget::FroxelHistory0:
                case RHIViewTarget::FroxelHistory1:
                    inv.Fog3DBytes += bytes;
                    break;
                case RHIViewTarget::GTAO:
                case RHIViewTarget::GTAOResolved:
                case RHIViewTarget::SubsurfaceDiffuse:
                case RHIViewTarget::SubsurfaceControl:
                case RHIViewTarget::SubsurfaceTemp:
                case RHIViewTarget::SubsurfaceCrisp:
                    inv.AOAndSSSBytes += bytes;
                    break;
                case RHIViewTarget::Bloom0:
                case RHIViewTarget::Bloom1:
                case RHIViewTarget::Bloom2:
                case RHIViewTarget::Bloom3:
                case RHIViewTarget::Bloom4:
                case RHIViewTarget::Bloom5:
                case RHIViewTarget::Exposure:
                    inv.BloomAndExposureBytes += bytes;
                    break;
                case RHIViewTarget::DDGI:
                case RHIViewTarget::DDGIDistance:
                    inv.DDGIBytes += bytes;
                    break;
                default:
                    break;
            }
        };

        for (size_t i = 0; i < static_cast<size_t>(RHIViewTarget::Count); ++i) {
            const auto slot = static_cast<RHIViewTarget>(i);
            accumulate(slot, targets.Get(slot));
        }
        for (uint32_t lvl = 8; lvl < targets.GetHiZLevelCount(); ++lvl) {
            accumulate(RHIViewTarget::HiZ0, targets.GetHiZ(lvl));
        }
        return inv;
    }

    struct WorkloadSpec {
        std::shared_ptr<Scene> ScenePtr;
        PerspectiveCamera Camera;
        Entity CenterpieceEntity{};
        Entity MovingSpotEntity{};
        uint32_t TotalEntities = 0;
        uint32_t MeshEntities = 0;
        uint64_t TotalVertices = 0;
        uint64_t TotalTriangles = 0;
        uint32_t DirectionalLights = 0;
        uint32_t PointLights = 0;
        uint32_t SpotLights = 0;
        uint32_t RectLights = 0;
        uint32_t DynamicReflectionProbes = 0;
        uint32_t PlanarReflectors = 0;
        uint32_t DDGIProbeCount = 0;
    };

    WorkloadSpec BuildWorkloadScene(bool includeDDGI, float aspect) {
        WorkloadSpec spec{};
        spec.ScenePtr = std::make_shared<Scene>();
        auto& scene = *spec.ScenePtr;

        // Skybox + atmospheric lighting
        {
            auto e = scene.CreateEntity("WorkloadSkybox");
            auto& sky = e.AddComponent<SkyboxComponent>();
            sky.Enabled = true;
            sky.EnvironmentLighting = true;
            sky.SyncWithSun = true;
            sky.Intensity = 1.0f;
            sky.Exposure = 1.0f;
            sky.Turbidity = 2.0f;
        }

        // Primary directional sun with 4-cascade CSM
        {
            auto e = scene.CreateEntity("SunLight");
            auto& tc = e.GetComponent<TransformComponent>();
            tc.Rotation = glm::quat(glm::radians(glm::vec3(-48.0f, 34.0f, 0.0f)));
            auto& dir = e.AddComponent<DirectionalLightComponent>();
            dir.Color = {1.0f, 0.97f, 0.92f};
            dir.Intensity = 1.0f;
            dir.AmbientIntensity = 0.25f;
            dir.CastShadows = true;
            dir.ShadowQuality = 1; // 2048 CSM
            dir.ShadowFilterRadius = 2.0f;
            dir.MaxShadowDistance = 120.0f;
            ++spec.DirectionalLights;
        }

        // Volumetric froxel fog
        {
            auto e = scene.CreateEntity("WorkloadFog");
            auto& fog = e.AddComponent<VolumetricFogComponent>();
            fog.Enabled = true;
            fog.Density = 0.015f;
            fog.HeightFalloff = 0.12f;
            fog.ScatteringAsymmetry = 0.65f;
            fog.Color = {0.72f, 0.80f, 0.90f};
            fog.MaxDistance = 90.0f;
            fog.Intensity = 1.0f;
        }

        // Dynamic reflection probe
        {
            auto e = scene.CreateEntity("WorkloadReflectionProbe");
            e.GetComponent<TransformComponent>().Translation = {0.0f, 3.0f, 0.0f};
            auto& probe = e.AddComponent<ReflectionProbeComponent>();
            probe.Enabled = true;
            probe.DynamicCapture = true;
            probe.CaptureResolution = 128;
            probe.ParallaxCorrected = true;
            probe.BoxExtents = {14.0f, 8.0f, 14.0f};
            probe.CaptureShadows = true;
            probe.Intensity = 1.0f;
            ++spec.DynamicReflectionProbes;
        }

        // Reflective floor with PlanarReflectionComponent
        {
            auto e = scene.CreateEntity("PlanarFloor");
            auto& tc = e.GetComponent<TransformComponent>();
            tc.Translation = {0.0f, 0.0f, 0.0f};
            tc.Scale = {16.0f, 1.0f, 16.0f};
            auto& mesh = e.AddComponent<MeshInstance3D>();
            mesh.AssetPath = "builtin://plane";
            mesh.PatternMode = 0;
            mesh.TileScale = 8.0f;
            mesh.Color1 = {0.82f, 0.84f, 0.88f, 1.0f};
            mesh.Color2 = {0.32f, 0.35f, 0.40f, 1.0f};
            auto& mat = e.AddComponent<MaterialComponent>();
            mat.AlbedoColor = {0.72f, 0.74f, 0.78f, 1.0f};
            mat.Metallic = 0.85f;
            mat.Roughness = 0.10f;
            auto& planar = e.AddComponent<PlanarReflectionComponent>();
            planar.Enabled = true;
            planar.HalfExtents = {14.0f, 14.0f};
            planar.ResolutionScale = 0.50f;
            planar.MaxRoughness = 0.45f;
            planar.CaptureShadows = true;
            planar.CaptureSkybox = true;
            ++spec.MeshEntities;
            ++spec.PlanarReflectors;
        }

        // 4x4 PBR sphere roughness/metallic sweep
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                auto e = scene.CreateEntity("SweepSphere_" + std::to_string(row * 4 + col));
                auto& tc = e.GetComponent<TransformComponent>();
                tc.Translation = {(col - 1.5f) * 2.2f, 1.15f, (row - 1.5f) * 2.2f};
                tc.Scale = {0.85f, 0.85f, 0.85f};
                auto& mesh = e.AddComponent<MeshInstance3D>();
                mesh.AssetPath = "builtin://sphere";
                mesh.PatternMode = 3;
                auto& mat = e.AddComponent<MaterialComponent>();
                const float rough = 0.06f + 0.28f * static_cast<float>(col);
                const float metal = static_cast<float>(row) / 3.0f;
                mat.AlbedoColor = {0.85f - 0.15f * row, 0.45f + 0.12f * col, 0.35f + 0.15f * row, 1.0f};
                mat.Roughness = rough;
                mat.Metallic = metal;
                ++spec.MeshEntities;
            }
        }

        // 4 Subsurface Scattering spheres
        for (int i = 0; i < 4; ++i) {
            auto e = scene.CreateEntity("SSSSphere_" + std::to_string(i));
            auto& tc = e.GetComponent<TransformComponent>();
            tc.Translation = {(i - 1.5f) * 2.4f, 1.0f, 5.2f};
            tc.Scale = {0.75f, 0.75f, 0.75f};
            auto& mesh = e.AddComponent<MeshInstance3D>();
            mesh.AssetPath = "builtin://sphere";
            mesh.PatternMode = 3;
            auto& mat = e.AddComponent<MaterialComponent>();
            mat.AlbedoColor = {0.92f, 0.72f, 0.65f, 1.0f};
            mat.Roughness = 0.38f;
            mat.Metallic = 0.0f;
            mat.SubsurfaceEnabled = true;
            mat.SubsurfaceColor = {1.0f, 0.30f, 0.22f};
            mat.SubsurfaceRadius = 1.2f;
            ++spec.MeshEntities;
        }

        // 2 Emissive bloom beacons
        for (int i = 0; i < 2; ++i) {
            auto e = scene.CreateEntity("EmissiveBeacon_" + std::to_string(i));
            auto& tc = e.GetComponent<TransformComponent>();
            tc.Translation = {(i == 0 ? -5.8f : 5.8f), 1.4f, -3.5f};
            tc.Scale = {0.7f, 1.4f, 0.7f};
            auto& mesh = e.AddComponent<MeshInstance3D>();
            mesh.AssetPath = "builtin://cylinder";
            mesh.PatternMode = 3;
            auto& mat = e.AddComponent<MaterialComponent>();
            mat.AlbedoColor = {0.2f, 0.2f, 0.25f, 1.0f};
            mat.Roughness = 0.25f;
            mat.EmissionColor = (i == 0) ? glm::vec3{1.0f, 0.62f, 0.22f} : glm::vec3{0.25f, 0.75f, 1.0f};
            mat.EmissionIntensity = 5.0f;
            ++spec.MeshEntities;
        }

        // 12 Architectural shadow casters (cubes & cylinders)
        for (int i = 0; i < 12; ++i) {
            const float angle = (static_cast<float>(i) / 12.0f) * 6.2831853f;
            const float radius = 8.6f;
            auto e = scene.CreateEntity("Pillar_" + std::to_string(i));
            auto& tc = e.GetComponent<TransformComponent>();
            tc.Translation = {std::cos(angle) * radius, 2.2f, std::sin(angle) * radius};
            tc.Scale = {0.9f, 4.4f, 0.9f};
            auto& mesh = e.AddComponent<MeshInstance3D>();
            const bool cyl = (i % 3 == 0);
            mesh.AssetPath = cyl ? "builtin://cylinder" : "builtin://cube";
            mesh.PatternMode = 1;
            mesh.TileScale = 2.0f;
            auto& mat = e.AddComponent<MaterialComponent>();
            mat.AlbedoColor = {0.65f, 0.67f, 0.70f, 1.0f};
            mat.Roughness = 0.55f;
            mat.Metallic = 0.1f;
            ++spec.MeshEntities;
        }

        // 1 Hero centerpiece mesh (animated in soak)
        {
            auto e = scene.CreateEntity("HeroCenterpiece");
            auto& tc = e.GetComponent<TransformComponent>();
            tc.Translation = {0.0f, 2.7f, 0.0f};
            tc.Scale = {1.15f, 1.15f, 1.15f};
            auto& mesh = e.AddComponent<MeshInstance3D>();
            mesh.AssetPath = "builtin://sphere";
            mesh.PatternMode = 2;
            mesh.TileScale = 4.0f;
            auto& mat = e.AddComponent<MaterialComponent>();
            mat.AlbedoColor = {0.92f, 0.76f, 0.42f, 1.0f};
            mat.Metallic = 0.95f;
            mat.Roughness = 0.08f;
            spec.CenterpieceEntity = e;
            ++spec.MeshEntities;
        }

        // 4 Point lights (2 shadowed, 2 fill)
        for (int i = 0; i < 4; ++i) {
            auto e = scene.CreateEntity("PointLight_" + std::to_string(i));
            const float angle = static_cast<float>(i) * 1.570796f + 0.4f;
            e.GetComponent<TransformComponent>().Translation = {std::cos(angle) * 5.0f, 3.2f, std::sin(angle) * 5.0f};
            auto& pt = e.AddComponent<PointLightComponent>();
            pt.Color = (i % 2 == 0) ? glm::vec3{1.0f, 0.78f, 0.55f} : glm::vec3{0.48f, 0.76f, 1.0f};
            pt.Intensity = 3.5f;
            pt.Radius = 16.0f;
            pt.Shadows.CastShadows = (i < 2);
            ++spec.PointLights;
        }

        // 2 Spot lights (shadowed)
        for (int i = 0; i < 2; ++i) {
            auto e = scene.CreateEntity("SpotLight_" + std::to_string(i));
            auto& tc = e.GetComponent<TransformComponent>();
            tc.Translation = {(i == 0 ? -4.5f : 4.5f), 6.0f, 4.5f};
            tc.Rotation = glm::quat(glm::radians(glm::vec3(-42.0f, (i == 0 ? -35.0f : 35.0f), 0.0f)));
            auto& spot = e.AddComponent<SpotLightComponent>();
            spot.Color = {1.0f, 0.95f, 0.88f};
            spot.Intensity = 9.0f;
            spot.Range = 28.0f;
            spot.InnerConeAngle = 18.0f;
            spot.OuterConeAngle = 34.0f;
            spot.Shadows.CastShadows = true;
            if (i == 0) spec.MovingSpotEntity = e;
            ++spec.SpotLights;
        }

        // 1 Analytic LTC Rect Light
        {
            auto e = scene.CreateEntity("StudioRectLight");
            auto& tc = e.GetComponent<TransformComponent>();
            tc.Translation = {0.0f, 5.5f, -5.5f};
            auto& rect = e.AddComponent<RectLightComponent>();
            rect.Enabled = true;
            rect.Shape = AreaLightShape::Rectangle;
            rect.Color = {0.85f, 0.92f, 1.0f};
            rect.Intensity = 12.0f;
            rect.Width = 4.0f;
            rect.Height = 1.5f;
            rect.Range = 22.0f;
            ++spec.RectLights;
        }

        // Optional DDGI probe grid
        if (includeDDGI) {
            auto e = scene.CreateEntity("WorkloadDDGI");
            auto& ddgi = e.AddComponent<DDGIProbeGridComponent>();
            ddgi.Enabled = true;
            ddgi.GridDimensions = {8, 4, 8};
            ddgi.GridSpacing = {2.5f, 2.0f, 2.5f};
            ddgi.GridOrigin = {0.0f, 2.5f, 0.0f};
            ddgi.RaysPerProbe = 96;
            ddgi.MaxProbesPerFrame = 8;
            ddgi.Hysteresis = 0.90f;
            spec.DDGIProbeCount = static_cast<uint32_t>(ddgi.GridDimensions.x * ddgi.GridDimensions.y * ddgi.GridDimensions.z);
        }

        spec.Camera = PerspectiveCamera(50.0f, aspect, 0.1f, 250.0f);
        const glm::vec3 camPos{0.0f, 4.5f, 13.5f};
        spec.Camera.SetViewMatrix(glm::lookAt(camPos, glm::vec3{0.0f, 1.2f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f}), camPos);

        // Add primary Scene Camera entity too so serialized .axlescene opens with identical camera
        {
            auto e = scene.CreateEntity("MainCamera");
            auto& tc = e.GetComponent<TransformComponent>();
            tc.Translation = camPos;
            tc.Rotation = glm::quat(glm::radians(glm::vec3(-13.8f, 0.0f, 0.0f)));
            auto& cam = e.AddComponent<CameraComponent>();
            cam.Primary = true;
            cam.FOV = 50.0f;
            cam.NearClip = 0.1f;
            cam.FarClip = 250.0f;
        }

        spec.TotalEntities = static_cast<uint32_t>(scene.GetRegistry().storage<entt::entity>().size());
        return spec;
    }

    void PopulateNativeMeshes(RenderWorld& world, WorkloadSpec* outCounts = nullptr) {
        auto& reg = RHIMeshRegistry::Get();
        uint64_t verts = 0;
        uint64_t tris = 0;
        for (auto& mesh : world.Meshes) {
            std::shared_ptr<RHIMesh> native;
            if (mesh.BuiltinId) {
                switch (*mesh.BuiltinId) {
                    case BuiltinMeshId::Cube: native = reg.GetCube(); break;
                    case BuiltinMeshId::Sphere: native = reg.GetSphere(); break;
                    case BuiltinMeshId::Cylinder: native = reg.GetCylinder(); break;
                    case BuiltinMeshId::Cone: native = reg.GetCone(); break;
                    case BuiltinMeshId::Plane: native = reg.GetPlane(); break;
                }
            }
            if (!native) {
                if (mesh.MeshURI == "builtin://cube") native = reg.GetCube();
                else if (mesh.MeshURI == "builtin://sphere") native = reg.GetSphere();
                else if (mesh.MeshURI == "builtin://cylinder") native = reg.GetCylinder();
                else if (mesh.MeshURI == "builtin://cone") native = reg.GetCone();
                else if (mesh.MeshURI == "builtin://plane") native = reg.GetPlane();
            }
            if (native && native->IsValid()) {
                mesh.NativeMesh = native;
                mesh.NativeView = native->CaptureView();
                verts += native->GetVertexCount();
                tris += native->GetIndexCount() / 3u;
            }
        }
        if (outCounts) {
            outCounts->TotalVertices = verts;
            outCounts->TotalTriangles = tris;
        }
    }

    SceneRenderSettings MakePresetSettings(const std::string& presetName) {
        SceneRenderSettings settings{};
        settings.EnableToneMapping = true;
        settings.ToneMappingMode = 0;
        if (presetName == "UltraDDGI") {
            ApplyRenderQualityPreset(settings, RenderQualityPreset::Ultra, true);
        } else {
            RenderQualityPreset preset = RenderQualityPreset::High;
            ParseRenderQualityPreset(presetName, preset);
            ApplyRenderQualityPreset(settings, preset, false);
        }
        return settings;
    }

    std::shared_ptr<RHIViewTargets> BuildConfiguredTargets(
        uint32_t width, uint32_t height,
        const SceneRenderSettings& authored,
        const RenderWorld& world,
        SceneRenderSettings& outEffective) {
        const auto policy = RenderBackendPolicy::FromDevice(RHIDevice::Get().GetCapabilities());
        const auto policyResolved = policy.ResolveSettings(authored);
        const RenderVolumetricFog* fogPtr =
            (policyResolved.EnableVolumetricFog && world.VolumetricFog && world.VolumetricFog->Enabled)
                ? &*world.VolumetricFog
                : nullptr;
        const RenderDDGIProbeGrid* ddgiPtr =
            (policyResolved.EnableDDGI && world.DDGIProbeGrid && world.DDGIProbeGrid->Enabled)
                ? &*world.DDGIProbeGrid
                : nullptr;
        outEffective = ViewTargetContract::ResolveEffectiveSettings(policyResolved, false, true, fogPtr);
        ViewTargetContract::ViewTargetPolicyInput input{};
        input.Width = width;
        input.Height = height;
        input.EnableDepth = true;
        input.OwnPrimary = true;
        input.RenderAuxiliary = true;
        input.IsSubCapture = false;
        input.WantsSubsurface = outEffective.EnableSubsurfaceScattering && outEffective.MSAASamples == 1;
        input.VolumetricFog = fogPtr;
        input.DDGIProbeGrid = ddgiPtr;
        const RHIViewTargetDesc desc = ViewTargetContract::BuildViewTargetDesc(outEffective, input);
        auto targets = std::make_shared<RHIViewTargets>();
        if (!targets->Configure(desc, nullptr, nullptr, FroxelHistoryPolicy::AllowStaticFog) ||
            !targets->Validate()) {
            return {};
        }
        return targets;
    }

    bool VerifyTextureHasFiniteRenderedPixels(RHITexture* tex) {
        if (!tex || !tex->IsValid()) return false;
        const auto pixels = RenderTests::ReadTexture(*tex);
        if (pixels.size() != static_cast<size_t>(tex->GetWidth()) * tex->GetHeight() * 4u) return false;
        uint64_t nonZero = 0;
        for (size_t i = 0; i < pixels.size(); i += 4) {
            if (pixels[i] > 2 || pixels[i + 1] > 2 || pixels[i + 2] > 2) {
                ++nonZero;
            }
        }
        return nonZero > 64;
    }

    std::string EscapeJson(std::string_view s) {
        std::ostringstream os;
        for (char c : s) {
            switch (c) {
                case '\\': os << "\\\\"; break;
                case '"': os << "\\\""; break;
                case '\n': os << "\\n"; break;
                case '\r': os << "\\r"; break;
                case '\t': os << "\\t"; break;
                default: os << c; break;
            }
        }
        return os.str();
    }

    struct PassAccum {
        std::vector<double> GPUMs;
        std::vector<double> CPUMs;
    };

    struct ConfigBenchmarkResult {
        std::string Backend;
        std::string Preset;
        uint32_t Width = 0;
        uint32_t Height = 0;
        double ColdInitMs = 0.0;
        double Frame0CPUMs = 0.0;
        double Frame0GPUMs = 0.0;
        uint32_t WarmupFrames = 0;
        uint32_t MeasuredFrames = 0;
        uint32_t ValidGPUTimestampFrames = 0;
        uint32_t DrawCalls = 0;
        uint32_t RenderedEntities = 0;
        uint32_t SceneTotalEntities = 0;
        uint32_t SceneMeshEntities = 0;
        uint64_t SceneVertices = 0;
        uint64_t SceneTriangles = 0;
        MetricStats CPUTotal;
        MetricStats GPUTotal;
        MetricStats GPULightingSubset;
        MetricStats GPUGeometryAndOpaque;
        MetricStats GPUPostProcess;
        std::map<std::string, MetricStats> PerPassGPU;
        std::map<std::string, MetricStats> PerPassCPU;
        uint64_t VRAMWarmupBytes = 0;
        uint64_t VRAMMidBytes = 0;
        uint64_t VRAMEndBytes = 0;
        uint64_t VRAMPeakBytes = 0;
        uint64_t VRAMBudgetBytes = 0;
        ViewTargetsInventory ViewInventory{};
        RenderQualityBudgetReport QualityBudget{};
        PlanarReflectionCacheStats PlanarStats{};
        DynamicReflectionCaptureCacheStats DynProbeStats{};
    };

    struct SoakResult {
        std::string Backend;
        std::string Preset;
        uint32_t Width = 0;
        uint32_t Height = 0;
        bool Validated = false;
        uint32_t WarmupFrames = 0;
        uint32_t MeasuredFrames = 0;
        uint64_t PresentCount = 0;
        MetricStats TotalFrameWallMs;
        MetricStats RenderServiceViewMs;
        uint64_t VRAMWarmupBytes = 0;
        uint64_t VRAMMidBytes = 0;
        uint64_t VRAMEndBytes = 0;
        uint64_t VRAMPeakBytes = 0;
        uint64_t VRAMBudgetBytes = 0;
        PlanarReflectionCacheStats PlanarStats{};
        DynamicReflectionCaptureCacheStats DynProbeStats{};
        uint64_t ValidationErrorCount = 0;
    };

    bool RunLifecycleTests(RHIType backend, bool validate) {
        std::cout << "\n=== U10-D2 Lifecycle & Recovery Suite (" << RHITypeToString(backend)
                  << ", validate=" << (validate ? "true" : "false") << ") ===\n";

        auto& device = RHIDevice::Get();
        RHIDeviceDesc init{};
        init.PreferredBackend = backend;
        init.AllowFallback = false;
        init.Headless = false;
        init.EnableNRIValidation = validate;
        init.EnableValidation = validate;
        if (validate) {
            RENDER_CHECK(RHIDevice::IsGraphicsAPIValidationAvailable(backend),
                         "API validation layer must be available for validated lifecycle checks");
        }

        WindowProps props{};
        props.Title = "U10D2 Lifecycle & Recovery";
        props.Width = 1280;
        props.Height = 720;
        props.Maximized = false;
        props.GraphicsBackend = (backend == RHIType::Vulkan) ? GraphicsAPI::Vulkan : GraphicsAPI::Direct3D12;
        Window window(props);
        RENDER_CHECK(window.GetNativeWindow() != nullptr, "GLFW window creation for presentation lifecycle");

        RENDER_CHECK(device.Init(init) && device.GetBackend() == backend && !device.IsSoftwareAdapter(),
                     "Strict native physical adapter initialization without fallback");
        RENDER_CHECK(RHIMeshRegistry::Get().Init(), "RHIMeshRegistry initializes");

        auto& service = RenderService::Get();
        RENDER_CHECK(service.Init(), "RenderService initializes cleanly");

        RHIPresentationContext presenter;
        RENDER_CHECK(presenter.Init(window.GetNativeWindow()), "RHIPresentationContext initializes cleanly");

        auto workload = BuildWorkloadScene(false, 16.0f / 9.0f);
        auto settings = MakePresetSettings("High");

        // M15-U10-12.1: Multi-cycle resize, odd extents, minimize (0x0) skip, restore, VSync toggle
        struct StepExtent { uint32_t W; uint32_t H; bool VSync; };
        const std::vector<StepExtent> cycleExtents = {
            {1280, 720, false},
            {1920, 1080, false},
            {2560, 1440, true},
            {320, 240, false},
            {1281, 721, false},
            {0, 0, false}, // Minimized skip
            {1920, 1080, true} // Restored
        };

        uint64_t cycle1Vram = 0;
        uint64_t cycleFinalVram = 0;
        RHICompletionToken epoch1LastToken{};

        for (unsigned cycle = 0; cycle < 4; ++cycle) {
            for (const auto& step : cycleExtents) {
                if (step.W == 0 || step.H == 0) {
                    for (int k = 0; k < 3; ++k) {
                        const auto outcome = presenter.RenderFrame(0, 0, step.VSync, {}, {}, nullptr);
                        RENDER_CHECK(outcome == RHIPresentOutcome::Skipped,
                                     "Minimized 0x0 present cleanly skips without swapchain failure");
                    }
                    continue;
                }
                workload.Camera.SetPerspective(50.0f, static_cast<float>(step.W) / static_cast<float>(step.H), 0.1f, 250.0f);
                workload.Camera.SetViewMatrix(glm::lookAt(glm::vec3{0.0f, 4.5f, 13.5f}, glm::vec3{0.0f, 1.2f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f}), glm::vec3{0.0f, 4.5f, 13.5f});
                for (int frame = 0; frame < 4; ++frame) {
                    ViewRequest req{};
                    req.Kind = RenderViewKind::Main;
                    req.Width = step.W;
                    req.Height = step.H;
                    req.Camera = workload.Camera;
                    req.Settings = settings;
                    req.Name = "LifecycleMain";
                    req.DeltaTime = 1.0f / 60.0f;
                    const auto res = service.RenderView(*workload.ScenePtr, req);
                    RENDER_CHECK(res.Success && !res.UsedFallback, "RenderService succeeds after resize/restore");
                    auto* target = service.GetDefaultMainColorTarget();
                    RENDER_CHECK(target && target->GetWidth() == step.W && target->GetHeight() == step.H,
                                 "Color target matches requested resized extent");
                    const auto outcome = presenter.RenderFrame(step.W, step.H, step.VSync, {}, {}, target->GetNative());
                    RENDER_CHECK(outcome == RHIPresentOutcome::Presented, "Swapchain presents resized frame");
                    epoch1LastToken = res.CompletionToken;
                }
            }
            const uint64_t vram = RHIMemoryManager::QueryVRAMInfo(RHIMemoryLocation::Device).UsageBytes;
            if (cycle == 0) cycle1Vram = vram;
            cycleFinalVram = vram;
        }
        RENDER_CHECK(device.WaitIdle(), "WaitIdle after resize/minimize/restore cycles");
        RENDER_CHECK(VerifyTextureHasFiniteRenderedPixels(service.GetDefaultMainColorTarget()),
                     "Restored main view contains finite rendered geometry pixels");
        std::cout << "[PASS] M15-U10-12.1 resize/minimize/restore (4 cycles x 7 states, cycle1VRAM="
                  << (cycle1Vram / (1024.0 * 1024.0)) << " MiB, cycle4VRAM="
                  << (cycleFinalVram / (1024.0 * 1024.0)) << " MiB)\n";

        // M15-U10-12.2: View switching, camera cut invalidation, rapid preset switch, device reinit
        {
            const auto world = ExtractRenderWorld(static_cast<const Scene&>(*workload.ScenePtr));
            SceneRenderSettings dummyEff{};
            auto targetsMain = BuildConfiguredTargets(1280, 720, settings, world, dummyEff);
            auto targetsPreview = BuildConfiguredTargets(640, 480, settings, world, dummyEff);
            auto targetsThumb = BuildConfiguredTargets(256, 256, settings, world, dummyEff);
            RENDER_CHECK(targetsMain && targetsPreview && targetsThumb, "Independent view target pools configured");
            const uint64_t mainGen = targetsMain->GetGeneration();
            const uint64_t prevGen = targetsPreview->GetGeneration();
            const uint64_t thumbGen = targetsThumb->GetGeneration();

            for (int iter = 0; iter < 15; ++iter) {
                auto renderSub = [&](RenderViewKind kind, uint32_t w, uint32_t h, const std::shared_ptr<RHIViewTargets>& vt, bool cut, const char* name) -> bool {
                    ViewRequest req{};
                    req.Kind = kind;
                    req.Width = w;
                    req.Height = h;
                    req.ViewTargets = vt;
                    req.ColorTarget = vt->Get(RHIViewTarget::Color);
                    req.DepthTarget = vt->Get(RHIViewTarget::Depth);
                    req.Camera = workload.Camera;
                    req.Camera.SetPerspective(50.0f, static_cast<float>(w) / static_cast<float>(h), 0.1f, 250.0f);
                    req.Settings = settings;
                    req.CameraCut = cut;
                    req.Name = name;
                    const auto res = service.RenderView(world, req);
                    return res.Success && !res.UsedFallback;
                };
                const bool cut = (iter % 3 == 0);
                if (cut) {
                    const glm::vec3 altPos = (iter % 2 == 0) ? glm::vec3{0.0f, 4.5f, 13.5f} : glm::vec3{-11.0f, 6.0f, -9.0f};
                    workload.Camera.SetViewMatrix(glm::lookAt(altPos, glm::vec3{0.0f, 1.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f}), altPos);
                }
                RENDER_CHECK(renderSub(RenderViewKind::Main, 1280, 720, targetsMain, cut, "MainView"), "Main view switch succeeds");
                RENDER_CHECK(renderSub(RenderViewKind::AssetPreview, 640, 480, targetsPreview, cut, "PreviewView"), "Preview view switch succeeds");
                RENDER_CHECK(renderSub(RenderViewKind::Thumbnail, 256, 256, targetsThumb, cut, "ThumbnailView"), "Thumbnail view switch succeeds");
                RENDER_CHECK(targetsMain->GetGeneration() == mainGen &&
                             targetsPreview->GetGeneration() == prevGen &&
                             targetsThumb->GetGeneration() == thumbGen,
                             "Explicit view target generations remain isolated and do not thrash across view switches");
            }

            // Rapid preset switch through Low -> Medium -> High -> Ultra -> High -> Low
            for (const char* pName : {"Low", "Medium", "High", "Ultra", "High", "Low"}) {
                const auto pSettings = MakePresetSettings(pName);
                const bool expectsFullFeatureParity = (std::string_view(pName) != "Low");
                for (int f = 0; f < 3; ++f) {
                    ViewRequest req{};
                    req.Kind = RenderViewKind::Main;
                    req.Width = 1280;
                    req.Height = 720;
                    req.Camera = workload.Camera;
                    req.Settings = pSettings;
                    req.Name = "PresetSwitch";
                    const auto res = service.RenderView(world, req);
                    if (!res.Success || (expectsFullFeatureParity && res.UsedFallback)) {
                        std::cerr << "[PRESET FAIL] preset=" << pName << " success=" << res.Success
                                  << " usedFallback=" << res.UsedFallback << " err=" << res.ErrorMessage << "\n";
                        for (const auto& fb : res.Fallbacks) std::cerr << "  fallback: " << fb << "\n";
                    }
                    RENDER_CHECK(res.Success && (!expectsFullFeatureParity || !res.UsedFallback) &&
                                 service.GetOwnershipReport().Backend == backend,
                                 "Quality preset transition submits cleanly on active native backend");
                }
            }
        }

        // Full Device Shutdown & Epoch-2 Reinitialization
        const uint64_t epoch1 = device.GetEpoch();
        RENDER_CHECK(presenter.Shutdown(), "Presenter shutdown succeeds before device reinit");
        service.Shutdown();
        RHIMeshRegistry::Get().Shutdown();
        RENDER_CHECK(device.Shutdown(), "Device shutdown completes before Epoch-2 reinit");

        RENDER_CHECK(!device.WaitForCompletion(epoch1LastToken, 0),
                     "Pre-reinit completion token is rejected after device shutdown");
        RENDER_CHECK(device.Init(init) && device.GetEpoch() > epoch1 && !device.IsSoftwareAdapter(),
                     "Device cleanly reinitializes into a strictly greater epoch");
        RENDER_CHECK(RHIMeshRegistry::Get().Init() && service.Init() && presenter.Init(window.GetNativeWindow()),
                     "Services and swapchain cleanly reinitialize in Epoch 2");
        RENDER_CHECK(!device.WaitForCompletion(epoch1LastToken, 0),
                     "Epoch 1 completion token remains invalid inside Epoch 2");

        for (int f = 0; f < 10; ++f) {
            ViewRequest req{};
            req.Kind = RenderViewKind::Main;
            req.Width = 1280;
            req.Height = 720;
            req.Camera = workload.Camera;
            req.Settings = settings;
            req.Name = "Epoch2Main";
            const auto res = service.RenderView(*workload.ScenePtr, req);
            RENDER_CHECK(res.Success && res.CompletionToken.Epoch == device.GetEpoch(),
                         "Epoch 2 renders cleanly with updated token epoch");
            RENDER_CHECK(presenter.RenderFrame(1280, 720, false, {}, {},
                                               service.GetDefaultMainColorTarget()->GetNative()) == RHIPresentOutcome::Presented,
                         "Epoch 2 swapchain presentation succeeds");
        }
        RENDER_CHECK(VerifyTextureHasFiniteRenderedPixels(service.GetDefaultMainColorTarget()),
                     "Epoch 2 color output contains finite rendered pixels");

        presenter.Shutdown();
        service.Shutdown();
        RHIMeshRegistry::Get().Shutdown();
        device.Shutdown();

        RENDER_CHECK(GetRHIValidationErrorCount() == 0, "Zero RHI validation errors across lifecycle/reinit suite");
        std::cout << "[PASS] M15-U10-12.2 view switch / camera cut / preset switch / device reinit (epoch "
                  << epoch1 << " -> " << (epoch1 + 1) << ")\n";
        return true;
    }

    bool RunWindowedSoak(
        RHIType backend,
        bool validate,
        uint32_t width,
        uint32_t height,
        uint32_t warmupFrames,
        uint32_t measuredFrames,
        const std::string& presetName,
        SoakResult& outResult) {
        std::cout << "\n=== U10-D2 Windowed Soak (" << RHITypeToString(backend)
                  << ", " << width << "x" << height
                  << ", preset=" << presetName
                  << ", validate=" << (validate ? "true" : "false")
                  << ", warmup=" << warmupFrames << ", measured=" << measuredFrames << ") ===\n";

        auto& device = RHIDevice::Get();
        RHIDeviceDesc init{};
        init.PreferredBackend = backend;
        init.AllowFallback = false;
        init.Headless = false;
        init.EnableNRIValidation = validate;
        init.EnableValidation = validate;
        if (validate) {
            RENDER_CHECK(RHIDevice::IsGraphicsAPIValidationAvailable(backend),
                         "API validation layer required for validated soak");
        }

        WindowProps props{};
        props.Title = "U10D2 Windowed Soak";
        props.Width = width;
        props.Height = height;
        props.Maximized = false;
        props.GraphicsBackend = (backend == RHIType::Vulkan) ? GraphicsAPI::Vulkan : GraphicsAPI::Direct3D12;
        Window window(props);
        RENDER_CHECK(window.GetNativeWindow() != nullptr, "Window created for soak");

        RENDER_CHECK(device.Init(init) && device.GetBackend() == backend && !device.IsSoftwareAdapter(),
                     "Strict native physical adapter init for soak");
        RENDER_CHECK(RHIMeshRegistry::Get().Init(), "RHIMeshRegistry init");
        auto& service = RenderService::Get();
        RENDER_CHECK(service.Init(), "RenderService init");
        RHIPresentationContext presenter;
        RENDER_CHECK(presenter.Init(window.GetNativeWindow()), "RHIPresentationContext init");

        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        auto workload = BuildWorkloadScene(presetName == "UltraDDGI", aspect);
        auto settings = MakePresetSettings(presetName);

        outResult.Backend = RHITypeToString(backend);
        outResult.Preset = presetName;
        outResult.Width = width;
        outResult.Height = height;
        outResult.Validated = validate;
        outResult.WarmupFrames = warmupFrames;
        outResult.MeasuredFrames = measuredFrames;

        std::vector<double> wallFrameMs;
        std::vector<double> viewCpuMs;
        wallFrameMs.reserve(measuredFrames);
        viewCpuMs.reserve(measuredFrames);

        uint64_t peakVram = 0;
        const uint32_t totalFrames = warmupFrames + measuredFrames;
        for (uint32_t f = 0; f < totalFrames; ++f) {
            const float angle = static_cast<float>(f) * 0.015f;
            if (workload.CenterpieceEntity) {
                auto& tc = workload.CenterpieceEntity.GetComponent<TransformComponent>();
                tc.Rotation = glm::quat(glm::vec3{0.0f, angle, 0.0f});
                tc.Translation.y = 2.7f + 0.12f * std::sin(angle * 2.0f);
            }
            if (workload.MovingSpotEntity) {
                auto& tc = workload.MovingSpotEntity.GetComponent<TransformComponent>();
                tc.Rotation = glm::quat(glm::radians(glm::vec3(-42.0f, -35.0f + 12.0f * std::sin(angle), 0.0f)));
            }

            ViewRequest req{};
            req.Kind = RenderViewKind::Main;
            req.Width = width;
            req.Height = height;
            req.Camera = workload.Camera;
            req.Settings = settings;
            req.Name = "WindowedSoak";
            req.DeltaTime = 1.0f / 60.0f;

            const auto t0 = Clock::now();
            const auto res = service.RenderView(*workload.ScenePtr, req);
            const auto tRender = Clock::now();
            if (!res.Success || res.UsedFallback) {
                std::cerr << "[SOAK FAIL] RenderView failed at frame " << f << ": " << res.ErrorMessage << "\n";
                return false;
            }
            auto* colorTarget = service.GetDefaultMainColorTarget();
            if (!colorTarget || !colorTarget->IsValid()) return false;
            const auto presentOutcome = presenter.RenderFrame(width, height, false, {}, {}, colorTarget->GetNative());
            const auto t1 = Clock::now();
            if (presentOutcome != RHIPresentOutcome::Presented) {
                std::cerr << "[SOAK FAIL] RenderFrame did not present at frame " << f << "\n";
                return false;
            }

            const uint64_t vram = RHIMemoryManager::QueryVRAMInfo(RHIMemoryLocation::Device).UsageBytes;
            peakVram = std::max(peakVram, vram);
            if (f + 1 == warmupFrames) outResult.VRAMWarmupBytes = vram;
            if (f + 1 == warmupFrames + measuredFrames / 2u) outResult.VRAMMidBytes = vram;
            if (f + 1 == totalFrames) outResult.VRAMEndBytes = vram;

            if (f >= warmupFrames) {
                wallFrameMs.push_back(ElapsedMs(t0, t1));
                viewCpuMs.push_back(ElapsedMs(t0, tRender));
            }
        }

        RENDER_CHECK(device.WaitIdle(), "WaitIdle at end of soak");
        RENDER_CHECK(VerifyTextureHasFiniteRenderedPixels(service.GetDefaultMainColorTarget()),
                     "Final soak frame retains finite rendered pixels");

        outResult.PresentCount = presenter.GetPresentCount();
        outResult.TotalFrameWallMs = Summarize(std::move(wallFrameMs));
        outResult.RenderServiceViewMs = Summarize(std::move(viewCpuMs));
        outResult.VRAMPeakBytes = peakVram;
        outResult.VRAMBudgetBytes = RHIMemoryManager::QueryVRAMInfo(RHIMemoryLocation::Device).BudgetBytes;
        outResult.PlanarStats = service.GetPlanarReflectionCacheStats();
        outResult.DynProbeStats = service.GetDynamicReflectionCaptureCacheStats();
        outResult.ValidationErrorCount = GetRHIValidationErrorCount();

        presenter.Shutdown();
        service.Shutdown();
        RHIMeshRegistry::Get().Shutdown();
        device.Shutdown();

        RENDER_CHECK(outResult.PresentCount == totalFrames && outResult.ValidationErrorCount == 0,
                     "All soak frames presented cleanly with zero RHI validation errors");

        std::cout << "[PASS] Soak " << totalFrames << " frames: wallTotal p50=" << std::fixed << std::setprecision(3)
                  << outResult.TotalFrameWallMs.P50 << " ms, p95=" << outResult.TotalFrameWallMs.P95
                  << " ms, p99=" << outResult.TotalFrameWallMs.P99 << " ms, max=" << outResult.TotalFrameWallMs.Max
                  << " ms | VRAM steady end=" << (outResult.VRAMEndBytes / (1024.0 * 1024.0))
                  << " MiB (warmup=" << (outResult.VRAMWarmupBytes / (1024.0 * 1024.0)) << " MiB)\n";
        return true;
    }

    bool RunOffscreenBenchmarkConfig(
        RHIType backend,
        RHISceneRenderer& renderer,
        double coldInitMs,
        uint32_t width,
        uint32_t height,
        const std::string& presetName,
        uint32_t warmupFrames,
        uint32_t measuredFrames,
        ConfigBenchmarkResult& out) {
        auto& device = RHIDevice::Get();
        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        auto spec = BuildWorkloadScene(presetName == "UltraDDGI", aspect);
        auto authored = MakePresetSettings(presetName);

        auto world = ExtractRenderWorld(static_cast<const Scene&>(*spec.ScenePtr));
        PopulateNativeMeshes(world, &spec);

        SceneRenderSettings effective{};
        auto targets = BuildConfiguredTargets(width, height, authored, world, effective);
        if (!targets) {
            std::cerr << "[BENCH FAIL] Failed to configure view targets for " << presetName << " " << width << "x" << height << "\n";
            return false;
        }
        auto* colorTarget = targets->Get(RHIViewTarget::Color);
        auto* depthTarget = targets->Get(RHIViewTarget::Depth);
        if (!colorTarget || !depthTarget) return false;

        out.Backend = RHITypeToString(backend);
        out.Preset = presetName;
        out.Width = width;
        out.Height = height;
        out.ColdInitMs = coldInitMs;
        out.WarmupFrames = warmupFrames;
        out.MeasuredFrames = measuredFrames;
        out.SceneTotalEntities = spec.TotalEntities;
        out.SceneMeshEntities = spec.MeshEntities;
        out.SceneVertices = spec.TotalVertices;
        out.SceneTriangles = spec.TotalTriangles;

        // Frame 0: cold target + preset-specific pipeline compile / first use
        const auto f0Start = Clock::now();
        renderer.RenderWorldView(world, spec.Camera, colorTarget, depthTarget, effective, nullptr, targets, true, RenderViewKind::Main, 1.0f / 60.0f);
        const auto f0Submit = Clock::now();
        if (!renderer.WasLastRenderSuccessful()) {
            std::cerr << "[BENCH FAIL] Frame 0 render failed on " << presetName << " " << width << "x" << height << "\n";
            return false;
        }
        RENDER_CHECK(device.WaitForCompletion(renderer.GetLastCompletion()), "Wait for frame 0 completion");
        out.Frame0CPUMs = ElapsedMs(f0Start, f0Submit);
        out.DrawCalls = renderer.GetDrawCallCount();
        out.RenderedEntities = renderer.GetRenderedEntityCount();

        if (!VerifyTextureHasFiniteRenderedPixels(colorTarget)) {
            std::cerr << "[BENCH FAIL] Frame 0 readback verification produced empty/black image on " << presetName << "\n";
            return false;
        }

        const auto policy = RenderBackendPolicy::FromDevice(device.GetCapabilities());
        RHI::RHIDDGIAtlasExtent ddgiExtent{};
        const RHI::RHIDDGIAtlasExtent* ddgiExtentPtr = nullptr;
        if (effective.EnableDDGI && world.DDGIProbeGrid && world.DDGIProbeGrid->Enabled) {
            ddgiExtent = RHIViewTargets::ComputeDDGIAtlasExtent(
                static_cast<uint32_t>(world.DDGIProbeGrid->GridDimensions.x),
                static_cast<uint32_t>(world.DDGIProbeGrid->GridDimensions.y),
                static_cast<uint32_t>(world.DDGIProbeGrid->GridDimensions.z));
            if (ddgiExtent.Valid) ddgiExtentPtr = &ddgiExtent;
        }
        out.QualityBudget = ComputeRenderQualityBudgetReport(width, height, authored, &policy, ddgiExtentPtr);
        out.ViewInventory = InspectViewTargets(*targets);

        // Warm-up remaining frames (note Frame 0 already consumed 1 frame)
        RHICompletionToken prevToken = renderer.GetLastCompletion();
        bool capturedFrame0Gpu = false;
        for (uint32_t w = 1; w < warmupFrames; ++w) {
            RENDER_CHECK(device.WaitForCompletion(prevToken), "Wait during warmup");
            renderer.RenderWorldView(world, spec.Camera, colorTarget, depthTarget, effective, nullptr, targets, false, RenderViewKind::Main, 1.0f / 60.0f);
            if (!renderer.WasLastRenderSuccessful()) return false;
            if (!capturedFrame0Gpu && renderer.GetLastGPUTimingFrame().Validity == DiagnosticValidity::Valid) {
                out.Frame0GPUMs = renderer.GetLastGPUTimingFrame().Milliseconds;
                capturedFrame0Gpu = true;
            }
            prevToken = renderer.GetLastCompletion();
        }

        const auto vramWarmup = RHIMemoryManager::QueryVRAMInfo(RHIMemoryLocation::Device);
        out.VRAMWarmupBytes = vramWarmup.UsageBytes;
        out.VRAMBudgetBytes = vramWarmup.BudgetBytes;
        uint64_t vramPeak = vramWarmup.UsageBytes;

        std::vector<double> cpuSamples;
        std::vector<double> gpuSamples;
        std::vector<double> lightingSubsetSamples;
        std::vector<double> geometrySamples;
        std::vector<double> postSamples;
        std::map<std::string, PassAccum> passAccum;

        cpuSamples.reserve(measuredFrames);
        gpuSamples.reserve(measuredFrames);
        lightingSubsetSamples.reserve(measuredFrames);
        geometrySamples.reserve(measuredFrames);
        postSamples.reserve(measuredFrames);

        auto ingestLastGpuFrame = [&](const RHISceneRenderer::GPUTimingFrame& gf) {
            if (gf.Validity != DiagnosticValidity::Valid) return;
            ++out.ValidGPUTimestampFrames;
            gpuSamples.push_back(gf.Milliseconds);
            double lightSum = 0.0;
            double geomSum = 0.0;
            double postSum = 0.0;
            for (const auto& p : gf.Passes) {
                if (p.Validity != DiagnosticValidity::Valid) continue;
                passAccum[p.Name].GPUMs.push_back(p.Milliseconds);
                const auto cat = ClassifyPass(p.Name);
                if (IsLightingSubsetCategory(cat)) lightSum += p.Milliseconds;
                else if (cat == PassCategory::GeometryAndOpaque) geomSum += p.Milliseconds;
                else if (cat == PassCategory::PostProcess) postSum += p.Milliseconds;
            }
            lightingSubsetSamples.push_back(lightSum);
            geometrySamples.push_back(geomSum);
            postSamples.push_back(postSum);
        };

        for (uint32_t i = 0; i < measuredFrames; ++i) {
            RENDER_CHECK(device.WaitForCompletion(prevToken), "Wait before measured frame");

            const auto t0 = Clock::now();
            renderer.RenderWorldView(world, spec.Camera, colorTarget, depthTarget, effective, nullptr, targets, false, RenderViewKind::Main, 1.0f / 60.0f);
            const auto t1 = Clock::now();
            if (!renderer.WasLastRenderSuccessful()) {
                std::cerr << "[BENCH FAIL] Measured frame " << i << " failed\n";
                return false;
            }

            cpuSamples.push_back(ElapsedMs(t0, t1));
            for (const auto& cp : renderer.GetDiagnostics().GetFrame().Passes) {
                passAccum[cp.Name].CPUMs.push_back(cp.CPUTimeMilliseconds);
            }

            // ConsumeReadyGPUTimestamps at the start of frame i harvested frame i-1's timestamps
            if (i > 0) {
                ingestLastGpuFrame(renderer.GetLastGPUTimingFrame());
            }

            prevToken = renderer.GetLastCompletion();
            if ((i + 1) % 100 == 0 || i + 1 == measuredFrames / 2u || i + 1 == measuredFrames) {
                const uint64_t vram = RHIMemoryManager::QueryVRAMInfo(RHIMemoryLocation::Device).UsageBytes;
                vramPeak = std::max(vramPeak, vram);
                if (i + 1 == measuredFrames / 2u) out.VRAMMidBytes = vram;
                if (i + 1 == measuredFrames) out.VRAMEndBytes = vram;
            }
        }

        // Flush final measured frame's GPU timestamp slot
        RENDER_CHECK(device.WaitForCompletion(prevToken), "Wait for final measured frame");
        renderer.RenderWorldView(world, spec.Camera, colorTarget, depthTarget, effective, nullptr, targets, false, RenderViewKind::Main, 1.0f / 60.0f);
        ingestLastGpuFrame(renderer.GetLastGPUTimingFrame());
        RENDER_CHECK(device.WaitForCompletion(renderer.GetLastCompletion()), "Drain final flush frame");

        out.VRAMPeakBytes = vramPeak;
        out.PlanarStats = renderer.GetPlanarReflectionCacheStats();
        out.DynProbeStats = renderer.GetDynamicReflectionCaptureCacheStats();
        out.CPUTotal = Summarize(std::move(cpuSamples));
        out.GPUTotal = Summarize(std::move(gpuSamples));
        out.GPULightingSubset = Summarize(std::move(lightingSubsetSamples));
        out.GPUGeometryAndOpaque = Summarize(std::move(geometrySamples));
        out.GPUPostProcess = Summarize(std::move(postSamples));

        for (auto& [name, acc] : passAccum) {
            if (!acc.GPUMs.empty()) out.PerPassGPU[name] = Summarize(std::move(acc.GPUMs));
            if (!acc.CPUMs.empty()) out.PerPassCPU[name] = Summarize(std::move(acc.CPUMs));
        }

        RENDER_CHECK(out.ValidGPUTimestampFrames == measuredFrames,
                     "Every measured frame yielded valid GPU timestamps");

        std::cout << std::fixed << std::setprecision(3)
                  << "[BENCH] " << out.Backend << " | " << out.Preset << " @ " << width << "x" << height
                  << " | CPU p50=" << out.CPUTotal.P50 << "ms"
                  << " | WholeGPU p50=" << out.GPUTotal.P50 << "ms (p95=" << out.GPUTotal.P95 << ", p99=" << out.GPUTotal.P99 << ", max=" << out.GPUTotal.Max << ")"
                  << " | LightSubset p50=" << out.GPULightingSubset.P50 << "ms"
                  << " | Geom p50=" << out.GPUGeometryAndOpaque.P50 << "ms"
                  << " | Post p50=" << out.GPUPostProcess.P50 << "ms"
                  << " | VRAM end=" << (out.VRAMEndBytes / (1024.0 * 1024.0)) << "MiB\n";
        return true;
    }

    void WriteStatsJson(std::ostream& os, const MetricStats& s) {
        os << std::fixed << std::setprecision(4)
           << "{\"count\":" << s.Count
           << ",\"min\":" << s.Min
           << ",\"p50\":" << s.P50
           << ",\"p95\":" << s.P95
           << ",\"p99\":" << s.P99
           << ",\"max\":" << s.Max
           << ",\"mean\":" << s.Mean << "}";
    }

    bool WriteJsonReport(
        const std::filesystem::path& path,
        const std::string& adapterName,
        uint64_t dedicatedVramBytes,
        uint64_t timestampHz,
        const std::vector<ConfigBenchmarkResult>& benchmarks,
        const std::vector<SoakResult>& soaks) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream os(path, std::ios::binary | std::ios::trunc);
        if (!os.is_open()) return false;

        os << "{\n";
        os << "  \"adapterName\": \"" << EscapeJson(adapterName) << "\",\n";
        os << "  \"dedicatedVramBytes\": " << dedicatedVramBytes << ",\n";
        os << "  \"timestampFrequencyHz\": " << timestampHz << ",\n";
        os << "  \"benchmarks\": [\n";
        for (size_t i = 0; i < benchmarks.size(); ++i) {
            const auto& b = benchmarks[i];
            os << "    {\n";
            os << "      \"backend\": \"" << EscapeJson(b.Backend) << "\",\n";
            os << "      \"preset\": \"" << EscapeJson(b.Preset) << "\",\n";
            os << "      \"width\": " << b.Width << ",\n";
            os << "      \"height\": " << b.Height << ",\n";
            os << "      \"coldInitMs\": " << std::fixed << std::setprecision(4) << b.ColdInitMs << ",\n";
            os << "      \"frame0CPUMs\": " << b.Frame0CPUMs << ",\n";
            os << "      \"frame0GPUMs\": " << b.Frame0GPUMs << ",\n";
            os << "      \"warmupFrames\": " << b.WarmupFrames << ",\n";
            os << "      \"measuredFrames\": " << b.MeasuredFrames << ",\n";
            os << "      \"validGPUTimestampFrames\": " << b.ValidGPUTimestampFrames << ",\n";
            os << "      \"drawCalls\": " << b.DrawCalls << ",\n";
            os << "      \"renderedEntities\": " << b.RenderedEntities << ",\n";
            os << "      \"sceneTotalEntities\": " << b.SceneTotalEntities << ",\n";
            os << "      \"sceneMeshEntities\": " << b.SceneMeshEntities << ",\n";
            os << "      \"sceneVertices\": " << b.SceneVertices << ",\n";
            os << "      \"sceneTriangles\": " << b.SceneTriangles << ",\n";
            os << "      \"cpuTotalMs\": "; WriteStatsJson(os, b.CPUTotal); os << ",\n";
            os << "      \"gpuTotalMs\": "; WriteStatsJson(os, b.GPUTotal); os << ",\n";
            os << "      \"gpuLightingSubsetMs\": "; WriteStatsJson(os, b.GPULightingSubset); os << ",\n";
            os << "      \"gpuGeometryMs\": "; WriteStatsJson(os, b.GPUGeometryAndOpaque); os << ",\n";
            os << "      \"gpuPostProcessMs\": "; WriteStatsJson(os, b.GPUPostProcess); os << ",\n";
            os << "      \"vramWarmupBytes\": " << b.VRAMWarmupBytes << ",\n";
            os << "      \"vramMidBytes\": " << b.VRAMMidBytes << ",\n";
            os << "      \"vramEndBytes\": " << b.VRAMEndBytes << ",\n";
            os << "      \"vramPeakBytes\": " << b.VRAMPeakBytes << ",\n";
            os << "      \"vramBudgetBytes\": " << b.VRAMBudgetBytes << ",\n";
            os << "      \"viewTargets\": {"
               << "\"residentTextures\":" << b.ViewInventory.ResidentTextures
               << ",\"totalBytes\":" << b.ViewInventory.TotalBytes
               << ",\"colorDepthMSAABytes\":" << b.ViewInventory.ColorDepthMSAABytes
               << ",\"historyVelocityBytes\":" << b.ViewInventory.HistoryVelocityBytes
               << ",\"reflectionsHiZBytes\":" << b.ViewInventory.ReflectionsHiZBytes
               << ",\"fog3DBytes\":" << b.ViewInventory.Fog3DBytes
               << ",\"aoSSSBytes\":" << b.ViewInventory.AOAndSSSBytes
               << ",\"bloomExposureBytes\":" << b.ViewInventory.BloomAndExposureBytes
               << ",\"ddgiBytes\":" << b.ViewInventory.DDGIBytes
               << "},\n";
            os << "      \"qualityBudget\": {"
               << "\"presetName\":\"" << EscapeJson(b.QualityBudget.PresetName) << "\""
               << ",\"viewTargetEstimatedBytes\":" << b.QualityBudget.ViewTargetEstimatedBytes
               << ",\"froxelMemoryBytes\":" << b.QualityBudget.FroxelMemoryBytes
               << ",\"ddgiMemoryBytes\":" << b.QualityBudget.DDGIMemoryBytes
               << ",\"reflectionCacheBudgetBytes\":" << b.QualityBudget.ReflectionCacheBudgetBytes
               << ",\"totalEstimatedBudgetBytes\":" << b.QualityBudget.TotalEstimatedBudgetBytes
               << ",\"froxelGrid\":[" << b.QualityBudget.FroxelExtent.Width << "," << b.QualityBudget.FroxelExtent.Height << "," << b.QualityBudget.FroxelExtent.Depth << "]"
               << ",\"aoExtent\":[" << b.QualityBudget.AOWidth << "," << b.QualityBudget.AOHeight << "]"
               << ",\"hiZMips\":" << b.QualityBudget.HiZMipLevels
               << ",\"msaaSamples\":" << static_cast<unsigned>(b.QualityBudget.ActiveMSAASamples)
               << "},\n";
            os << "      \"planarCache\": {"
               << "\"activeEntries\":" << b.PlanarStats.ActiveEntries
               << ",\"successfulCaptures\":" << b.PlanarStats.SuccessfulCaptures
               << ",\"cacheHits\":" << b.PlanarStats.CacheHits
               << ",\"invalidatedCaptures\":" << b.PlanarStats.InvalidatedCaptures
               << ",\"failedCaptures\":" << b.PlanarStats.FailedCaptures
               << "},\n";
            os << "      \"dynamicProbeCache\": {"
               << "\"cachedProbeCount\":" << b.DynProbeStats.CachedProbeCount
               << ",\"usedBytes\":" << b.DynProbeStats.UsedBytes
               << ",\"budgetBytes\":" << b.DynProbeStats.BudgetBytes
               << ",\"evictionCount\":" << b.DynProbeStats.EvictionCount
               << ",\"lastScheduledProbes\":" << b.DynProbeStats.LastScheduledProbes
               << ",\"lastScheduledFaces\":" << b.DynProbeStats.LastScheduledFaces
               << "},\n";
            os << "      \"perPassGPU\": {\n";
            size_t pIdx = 0;
            for (const auto& [name, st] : b.PerPassGPU) {
                os << "        \"" << EscapeJson(name) << "\": {\"category\":\"" << CategoryLabel(ClassifyPass(name)) << "\",\"stats\":";
                WriteStatsJson(os, st);
                os << "}" << (++pIdx < b.PerPassGPU.size() ? "," : "") << "\n";
            }
            os << "      },\n";
            os << "      \"perPassCPU\": {\n";
            size_t cIdx = 0;
            for (const auto& [name, st] : b.PerPassCPU) {
                os << "        \"" << EscapeJson(name) << "\": ";
                WriteStatsJson(os, st);
                os << (++cIdx < b.PerPassCPU.size() ? "," : "") << "\n";
            }
            os << "      }\n";
            os << "    }" << (i + 1 < benchmarks.size() ? "," : "") << "\n";
        }
        os << "  ],\n";
        os << "  \"soaks\": [\n";
        for (size_t i = 0; i < soaks.size(); ++i) {
            const auto& s = soaks[i];
            os << "    {\n";
            os << "      \"backend\": \"" << EscapeJson(s.Backend) << "\",\n";
            os << "      \"preset\": \"" << EscapeJson(s.Preset) << "\",\n";
            os << "      \"width\": " << s.Width << ",\n";
            os << "      \"height\": " << s.Height << ",\n";
            os << "      \"validated\": " << (s.Validated ? "true" : "false") << ",\n";
            os << "      \"warmupFrames\": " << s.WarmupFrames << ",\n";
            os << "      \"measuredFrames\": " << s.MeasuredFrames << ",\n";
            os << "      \"presentCount\": " << s.PresentCount << ",\n";
            os << "      \"totalFrameWallMs\": "; WriteStatsJson(os, s.TotalFrameWallMs); os << ",\n";
            os << "      \"renderServiceViewMs\": "; WriteStatsJson(os, s.RenderServiceViewMs); os << ",\n";
            os << "      \"vramWarmupBytes\": " << s.VRAMWarmupBytes << ",\n";
            os << "      \"vramMidBytes\": " << s.VRAMMidBytes << ",\n";
            os << "      \"vramEndBytes\": " << s.VRAMEndBytes << ",\n";
            os << "      \"vramPeakBytes\": " << s.VRAMPeakBytes << ",\n";
            os << "      \"vramBudgetBytes\": " << s.VRAMBudgetBytes << ",\n";
            os << "      \"validationErrors\": " << s.ValidationErrorCount << "\n";
            os << "    }" << (i + 1 < soaks.size() ? "," : "") << "\n";
        }
        os << "  ]\n";
        os << "}\n";
        return true;
    }

    struct CliOptions {
        std::string Mode = "lifecycle";
        std::string Backend = "dx12";
        std::string Res = "all";
        std::string Preset = "all";
        uint32_t Warmup = 120;
        uint32_t Frames = 1000;
        bool Validate = false;
        std::string JsonOut;
        std::string SceneOut;
    };

    CliOptions ParseArgs(int argc, char** argv) {
        CliOptions opt;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            auto split = [&](std::string_view prefix, std::string& dst) -> bool {
                if (arg.rfind(prefix, 0) == 0) {
                    dst = arg.substr(prefix.size());
                    return true;
                }
                return false;
            };
            std::string val;
            if (split("--mode=", opt.Mode)) continue;
            if (split("--backend=", opt.Backend)) continue;
            if (split("--res=", opt.Res)) continue;
            if (split("--preset=", opt.Preset)) continue;
            if (split("--warmup=", val)) { opt.Warmup = static_cast<uint32_t>(std::stoul(val)); continue; }
            if (split("--frames=", val)) { opt.Frames = static_cast<uint32_t>(std::stoul(val)); continue; }
            if (arg == "--validate") { opt.Validate = true; continue; }
            if (split("--json-out=", opt.JsonOut)) continue;
            if (split("--scene-out=", opt.SceneOut)) continue;
            if (arg == "dx12" || arg == "vulkan") { opt.Backend = arg; continue; }
            if (arg == "lifecycle" || arg == "soak" || arg == "benchmark" || arg == "export-scene" || arg == "all") {
                opt.Mode = arg;
            }
        }
        return opt;
    }

} // namespace

int main(int argc, char** argv) {
    Axle::RenderTests::ConfigureFailureReporting();
    Log::Init();
    JobSystem::Init(4);

    const CliOptions opt = ParseArgs(argc, argv);
    const RHIType backend = (opt.Backend == "vulkan") ? RHIType::Vulkan : RHIType::Direct3D12;

    if (opt.Mode == "export-scene") {
        auto spec = BuildWorkloadScene(false, 16.0f / 9.0f);
        const std::filesystem::path outPath = opt.SceneOut.empty()
            ? std::filesystem::path("build/agent-runs/u10d2/workload/U10D2_Workload.axlescene")
            : std::filesystem::path(opt.SceneOut);
        std::filesystem::create_directories(outPath.parent_path());
        SceneSerializer serializer(spec.ScenePtr.get(), nullptr);
        const bool ok = serializer.Serialize(outPath);
        std::cout << "Export scene " << outPath.string() << ": " << (ok ? "PASS" : "FAIL") << "\n";
        JobSystem::Shutdown();
        return ok ? 0 : 1;
    }

    bool passed = true;
    std::string adapterName;
    uint64_t dedicatedVram = 0;
    uint64_t timestampHz = 0;
    std::vector<ConfigBenchmarkResult> benchResults;
    std::vector<SoakResult> soakResults;

    try {
        if (opt.Mode == "lifecycle" || opt.Mode == "all") {
            passed = RunLifecycleTests(backend, opt.Validate) && passed;
        }

        if (passed && (opt.Mode == "benchmark" || opt.Mode == "all")) {
            auto& device = RHIDevice::Get();
            RHIDeviceDesc desc{};
            desc.PreferredBackend = backend;
            desc.AllowFallback = false;
            desc.Headless = true;
            desc.EnableNRIValidation = opt.Validate;
            desc.EnableValidation = opt.Validate;
            if (opt.Validate) {
                passed = RHIDevice::IsGraphicsAPIValidationAvailable(backend) && passed;
            }
            if (passed && device.Init(desc) && device.GetBackend() == backend && !device.IsSoftwareAdapter()) {
                for (const auto& a : device.GetAdapters()) {
                    if (a.IsDiscreteGPU) {
                        adapterName = a.Name;
                        dedicatedVram = a.DedicatedVideoMemory;
                        break;
                    }
                }
                if (adapterName.empty() && !device.GetAdapters().empty()) {
                    adapterName = device.GetAdapters().front().Name;
                    dedicatedVram = device.GetAdapters().front().DedicatedVideoMemory;
                }
                timestampHz = device.GetCapabilities().TimestampFrequencyHz;

                passed = RHIMeshRegistry::Get().Init();
                RHISceneRenderer renderer;
                const auto initStart = Clock::now();
                const bool initOk = renderer.Init(device, false);
                const double coldInitMs = ElapsedMs(initStart, Clock::now());
                passed = passed && initOk;

                std::vector<std::pair<uint32_t, uint32_t>> resolutions;
                if (opt.Res == "1080p") resolutions.emplace_back(1920u, 1080u);
                else if (opt.Res == "1440p") resolutions.emplace_back(2560u, 1440u);
                else {
                    resolutions.emplace_back(1920u, 1080u);
                    resolutions.emplace_back(2560u, 1440u);
                }

                std::vector<std::string> presets;
                if (opt.Preset == "all") {
                    presets = {"Low", "Medium", "High", "Ultra", "UltraDDGI"};
                } else {
                    presets.push_back(opt.Preset);
                }

                for (const auto& [w, h] : resolutions) {
                    for (const auto& p : presets) {
                        if (!passed) break;
                        ConfigBenchmarkResult res{};
                        passed = RunOffscreenBenchmarkConfig(backend, renderer, coldInitMs, w, h, p,
                                                             opt.Warmup, opt.Frames, res) && passed;
                        benchResults.push_back(std::move(res));
                    }
                }

                renderer.Shutdown();
                RHIMeshRegistry::Get().Shutdown();
                passed = device.Shutdown() && passed;
            } else {
                passed = false;
            }
        }

        if (passed && (opt.Mode == "soak" || opt.Mode == "all")) {
            std::vector<std::pair<uint32_t, uint32_t>> soakRes;
            if (opt.Res == "1080p") soakRes.emplace_back(1920u, 1080u);
            else if (opt.Res == "1440p") soakRes.emplace_back(2560u, 1440u);
            else {
                soakRes.emplace_back(1920u, 1080u);
                soakRes.emplace_back(2560u, 1440u);
            }
            const std::string soakPreset = (opt.Preset == "all") ? "High" : opt.Preset;
            for (const auto& [w, h] : soakRes) {
                SoakResult sr{};
                passed = RunWindowedSoak(backend, opt.Validate, w, h, opt.Warmup, opt.Frames, soakPreset, sr) && passed;
                soakResults.push_back(std::move(sr));
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[U10D2 EXCEPTION] " << e.what() << "\n";
        passed = false;
    }

    RenderService::Get().Shutdown();
    RHIMeshRegistry::Get().Shutdown();
    RHIDevice::Get().Shutdown();
    JobSystem::Shutdown();

    if (!opt.JsonOut.empty()) {
        if (!WriteJsonReport(opt.JsonOut, adapterName, dedicatedVram, timestampHz, benchResults, soakResults)) {
            std::cerr << "[WARN] Failed to write JSON report to " << opt.JsonOut << "\n";
            passed = false;
        }
    }

    passed = passed && (GetRHIValidationErrorCount() == 0);
    std::cout << "\nM15 U10-D2 Baseline Scorecard (" << opt.Backend << ", mode=" << opt.Mode << "): "
              << (passed ? "PASS" : "FAIL") << "\n";
    return passed ? 0 : 1;
}
