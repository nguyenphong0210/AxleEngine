#include <Axle/RHI/RHISwapChain.hpp>
#include <Axle/Log.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <limits>

#include <algorithm>

namespace Axle::RHI {

    RHISwapChain::~RHISwapChain() {
        Shutdown();
    }

    bool RHISwapChain::Init(const RHISurface& surface, const RHISwapChainDesc& desc) {
        Shutdown();
        if (!desc.FramesInFlight || desc.FramesInFlight > 3 || desc.BufferCount < 2 || desc.BufferCount > 4 ||
            surface.GetWidth() > std::numeric_limits<nri::Dim_t>::max() || surface.GetHeight() > std::numeric_limits<nri::Dim_t>::max()) return false;

        if (!surface.IsValid()) {
            AXLE_CORE_ERROR("Failed to initialize RHISwapChain: provided RHISurface is invalid!");
            return false;
        }

        auto& device = RHIDevice::Get();
        if (!device.IsBackendSelected() ||
            (device.GetBackend() != RHIType::OpenGL && !device.CanCreateResources())) {
            AXLE_CORE_ERROR("Failed to initialize RHISwapChain: RHIDevice is not initialized!");
            return false;
        }

        m_Surface = &surface;
        m_Desc = desc;
        m_Desc.Width = surface.GetWidth();
        m_Desc.Height = surface.GetHeight();
        m_IsMinimized = (m_Desc.Width == 0 || m_Desc.Height == 0);

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
            DestroyNRIResources();
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
        scDesc.scaling = nri::Scaling::STRETCH;
        scDesc.queuedFrameNum = static_cast<uint8_t>(std::clamp(m_Desc.FramesInFlight, 1u, 3u));

        nri::Result res = ScopedRHIFault::Invoke("CreateSwapChain", [&] { return sc.CreateSwapChain(*nriDevice, scDesc, m_SwapChain); });
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
        const auto functions = core; const auto swapFunctions = sc;
        const auto frames = m_Frames; const auto ownedTextures = m_Textures;
        auto fence = m_FrameFence; auto swapchain = m_SwapChain;
        m_Lease = device.GetResources().Publish(RHIResourceKind::Surface, "Swapchain", {swapchain, fence},
            [functions, swapFunctions, frames, ownedTextures, fence, swapchain] {
                for (const auto& frame : frames) if (frame.CommandAllocator) functions.DestroyCommandAllocator(frame.CommandAllocator);
                for (const auto& texture : ownedTextures) {
                    if (texture.AcquireSemaphore) functions.DestroyFence(texture.AcquireSemaphore);
                    if (texture.ReleaseSemaphore) functions.DestroyFence(texture.ReleaseSemaphore);
                }
                if (fence) functions.DestroyFence(fence);
                if (swapchain) swapFunctions.DestroySwapChain(swapchain);
            });
        if (!m_Lease.IsLive()) return false;
        for (const auto& frame : frames) device.GetResources().AddAlias(m_Lease, frame.CommandAllocator);
        for (const auto& texture : ownedTextures) device.GetResources().AddAlias(m_Lease, texture.Texture);
        return true;
    }

    void RHISwapChain::DestroyNRIResources() {
        auto& device = RHIDevice::Get();
        if (m_Lease) {
            const bool current = m_Lease.IsLive();
            m_Lease.Reset();
            if (current) device.WaitIdle();
        } else if (device.IsNativeReady()) {
            device.WaitIdle();
            const auto core = device.GetNRICore();
            const auto sc = device.GetNRISwapChain();
            auto frames = std::move(m_Frames);
            auto textures = std::move(m_Textures);
            auto fence = m_FrameFence; auto swapchain = m_SwapChain;
            device.ReleaseNative([core, sc, frames = std::move(frames), textures = std::move(textures), fence, swapchain] {
                for (const auto& frame : frames) if (frame.CommandAllocator) core.DestroyCommandAllocator(frame.CommandAllocator);
                for (const auto& texture : textures) {
                    if (texture.AcquireSemaphore) core.DestroyFence(texture.AcquireSemaphore);
                    if (texture.ReleaseSemaphore) core.DestroyFence(texture.ReleaseSemaphore);
                }
                if (fence) core.DestroyFence(fence);
                if (swapchain) sc.DestroySwapChain(swapchain);
            });
        }
        m_Frames.clear(); m_Textures.clear();
        m_FrameFence = nullptr; m_SwapChain = nullptr;
        m_ActualTextureCount = 0; m_Acquired = false;
    }

