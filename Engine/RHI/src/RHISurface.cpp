#include <Axle/RHI/RHISurface.hpp>
#include <Axle/Log.hpp>

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#elif defined(__linux__)
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#endif

#include <algorithm>

namespace Axle::RHI {

    RHISurface::~RHISurface() {
        Shutdown();
    }

    bool RHISurface::Init(GLFWwindow* window) {
        if (!window) {
            AXLE_CORE_ERROR("Cannot initialize RHISurface with null GLFWwindow handle!");
            return false;
        }

        m_WindowHandle = window;

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        m_Width = (width > 0) ? static_cast<uint32_t>(width) : 0;
        m_Height = (height > 0) ? static_cast<uint32_t>(height) : 0;

#if defined(_WIN32)
        m_NativeHandle = reinterpret_cast<void*>(glfwGetWin32Window(window));
        if (!m_NativeHandle) {
            AXLE_CORE_ERROR("Failed to extract Win32 HWND from GLFWwindow!");
            return false;
        }
        AXLE_CORE_INFO("RHISurface bound to Native Win32 Surface HWND: 0x{0:x} ({1}x{2})",
            reinterpret_cast<uintptr_t>(m_NativeHandle), m_Width, m_Height);
#elif defined(__linux__)
        m_NativeHandle = reinterpret_cast<void*>(glfwGetX11Window(window));
        AXLE_CORE_INFO("RHISurface bound to Native X11 Window: 0x{0:x} ({1}x{2})",
            reinterpret_cast<uintptr_t>(m_NativeHandle), m_Width, m_Height);
#else
        m_NativeHandle = reinterpret_cast<void*>(window);
#endif

        return IsValid();
    }

    bool RHISurface::Init(const Axle::Window& window) {
        return Init(window.GetNativeWindow());
    }

    bool RHISurface::Init(void* nativeHandle, uint32_t width, uint32_t height) {
        m_WindowHandle = nullptr;
        m_NativeHandle = nativeHandle;
        m_Width = width;
        m_Height = height;
        return IsValid();
    }

    void RHISurface::Resize(uint32_t width, uint32_t height) {
        m_Width = width;
        m_Height = height;
    }

    nri::Window RHISurface::GetNRIWindow() const {
        nri::Window window = {};
#if defined(_WIN32)
        window.windows.hwnd = m_NativeHandle;
#elif defined(__linux__)
        window.x11.window = reinterpret_cast<uint64_t>(m_NativeHandle);
#endif
        return window;
    }

    void RHISurface::Shutdown() {
        m_WindowHandle = nullptr;
        m_NativeHandle = nullptr;
        m_Width = 0;
        m_Height = 0;
    }

}
