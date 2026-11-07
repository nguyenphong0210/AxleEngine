#pragma once
#include <Axle/Types.hpp>
#include <Axle/ApplicationUI.hpp>
#include <Axle/Window.hpp>
#include <Axle/GuiTextureLease.hpp>
#include <Axle/RHI/RHIResource.hpp>
#include <vector>
#include <functional>
#include <memory>

struct GLFWwindow;
struct ID3D12DescriptorHeap;
struct ID3D12GraphicsCommandList;
struct ImFont;

namespace Axle {
    struct ImGuiDescriptorCallbackState;
    struct ImGuiDescriptorArena;
    struct GuiFrameState;
    struct ImGuiBackendRetirement;

    namespace RHI {
        class RHICommandBuffer;
        class RHITexture;
    }

    enum class ImGuiBackendType {
        OpenGL,
        Direct3D12,
        Vulkan
    };

    inline ImGuiBackendType ToImGuiBackend(GraphicsAPI api) {
        switch (api) {
            case GraphicsAPI::Direct3D12: return ImGuiBackendType::Direct3D12;
            case GraphicsAPI::Vulkan:     return ImGuiBackendType::Vulkan;
            case GraphicsAPI::OpenGL:     return ImGuiBackendType::OpenGL;
            default:                      return ImGuiBackendType::OpenGL;
        }
    }

    class ImGuiLayer : public ApplicationUIContext {
    public:
        explicit ImGuiLayer(ImGuiBackendType backend = ImGuiBackendType::OpenGL);
        ~ImGuiLayer() override;

        void Init(GLFWwindow* window) override;
        void Shutdown() override;

        void Begin() override;
        void End() override;
        bool IsOperational() const override;

        // Render recorded ImGui draw data into a command buffer (for D3D12/Vulkan RHI backends)
        bool Render(RHI::RHICommandBuffer* cmdBuffer, nri::Format colorFormat = nri::Format::RGBA8_UNORM);
        std::unique_ptr<ApplicationRenderContext> CreateRenderContext(const Window& window);
        void Render(ID3D12GraphicsCommandList* cmdList);

        void SetDarkThemeColors();

        // Modern typography font accessors (Unity Style)
        static ::ImFont* GetRegularFont() { return s_RegularFont; }
        static ::ImFont* GetBoldFont()    { return s_BoldFont; }
        static ::ImFont* GetHeaderFont()  { return s_HeaderFont; }
        static ::ImFont* GetCodeFont()    { return s_CodeFont; }

        ImGuiBackendType GetBackendType() const { return m_BackendType; }
        bool IsInitialized() const { return m_Initialized; }

        // D3D12 Descriptor Heap management for textures & viewports
        ID3D12DescriptorHeap* GetD3D12DescriptorHeap() const { return m_D3D12SrvHeap; }
        uint32_t GetAllocatedDescriptorCount() const;
        GuiTextureLease RegisterTexture(const RHI::RHITexture& texture);
        GuiTextureRegistrar GetTextureRegistrar() const;
        GuiDeferredReleaser GetDeferredReleaser() const;

        // Register / copy an external D3D12 SRV CPU descriptor or Vulkan ImageView into the ImGui heap/pool, returning a GPU handle (ImTextureID)
        uint64_t RegisterTextureDescriptor(uint64_t cpuHandlePtr);
        void UnregisterTextureDescriptor(uint64_t gpuHandlePtr);

        // Retain textures/descriptors referenced by this frame's draw lists until
        // End has submitted all UI draws and the GPU has finished using them.
        void DeferRelease(std::function<void()> release);

    private:
        static ::ImFont* s_RegularFont;
        static ::ImFont* s_BoldFont;
        static ::ImFont* s_HeaderFont;
        static ::ImFont* s_CodeFont;
        void InitOpenGL();
        void InitD3D12();
        void InitVulkan();

        void ShutdownOpenGL();
        void ShutdownD3D12();
        void ShutdownVulkan();

        void AllocateD3D12Descriptor(void* outCpuHandle, void* outGpuHandle);
        void FreeD3D12Descriptor(void* cpuHandle, void* gpuHandle);
        void FlushDeferredReleases();
        void FinishGuiFrame() noexcept;
        bool PrepareGuiTextures(RHI::RHICommandBuffer* commands = nullptr);

    private:
        ImGuiBackendType m_BackendType = ImGuiBackendType::OpenGL;
        bool m_Initialized = false;
        bool m_ContextCreated = false;
        GLFWwindow* m_Window = nullptr;
        std::vector<std::function<void()>> m_DeferredReleases;
        std::shared_ptr<ImGuiDescriptorCallbackState> m_DescriptorCallbacks;
        std::shared_ptr<ImGuiDescriptorArena> m_DescriptorArena;
        std::shared_ptr<GuiFrameState> m_GuiFrame;
        std::shared_ptr<ImGuiBackendRetirement> m_BackendRetirement;
        RHI::RHIResourceLease m_BackendLease;

        // D3D12 Descriptor Heap
        static constexpr uint32_t D3D12DescriptorCapacity = 1024;
        ID3D12DescriptorHeap* m_D3D12SrvHeap = nullptr;
        uint32_t m_D3D12DescriptorSize = 0;
        uint32_t m_AllocatedDescriptorCount = 0;
        std::vector<uint32_t> m_FreeDescriptorIndices;

        bool m_ComposedPresentation = false;
        uint32_t m_VulkanColorFormat = 0;
    };

}
