#pragma once
#include <Axle/RHI/RHICommon.hpp>
#include <Axle/RHI/RHIDevice.hpp>
#include <Axle/RHI/RHISurface.hpp>

#include <vector>
#include <memory>

namespace Axle::RHI {

    struct RHISwapChainDesc {
        uint32_t Width = 1280;
        uint32_t Height = 720;
        uint32_t BufferCount = 3;       // 2 = Double Buffering, 3 = Triple Buffering
        uint32_t FramesInFlight = 2;    // 2 or 3 frames-in-flight
        bool VSync = true;              // true = VSync cap, false = Mailbox / low-latency
        nri::SwapChainFormat Format = nri::SwapChainFormat::BT709_G22_8BIT; // Standard sRGB RGBA8
    };

    struct SwapChainTextureEntry {
        nri::Texture* Texture = nullptr;
        nri::Fence* AcquireSemaphore = nullptr;
        nri::Fence* ReleaseSemaphore = nullptr;
    };

    struct FrameInFlight {
        nri::CommandAllocator* CommandAllocator = nullptr;
        uint64_t FenceValue = 0;
    };

    class RHISwapChain {
    public:
        RHISwapChain() = default;
        ~RHISwapChain();

        RHISwapChain(const RHISwapChain&) = delete;
        RHISwapChain& operator=(const RHISwapChain&) = delete;

        // Initialize SwapChain with surface and descriptor
        bool Init(const RHISurface& surface, const RHISwapChainDesc& desc = RHISwapChainDesc());

        // Graceful shutdown & destruction of swapchain, fences, command allocators
        void Shutdown();

        // Prepare frame for rendering: waits for GPU fence for current frame in flight,
        // resets its command allocator, and acquires the next swapchain backbuffer texture
        bool BeginFrame();

        // Submit recorded command buffers (optional) and present the current frame
        bool PresentFrame(const nri::CommandBuffer* const* commandBuffers = nullptr, uint32_t commandBufferNum = 0);

        // Resize the swapchain
        bool Resize(uint32_t width, uint32_t height);

        // Reconfigure presentation mode (VSync vs Low-Latency Mailbox)
        void SetVSync(bool vsync);

        bool IsValid() const { return m_Initialized && (m_SwapChain != nullptr || m_IsLegacyOpenGL); }
        bool NeedsResize() const { return m_NeedsResize; }
        bool IsMinimized() const { return m_IsMinimized; }

        uint32_t GetWidth() const { return m_Desc.Width; }
        uint32_t GetHeight() const { return m_Desc.Height; }
        uint32_t GetBufferCount() const { return m_ActualTextureCount; }
        uint32_t GetFramesInFlight() const { return m_Desc.FramesInFlight; }
        bool IsVSync() const { return m_Desc.VSync; }

        uint32_t GetCurrentTextureIndex() const { return m_CurrentTextureIndex; }
        uint32_t GetCurrentFrameInFlightIndex() const { return m_FrameInFlightIndex; }
        uint64_t GetFrameCount() const { return m_FrameCount; }

        nri::Texture* GetCurrentTexture() const;
        nri::Texture* GetTexture(uint32_t index) const;
        nri::CommandAllocator* GetCurrentCommandAllocator() const;

        nri::Fence* GetCurrentAcquireSemaphore() const;
        nri::Fence* GetCurrentReleaseSemaphore() const;
        nri::Fence* GetFrameFence() const { return m_FrameFence; }

        nri::SwapChain* GetNRISwapChain() const { return m_SwapChain; }

    private:
        bool CreateNRIResources(const RHISurface& surface);
        void DestroyNRIResources();
        bool RecreateSwapChain();

    private:
        bool m_Initialized = false;
        bool m_IsLegacyOpenGL = false;
        bool m_NeedsResize = false;
        bool m_IsMinimized = false;

        const RHISurface* m_Surface = nullptr;
        RHISwapChainDesc m_Desc;
        nri::SwapChain* m_SwapChain = nullptr;
        nri::Fence* m_FrameFence = nullptr;

        std::vector<SwapChainTextureEntry> m_Textures;
        uint32_t m_ActualTextureCount = 0;
        uint32_t m_CurrentTextureIndex = 0;

        std::vector<FrameInFlight> m_Frames;
        uint32_t m_FrameInFlightIndex = 0;
        uint64_t m_FrameCount = 0;
    };

}
