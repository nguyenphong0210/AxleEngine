#pragma once
#include <Axle/RHI/RHISwapChain.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace Axle::RHI {
    class RHICommandBuffer;
    enum class RHIPresentOutcome { Presented, Skipped, RebuildRequired, Failed };
    // Application composition owns this object. UI and views only record through
    // callbacks; they never acquire, submit, or present the window themselves.
    class RHIPresentationContext {
    public:
        using Recorder = std::function<bool(RHICommandBuffer&, nri::Format)>;
        using FrameCaptureCallback = std::function<void(uint32_t width, uint32_t height, const std::vector<uint8_t>& rgba8)>;
        RHIPresentationContext();
        ~RHIPresentationContext();
        bool Init(GLFWwindow* window, const RHISwapChainDesc& desc = {});
        bool Shutdown();
        void RequestNextFrameCapture(FrameCaptureCallback callback);
        RHIPresentOutcome RenderFrame(uint32_t width, uint32_t height, bool vsync,
            const Recorder& prepare = {}, const Recorder& draw = {}, nri::Texture* blitSource = nullptr);
        uint64_t GetPresentCount() const;
        bool IsVSync() const;
    private:
        struct Storage;
        std::unique_ptr<Storage> m_Storage;
    };
}
