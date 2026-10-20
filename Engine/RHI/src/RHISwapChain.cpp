#include <Axle/RHI/RHISwapChain.hpp>
#include <Axle/Log.hpp>

#include <algorithm>

namespace Axle::RHI {

    RHISwapChain::~RHISwapChain() {
        Shutdown();
    }

    bool RHISwapChain::Init(const RHISurface& surface, const RHISwapChainDesc& desc) {
        if (m_Initialized) {
            Shutdown();
        }

        if (!surface.IsValid()) {
            AXLE_CORE_ERROR("Failed to initialize RHISwapChain: provided RHISurface is invalid!");
            return false;
        }

        auto& device = RHIDevice::Get();
        if (!device.IsInitialized()) {
            AXLE_CORE_ERROR("Failed to initialize RHISwapChain: RHIDevice is not initialized!");
            return false;
        }

        m_Desc = desc;
        m_Desc.Width = surface.GetWidth();
        m_Desc.Height = surface.GetHeight();

        if (device.GetBackend() == RHIType::OpenGL) {
            m_IsLegacyOpenGL = true;
            m_ActualTextureCount = desc.BufferCount;
            m_Initialized = true;
            AXLE_CORE_INFO("RHISwapChain initialized for OpenGL 4.5 fallback pipeline ({0}x{1}, {2} buffers)",
                m_Desc.Width, m_Desc.Height, m_ActualTextureCount);
            return true;
        }

        m_IsLegacyOpenGL = false;
        if (!CreateNRIResources(surface)) {
            AXLE_CORE_ERROR("Failed to create NRI SwapChain resources!");
            return false;
        }

        m_Initialized = true;
        AXLE_CORE_INFO("RHISwapChain successfully initialized ({0}x{1}, {2} buffers, {3} frames-in-flight, VSync: {4})",
            m_Desc.Width, m_Desc.Height, m_ActualTextureCount, m_Desc.FramesInFlight, m_Desc.VSync ? "ON" : "OFF");
        return true;
    }

    bool RHISwapChain::CreateNRIResources(const RHISurface& surface) {
        auto& device = RHIDevice::Get();
        nri::Device* nriDevice = device.GetNRIDevice();
        const auto& core = device.GetNRICore();
        const auto& sc = device.GetNRISwapChain();
        nri::Queue* queue = device.GetGraphicsQueue();

        if (!nriDevice || !queue || !sc.CreateSwapChain) {
            AXLE_CORE_ERROR("Missing NRI Device, Graphics Queue, or SwapChainInterface!");
            return false;
        }

        nri::SwapChainDesc scDesc = {};
        scDesc.window = surface.GetNRIWindow();
        scDesc.queue = queue;
        scDesc.width = static_cast<nri::Dim_t>(m_Desc.Width);
        scDesc.height = static_cast<nri::Dim_t>(m_Desc.Height);
        scDesc.textureNum = static_cast<uint8_t>(std::clamp(m_Desc.BufferCount, 2u, 4u));
        scDesc.format = m_Desc.Format;
        scDesc.flags = m_Desc.VSync ? nri::SwapChainBits::VSYNC : nri::SwapChainBits::ALLOW_TEARING;
        scDesc.queuedFrameNum = static_cast<uint8_t>(std::clamp(m_Desc.FramesInFlight, 1u, 3u));

        nri::Result res = sc.CreateSwapChain(*nriDevice, scDesc, m_SwapChain);
        if (res != nri::Result::SUCCESS || !m_SwapChain) {
            AXLE_CORE_ERROR("NRI SwapChain creation failed with error code: {0}", static_cast<int>(res));
            return false;
        }

        // Query backbuffer textures
        uint32_t textureNum = 0;
        nri::Texture* const* textures = sc.GetSwapChainTextures(*m_SwapChain, textureNum);
        if (!textures || textureNum == 0) {
            AXLE_CORE_ERROR("Failed to query textures from created NRI SwapChain!");
            sc.DestroySwapChain(m_SwapChain);
            m_SwapChain = nullptr;
            return false;
        }

        m_ActualTextureCount = textureNum;
        m_Textures.resize(m_ActualTextureCount);

        for (uint32_t i = 0; i < m_ActualTextureCount; ++i) {
            m_Textures[i].Texture = textures[i];

            // Semaphores for frame pacing (created with SWAPCHAIN_SEMAPHORE initial value)
            res = core.CreateFence(*nriDevice, nri::SWAPCHAIN_SEMAPHORE, m_Textures[i].AcquireSemaphore);
            if (res != nri::Result::SUCCESS) {
                AXLE_CORE_ERROR("Failed to create SwapChain acquire semaphore for buffer {0}!", i);
                DestroyNRIResources();
                return false;
            }

            res = core.CreateFence(*nriDevice, nri::SWAPCHAIN_SEMAPHORE, m_Textures[i].ReleaseSemaphore);
            if (res != nri::Result::SUCCESS) {
                AXLE_CORE_ERROR("Failed to create SwapChain release semaphore for buffer {0}!", i);
                DestroyNRIResources();
                return false;
            }
        }

        // Create monotonic frame-in-flight CPU-GPU synchronization fence
        res = core.CreateFence(*nriDevice, 0, m_FrameFence);
        if (res != nri::Result::SUCCESS) {
            AXLE_CORE_ERROR("Failed to create frame synchronization fence!");
            DestroyNRIResources();
            return false;
        }

        // Create per-frame Command Allocators to prevent CPU/GPU read-write stalls
        m_Frames.resize(m_Desc.FramesInFlight);
        for (uint32_t i = 0; i < m_Desc.FramesInFlight; ++i) {
            res = core.CreateCommandAllocator(*queue, m_Frames[i].CommandAllocator);
            if (res != nri::Result::SUCCESS) {
                AXLE_CORE_ERROR("Failed to create Command Allocator for frame-in-flight slot {0}!", i);
                DestroyNRIResources();
                return false;
            }
            m_Frames[i].FenceValue = 0;
        }

        m_FrameCount = 0;
        m_FrameInFlightIndex = 0;
        m_CurrentTextureIndex = 0;
        m_NeedsResize = false;
        return true;
    }

