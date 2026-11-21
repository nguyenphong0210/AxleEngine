#include <Axle/RenderQualityPreset.hpp>
#include <Axle/RenderContracts.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace Axle {
    namespace {
        constexpr std::array<RenderQualityTierBudget, 5> kQualityTierBudgets = {{
            // M15-U10-05.1: Low - reduced AO/SSR/fog/post, no DDGI/RT, bounded probe updates
            RenderQualityTierBudget{
                RenderQualityPreset::Low, "Low",
                true, true, true, true, 2u, 3u, false, 8u, false, 16u,
                false, 0.25f, false, true, FroxelFogQuality::Low, 0.0f,
                false, 0u, false, true, false, 1u,
                1u, 2u, 16ull * 1024ull * 1024ull,
                false, false, false
            },
            // M15-U10-05.2: Medium - half-res AO, raster SSR/probes, reduced fog/post history
            RenderQualityTierBudget{
                RenderQualityPreset::Medium, "Medium",
                true, true, true, true, 4u, 4u, true, 8u, true, 32u,
                false, 0.35f, true, true, FroxelFogQuality::Medium, 0.50f,
                true, 6u, true, true, false, 1u,
                1u, 6u, 32ull * 1024ull * 1024ull,
                false, false, false
            },
            // M15-U10-05.3: High - declared raster quality, dynamic probe scheduling & temporal quality
            RenderQualityTierBudget{
                RenderQualityPreset::High, "High",
                true, true, true, true, 8u, 7u, true, 16u, true, 64u,
                true, 0.50f, true, true, FroxelFogQuality::High, 0.80f,
                true, 6u, true, false, true, 1u,
                2u, 12u, 64ull * 1024ull * 1024ull,
                false, false, false
            },
            // M15-U10-05.4: Ultra - optional DDGI in M15; RT tiers gated by M16
            RenderQualityTierBudget{
                RenderQualityPreset::Ultra, "Ultra",
                true, true, true, true, 8u, 8u, true, 24u, true, 96u,
                true, 0.75f, true, true, FroxelFogQuality::Ultra, 0.85f,
                true, 6u, true, false, true, 1u,
                4u, 24u, 128ull * 1024ull * 1024ull,
                false, true, false
            },
            // Custom fallback descriptor (reports declared default raster envelopes)
            RenderQualityTierBudget{
                RenderQualityPreset::Custom, "Custom",
                true, true, true, true, 8u, 7u, true, 16u, true, 64u,
                true, 0.50f, true, true, FroxelFogQuality::High, 0.80f,
                true, 6u, false, true, false, 1u,
                2u, 12u, 64ull * 1024ull * 1024ull,
                false, true, false
            }
        }};

        uint64_t FormatBytesPerPixel(RHI::RHITextureFormat fmt) {
            switch (fmt) {
                case RHI::RHITextureFormat::R8_UNORM:     return 1ull;
                case RHI::RHITextureFormat::RG16_FLOAT:   return 4ull;
                case RHI::RHITextureFormat::R32_FLOAT:    return 4ull;
                case RHI::RHITextureFormat::D32_FLOAT:    return 4ull;
                case RHI::RHITextureFormat::RGBA8_UNORM:
                case RHI::RHITextureFormat::RGBA8_SRGB:
                case RHI::RHITextureFormat::BGRA8_UNORM:  return 4ull;
                case RHI::RHITextureFormat::RGBA16_FLOAT: return 8ull;
                case RHI::RHITextureFormat::RGBA32_FLOAT: return 16ull;
                default:                                  return 4ull;
            }
        }

        uint64_t Bytes2D(uint32_t w, uint32_t h, RHI::RHITextureFormat fmt, uint32_t samples = 1u) {
            return static_cast<uint64_t>(w) * static_cast<uint64_t>(h) *
                   FormatBytesPerPixel(fmt) * static_cast<uint64_t>(std::max(1u, samples));
        }

        bool MatchesBudget(const SceneRenderSettings& s, const RenderQualityTierBudget& b) {
            if (s.EnableShadows != b.EnableShadows ||
                s.EnableSkybox != b.EnableSkybox ||
                s.EnableGTAO != b.EnableGTAO ||
                s.GTAOSliceCount != b.GTAOSliceCount ||
                s.GTAOStepCount != b.GTAOStepCount ||
                s.EnableContactShadows != b.EnableContactShadows ||
                (b.EnableContactShadows && s.ContactShadowSteps != b.ContactShadowSteps) ||
                s.EnableSSR != b.EnableSSR ||
                (b.EnableSSR && s.SSRMaxSteps != b.SSRMaxSteps) ||
                s.EnablePlanarReflections != b.EnablePlanarReflections ||
                (b.EnablePlanarReflections && std::abs(s.PlanarReflectionResolutionScale - b.PlanarResolutionScale) > 1e-3f) ||
                s.EnableSubsurfaceScattering != b.EnableSubsurfaceScattering ||
                s.EnableVolumetricFog != b.EnableVolumetricFog ||
                s.FogQuality != b.FogQuality ||
                std::abs(s.FogTemporalWeight - b.FogTemporalWeight) > 1e-3f ||
                s.EnableBloom != b.EnableBloom ||
                s.EnableAutoExposure != b.EnableAutoExposure ||
                s.EnableTAA != b.EnableTAA ||
                s.EnableFXAA != b.EnableFXAA ||
                s.MSAASamples != b.MSAASamples ||
                s.ReflectionCaptureMaxProbesPerFrame != b.ReflectionMaxProbesPerFrame ||
                s.ReflectionCaptureMaxFacesPerFrame != b.ReflectionMaxFacesPerFrame ||
                s.ReflectionCaptureCacheBudgetBytes != b.ReflectionCacheBudgetBytes) {
                return false;
            }
            if (!b.SupportsOptionalDDGI && s.EnableDDGI) return false;
            return true;
        }
    } // namespace

    const char* RenderQualityPresetName(RenderQualityPreset preset) {
        const auto index = static_cast<size_t>(preset);
        return index < kQualityTierBudgets.size() ? kQualityTierBudgets[index].Name : "Custom";
    }

    bool ParseRenderQualityPreset(const std::string& text, RenderQualityPreset& outPreset) {
        std::string lower;
        lower.reserve(text.size());
        for (unsigned char c : text) lower.push_back(static_cast<char>(std::tolower(c)));
        if (lower == "low" || lower == "0") { outPreset = RenderQualityPreset::Low; return true; }
        if (lower == "medium" || lower == "med" || lower == "1") { outPreset = RenderQualityPreset::Medium; return true; }
        if (lower == "high" || lower == "2") { outPreset = RenderQualityPreset::High; return true; }
        if (lower == "ultra" || lower == "3") { outPreset = RenderQualityPreset::Ultra; return true; }
        if (lower == "custom" || lower == "4") { outPreset = RenderQualityPreset::Custom; return true; }
        return false;
    }

    const RenderQualityTierBudget& GetRenderQualityTierBudget(RenderQualityPreset preset) {
        const auto index = static_cast<size_t>(preset);
        return index < kQualityTierBudgets.size() ? kQualityTierBudgets[index] : kQualityTierBudgets.back();
    }

    void ApplyRenderQualityPreset(SceneRenderSettings& settings,
                                  RenderQualityPreset preset,
                                  bool enableOptionalDDGI) {
        if (preset == RenderQualityPreset::Custom) {
            settings.QualityPreset = RenderQualityPreset::Custom;
            return;
        }
        const auto& b = GetRenderQualityTierBudget(preset);
        settings.QualityPreset = b.Preset;
        settings.EnableShadows = b.EnableShadows;
        settings.EnableSkybox = b.EnableSkybox;
        settings.EnableGTAO = b.EnableGTAO;
        settings.GTAOSliceCount = b.GTAOSliceCount;
        settings.GTAOStepCount = b.GTAOStepCount;
        settings.EnableContactShadows = b.EnableContactShadows;
        settings.ContactShadowSteps = b.ContactShadowSteps;
        settings.EnableSSR = b.EnableSSR;
        settings.SSRMaxSteps = b.SSRMaxSteps;
        settings.EnablePlanarReflections = b.EnablePlanarReflections;
        settings.PlanarReflectionResolutionScale = b.PlanarResolutionScale;
        settings.EnableSubsurfaceScattering = b.EnableSubsurfaceScattering;
        settings.EnableVolumetricFog = b.EnableVolumetricFog;
        settings.FogQuality = b.FogQuality;
        settings.FogTemporalWeight = b.FogTemporalWeight;
        settings.EnableBloom = b.EnableBloom;
        settings.EnableAutoExposure = b.EnableAutoExposure;
        settings.EnableTAA = b.EnableTAA;
        settings.EnableFXAA = b.EnableFXAA;
        settings.MSAASamples = b.MSAASamples;
        settings.ReflectionCaptureMaxProbesPerFrame = b.ReflectionMaxProbesPerFrame;
        settings.ReflectionCaptureMaxFacesPerFrame = b.ReflectionMaxFacesPerFrame;
        settings.ReflectionCaptureCacheBudgetBytes = b.ReflectionCacheBudgetBytes;
        settings.EnableDDGI = b.SupportsOptionalDDGI ? enableOptionalDDGI : false;
    }

    RenderQualityPreset DetectRenderQualityPreset(const SceneRenderSettings& settings) {
        for (size_t i = 0; i < 4; ++i) {
            if (MatchesBudget(settings, kQualityTierBudgets[i])) {
                return kQualityTierBudgets[i].Preset;
            }
        }
        return RenderQualityPreset::Custom;
    }

    RenderQualityBudgetReport ComputeRenderQualityBudgetReport(
        uint32_t viewWidth,
        uint32_t viewHeight,
        const SceneRenderSettings& authored,
        const RenderBackendPolicy* policy,
        const RHI::RHIDDGIAtlasExtent* ddgiGrid) {
        RenderQualityBudgetReport report{};
        const RenderQualityPreset detected = DetectRenderQualityPreset(authored);
        report.ResolvedPreset = (detected != RenderQualityPreset::Custom) ? detected : authored.QualityPreset;
        report.PresetName = RenderQualityPresetName(report.ResolvedPreset);

        const uint32_t safeW = std::max(1u, viewWidth);
        const uint32_t safeH = std::max(1u, viewHeight);
        report.ViewWidth = safeW;
        report.ViewHeight = safeH;

        const SceneRenderSettings effective = policy ? policy->ResolveSettings(authored) : authored;
        const bool isCompat = policy && (policy->GetQualityTier() == BackendQualityTier::Compatibility ||
                                         policy->GetQualityTier() == BackendQualityTier::None);
        report.ActiveMSAASamples = std::max<uint8_t>(1u, effective.MSAASamples);

        const auto aoExtent = OcclusionContract::ComputeHalfResolution(safeW, safeH);
        if (effective.EnableGTAO) {
            report.AOWidth = aoExtent.Width;
            report.AOHeight = aoExtent.Height;
            report.GTAOSliceCount = std::clamp(effective.GTAOSliceCount, 2u, 8u);
            report.GTAOStepCount = std::clamp(effective.GTAOStepCount, 2u, 8u);
        }
        report.ContactShadowSteps = effective.EnableContactShadows ? std::clamp(effective.ContactShadowSteps, 8u, 32u) : 0u;
        report.SSRMaxSteps = effective.EnableSSR ? std::clamp(effective.SSRMaxSteps, 16u, 96u) : 0u;

        ViewTargetContract::ViewTargetPolicyInput targetInput{};
        targetInput.Width = safeW;
        targetInput.Height = safeH;
        targetInput.EnableDepth = true;
        targetInput.OwnPrimary = true;
        targetInput.IsSubCapture = false;
        targetInput.WantsSubsurface = effective.EnableSubsurfaceScattering && (report.ActiveMSAASamples == 1u) && !isCompat;
        RenderVolumetricFog syntheticFog{};
        syntheticFog.Enabled = true;
        if (effective.EnableVolumetricFog && !isCompat) {
            targetInput.VolumetricFog = &syntheticFog;
        }
        RenderDDGIProbeGrid syntheticDDGI{};
        syntheticDDGI.Enabled = true;
        if (effective.EnableDDGI && !isCompat) {
            targetInput.DDGIProbeGrid = &syntheticDDGI;
        }
        RHI::RHIViewTargetDesc desc = ViewTargetContract::BuildViewTargetDesc(effective, targetInput);
        if (isCompat) {
            desc.RenderAuxiliary = false;
            desc.HDR = false;
            desc.FXAA = false;
            desc.Samples = 1u;
            desc.EnableSubsurface = false;
            desc.EnableGTAO = false;
            desc.EnableBloom = false;
            desc.EnableAutoExposure = false;
            desc.EnableHiZ = false;
            desc.HistoryCount = 0u;
            desc.EnableVelocity = false;
            desc.EnableReactive = false;
            desc.EnablePlanarReflection = false;
            desc.EnableVolumetricFog = false;
            desc.EnableDDGI = false;
        }

        uint64_t viewBytes = 0ull;
        if (desc.OwnPrimary) viewBytes += Bytes2D(safeW, safeH, desc.ColorFormat);
        if (desc.OwnPrimary && desc.EnableDepth) viewBytes += Bytes2D(safeW, safeH, desc.DepthFormat);
        if (desc.RenderAuxiliary) {
            const auto sceneFmt = desc.HDR ? RHI::RHITextureFormat::RGBA16_FLOAT : desc.ColorFormat;
            viewBytes += Bytes2D(safeW, safeH, sceneFmt); // OpaqueColor
            if (desc.EnableDepth) {
                viewBytes += Bytes2D(safeW, safeH, desc.DepthFormat) * 2ull; // OpaqueDepth + ParticleDepth
            }
            if (desc.Samples > 1u) {
                viewBytes += Bytes2D(safeW, safeH, sceneFmt, desc.Samples);
                if (desc.EnableDepth) viewBytes += Bytes2D(safeW, safeH, desc.DepthFormat, desc.Samples);
            }
            if (desc.HDR) viewBytes += Bytes2D(safeW, safeH, RHI::RHITextureFormat::RGBA16_FLOAT);
            if (desc.FXAA) viewBytes += Bytes2D(safeW, safeH, desc.ColorFormat);
            if (desc.EnableNormal) viewBytes += Bytes2D(safeW, safeH, desc.NormalFormat);
            if (desc.EnableScreenStorage) viewBytes += Bytes2D(safeW, safeH, sceneFmt);
            if (desc.EnableReflectionTerms) {
                viewBytes += Bytes2D(safeW, safeH, RHI::RHITextureFormat::RGBA16_FLOAT) * 2ull;
                for (uint32_t lvl = 0; lvl < 4u; ++lvl) {
                    viewBytes += Bytes2D(std::max(1u, safeW >> (lvl + 1u)),
                                         std::max(1u, safeH >> (lvl + 1u)),
                                         RHI::RHITextureFormat::RGBA16_FLOAT);
                }
            }
            if (desc.EnableHiZ) {
                report.HiZMipLevels = RHI::RHIViewTargets::ComputeAllocatedHiZLevelCount(safeW, safeH);
                for (uint32_t lvl = 0; lvl < report.HiZMipLevels; ++lvl) {
                    viewBytes += Bytes2D(std::max(1u, safeW >> lvl),
                                         std::max(1u, safeH >> lvl),
                                         RHI::RHITextureFormat::R32_FLOAT);
                }
            }
            if (desc.EnableGTAO) {
                viewBytes += Bytes2D(aoExtent.Width, aoExtent.Height, RHI::RHITextureFormat::RGBA16_FLOAT);
                viewBytes += Bytes2D(safeW, safeH, RHI::RHITextureFormat::RGBA16_FLOAT);
            }
            if (desc.EnableSubsurface) {
                viewBytes += Bytes2D(safeW, safeH, RHI::RHITextureFormat::RGBA16_FLOAT) * 4ull;
            }
            if (desc.EnableBloom) {
                report.BloomLevels = 6u;
                for (uint32_t lvl = 0; lvl < 6u; ++lvl) {
                    uint32_t bw = 1u, bh = 1u;
                    RHI::RHIViewTargets::ComputeBloomExtent(safeW, safeH, lvl, bw, bh);
                    viewBytes += Bytes2D(bw, bh, RHI::RHITextureFormat::RGBA16_FLOAT);
                }
            }
            if (desc.EnableVelocity) {
                viewBytes += Bytes2D(safeW, safeH, RHI::RHITextureFormat::RG16_FLOAT);
                viewBytes += Bytes2D(safeW, safeH, RHI::RHITextureFormat::R32_FLOAT);
            }
            if (desc.EnableReactive) {
                viewBytes += Bytes2D(safeW, safeH, RHI::RHITextureFormat::R8_UNORM);
            }
            viewBytes += Bytes2D(safeW, safeH, sceneFmt) * static_cast<uint64_t>(desc.HistoryCount);
            if (desc.HistoryCount > 1u && desc.EnableDepth && desc.Samples == 1u) {
                viewBytes += Bytes2D(safeW, safeH, desc.DepthFormat);
            }
            if (desc.EnablePlanarReflection) {
                const auto pExt = PlanarReflectionContract::ComputeScaledTargetExtent(safeW, safeH, desc.PlanarResolutionScale);
                report.PlanarWidth = pExt.x;
                report.PlanarHeight = pExt.y;
                viewBytes += Bytes2D(pExt.x, pExt.y, RHI::RHITextureFormat::RGBA16_FLOAT);
                if (desc.EnableDepth) viewBytes += Bytes2D(pExt.x, pExt.y, desc.DepthFormat);
            }
        }
        report.ViewTargetEstimatedBytes = viewBytes;

        if (effective.EnableVolumetricFog && desc.EnableVolumetricFog && desc.FroxelQuality <= RHI::RHIViewTargets::MaxFroxelQualityTier) {
            const auto fq = static_cast<FroxelFogQuality>(desc.FroxelQuality);
            const auto fogBudget = FroxelFogContract::EstimateMemoryBudget(safeW, safeH, fq, false);
            report.FroxelExtent = fogBudget.Extent;
            report.FroxelMemoryBytes = fogBudget.TotalLogicalBytes;
        }

        if (isCompat) {
            report.ReflectionMaxProbesPerFrame = 0u;
            report.ReflectionMaxFacesPerFrame = 0u;
            report.ReflectionCacheBudgetBytes = 0ull;
        } else {
            report.ReflectionMaxProbesPerFrame = std::clamp(effective.ReflectionCaptureMaxProbesPerFrame, 1u, 8u);
            report.ReflectionMaxFacesPerFrame = std::clamp(effective.ReflectionCaptureMaxFacesPerFrame, 1u, 32u);
            report.ReflectionCacheBudgetBytes = effective.ReflectionCaptureCacheBudgetBytes;
        }

        if (isCompat) {
            report.DDGIPolicy = "Disabled (OpenGL compatibility)";
        } else if (effective.EnableDDGI) {
            const auto fallbackGrid = RHI::RHIViewTargets::ComputeDDGIAtlasExtent(4u, 2u, 4u);
            const auto& grid = (ddgiGrid && ddgiGrid->Valid) ? *ddgiGrid : fallbackGrid;
            report.DDGIMemoryBytes =
                Bytes2D(grid.IrradianceWidth, grid.IrradianceHeight, RHI::RHITextureFormat::RGBA16_FLOAT) +
                Bytes2D(grid.DistanceWidth, grid.DistanceHeight, RHI::RHITextureFormat::RGBA16_FLOAT);
            report.DDGIPolicy = (report.ResolvedPreset == RenderQualityPreset::Ultra)
                ? "Enabled (Ultra optional experimental DDGI)"
                : "Enabled (Experimental opt-in)";
        } else {
            report.DDGIPolicy = (report.ResolvedPreset == RenderQualityPreset::Ultra)
                ? "Opt-in available (experimental off by default)"
                : "Disabled (experimental opt-in only)";
        }

        report.RayTracingPolicy = "Disabled in M15 (deferred to M16)";
        report.TotalEstimatedBudgetBytes =
            report.ViewTargetEstimatedBytes +
            report.FroxelMemoryBytes +
            report.DDGIMemoryBytes +
            report.ReflectionCacheBudgetBytes;

        char buf[512];
        const double totalMiB = static_cast<double>(report.TotalEstimatedBudgetBytes) / (1024.0 * 1024.0);
        const double targetsMiB = static_cast<double>(report.ViewTargetEstimatedBytes + report.FroxelMemoryBytes) / (1024.0 * 1024.0);
        const double probeMiB = static_cast<double>(report.ReflectionCacheBudgetBytes) / (1024.0 * 1024.0);
        std::snprintf(
            buf, sizeof(buf),
            "%s | %ux%u MSAA=%ux | AO=%ux%u (%ux%u=%u taps) | Contact=%u | SSR=%u steps | Planar=%ux%u | Fog=%ux%ux%u (%.1f MiB targets) | Probes=%up/%uf (%.0f MiB) | Total=%.1f MiB | DDGI: %s | RT: M16",
            report.PresetName.c_str(),
            report.ViewWidth, report.ViewHeight, static_cast<unsigned>(report.ActiveMSAASamples),
            report.AOWidth, report.AOHeight, report.GTAOSliceCount, report.GTAOStepCount,
            report.GTAOSliceCount * report.GTAOStepCount,
            report.ContactShadowSteps, report.SSRMaxSteps,
            report.PlanarWidth, report.PlanarHeight,
            report.FroxelExtent.Width, report.FroxelExtent.Height, report.FroxelExtent.Depth,
            targetsMiB,
            report.ReflectionMaxProbesPerFrame, report.ReflectionMaxFacesPerFrame, probeMiB,
            totalMiB,
            report.DDGIPolicy.c_str());
        report.Summary = buf;
        return report;
    }

} // namespace Axle
