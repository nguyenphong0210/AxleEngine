#pragma once
#include <Axle/RHI/RHICommon.hpp>
#include <Axle/Window.hpp>

#include <NRI.h>
#include <Extensions/NRISwapChain.h>

struct GLFWwindow;

namespace Axle::RHI {

    class RHISurface {
    public:
        RHISurface() = default;
        ~RHISurface();

        // Prevent copying
        RHISurface(const RHISurface&) = delete;
        RHISurface& operator=(const RHISurface&) = delete;

        // Initialize surface from GLFW window
        bool Init(GLFWwindow* window);

        // Initialize surface from Axle Window wrapper
        bool Init(const Axle::Window& window);

        // Initialize surface directly from native OS handle (e.g. HWND)
        bool Init(void* nativeHandle, uint32_t width, uint32_t height);

        // Release surface
        void Shutdown();

        bool IsValid() const { return m_NativeHandle != nullptr && m_Width > 0 && m_Height > 0; }
        void* GetNativeHandle() const { return m_NativeHandle; }
        GLFWwindow* GetGLFWWindow() const { return m_WindowHandle; }
        uint32_t GetWidth() const { return m_Width; }
        uint32_t GetHeight() const { return m_Height; }

        // Update dimensions on resize
        void Resize(uint32_t width, uint32_t height);

        // Build NRI Window descriptor for SwapChain creation
        nri::Window GetNRIWindow() const;

    private:
        GLFWwindow* m_WindowHandle = nullptr;
        void* m_NativeHandle = nullptr;
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
    };

}