    void RHISwapChain::Shutdown() {
        if (!m_IsLegacyOpenGL && (m_SwapChain || m_FrameFence || !m_Frames.empty())) DestroyNRIResources();
        m_Initialized = m_IsLegacyOpenGL = m_NeedsResize = m_IsMinimized = m_Acquired = false;
        m_Surface = nullptr; m_FrameCount = 0;
    }

    bool RHISwapChain::BeginFrame() {
        if (!IsValid() || m_IsMinimized || m_Acquired) return false;

        if (m_IsLegacyOpenGL) {
            m_CurrentTextureIndex = (m_CurrentTextureIndex + 1) % m_ActualTextureCount;
            m_FrameInFlightIndex = static_cast<uint32_t>(m_FrameCount % m_Desc.FramesInFlight);
            m_Acquired = true;
            return true;
        }

        if (m_ActualTextureCount == 0 || m_Textures.empty() || !m_SwapChain) {
            return false;
        }

        auto& device = RHIDevice::Get();
        const auto& core = device.GetNRICore();
        const auto& sc = device.GetNRISwapChain();

        if (!device.CanSubmit()) return false;
        m_FrameInFlightIndex = static_cast<uint32_t>(m_FrameCount % m_Desc.FramesInFlight);
        auto& frame = m_Frames[m_FrameInFlightIndex];
        if (!device.WaitForFence(m_FrameFence, frame.FenceValue)) return false;
        if (frame.CommandAllocator) core.ResetCommandAllocator(*frame.CommandAllocator);
        const uint32_t acquireIndex = static_cast<uint32_t>(m_FrameCount % m_ActualTextureCount);
        const auto result = ScopedRHIFault::Invoke("AcquireNextTexture", [&] {
            return sc.AcquireNextTexture(*m_SwapChain, *m_Textures[acquireIndex].AcquireSemaphore, m_CurrentTextureIndex);
        });
        if (result == nri::Result::OUT_OF_DATE) m_NeedsResize = true;
        if (result != nri::Result::SUCCESS) return false;
        if (m_CurrentTextureIndex >= m_Textures.size()) return device.ObserveResult(nri::Result::FAILURE, "AcquireNextTexture", "invalid backbuffer index");
        m_Acquired = true;
        return true;
    }

    bool RHISwapChain::PresentFrame(const nri::CommandBuffer* const* commandBuffers, uint32_t commandBufferNum) {
        if (!IsValid() || m_IsMinimized || !m_Acquired) return false;

        if (m_IsLegacyOpenGL) {
            m_FrameCount++; m_Acquired = false;
            return true;
        }

        if (m_ActualTextureCount == 0 || m_Textures.empty() || !m_SwapChain) {
            return false;
        }

        auto& device = RHIDevice::Get();
        const auto& core = device.GetNRICore();
        const auto& sc = device.GetNRISwapChain();
        nri::Queue* queue = device.GetGraphicsQueue();

        if (!device.CanSubmit() || (commandBufferNum && !commandBuffers)) return false;
        for (uint32_t i = 0; i < commandBufferNum; ++i) if (!commandBuffers[i]) return false;
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

        const auto submitted = ScopedRHIFault::Invoke("QueueSubmit", [&] { return core.QueueSubmit(*queue, submitDesc); });
        m_Acquired = false;
        if (submitted != nri::Result::SUCCESS) return false;
        m_Frames[m_FrameInFlightIndex].FenceValue = signalValue;
        m_FrameCount = signalValue;
        // Presentation cannot undo an accepted submission or its completion value.
        nri::Result res = ScopedRHIFault::Invoke("QueuePresent", [&] { return sc.QueuePresent(*m_SwapChain, *releaseSemaphore, signalValue); });
        if (res == nri::Result::OUT_OF_DATE) {
            m_NeedsResize = true;
        }

        return (res == nri::Result::SUCCESS);
    }

