#include <Axle/Window.hpp>
#include <GLFW/glfw3.h>
#include <iostream>

namespace Axle {

    Window::Window(const WindowProps& props) : m_Props(props) {
        if (!glfwInit()) {
            std::cerr << "Error: Failed to initialize GLFW!\n";
            return;
        }

        m_Window = glfwCreateWindow(m_Props.Width, m_Props.Height, m_Props.Title.c_str(), nullptr, nullptr);
        if (!m_Window) {
            std::cerr << "Error: Failed to create GLFW window!\n";
            glfwTerminate();
            return;
        }

        glfwMakeContextCurrent(m_Window);
        glfwSwapInterval(1); // V-Sync 60 FPS
    }

    Window::~Window() {
        if (m_Window) {
            glfwDestroyWindow(m_Window);
        }
        glfwTerminate();
    }

    void Window::PollEvents() {
        glfwPollEvents();
    }

    void Window::SwapBuffers() {
        glfwSwapBuffers(m_Window);
    }

    bool Window::ShouldClose() const {
        return glfwWindowShouldClose(m_Window);
    }

}