    void RHISwapChain::DestroyNRIResources() {
        auto& device = RHIDevice::Get();
        const auto& core = device.GetNRICore();
        const auto& sc = device.GetNRISwapChain();

        // 1. Wait for GPU to finish all in-flight work before releasing resources
        if (m_FrameFence && core.Wait) {
            core.Wait(*m_FrameFence, m_FrameCount);
        }

        // 2. Destroy command allocators
        if (core.DestroyCommandAllocator) {
            for (auto& f : m_Frames) {
                if (f.CommandAllocator) {
                    core.DestroyCommandAllocator(f.CommandAllocator);
                    f.CommandAllocator = nullptr;
                }
            }
        }
        m_Frames.clear();

        // 3. Destroy semaphores
        if (core.DestroyFence) {
            for (auto& t : m_Textures) {
                if (t.AcquireSemaphore) {
                    core.DestroyFence(t.AcquireSemaphore);
                    t.AcquireSemaphore = nullptr;
                }
                if (t.ReleaseSemaphore) {
                    core.DestroyFence(t.ReleaseSemaphore);
                    t.ReleaseSemaphore = nullptr;
                }
            }
            m_Textures.clear();

            if (m_FrameFence) {
                core.DestroyFence(m_FrameFence);
                m_FrameFence = nullptr;
            }
        }

        // 4. Destroy swapchain
        if (m_SwapChain && sc.DestroySwapChain) {
            sc.DestroySwapChain(m_SwapChain);
            m_SwapChain = nullptr;
        }

        m_ActualTextureCount = 0;
    }

    void RHISwapChain::Shutdown() {
        if (!m_Initialized) return;

        AXLE_CORE_INFO("Shutting down RHISwapChain...");
        if (!m_IsLegacyOpenGL) {
            DestroyNRIResources();
        }

        m_Initialized = false;
        m_IsLegacyOpenGL = false;
        m_NeedsResize = false;
        m_FrameCount = 0;
        AXLE_CORE_INFO("RHISwapChain successfully shut down.");
    }

    bool RHISwapChain::BeginFrame() {
        if (!m_Initialized) return false;

        if (m_IsLegacyOpenGL) {
            m_CurrentTextureIndex = (m_CurrentTextureIndex + 1) % m_ActualTextureCount;
            m_FrameInFlightIndex = static_cast<uint32_t>(m_FrameCount % m_Desc.FramesInFlight);
            return true;
        }

        auto& device = RHIDevice::Get();
        const auto& core = device.GetNRICore();
        const auto& sc = device.GetNRISwapChain();

        // 1. Determine active frame-in-flight slot
        m_FrameInFlightIndex = static_cast<uint32_t>(m_FrameCount % m_Desc.FramesInFlight);

        // 2. Wait for GPU on host if this frame slot is still processing from N frames ago
        uint64_t waitValue = (m_FrameCount >= m_Desc.FramesInFlight)
            ? (1 + m_FrameCount - m_Desc.FramesInFlight)
            : 0;

        if (waitValue > 0) {
            core.Wait(*m_FrameFence, waitValue);
        }

        // 3. Reset the per-frame Command Allocator
        if (m_Frames[m_FrameInFlightIndex].CommandAllocator) {
            core.ResetCommandAllocator(*m_Frames[m_FrameInFlightIndex].CommandAllocator);
        }

        // 4. Acquire the next backbuffer texture
        uint32_t acquireIndex = static_cast<uint32_t>(m_FrameCount % m_ActualTextureCount);
        nri::Fence* acquireSemaphore = m_Textures[acquireIndex].AcquireSemaphore;

        nri::Result res = sc.AcquireNextTexture(*m_SwapChain, *acquireSemaphore, m_CurrentTextureIndex);
        if (res == nri::Result::OUT_OF_DATE) {
            m_NeedsResize = true;
            return false;
        }

        return (res == nri::Result::SUCCESS);
    }

