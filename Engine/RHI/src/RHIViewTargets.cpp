#include <Axle/RHI/RHIViewTargets.hpp>
#include <Axle/RHI/RHIDevice.hpp>

namespace Axle::RHI {
    bool RHIViewTargets::IsValid() const {
        if (!m_Initialized) return false;
        for (const auto& texture : m_Textures) if (texture && !texture->IsValid()) return false;
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
            candidate.m_Initialized = true; *this = std::move(candidate); return true;
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
        for (uint8_t i = 0; i < desc.HistoryCount; ++i)
            if (!create(i ? RHIViewTarget::History1 : RHIViewTarget::History0, desc.ColorFormat, 1, false)) return false;
        candidate.m_Initialized = true;
        *this = std::move(candidate); return true;
    }
}
