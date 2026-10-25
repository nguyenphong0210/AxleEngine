#pragma once
#include <Axle/Types.hpp>
#include <Axle/RHI/RHITexture.hpp>
#include <Axle/ImGuiLayer.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace Axle {

    struct RHIViewportDesc {
        uint32_t Width = 1280;
        uint32_t Height = 720;
        RHI::RHITextureFormat ColorFormat = RHI::RHITextureFormat::RGBA16_FLOAT;
        bool EnableDepth = true;
        RHI::RHITextureFormat DepthFormat = RHI::RHITextureFormat::D32_FLOAT;
        std::string DebugName = "EditorViewport";
    };

    class RHIViewportDescriptor {
    public:
        RHIViewportDescriptor() = default;
        ~RHIViewportDescriptor();

        RHIViewportDescriptor(const RHIViewportDescriptor&) = delete;
        RHIViewportDescriptor& operator=(const RHIViewportDescriptor&) = delete;

        RHIViewportDescriptor(RHIViewportDescriptor&& other) noexcept;
        RHIViewportDescriptor& operator=(RHIViewportDescriptor&& other) noexcept;

        // Initialize and allocate offscreen color (+ optional depth) textures and bind to ImGuiLayer
        bool Init(ImGuiLayer* imguiLayer, const RHIViewportDesc& desc);
        bool Init(ImGuiLayer* imguiLayer, uint32_t width, uint32_t height, 
                  RHI::RHITextureFormat colorFormat = RHI::RHITextureFormat::RGBA16_FLOAT, 
                  bool enableDepth = true);

        // Bind an externally-managed RHITexture to ImGuiLayer
        bool BindExternalTexture(ImGuiLayer* imguiLayer, RHI::RHITexture* texture);

        // Dynamic format and HDR toggling
        bool IsHDR() const { return m_Desc.ColorFormat == RHI::RHITextureFormat::RGBA16_FLOAT; }
        bool SetHDR(bool enableHDR);
        bool SetColorFormat(RHI::RHITextureFormat newFormat);
        bool SetDepthFormat(RHI::RHITextureFormat depthFormat);

        RHI::RHITextureFormat GetColorFormat() const { return m_Desc.ColorFormat; }
        RHI::RHITextureFormat GetDepthFormat() const { return m_Desc.DepthFormat; }
        nri::Format GetNRIColorFormat() const { return RHI::ToNRIFormat(m_Desc.ColorFormat); }
        nri::Format GetNRIDepthFormat() const { return RHI::ToNRIFormat(m_Desc.DepthFormat); }

        // Resize the offscreen viewport render targets and refresh ImGui descriptor binding
        bool Resize(uint32_t newWidth, uint32_t newHeight);

        void Shutdown();

        bool IsValid() const { return m_ColorTexture != nullptr && m_ImTextureID != 0; }
        uint64_t GetImTextureID() const { return m_ImTextureID; }

        uint32_t GetWidth() const { return m_Desc.Width; }
        uint32_t GetHeight() const { return m_Desc.Height; }
        float GetAspectRatio() const { return m_Desc.Height > 0 ? static_cast<float>(m_Desc.Width) / static_cast<float>(m_Desc.Height) : 1.0f; }

        RHI::RHITexture* GetColorTexture() const { return m_ColorTexture; }
        RHI::RHITexture* GetDepthTexture() const { return m_DepthTexture.get(); }
        const RHIViewportDesc& GetDesc() const { return m_Desc; }

        // Returns backend-specific UV coordinates for ImGui::Image
        // D3D12/Vulkan: UV0=(0, 0), UV1=(1, 1)
        // OpenGL:       UV0=(0, 1), UV1=(1, 0)
        glm::vec2 GetUV0() const;
        glm::vec2 GetUV1() const;
        void GetUVs(glm::vec2& outUV0, glm::vec2& outUV1) const;

    private:
        bool RegisterWithImGui();
        void UnregisterFromImGui();

    private:
        ImGuiLayer* m_ImGuiLayer = nullptr;
        RHIViewportDesc m_Desc;

        std::unique_ptr<RHI::RHITexture> m_OwnedColorTexture;
        std::unique_ptr<RHI::RHITexture> m_DepthTexture;

        RHI::RHITexture* m_ColorTexture = nullptr;
        bool m_OwnsColorTexture = true;

        uint64_t m_ImTextureID = 0;
    };

}
