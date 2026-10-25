#include <Axle/RHIViewportDescriptor.hpp>
#include <Axle/Log.hpp>

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
        Shutdown();
        if (!imguiLayer) {
            AXLE_CORE_ERROR("RHIViewportDescriptor::Init failed: ImGuiLayer is null!");
            return false;
        }

        m_ImGuiLayer = imguiLayer;
        m_Desc = desc;
        m_OwnsColorTexture = true;

        m_OwnedColorTexture = std::make_unique<RHI::RHITexture>();
        auto colorDesc = RHI::RHITextureDesc::RenderTarget2D(desc.Width, desc.Height, desc.ColorFormat, desc.DebugName + "_Color");
        if (!m_OwnedColorTexture->Init(colorDesc)) {
            AXLE_CORE_ERROR("RHIViewportDescriptor::Init: Failed to create color texture!");
            Shutdown();
            return false;
        }
        m_ColorTexture = m_OwnedColorTexture.get();

        if (desc.EnableDepth) {
            m_DepthTexture = std::make_unique<RHI::RHITexture>();
            auto depthDesc = RHI::RHITextureDesc::DepthStencil2D(desc.Width, desc.Height, desc.DepthFormat, desc.DebugName + "_Depth");
            if (!m_DepthTexture->Init(depthDesc)) {
                AXLE_CORE_ERROR("RHIViewportDescriptor::Init: Failed to create depth texture!");
                Shutdown();
                return false;
            }
        }

        if (!RegisterWithImGui()) {
            AXLE_CORE_ERROR("RHIViewportDescriptor::Init: Failed to register texture with ImGuiLayer!");
            Shutdown();
            return false;
        }

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
        Shutdown();
        if (!imguiLayer || !texture || !texture->IsValid()) {
            AXLE_CORE_ERROR("RHIViewportDescriptor::BindExternalTexture failed: Invalid arguments!");
            return false;
        }

        m_ImGuiLayer = imguiLayer;
        m_ColorTexture = texture;
        m_OwnsColorTexture = false;
        m_Desc.Width = texture->GetWidth();
        m_Desc.Height = texture->GetHeight();
        m_Desc.ColorFormat = texture->GetFormat();
        m_Desc.EnableDepth = false;

        return RegisterWithImGui();
    }

    bool RHIViewportDescriptor::SetHDR(bool enableHDR) {
        RHI::RHITextureFormat targetFormat = enableHDR ? RHI::RHITextureFormat::RGBA16_FLOAT : RHI::RHITextureFormat::RGBA8_UNORM;
        return SetColorFormat(targetFormat);
    }

    bool RHIViewportDescriptor::SetColorFormat(RHI::RHITextureFormat newFormat) {
        if (m_Desc.ColorFormat == newFormat && IsValid()) {
            return true;
        }

        if (!m_OwnsColorTexture) {
            AXLE_CORE_WARN("RHIViewportDescriptor::SetColorFormat cannot modify externally-owned texture!");
            return false;
        }

        m_Desc.ColorFormat = newFormat;
        if (m_Desc.Width == 0 || m_Desc.Height == 0) {
            return true;
        }

        UnregisterFromImGui();

        auto colorDesc = RHI::RHITextureDesc::RenderTarget2D(m_Desc.Width, m_Desc.Height, m_Desc.ColorFormat, m_Desc.DebugName + "_Color");
        if (!m_OwnedColorTexture) {
            m_OwnedColorTexture = std::make_unique<RHI::RHITexture>();
        }
        if (!m_OwnedColorTexture->Init(colorDesc)) {
            AXLE_CORE_ERROR("RHIViewportDescriptor::SetColorFormat: Failed to recreate color texture!");
            Shutdown();
            return false;
        }
        m_ColorTexture = m_OwnedColorTexture.get();

        if (!RegisterWithImGui()) {
            AXLE_CORE_ERROR("RHIViewportDescriptor::SetColorFormat: Failed to register texture with ImGui!");
            Shutdown();
            return false;
        }

        return true;
    }

    bool RHIViewportDescriptor::SetDepthFormat(RHI::RHITextureFormat depthFormat) {
        if (m_Desc.DepthFormat == depthFormat && m_DepthTexture && m_DepthTexture->IsValid()) {
            return true;
        }

        m_Desc.DepthFormat = depthFormat;
        if (!m_Desc.EnableDepth || m_Desc.Width == 0 || m_Desc.Height == 0) {
            return true;
        }

        if (!m_DepthTexture) {
            m_DepthTexture = std::make_unique<RHI::RHITexture>();
        }
        auto depthDesc = RHI::RHITextureDesc::DepthStencil2D(m_Desc.Width, m_Desc.Height, m_Desc.DepthFormat, m_Desc.DebugName + "_Depth");
        if (!m_DepthTexture->Init(depthDesc)) {
            AXLE_CORE_ERROR("RHIViewportDescriptor::SetDepthFormat: Failed to recreate depth texture!");
            return false;
        }

        return true;
    }

    bool RHIViewportDescriptor::Resize(uint32_t newWidth, uint32_t newHeight) {
        if (newWidth == 0 || newHeight == 0) {
            return false;
        }

        if (newWidth == m_Desc.Width && newHeight == m_Desc.Height && IsValid()) {
            return true;
        }

        if (!m_OwnsColorTexture) {
            AXLE_CORE_WARN("RHIViewportDescriptor::Resize cannot resize externally-owned texture!");
            return false;
        }

        UnregisterFromImGui();

        m_Desc.Width = newWidth;
        m_Desc.Height = newHeight;

        auto colorDesc = RHI::RHITextureDesc::RenderTarget2D(newWidth, newHeight, m_Desc.ColorFormat, m_Desc.DebugName + "_Color");
        if (!m_OwnedColorTexture->Init(colorDesc)) {
            AXLE_CORE_ERROR("RHIViewportDescriptor::Resize: Failed to recreate color texture!");
            Shutdown();
            return false;
        }
        m_ColorTexture = m_OwnedColorTexture.get();

        if (m_Desc.EnableDepth) {
            if (!m_DepthTexture) {
                m_DepthTexture = std::make_unique<RHI::RHITexture>();
            }
            auto depthDesc = RHI::RHITextureDesc::DepthStencil2D(newWidth, newHeight, m_Desc.DepthFormat, m_Desc.DebugName + "_Depth");
            if (!m_DepthTexture->Init(depthDesc)) {
                AXLE_CORE_ERROR("RHIViewportDescriptor::Resize: Failed to recreate depth texture!");
                Shutdown();
                return false;
            }
        }

        if (!RegisterWithImGui()) {
            AXLE_CORE_ERROR("RHIViewportDescriptor::Resize: Failed to register resized texture with ImGuiLayer!");
            Shutdown();
            return false;
        }

        return true;
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
            m_ImTextureID = 1;
            return true;
        } else if (m_ImGuiLayer->GetBackendType() == ImGuiBackendType::Vulkan) {
            m_ImTextureID = 1;
            return true;
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
