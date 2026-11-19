#include <Axle/RHI/RHIViewTargets.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>

namespace Axle::RHI {
    namespace {
        std::atomic<uint64_t> s_NextViewGeneration{1};
        constexpr std::array<RHIViewTarget, 4> kFroxelSlots = {
            RHIViewTarget::FroxelScattering,
            RHIViewTarget::FroxelIntegrated,
            RHIViewTarget::FroxelHistory0,
            RHIViewTarget::FroxelHistory1
        };
        constexpr std::array<RHIFroxelTierSpec, RHIViewTargets::MaxFroxelQualityTier + 1> kFroxelTiers = {{
            {{ 80u,  45u, 32u}, 16u, "Low"},
            {{128u,  72u, 48u}, 10u, "Medium"},
            {{160u,  90u, 64u},  8u, "High"},
            {{240u, 135u, 96u},  8u, "Ultra"}
        }};
    }

    uint32_t RHIViewTargets::ComputePlanarTargetDimension(uint32_t extent, float scale) {
        if (extent == 0u) return 0u;
        const float clamped = std::isfinite(scale)
            ? std::clamp(scale, MinPlanarResolutionScale, MaxPlanarResolutionScale)
            : 0.5f;
        const auto scaled = static_cast<uint32_t>(std::lround(static_cast<double>(extent) * static_cast<double>(clamped)));
        return std::clamp(scaled, 1u, extent);
    }

    uint32_t RHIViewTargets::ComputeUsefulHiZLevelCount(uint32_t width, uint32_t height) {
        if (width == 0 || height == 0) return 0;
        uint32_t levels = 1;
        uint32_t w = width;
        uint32_t h = height;
        while ((w > 1 || h > 1) && levels < MaxHiZLevels) {
            w = std::max(1u, w >> 1);
            h = std::max(1u, h >> 1);
            ++levels;
        }
        return levels;
    }

    uint32_t RHIViewTargets::ComputeAllocatedHiZLevelCount(uint32_t width, uint32_t height) {
        const uint32_t useful = ComputeUsefulHiZLevelCount(width, height);
        return useful == 0 ? 0 : std::min(MaxHiZLevels, std::max(MinSSRHiZLevels, useful));
    }

    const RHIFroxelTierSpec* RHIViewTargets::GetFroxelTierSpec(uint8_t qualityTier) {
        return qualityTier < kFroxelTiers.size() ? &kFroxelTiers[qualityTier] : nullptr;
    }

    RHIFroxelGridExtent RHIViewTargets::ComputeFroxelGridExtent(uint32_t width, uint32_t height, uint8_t qualityTier) {
        const auto* tier = GetFroxelTierSpec(qualityTier);
        if (!width || !height || !tier) return {};
        const auto& limit = tier->Limit;
        const double maxTileScale = 1.0 / static_cast<double>(tier->MinTilePixels);
        const double fitScale = std::min({
            maxTileScale,
            static_cast<double>(limit.Width) / static_cast<double>(width),
            static_cast<double>(limit.Height) / static_cast<double>(height)
        });
        const uint32_t froxelW = std::clamp(
            static_cast<uint32_t>(std::lround(static_cast<double>(width) * fitScale)),
            1u, limit.Width);
        const uint32_t froxelH = std::clamp(
            static_cast<uint32_t>(std::lround(static_cast<double>(height) * fitScale)),
            1u, limit.Height);
        return { froxelW, froxelH, limit.Depth };
    }

