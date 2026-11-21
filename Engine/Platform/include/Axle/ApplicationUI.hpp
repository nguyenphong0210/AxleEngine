#pragma once
#include <cstdint>
#include <functional>
#include <vector>

struct GLFWwindow;

namespace Axle {
    class Window;
    class ApplicationRenderContext {
    public:
        using FrameCaptureCallback = std::function<void(uint32_t width, uint32_t height, const std::vector<uint8_t>& rgba8)>;
        virtual ~ApplicationRenderContext() = default;
        virtual bool Present(const Window& window) = 0;
        virtual void RequestNextFrameCapture(FrameCaptureCallback) {}
        virtual void Shutdown() = 0;
    };

    class ApplicationUIContext {
    public:
        virtual ~ApplicationUIContext() = default;
        virtual void Init(GLFWwindow* window) = 0;
        virtual void Shutdown() = 0;
        virtual void Begin() = 0;
        virtual void End() = 0;
        virtual bool IsOperational() const { return true; }
    };

}
