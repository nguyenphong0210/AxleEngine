#pragma once
#include <Axle/Types.hpp>
#include <Axle/Events.hpp>
#include <string>
#include <functional>

struct GLFWwindow;

namespace Axle {

    struct WindowProps {
        std::string Title = "Axle Engine";
        u32 Width = 1280;
        u32 Height = 720;
        bool VSync = true;
        bool Maximized = true;
    };

    class Window {
    public:
        // Generic callback interface for dispatching any event
        using EventCallbackFn = std::function<void(
            // Window
            const WindowResizeEvent*,
            const WindowCloseEvent*,
            // Key
            const KeyPressedEvent*,
            const KeyReleasedEvent*,
            // Mouse
            const MouseMovedEvent*,
            const MouseScrolledEvent*,
            const MouseButtonPressedEvent*,
            const MouseButtonReleasedEvent*
        )>;

        Window(const WindowProps& props = WindowProps());
        ~Window();

        void PollEvents();
        void SwapBuffers();
        bool ShouldClose() const;

        void SetVSync(bool enabled);
        bool IsVSync() const { return m_Data.VSync; }

        void Maximize();
        void Restore();
        bool IsMaximized() const;

        void SetTitle(const std::string& title);
        const std::string& GetTitle() const { return m_Data.Title; }

        u32 GetWidth() const { return m_Data.Width; }
        u32 GetHeight() const { return m_Data.Height; }
        GLFWwindow* GetNativeWindow() const { return m_Window; }

        static bool IsGLFWInitialized();
        static bool HasActiveContext();

        // Event callbacks
        void SetWindowResizeCallback(std::function<void(const WindowResizeEvent&)> callback) { m_Data.OnWindowResize = callback; }
        void SetWindowCloseCallback(std::function<void(const WindowCloseEvent&)> callback) { m_Data.OnWindowClose = callback; }
        void SetKeyPressedCallback(std::function<void(const KeyPressedEvent&)> callback) { m_Data.OnKeyPressed = callback; }
        void SetKeyReleasedCallback(std::function<void(const KeyReleasedEvent&)> callback) { m_Data.OnKeyReleased = callback; }
        void SetMouseMovedCallback(std::function<void(const MouseMovedEvent&)> callback) { m_Data.OnMouseMoved = callback; }
        void SetMouseScrolledCallback(std::function<void(const MouseScrolledEvent&)> callback) { m_Data.OnMouseScrolled = callback; }
        void SetMouseButtonPressedCallback(std::function<void(const MouseButtonPressedEvent&)> callback) { m_Data.OnMouseButtonPressed = callback; }
        void SetMouseButtonReleasedCallback(std::function<void(const MouseButtonReleasedEvent&)> callback) { m_Data.OnMouseButtonReleased = callback; }
        void SetWindowDropCallback(std::function<void(const std::vector<std::string>&)> callback) { m_Data.OnFileDrop = callback; }

    private:
        struct WindowData {
            std::string Title;
            u32 Width;
            u32 Height;
            bool VSync;

            std::function<void(const WindowResizeEvent&)> OnWindowResize;
            std::function<void(const WindowCloseEvent&)> OnWindowClose;
            std::function<void(const KeyPressedEvent&)> OnKeyPressed;
            std::function<void(const KeyReleasedEvent&)> OnKeyReleased;
            std::function<void(const MouseMovedEvent&)> OnMouseMoved;
            std::function<void(const MouseScrolledEvent&)> OnMouseScrolled;
            std::function<void(const MouseButtonPressedEvent&)> OnMouseButtonPressed;
            std::function<void(const MouseButtonReleasedEvent&)> OnMouseButtonReleased;
            std::function<void(const std::vector<std::string>&)> OnFileDrop;
        };

        GLFWwindow* m_Window = nullptr;
        WindowData m_Data;
    };

}