    bool RHIViewTargets::IsValid() const {
        if (!m_Initialized) return false;
        for (const auto& texture : m_Textures) if (texture && !texture->IsValid()) return false;
        for (const auto& texture : m_ExtraHiZTextures) if (texture && !texture->IsValid()) return false;
        return true;
    }
    bool RHIViewTargets::Validate() const {
        if (!IsValid()) return false;
        const auto require = [&](RHIViewTarget slot) { auto* texture = Get(slot); return texture && texture->IsValid(); };
        const auto check2D = [&](RHIViewTarget slot, uint32_t width, uint32_t height, RHITextureFormat format, uint8_t samples = 1) {
            auto* texture = Get(slot);
            return texture && texture->IsValid() && texture->GetWidth() == width && texture->GetHeight() == height &&
                   !texture->GetDesc().Is3D() && texture->GetFormat() == format && texture->GetDesc().SampleCount == samples;
        };
        if (m_Desc.OwnPrimary && !check2D(RHIViewTarget::Color, m_Desc.Width, m_Desc.Height, m_Desc.ColorFormat)) return false;
        if (m_Desc.EnableDepth && m_Desc.OwnPrimary && !check2D(RHIViewTarget::Depth, m_Desc.Width, m_Desc.Height, m_Desc.DepthFormat)) return false;
        if (!m_Desc.RenderAuxiliary) return true;
        const auto sceneFormat = m_Desc.HDR ? RHITextureFormat::RGBA16_FLOAT : m_Desc.ColorFormat;
        if (!check2D(RHIViewTarget::OpaqueColor, m_Desc.Width, m_Desc.Height, sceneFormat)) return false;
        if (m_Desc.EnableDepth && (!check2D(RHIViewTarget::OpaqueDepth, m_Desc.Width, m_Desc.Height, m_Desc.DepthFormat) ||
            !check2D(RHIViewTarget::ParticleDepth, m_Desc.Width, m_Desc.Height, m_Desc.DepthFormat))) return false;
        if (m_Desc.Samples > 1 && (!check2D(RHIViewTarget::MSAAColor, m_Desc.Width, m_Desc.Height, sceneFormat, m_Desc.Samples) ||
            (m_Desc.EnableDepth && !check2D(RHIViewTarget::MSAADepth, m_Desc.Width, m_Desc.Height, m_Desc.DepthFormat, m_Desc.Samples)))) return false;
        if (m_Desc.HDR && !check2D(RHIViewTarget::HDR, m_Desc.Width, m_Desc.Height, RHITextureFormat::RGBA16_FLOAT)) return false;
        if (m_Desc.FXAA && !check2D(RHIViewTarget::FXAA, m_Desc.Width, m_Desc.Height, m_Desc.ColorFormat)) return false;
        if (m_Desc.EnableNormal && !check2D(RHIViewTarget::ScreenNormal, m_Desc.Width, m_Desc.Height, m_Desc.NormalFormat)) return false;
        if (m_Desc.EnableReflectionTerms &&
            (!check2D(RHIViewTarget::ReflectionEnvironment, m_Desc.Width, m_Desc.Height, RHITextureFormat::RGBA16_FLOAT) ||
             !check2D(RHIViewTarget::ReflectionResponse, m_Desc.Width, m_Desc.Height, RHITextureFormat::RGBA16_FLOAT))) return false;
        for (uint32_t level = 0; m_Desc.EnableReflectionTerms && level < 4; ++level) {
            const uint32_t expectedW = std::max(1u, m_Desc.Width >> (level + 1u));
            const uint32_t expectedH = std::max(1u, m_Desc.Height >> (level + 1u));
            auto* ssrColor = GetSSRColor(level);
            if (!check2D(static_cast<RHIViewTarget>(static_cast<uint8_t>(RHIViewTarget::SSRColor0) + level),
                         expectedW, expectedH, RHITextureFormat::RGBA16_FLOAT) ||
                !ssrColor || !ssrColor->GetUAV()) return false;
        }
        if (m_Desc.EnableScreenStorage &&
            (!check2D(RHIViewTarget::ScreenStorage, m_Desc.Width, m_Desc.Height,
                      m_Desc.HDR ? RHITextureFormat::RGBA16_FLOAT : m_Desc.ColorFormat) ||
             !Get(RHIViewTarget::ScreenStorage)->GetUAV())) return false;
        if (m_Desc.EnableGTAO && (!check2D(RHIViewTarget::GTAO, std::max(1u, m_Desc.Width >> 1), std::max(1u, m_Desc.Height >> 1), RHITextureFormat::RGBA16_FLOAT) || !Get(RHIViewTarget::GTAO)->GetUAV() ||
            !check2D(RHIViewTarget::GTAOResolved, m_Desc.Width, m_Desc.Height, RHITextureFormat::RGBA16_FLOAT) || !Get(RHIViewTarget::GTAOResolved)->GetUAV())) return false;
        for (uint32_t level = 0; m_Desc.EnableBloom && level < 6; ++level) {
            uint32_t width = std::max(1u, m_Desc.Width >> (level + 1));
            uint32_t height = std::max(1u, m_Desc.Height >> (level + 1));
            if (!check2D(static_cast<RHIViewTarget>(static_cast<uint8_t>(RHIViewTarget::Bloom0) + level), width, height,
                         m_Desc.HDR ? RHITextureFormat::RGBA16_FLOAT : m_Desc.ColorFormat)) return false;
        }
        if (m_Desc.EnableAutoExposure && !check2D(RHIViewTarget::Exposure, 1, 1, RHITextureFormat::RGBA16_FLOAT)) return false;
        if (m_Desc.EnableDDGI && !check2D(RHIViewTarget::DDGI, 256, 128, RHITextureFormat::RGBA16_FLOAT)) return false;
        if (m_Desc.EnableHiZ) {
            const uint32_t expectedUseful = ComputeUsefulHiZLevelCount(m_Desc.Width, m_Desc.Height);
            const uint32_t expectedAllocated = ComputeAllocatedHiZLevelCount(m_Desc.Width, m_Desc.Height);
            if (m_HiZUsefulLevels != expectedUseful || m_HiZAllocatedLevels != expectedAllocated || expectedAllocated == 0)
                return false;
            for (uint32_t level = 0; level < MaxHiZLevels; ++level) {
                auto* texture = GetHiZ(level);
                if (level < expectedAllocated) {
                    const uint32_t expectedW = std::max(1u, m_Desc.Width >> level);
                    const uint32_t expectedH = std::max(1u, m_Desc.Height >> level);
                    if (!texture || !texture->IsValid() || texture->GetWidth() != expectedW ||
                        texture->GetHeight() != expectedH || texture->GetFormat() != RHITextureFormat::R32_FLOAT ||
                        texture->GetDesc().SampleCount != 1 || !texture->GetUAV()) return false;
                } else if (texture != nullptr) {
                    return false;
                }
            }
        } else if (m_HiZUsefulLevels != 0 || m_HiZAllocatedLevels != 0) {
            return false;
        }
        if (m_Desc.HistoryCount > 0 && !check2D(RHIViewTarget::History0, m_Desc.Width, m_Desc.Height, sceneFormat)) return false;
        if (m_Desc.HistoryCount > 1 && !check2D(RHIViewTarget::History1, m_Desc.Width, m_Desc.Height, sceneFormat)) return false;
        if (m_Desc.EnablePlanarReflection) {
            if (!std::isfinite(m_Desc.PlanarResolutionScale) ||
                m_Desc.PlanarResolutionScale < MinPlanarResolutionScale ||
                m_Desc.PlanarResolutionScale > MaxPlanarResolutionScale) {
                return false;
            }
            const uint32_t planarW = ComputePlanarTargetDimension(m_Desc.Width, m_Desc.PlanarResolutionScale);
            const uint32_t planarH = ComputePlanarTargetDimension(m_Desc.Height, m_Desc.PlanarResolutionScale);
            if (planarW == 0u || planarH == 0u ||
                !check2D(RHIViewTarget::PlanarColor, planarW, planarH, RHITextureFormat::RGBA16_FLOAT) ||
                (m_Desc.EnableDepth && !check2D(RHIViewTarget::PlanarDepth, planarW, planarH, m_Desc.DepthFormat))) {
                return false;
            }
        } else {
            if (Get(RHIViewTarget::PlanarColor) != nullptr || Get(RHIViewTarget::PlanarDepth) != nullptr) {
                return false;
            }
        }
        const auto check3D = [&](RHIViewTarget slot, const RHIFroxelGridExtent& extent, RHITextureFormat format) {
            auto* texture = Get(slot);
            return texture && texture->IsValid() &&
                   texture->GetWidth() == extent.Width &&
                   texture->GetHeight() == extent.Height &&
                   texture->GetDepth() == extent.Depth &&
                   texture->GetDesc().Is3D() &&
                   texture->GetMipCount() == 1u &&
                   texture->GetDesc().ArrayLayers == 1u &&
                   texture->GetDesc().SampleCount == 1u &&
                   texture->GetFormat() == format &&
                   texture->GetSRV() != nullptr &&
                   texture->GetUAV() != nullptr;
        };
        if (m_Desc.FroxelQuality > MaxFroxelQualityTier) return false;
        if (m_Desc.EnableVolumetricFog) {
            if (!m_Desc.EnableDepth || m_Desc.Samples != 1u) return false;
            const auto froxelExtent = ComputeFroxelGridExtent(m_Desc.Width, m_Desc.Height, m_Desc.FroxelQuality);
            if (!froxelExtent.Width || !froxelExtent.Height || !froxelExtent.Depth) return false;
            for (const auto slot : kFroxelSlots) {
                if (!check3D(slot, froxelExtent, RHITextureFormat::RGBA16_FLOAT)) return false;
            }
        } else {
            for (const auto slot : kFroxelSlots) {
                if (Get(slot) != nullptr) return false;
            }
        }
        return true;
    }
    bool RHIViewTargets::Configure(const RHIViewTargetDesc& desc, const RHITexture* externalColor, const RHITexture* externalDepth) {
        if (!desc.Width || !desc.Height || desc.HistoryCount > 2 ||
            (desc.Samples != 1 && desc.Samples != 2 && desc.Samples != 4 && desc.Samples != 8)) return false;
        if (desc.FroxelQuality > MaxFroxelQualityTier) return false;
        if (desc.EnableVolumetricFog && (!desc.RenderAuxiliary || !desc.EnableDepth || desc.Samples != 1)) return false;
        if (desc.EnablePlanarReflection &&
            (!std::isfinite(desc.PlanarResolutionScale) ||
             desc.PlanarResolutionScale < MinPlanarResolutionScale ||
             desc.PlanarResolutionScale > MaxPlanarResolutionScale)) {
            return false;
        }
        RHIViewTargets candidate; candidate.m_Desc = desc;
        auto createSized = [&](RHIViewTarget slot, uint32_t w, uint32_t h, RHITextureFormat format, uint8_t samples, bool depth) {
            auto texture = std::make_unique<RHITexture>();
            auto native = depth ? RHITextureDesc::DepthStencil2D(w, h, format) :
                RHITextureDesc::RenderTarget2D(w, h, format);
            native.SampleCount = samples; native.DebugName = "ViewTarget_" + std::to_string(static_cast<unsigned>(slot));
            if (!texture->Init(native)) return false;
            candidate.m_Textures[static_cast<size_t>(slot)] = std::move(texture); return true;
        };
        auto create = [&](RHIViewTarget slot, RHITextureFormat format, uint8_t samples, bool depth) {
            return createSized(slot, desc.Width, desc.Height, format, samples, depth);
        };
        auto import = [&](RHIViewTarget slot, const RHITexture& texture, RHITextureFormat format) {
            if (!texture.IsValid() || texture.GetWidth() != desc.Width || texture.GetHeight() != desc.Height ||
                texture.GetFormat() != format || texture.GetDesc().Is3D() || texture.GetDesc().SampleCount != 1 ||
                texture.GetHandle().Epoch != RHIDevice::Get().GetEpoch()) return false;
            candidate.m_Textures[static_cast<size_t>(slot)] = std::make_unique<RHITexture>(texture.RetainGeneration()); return true;
        };
        if (externalColor) { if (!import(RHIViewTarget::Color, *externalColor, desc.ColorFormat)) return false; }
        else if (desc.OwnPrimary && !create(RHIViewTarget::Color, desc.ColorFormat, 1, false)) return false;
        if (externalDepth) { if (!desc.EnableDepth || !import(RHIViewTarget::Depth, *externalDepth, desc.DepthFormat)) return false; }
        else if (desc.OwnPrimary && desc.EnableDepth && !create(RHIViewTarget::Depth, desc.DepthFormat, 1, true)) return false;
        if (!desc.RenderAuxiliary) {
            if (desc.Samples != 1 || desc.HistoryCount || desc.HDR || desc.FXAA ||
                desc.EnablePlanarReflection) return false;
            candidate.m_Initialized = true;
            candidate.m_Generation = s_NextViewGeneration.fetch_add(1);
            if (!candidate.Validate()) return false;
            *this = std::move(candidate); return true;
        }
        const auto sceneColor = desc.HDR ? RHITextureFormat::RGBA16_FLOAT : desc.ColorFormat;
        if (desc.Samples > 1 && (!create(RHIViewTarget::MSAAColor, sceneColor, desc.Samples, false) ||
            (desc.EnableDepth && !create(RHIViewTarget::MSAADepth, desc.DepthFormat, desc.Samples, true)))) return false;
        if (desc.HDR && !create(RHIViewTarget::HDR, RHITextureFormat::RGBA16_FLOAT, 1, false)) return false;
        if (desc.FXAA && !create(RHIViewTarget::FXAA, desc.ColorFormat, 1, false)) return false;
        if (!create(RHIViewTarget::OpaqueColor, sceneColor, 1, false)) return false;
        if (desc.EnableDepth && (!create(RHIViewTarget::OpaqueDepth, desc.DepthFormat, 1, true) ||
            !create(RHIViewTarget::ParticleDepth, desc.DepthFormat, 1, true))) return false;
        if (desc.EnableNormal && !create(RHIViewTarget::ScreenNormal, desc.NormalFormat, 1, false)) return false;
        if (desc.EnableReflectionTerms &&
            (!create(RHIViewTarget::ReflectionEnvironment, RHITextureFormat::RGBA16_FLOAT, 1, false) ||
             !create(RHIViewTarget::ReflectionResponse, RHITextureFormat::RGBA16_FLOAT, 1, false))) return false;
        if (desc.EnablePlanarReflection) {
            const uint32_t planarW = ComputePlanarTargetDimension(desc.Width, desc.PlanarResolutionScale);
            const uint32_t planarH = ComputePlanarTargetDimension(desc.Height, desc.PlanarResolutionScale);
            if (!createSized(RHIViewTarget::PlanarColor, planarW, planarH, RHITextureFormat::RGBA16_FLOAT, 1, false) ||
                (desc.EnableDepth && !createSized(RHIViewTarget::PlanarDepth, planarW, planarH, desc.DepthFormat, 1, true))) {
                return false;
            }
        }
        if (desc.EnableReflectionTerms) {
            for (uint32_t level = 0; level < 4; ++level) {
                const uint32_t colorWidth = std::max(1u, desc.Width >> (level + 1u));
                const uint32_t colorHeight = std::max(1u, desc.Height >> (level + 1u));
                auto texture = std::make_unique<RHITexture>();
                auto native = RHITextureDesc::StorageTexture2D(
                    colorWidth, colorHeight, RHITextureFormat::RGBA16_FLOAT, "ViewTarget_SSRColor" + std::to_string(level));
                if (!texture->Init(native)) return false;
                candidate.m_Textures[static_cast<size_t>(RHIViewTarget::SSRColor0) + level] = std::move(texture);
            }
        }
        if (desc.EnableScreenStorage) {
            auto texture = std::make_unique<RHITexture>();
            auto native = RHITextureDesc::StorageTexture2D(desc.Width, desc.Height, desc.HDR ? RHITextureFormat::RGBA16_FLOAT : desc.ColorFormat, "ViewTarget_ScreenStorage");
            if (!texture->Init(native)) return false;
            candidate.m_Textures[static_cast<size_t>(RHIViewTarget::ScreenStorage)] = std::move(texture);
        }
        if (desc.EnableGTAO) {
            auto texture = std::make_unique<RHITexture>();
            auto native = RHITextureDesc::StorageTexture2D(
                std::max(1u, desc.Width >> 1), std::max(1u, desc.Height >> 1), RHITextureFormat::RGBA16_FLOAT,
                "ViewTarget_GTAO_HalfRes");
            if (!texture->Init(native)) return false;
            candidate.m_Textures[static_cast<size_t>(RHIViewTarget::GTAO)] = std::move(texture);

            auto resolvedTexture = std::make_unique<RHITexture>();
            auto resolvedNative = RHITextureDesc::StorageTexture2D(
                desc.Width, desc.Height, RHITextureFormat::RGBA16_FLOAT,
                "ViewTarget_GTAO_Resolved");
            if (!resolvedTexture->Init(resolvedNative)) return false;
            candidate.m_Textures[static_cast<size_t>(RHIViewTarget::GTAOResolved)] = std::move(resolvedTexture);
        }
        if (desc.EnableBloom) {
            uint32_t bloomWidth = std::max(1u, desc.Width >> 1);
            uint32_t bloomHeight = std::max(1u, desc.Height >> 1);
            const auto bloomFormat = desc.HDR ? RHITextureFormat::RGBA16_FLOAT : desc.ColorFormat;
            for (uint32_t level = 0; level < 6; ++level) {
                auto texture = std::make_unique<RHITexture>();
                auto native = RHITextureDesc::StorageTexture2D(
                    bloomWidth, bloomHeight, bloomFormat, "ViewTarget_Bloom" + std::to_string(level));
                if (!texture->Init(native)) return false;
                candidate.m_Textures[static_cast<size_t>(RHIViewTarget::Bloom0) + level] = std::move(texture);
                bloomWidth = std::max(1u, bloomWidth >> 1);
                bloomHeight = std::max(1u, bloomHeight >> 1);
            }
        }
        if (desc.EnableAutoExposure) {
            auto texture = std::make_unique<RHITexture>();
            auto native = RHITextureDesc::StorageTexture2D(1, 1, RHITextureFormat::RGBA16_FLOAT, "ViewTarget_Exposure");
            if (!texture->Init(native)) return false;
            candidate.m_Textures[static_cast<size_t>(RHIViewTarget::Exposure)] = std::move(texture);
        }
        if (desc.EnableDDGI) {
            auto texture = std::make_unique<RHITexture>();
            auto native = RHITextureDesc::StorageTexture2D(256, 128, RHITextureFormat::RGBA16_FLOAT, "ViewTarget_DDGI_Irradiance");
            if (!texture->Init(native)) return false;
            candidate.m_Textures[static_cast<size_t>(RHIViewTarget::DDGI)] = std::move(texture);
        }
        if (desc.EnableHiZ) {
            candidate.m_HiZUsefulLevels = ComputeUsefulHiZLevelCount(desc.Width, desc.Height);
            candidate.m_HiZAllocatedLevels = ComputeAllocatedHiZLevelCount(desc.Width, desc.Height);
            uint32_t mipWidth = desc.Width;
            uint32_t mipHeight = desc.Height;
            for (uint32_t level = 0; level < candidate.m_HiZAllocatedLevels; ++level) {
                auto texture = std::make_unique<RHITexture>();
                auto native = RHITextureDesc::StorageTexture2D(
                    std::max(1u, mipWidth), std::max(1u, mipHeight), RHITextureFormat::R32_FLOAT,
                    "ViewTarget_HiZ" + std::to_string(level));
                if (!texture->Init(native)) return false;
                if (level < 8) {
                    candidate.m_Textures[static_cast<size_t>(RHIViewTarget::HiZ0) + level] = std::move(texture);
                } else {
                    candidate.m_ExtraHiZTextures[level - 8] = std::move(texture);
                }
                mipWidth = std::max(1u, mipWidth >> 1);
                mipHeight = std::max(1u, mipHeight >> 1);
            }
        }
        for (uint8_t i = 0; i < desc.HistoryCount; ++i)
            if (!create(i ? RHIViewTarget::History1 : RHIViewTarget::History0, sceneColor, 1, false)) return false;
        if (desc.EnableVolumetricFog) {
            const auto froxelExtent = ComputeFroxelGridExtent(desc.Width, desc.Height, desc.FroxelQuality);
            if (!froxelExtent.Width || !froxelExtent.Height || !froxelExtent.Depth) return false;
            for (const auto slot : kFroxelSlots) {
                auto texture = std::make_unique<RHITexture>();
                auto native = RHITextureDesc::StorageTexture3D(
                    froxelExtent.Width, froxelExtent.Height, froxelExtent.Depth, RHITextureFormat::RGBA16_FLOAT,
                    "ViewTarget_" + std::to_string(static_cast<unsigned>(slot)));
                if (!texture->Init(native)) return false;
                candidate.m_Textures[static_cast<size_t>(slot)] = std::move(texture);
            }
        }
        candidate.m_Initialized = true;
        candidate.m_Generation = s_NextViewGeneration.fetch_add(1);
        if (!candidate.Validate()) return false;
        *this = std::move(candidate); return true;
    }
}
