#include <Axle/RHIViewportDescriptor.hpp>
#include <Axle/Log.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>

namespace Axle {

    RHIViewportDescriptor::~RHIViewportDescriptor() {
        Shutdown();
    }

    RHIViewportDescriptor::RHIViewportDescriptor(RHIViewportDescriptor&& other) noexcept
        : m_Registrar(std::move(other.m_Registrar))
        , m_Desc(std::move(other.m_Desc))
        , m_OwnedColorTexture(std::move(other.m_OwnedColorTexture))
        , m_DepthTexture(std::move(other.m_DepthTexture))
        , m_ColorTexture(other.m_ColorTexture)
        , m_OwnsColorTexture(other.m_OwnsColorTexture)
        , m_GuiTexture(std::move(other.m_GuiTexture)) {
        other.m_Registrar = {};
        other.m_ColorTexture = nullptr;
        other.m_GuiTexture.Reset();
    }

    RHIViewportDescriptor& RHIViewportDescriptor::operator=(RHIViewportDescriptor&& other) noexcept {
        if (this != &other) {
            Shutdown();
            m_Registrar = std::move(other.m_Registrar);
            m_Desc = std::move(other.m_Desc);
            m_OwnedColorTexture = std::move(other.m_OwnedColorTexture);
            m_DepthTexture = std::move(other.m_DepthTexture);
            m_ColorTexture = other.m_ColorTexture;
            m_OwnsColorTexture = other.m_OwnsColorTexture;
            m_GuiTexture = std::move(other.m_GuiTexture);

            other.m_Registrar = {};
            other.m_ColorTexture = nullptr;
            other.m_GuiTexture.Reset();
        }
        return *this;
    }

    bool RHIViewportDescriptor::Init(ImGuiLayer* imguiLayer, const RHIViewportDesc& desc) {
        if (!imguiLayer) return false;
        RHIViewportDescriptor candidate;
        candidate.m_Registrar = imguiLayer->GetTextureRegistrar();
        if (!candidate.Build(desc)) return false;
        *this = std::move(candidate);
        return true;
    }

    bool RHIViewportDescriptor::Build(const RHIViewportDesc& desc) {
        m_Desc = desc;
        m_OwnsColorTexture = true;

        m_OwnedColorTexture = std::make_unique<RHI::RHITexture>();
        auto colorDesc = RHI::RHITextureDesc::RenderTarget2D(desc.Width, desc.Height, desc.ColorFormat, desc.DebugName + "_Color");
        if (!m_OwnedColorTexture->Init(colorDesc)) {
            AXLE_CORE_ERROR("RHIViewportDescriptor::Init: Failed to create color texture!");
            return false;
        }
        m_ColorTexture = m_OwnedColorTexture.get();

        if (desc.EnableDepth) {
            m_DepthTexture = std::make_unique<RHI::RHITexture>();
            auto depthDesc = RHI::RHITextureDesc::DepthStencil2D(desc.Width, desc.Height, desc.DepthFormat, desc.DebugName + "_Depth");
            if (!m_DepthTexture->Init(depthDesc)) {
                AXLE_CORE_ERROR("RHIViewportDescriptor::Init: Failed to create depth texture!");
                return false;
            }
        }

        if (!RegisterWithImGui()) {
            AXLE_CORE_ERROR("RHIViewportDescriptor::Init: Failed to register texture with ImGuiLayer!");
            return false;
        }

        return true;
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
        candidate.m_ColorTexture = texture;
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
        UnregisterFromImGui();
        if (m_OwnsColorTexture) {
            if (m_OwnedColorTexture) {
                m_OwnedColorTexture->Shutdown();
                m_OwnedColorTexture.reset();
            }
        }
        if (m_DepthTexture) {
            m_DepthTexture->Shutdown();
            m_DepthTexture.reset();
        }
        m_ColorTexture = nullptr;
        m_Registrar = {};
        m_GuiTexture.Reset();
        m_OwnsColorTexture = true;
    }

    bool RHIViewportDescriptor::RegisterWithImGui() {
        if (RHI::ScopedRHIFault::Invoke("RegisterViewportTexture", [] { return nri::Result::SUCCESS; }) != nri::Result::SUCCESS) return false;
        if (!m_Registrar || !m_ColorTexture || !m_ColorTexture->IsValid()) return false;
        m_GuiTexture = m_Registrar(*m_ColorTexture);
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
