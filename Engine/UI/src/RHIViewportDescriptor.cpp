#include <Axle/RHIViewportDescriptor.hpp>
#include <Axle/Log.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>

namespace Axle {

    RHIViewportDescriptor::~RHIViewportDescriptor() {
        Shutdown();
    }

    RHIViewportDescriptor::RHIViewportDescriptor(RHIViewportDescriptor&& other) noexcept
        : m_ImGuiLayer(other.m_ImGuiLayer)
        , m_Desc(std::move(other.m_Desc))
        , m_OwnedColorTexture(std::move(other.m_OwnedColorTexture))
        , m_DepthTexture(std::move(other.m_DepthTexture))
        , m_ColorTexture(other.m_ColorTexture)
        , m_OwnsColorTexture(other.m_OwnsColorTexture)
        , m_ImTextureID(other.m_ImTextureID) {
        other.m_ImGuiLayer = nullptr;
        other.m_ColorTexture = nullptr;
        other.m_ImTextureID = 0;
    }

    RHIViewportDescriptor& RHIViewportDescriptor::operator=(RHIViewportDescriptor&& other) noexcept {
        if (this != &other) {
            Shutdown();
            m_ImGuiLayer = other.m_ImGuiLayer;
            m_Desc = std::move(other.m_Desc);
            m_OwnedColorTexture = std::move(other.m_OwnedColorTexture);
            m_DepthTexture = std::move(other.m_DepthTexture);
            m_ColorTexture = other.m_ColorTexture;
            m_OwnsColorTexture = other.m_OwnsColorTexture;
            m_ImTextureID = other.m_ImTextureID;

            other.m_ImGuiLayer = nullptr;
            other.m_ColorTexture = nullptr;
            other.m_ImTextureID = 0;
        }
        return *this;
    }

    bool RHIViewportDescriptor::Init(ImGuiLayer* imguiLayer, const RHIViewportDesc& desc) {
        RHIViewportDescriptor candidate;
        if (!imguiLayer) {
            AXLE_CORE_ERROR("RHIViewportDescriptor::Init failed: ImGuiLayer is null!");
            return false;
        }

        candidate.m_ImGuiLayer = imguiLayer;
        candidate.m_Desc = desc;
        candidate.m_OwnsColorTexture = true;

        candidate.m_OwnedColorTexture = std::make_unique<RHI::RHITexture>();
        auto colorDesc = RHI::RHITextureDesc::RenderTarget2D(desc.Width, desc.Height, desc.ColorFormat, desc.DebugName + "_Color");
        if (!candidate.m_OwnedColorTexture->Init(colorDesc)) {
            AXLE_CORE_ERROR("RHIViewportDescriptor::Init: Failed to create color texture!");
            return false;
        }
        candidate.m_ColorTexture = candidate.m_OwnedColorTexture.get();

        if (desc.EnableDepth) {
            candidate.m_DepthTexture = std::make_unique<RHI::RHITexture>();
            auto depthDesc = RHI::RHITextureDesc::DepthStencil2D(desc.Width, desc.Height, desc.DepthFormat, desc.DebugName + "_Depth");
            if (!candidate.m_DepthTexture->Init(depthDesc)) {
                AXLE_CORE_ERROR("RHIViewportDescriptor::Init: Failed to create depth texture!");
                return false;
            }
        }

        if (!candidate.RegisterWithImGui()) {
            AXLE_CORE_ERROR("RHIViewportDescriptor::Init: Failed to register texture with ImGuiLayer!");
            return false;
        }

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

        candidate.m_ImGuiLayer = imguiLayer;
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
        return Init(m_ImGuiLayer, desc);
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
        return Init(m_ImGuiLayer, desc);
    }

    bool RHIViewportDescriptor::Resize(uint32_t width, uint32_t height) {
        if (!width || !height || !m_OwnsColorTexture) return false;
        if (m_Desc.Width == width && m_Desc.Height == height && IsValid()) return true;
        auto desc = m_Desc;
        desc.Width = width; desc.Height = height;
        return Init(m_ImGuiLayer, desc);
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
        m_ImGuiLayer = nullptr;
        m_ImTextureID = 0;
        m_OwnsColorTexture = true;
    }

    bool RHIViewportDescriptor::RegisterWithImGui() {
        if (RHI::ScopedRHIFault::Invoke("RegisterViewportTexture", [] { return nri::Result::SUCCESS; }) != nri::Result::SUCCESS) return false;
        if (!m_ImGuiLayer || !m_ColorTexture || !m_ColorTexture->IsValid()) {
            return false;
        }

        if (m_ImGuiLayer->GetBackendType() == ImGuiBackendType::Direct3D12) {
            uint64_t cpuHandle = m_ColorTexture->GetNativeSRVHandle();
            if (cpuHandle == 0) {
                AXLE_CORE_ERROR("RHIViewportDescriptor::RegisterWithImGui: Native SRV handle is null!");
                return false;
            }
            m_ImTextureID = m_ImGuiLayer->RegisterTextureDescriptor(cpuHandle);
            return m_ImTextureID != 0;
        } else if (m_ImGuiLayer->GetBackendType() == ImGuiBackendType::OpenGL) {
            return false; // Native RHI textures have no OpenGL registration.
        } else if (m_ImGuiLayer->GetBackendType() == ImGuiBackendType::Vulkan) {
            uint64_t vkImageView = m_ColorTexture->GetNativeSRVHandle();
            if (vkImageView == 0) {
                AXLE_CORE_ERROR("RHIViewportDescriptor::RegisterWithImGui: Native Vulkan ImageView handle is null!");
                return false;
            }
            m_ImTextureID = m_ImGuiLayer->RegisterTextureDescriptor(vkImageView);
            return m_ImTextureID != 0;
        }
        return false;
    }

    void RHIViewportDescriptor::UnregisterFromImGui() {
        if (m_ImGuiLayer && m_ImTextureID != 0) {
            m_ImGuiLayer->UnregisterTextureDescriptor(m_ImTextureID);
            m_ImTextureID = 0;
        }
    }

    glm::vec2 RHIViewportDescriptor::GetUV0() const {
        if (m_ImGuiLayer && m_ImGuiLayer->GetBackendType() == ImGuiBackendType::OpenGL) {
            return { 0.0f, 1.0f };
        }
        return { 0.0f, 0.0f };
    }

    glm::vec2 RHIViewportDescriptor::GetUV1() const {
        if (m_ImGuiLayer && m_ImGuiLayer->GetBackendType() == ImGuiBackendType::OpenGL) {
            return { 1.0f, 0.0f };
        }
        return { 1.0f, 1.0f };
    }

    void RHIViewportDescriptor::GetUVs(glm::vec2& outUV0, glm::vec2& outUV1) const {
        outUV0 = GetUV0();
        outUV1 = GetUV1();
    }

}
