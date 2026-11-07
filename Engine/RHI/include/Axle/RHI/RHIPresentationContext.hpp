#pragma once
#include <Axle/RHI/RHISwapChain.hpp>
#include <functional>
#include <memory>

namespace Axle::RHI {
    class RHICommandBuffer;
    enum class RHIPresentOutcome { Presented, Skipped, RebuildRequired, Failed };
    // Application composition owns this object. UI and views only record through
    // callbacks; they never acquire, submit, or present the window themselves.
    class RHIPresentationContext {
    public:
        using Recorder = std::function<bool(RHICommandBuffer&, nri::Format)>;
        RHIPresentationContext();
        ~RHIPresentationContext();
        bool Init(GLFWwindow* window, const RHISwapChainDesc& desc = {});
        bool Shutdown();
        RHIPresentOutcome RenderFrame(uint32_t width, uint32_t height, bool vsync,
            const Recorder& prepare = {}, const Recorder& draw = {});
        uint64_t GetPresentCount() const;
        bool IsVSync() const;
    private:
        struct Storage;
        std::unique_ptr<Storage> m_Storage;
    };
}
