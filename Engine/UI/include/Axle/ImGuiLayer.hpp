#pragma once
#include <Axle/Types.hpp>
#include <Axle/ApplicationUI.hpp>
#include <vector>

struct GLFWwindow;
struct ID3D12DescriptorHeap;
struct ID3D12GraphicsCommandList;

namespace Axle {

    namespace RHI {
        class RHICommandBuffer;
    }

    enum class ImGuiBackendType {
        OpenGL,
        Direct3D12,
        Vulkan
    };

    class ImGuiLayer : public ApplicationUIContext {
    public:
        explicit ImGuiLayer(ImGuiBackendType backend = ImGuiBackendType::OpenGL);
        ~ImGuiLayer() override;

        void Init(GLFWwindow* window) override;
        void Shutdown() override;

        void Begin() override;
        void End() override;

        // Render recorded ImGui draw data into a command buffer (for D3D12/Vulkan RHI backends)
        void Render(RHI::RHICommandBuffer* cmdBuffer);
        void Render(ID3D12GraphicsCommandList* cmdList);

        void SetDarkThemeColors();

        ImGuiBackendType GetBackendType() const { return m_BackendType; }
        bool IsInitialized() const { return m_Initialized; }

        // D3D12 Descriptor Heap management for textures & viewports
        ID3D12DescriptorHeap* GetD3D12DescriptorHeap() const { return m_D3D12SrvHeap; }
        uint32_t GetAllocatedDescriptorCount() const { return m_AllocatedDescriptorCount; }

        // Register / copy an external D3D12 SRV CPU descriptor into the ImGui heap, returning a GPU handle (ImTextureID)
        uint64_t RegisterTextureDescriptor(uint64_t cpuHandlePtr);
        void UnregisterTextureDescriptor(uint64_t gpuHandlePtr);

    private:
        void InitOpenGL();
        void InitD3D12();
        void InitVulkan();

        void ShutdownOpenGL();
        void ShutdownD3D12();
        void ShutdownVulkan();

        void AllocateD3D12Descriptor(void* outCpuHandle, void* outGpuHandle);
        void FreeD3D12Descriptor(void* cpuHandle, void* gpuHandle);

    private:
        ImGuiBackendType m_BackendType = ImGuiBackendType::OpenGL;
        bool m_Initialized = false;
        GLFWwindow* m_Window = nullptr;

        // D3D12 Descriptor Heap
        ID3D12DescriptorHeap* m_D3D12SrvHeap = nullptr;
        uint32_t m_D3D12DescriptorSize = 0;
        uint32_t m_AllocatedDescriptorCount = 0;
        std::vector<uint32_t> m_FreeDescriptorIndices;
    };

}