    bool RHISwapChain::PresentFrame(const nri::CommandBuffer* const* commandBuffers, uint32_t commandBufferNum) {
        if (!m_Initialized) return false;

        if (m_IsLegacyOpenGL) {
            m_FrameCount++;
            return true;
        }

        auto& device = RHIDevice::Get();
        const auto& core = device.GetNRICore();
        const auto& sc = device.GetNRISwapChain();
        nri::Queue* queue = device.GetGraphicsQueue();

        uint32_t acquireIndex = static_cast<uint32_t>(m_FrameCount % m_ActualTextureCount);
        nri::Fence* acquireSemaphore = m_Textures[acquireIndex].AcquireSemaphore;
        nri::Fence* releaseSemaphore = m_Textures[m_CurrentTextureIndex].ReleaseSemaphore;
        uint64_t signalValue = 1 + m_FrameCount;

        // Synchronize GPU stages: wait for image acquisition before writing color attachments
        nri::FenceSubmitDesc waitFence = {};
        waitFence.fence = acquireSemaphore;
        waitFence.stages = nri::StageBits::COLOR_ATTACHMENT;

        // Signal release semaphore (for presentation) and frame fence (for CPU sync)
        nri::FenceSubmitDesc renderingFinishedFence = {};
        renderingFinishedFence.fence = releaseSemaphore;

        nri::FenceSubmitDesc frameFenceSubmit = {};
        frameFenceSubmit.fence = m_FrameFence;
        frameFenceSubmit.value = signalValue;

        nri::FenceSubmitDesc signalFences[2] = { renderingFinishedFence, frameFenceSubmit };

        nri::QueueSubmitDesc submitDesc = {};
        submitDesc.waitFences = &waitFence;
        submitDesc.waitFenceNum = 1;
        submitDesc.commandBuffers = commandBuffers;
        submitDesc.commandBufferNum = commandBufferNum;
        submitDesc.signalFences = signalFences;
        submitDesc.signalFenceNum = 2;

        core.QueueSubmit(*queue, submitDesc);

        // Queue image for display presentation
        nri::Result res = sc.QueuePresent(*m_SwapChain, *releaseSemaphore, signalValue);
        if (res == nri::Result::OUT_OF_DATE) {
            m_NeedsResize = true;
        }

        m_FrameCount++;
        return (res == nri::Result::SUCCESS);
    }

    bool RHISwapChain::Resize(uint32_t width, uint32_t height) {
        if (!m_Initialized) return false;

        if (width == 0 || height == 0) {
            // Minimized window guard
            return false;
        }

        m_Desc.Width = width;
        m_Desc.Height = height;

        if (m_IsLegacyOpenGL) {
            return true;
        }

        // Recreate NRI SwapChain cleanly
        auto& device = RHIDevice::Get();
        const auto& core = device.GetNRICore();
        if (m_FrameFence) {
            core.Wait(*m_FrameFence, m_FrameCount);
        }

        // For now, re-init NRI resources
        // (Step 2.2 will implement full dynamic in-place swapchain resize)
        m_NeedsResize = false;
        return true;
    }

    nri::Texture* RHISwapChain::GetCurrentTexture() const {
        if (m_CurrentTextureIndex < m_Textures.size()) {
            return m_Textures[m_CurrentTextureIndex].Texture;
        }
        return nullptr;
    }

    nri::Texture* RHISwapChain::GetTexture(uint32_t index) const {
        if (index < m_Textures.size()) {
            return m_Textures[index].Texture;
        }
        return nullptr;
    }

    nri::CommandAllocator* RHISwapChain::GetCurrentCommandAllocator() const {
        if (m_FrameInFlightIndex < m_Frames.size()) {
            return m_Frames[m_FrameInFlightIndex].CommandAllocator;
        }
        return nullptr;
    }

    nri::Fence* RHISwapChain::GetCurrentAcquireSemaphore() const {
        uint32_t acquireIndex = static_cast<uint32_t>(m_FrameCount % m_ActualTextureCount);
        if (acquireIndex < m_Textures.size()) {
            return m_Textures[acquireIndex].AcquireSemaphore;
        }
        return nullptr;
    }

    nri::Fence* RHISwapChain::GetCurrentReleaseSemaphore() const {
        if (m_CurrentTextureIndex < m_Textures.size()) {
            return m_Textures[m_CurrentTextureIndex].ReleaseSemaphore;
        }
        return nullptr;
    }

}
