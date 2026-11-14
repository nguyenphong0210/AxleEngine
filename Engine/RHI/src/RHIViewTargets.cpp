#include <Axle/RHI/RHIViewTargets.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <algorithm>
#include <atomic>

namespace Axle::RHI {
    namespace { std::atomic<uint64_t> s_NextViewGeneration{1}; }

    bool RHIViewTargets::IsValid() const {
        if (!m_Initialized) return false;
        for (const auto& texture : m_Textures) if (texture && !texture->IsValid()) return false;
        return true;
    }
    bool RHIViewTargets::Validate() const {
        if (!IsValid()) return false;
        const auto require = [&](RHIViewTarget slot) { auto* texture = Get(slot); return texture && texture->IsValid(); };
        const auto check2D = [&](RHIViewTarget slot, uint32_t width, uint32_t height, RHITextureFormat format, uint8_t samples = 1) {
            auto* texture = Get(slot);
            return texture && texture->IsValid() && texture->GetWidth() == width && texture->GetHeight() == height &&
                   texture->GetFormat() == format && texture->GetDesc().SampleCount == samples;
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
        if (m_Desc.EnableScreenStorage &&
            (!check2D(RHIViewTarget::ScreenStorage, m_Desc.Width, m_Desc.Height,
                      m_Desc.HDR ? RHITextureFormat::RGBA16_FLOAT : m_Desc.ColorFormat) ||
             !Get(RHIViewTarget::ScreenStorage)->GetUAV())) return false;
        if (m_Desc.EnableGTAO && (!check2D(RHIViewTarget::GTAO, std::max(1u, m_Desc.Width >> 1), std::max(1u, m_Desc.Height >> 1), RHITextureFormat::RGBA16_FLOAT) || !Get(RHIViewTarget::GTAO)->GetUAV())) return false;
        for (uint32_t level = 0; m_Desc.EnableBloom && level < 6; ++level) {
            uint32_t width = std::max(1u, m_Desc.Width >> (level + 1));
            uint32_t height = std::max(1u, m_Desc.Height >> (level + 1));
            if (!check2D(static_cast<RHIViewTarget>(static_cast<uint8_t>(RHIViewTarget::Bloom0) + level), width, height,
                         m_Desc.HDR ? RHITextureFormat::RGBA16_FLOAT : m_Desc.ColorFormat)) return false;
        }
        if (m_Desc.EnableAutoExposure && !check2D(RHIViewTarget::Exposure, 1, 1, RHITextureFormat::RGBA16_FLOAT)) return false;
        if (m_Desc.EnableDDGI && !check2D(RHIViewTarget::DDGI, 256, 128, RHITextureFormat::RGBA16_FLOAT)) return false;
        if (m_Desc.EnableHiZ) {
            for (uint32_t level = 0; level < 8; ++level) {
                if (!check2D(static_cast<RHIViewTarget>(static_cast<uint8_t>(RHIViewTarget::HiZ0) + level),
                             std::max(1u, m_Desc.Width >> level), std::max(1u, m_Desc.Height >> level), RHITextureFormat::R32_FLOAT)) return false;
            }
        }
        if (m_Desc.HistoryCount > 0 && !check2D(RHIViewTarget::History0, m_Desc.Width, m_Desc.Height, sceneFormat)) return false;
        if (m_Desc.HistoryCount > 1 && !check2D(RHIViewTarget::History1, m_Desc.Width, m_Desc.Height, sceneFormat)) return false;
        return true;
    }
    bool RHIViewTargets::Configure(const RHIViewTargetDesc& desc, const RHITexture* externalColor, const RHITexture* externalDepth) {
        if (!desc.Width || !desc.Height || desc.HistoryCount > 2 ||
            (desc.Samples != 1 && desc.Samples != 2 && desc.Samples != 4 && desc.Samples != 8)) return false;
        RHIViewTargets candidate; candidate.m_Desc = desc;
        auto create = [&](RHIViewTarget slot, RHITextureFormat format, uint8_t samples, bool depth) {
            auto texture = std::make_unique<RHITexture>();
            auto native = depth ? RHITextureDesc::DepthStencil2D(desc.Width, desc.Height, format) :
                RHITextureDesc::RenderTarget2D(desc.Width, desc.Height, format);
            native.SampleCount = samples; native.DebugName = "ViewTarget_" + std::to_string(static_cast<unsigned>(slot));
            if (!texture->Init(native)) return false;
            candidate.m_Textures[static_cast<size_t>(slot)] = std::move(texture); return true;
        };
        auto import = [&](RHIViewTarget slot, const RHITexture& texture, RHITextureFormat format) {
            if (!texture.IsValid() || texture.GetWidth() != desc.Width || texture.GetHeight() != desc.Height ||
                texture.GetFormat() != format || texture.GetDesc().SampleCount != 1 ||
                texture.GetHandle().Epoch != RHIDevice::Get().GetEpoch()) return false;
            candidate.m_Textures[static_cast<size_t>(slot)] = std::make_unique<RHITexture>(texture.RetainGeneration()); return true;
        };
        if (externalColor) { if (!import(RHIViewTarget::Color, *externalColor, desc.ColorFormat)) return false; }
        else if (desc.OwnPrimary && !create(RHIViewTarget::Color, desc.ColorFormat, 1, false)) return false;
        if (externalDepth) { if (!desc.EnableDepth || !import(RHIViewTarget::Depth, *externalDepth, desc.DepthFormat)) return false; }
        else if (desc.OwnPrimary && desc.EnableDepth && !create(RHIViewTarget::Depth, desc.DepthFormat, 1, true)) return false;
        if (!desc.RenderAuxiliary) {
            if (desc.Samples != 1 || desc.HistoryCount || desc.HDR || desc.FXAA) return false;
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
            uint32_t mipWidth = desc.Width;
            uint32_t mipHeight = desc.Height;
            for (uint32_t level = 0; level < 8; ++level) {
                auto texture = std::make_unique<RHITexture>();
                auto native = RHITextureDesc::StorageTexture2D(
                    std::max(1u, mipWidth), std::max(1u, mipHeight), RHITextureFormat::R32_FLOAT,
                    "ViewTarget_HiZ" + std::to_string(level));
                if (!texture->Init(native)) return false;
                candidate.m_Textures[static_cast<size_t>(RHIViewTarget::HiZ0) + level] = std::move(texture);
                mipWidth = std::max(1u, mipWidth >> 1);
                mipHeight = std::max(1u, mipHeight >> 1);
            }
        }
        for (uint8_t i = 0; i < desc.HistoryCount; ++i)
            if (!create(i ? RHIViewTarget::History1 : RHIViewTarget::History0, sceneColor, 1, false)) return false;
        candidate.m_Initialized = true;
        candidate.m_Generation = s_NextViewGeneration.fetch_add(1);
        if (!candidate.Validate()) return false;
        *this = std::move(candidate); return true;
    }
}
