#include <Axle/RHIViewportDescriptor.hpp>
#include <Axle/Log.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>

namespace Axle {

    RHIViewportDescriptor::~RHIViewportDescriptor() {
        Shutdown();
    }

    RHIViewportDescriptor::RHIViewportDescriptor(RHIViewportDescriptor&& other) noexcept = default;
    RHIViewportDescriptor& RHIViewportDescriptor::operator=(RHIViewportDescriptor&& other) noexcept = default;

    bool RHIViewportDescriptor::Init(ImGuiLayer* imguiLayer, const RHIViewportDesc& desc) {
        if (!imguiLayer) return false;
        RHIViewportDescriptor candidate;
        candidate.m_Registrar = imguiLayer->GetTextureRegistrar();
        if (!candidate.Build(desc)) return false;
        *this = std::move(candidate);
        return true;
    }

    bool RHIViewportDescriptor::Build(const RHIViewportDesc& desc) {
        m_Desc = desc; m_OwnsColorTexture = true;
        RHI::RHIViewTargetDesc targets; targets.Width = desc.Width; targets.Height = desc.Height;
        targets.ColorFormat = desc.ColorFormat; targets.DepthFormat = desc.DepthFormat; targets.EnableDepth = desc.EnableDepth;
        targets.Samples = desc.MSAASamples; targets.HDR = desc.IntermediateHDR; targets.FXAA = desc.FXAA; targets.HistoryCount = desc.HistoryCount;
        targets.EnableNormal = desc.EnableNormal;
        targets.EnableScreenStorage = desc.EnableScreenStorage;
        m_Targets = std::make_shared<RHI::RHIViewTargets>();
        return m_Targets->Configure(targets) && RegisterWithImGui();
    }

    bool RHIViewportDescriptor::Recreate(const RHIViewportDesc& desc) {
        RHIViewportDescriptor candidate;
        candidate.m_Registrar = m_Registrar;
        if (!candidate.Build(desc)) return false;
        *this = std::move(candidate);
        return true;
    }

    bool RHIViewportDescriptor::Init(ImGuiLayer* imguiLayer, uint32_t width, uint32_t height, 
                                      RHI::RHITextureFormat colorFormat, bool enableDepth) {
        RHIViewportDesc desc;
        desc.Width = width;
        desc.Height = height;
        desc.ColorFormat = colorFormat;
        desc.EnableDepth = enableDepth;
        return Init(imguiLayer, desc);
    }

    bool RHIViewportDescriptor::BindExternalTexture(ImGuiLayer* imguiLayer, RHI::RHITexture* texture) {
        RHIViewportDescriptor candidate;
        if (!imguiLayer || !texture || !texture->IsValid()) {
            AXLE_CORE_ERROR("RHIViewportDescriptor::BindExternalTexture failed: Invalid arguments!");
            return false;
        }

        candidate.m_Registrar = imguiLayer->GetTextureRegistrar();
        RHI::RHIViewTargetDesc targets; targets.Width = texture->GetWidth(); targets.Height = texture->GetHeight();
        targets.ColorFormat = texture->GetFormat(); targets.EnableDepth = false; targets.OwnPrimary = false; targets.RenderAuxiliary = false;
        candidate.m_Targets = std::make_shared<RHI::RHIViewTargets>();
        if (!candidate.m_Targets->Configure(targets, texture)) return false;
        candidate.m_OwnsColorTexture = false;
        candidate.m_Desc.Width = texture->GetWidth();
        candidate.m_Desc.Height = texture->GetHeight();
        candidate.m_Desc.ColorFormat = texture->GetFormat();
        candidate.m_Desc.EnableDepth = false;

        if (!candidate.RegisterWithImGui()) return false;
        *this = std::move(candidate);
        return true;
    }

    bool RHIViewportDescriptor::SetHDR(bool enableHDR) {
        RHI::RHITextureFormat targetFormat = enableHDR ? RHI::RHITextureFormat::RGBA16_FLOAT : RHI::RHITextureFormat::RGBA8_UNORM;
        return SetColorFormat(targetFormat);
    }

    bool RHIViewportDescriptor::SetColorFormat(RHI::RHITextureFormat format) {
        if (!m_OwnsColorTexture) return false;
        if (m_Desc.ColorFormat == format && IsValid()) return true;
        auto desc = m_Desc;
        desc.ColorFormat = format;
        return Recreate(desc);
    }

    bool RHIViewportDescriptor::SetDepthFormat(RHI::RHITextureFormat format) {
        if (m_Desc.DepthFormat == format && IsValid()) return true;
        if (!m_Desc.EnableDepth) {
            if (format != RHI::RHITextureFormat::D32_FLOAT && format != RHI::RHITextureFormat::D24_UNORM_S8_UINT) return false;
            m_Desc.DepthFormat = format;
            return true;
        }
        if (!m_OwnsColorTexture) return false;
        auto desc = m_Desc;
        desc.DepthFormat = format;
        return Recreate(desc);
    }

    bool RHIViewportDescriptor::Resize(uint32_t width, uint32_t height) {
        if (!width || !height || !m_OwnsColorTexture) return false;
        if (m_Desc.Width == width && m_Desc.Height == height && IsValid()) return true;
        auto desc = m_Desc;
        desc.Width = width; desc.Height = height;
        return Recreate(desc);
    }

    void RHIViewportDescriptor::Shutdown() {
        UnregisterFromImGui(); m_Targets.reset(); m_Registrar = {}; m_OwnsColorTexture = true;
    }

    bool RHIViewportDescriptor::SetRenderOptions(uint8_t samples, bool hdr, bool fxaa, std::optional<uint8_t> requestedHistory,
                                                 bool enableNormal, bool enableScreenStorage) {
        if (!m_OwnsColorTexture) return false;
        const auto historyCount = requestedHistory.value_or(m_Desc.HistoryCount);
        if (m_Desc.MSAASamples == samples && m_Desc.IntermediateHDR == hdr && m_Desc.FXAA == fxaa &&
            m_Desc.HistoryCount == historyCount && m_Desc.EnableNormal == enableNormal &&
            m_Desc.EnableScreenStorage == enableScreenStorage && IsValid()) return true;
        auto desc = m_Desc; desc.MSAASamples = samples; desc.IntermediateHDR = hdr; desc.FXAA = fxaa;
        desc.HistoryCount = historyCount; desc.EnableNormal = enableNormal; desc.EnableScreenStorage = enableScreenStorage;
        return Recreate(desc);
    }

    bool RHIViewportDescriptor::RegisterWithImGui() {
        if (RHI::ScopedRHIFault::Invoke("RegisterViewportTexture", [] { return nri::Result::SUCCESS; }) != nri::Result::SUCCESS) return false;
        auto* color = GetColorTexture();
        if (!m_Registrar || !color || !color->IsValid()) return false;
        m_GuiTexture = m_Registrar(*color);
        return m_GuiTexture.IsValid();
    }

    void RHIViewportDescriptor::UnregisterFromImGui() { m_GuiTexture.Reset(); }
    glm::vec2 RHIViewportDescriptor::GetUV0() const { return {0, 0}; }
    glm::vec2 RHIViewportDescriptor::GetUV1() const { return {1, 1}; }

    void RHIViewportDescriptor::GetUVs(glm::vec2& outUV0, glm::vec2& outUV1) const {
        outUV0 = GetUV0();
        outUV1 = GetUV1();
    }

}
