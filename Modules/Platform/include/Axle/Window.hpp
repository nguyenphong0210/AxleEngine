#pragma once
#include <Axle/Types.hpp>
#include <string>

struct GLFWwindow;

namespace Axle {

    struct WindowProps {
        std::string Title = "Axle Engine";
        u32 Width = 1280;
        u32 Height = 720;
    };

    class Window {
    public:
        Window(const WindowProps& props = WindowProps());
        ~Window();

        void PollEvents();
        void SwapBuffers();
        bool ShouldClose() const;

        GLFWwindow* GetNativeWindow() const { return m_Window; }
        u32 GetWidth() const { return m_Props.Width; }
        u32 GetHeight() const { return m_Props.Height; }

    private:
        GLFWwindow* m_Window = nullptr;
        WindowProps m_Props;
    };

}