    bool RHISwapChain::Resize(uint32_t width, uint32_t height) {
        if (!m_Initialized) return false;

        if (width == 0 || height == 0) {
            m_IsMinimized = true;
            AXLE_CORE_WARN("RHISwapChain: Window minimized ({0}x{1}), rendering paused.", width, height);
            return true;
        }

        bool wasMinimized = m_IsMinimized;
        m_IsMinimized = false;

        if (wasMinimized) {
            AXLE_CORE_INFO("RHISwapChain: Window restored from minimized state to {0}x{1}.", width, height);
        }

        if (!wasMinimized && m_Desc.Width == width && m_Desc.Height == height && !m_NeedsResize) {
            return true;
        }

        m_Desc.Width = width;
        m_Desc.Height = height;

        if (m_IsLegacyOpenGL) {
            m_NeedsResize = false;
            return true;
        }

        return RecreateSwapChain();
    }

    void RHISwapChain::SetVSync(bool vsync) {
        if (m_Desc.VSync == vsync) {
            return;
        }

        m_Desc.VSync = vsync;
        AXLE_CORE_INFO("RHISwapChain: Setting VSync to {0}", vsync ? "ON (FIFO)" : "OFF (Mailbox / Low-Latency)");

        if (m_Initialized && !m_IsLegacyOpenGL && !m_IsMinimized) {
            RecreateSwapChain();
        }
    }

    bool RHISwapChain::RecreateSwapChain() {
        if (!m_Initialized || m_IsLegacyOpenGL) return true;
        auto& device = RHIDevice::Get();
        if (m_IsMinimized || !m_Desc.Width || !m_Desc.Height || !m_Surface || !device.CanCreateResources()) return false;
        if (m_Desc.Width > std::numeric_limits<nri::Dim_t>::max() || m_Desc.Height > std::numeric_limits<nri::Dim_t>::max() || !device.WaitIdle()) return false;
        DestroyNRIResources();
        if (!CreateNRIResources(*m_Surface)) {
            DestroyNRIResources(); m_NeedsResize = true;
            return false;
        }
        device.SurfaceRebuilt();
        return true;
    }

    nri::Texture* RHISwapChain::GetCurrentTexture() const {
        if (!IsValid()) return nullptr;
        if (m_CurrentTextureIndex < m_Textures.size()) {
            return m_Textures[m_CurrentTextureIndex].Texture;
        }
        return nullptr;
    }

    nri::Texture* RHISwapChain::GetTexture(uint32_t index) const {
        if (!IsValid()) return nullptr;
        if (index < m_Textures.size()) {
            return m_Textures[index].Texture;
        }
        return nullptr;
    }

    nri::CommandAllocator* RHISwapChain::GetCurrentCommandAllocator() const {
        if (!IsValid()) return nullptr;
        if (m_FrameInFlightIndex < m_Frames.size()) {
            return m_Frames[m_FrameInFlightIndex].CommandAllocator;
        }
        return nullptr;
    }

    nri::Fence* RHISwapChain::GetCurrentAcquireSemaphore() const {
        if (!IsValid()) return nullptr;
        if (!m_ActualTextureCount) return nullptr;
        uint32_t acquireIndex = static_cast<uint32_t>(m_FrameCount % m_ActualTextureCount);
        if (acquireIndex < m_Textures.size()) {
            return m_Textures[acquireIndex].AcquireSemaphore;
        }
        return nullptr;
    }

    nri::Fence* RHISwapChain::GetCurrentReleaseSemaphore() const {
        if (!IsValid()) return nullptr;
        if (m_CurrentTextureIndex < m_Textures.size()) {
            return m_Textures[m_CurrentTextureIndex].ReleaseSemaphore;
        }
        return nullptr;
    }

}
