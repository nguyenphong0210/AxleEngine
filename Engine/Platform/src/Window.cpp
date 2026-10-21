#include <Axle/Window.hpp>
#include <Axle/Input.hpp>
#include <Axle/Log.hpp>
#include <GLFW/glfw3.h>

namespace Axle {

    static u32 s_GLFWWindowCount = 0;
    static bool s_GLFWInitialized = false;

    static void GLFWErrorCallback(int error, const char* description) {
        AXLE_CORE_ERROR("[GLFW Error] ({0}): {1}", error, description);
    }

    bool Window::IsGLFWInitialized() {
        return s_GLFWInitialized;
    }

    bool Window::HasActiveContext() {
        return s_GLFWInitialized && (glfwGetCurrentContext() != nullptr);
    }

    Window::Window(const WindowProps& props) {
        m_Data.Title = props.Title;
        m_Data.Width = props.Width;
        m_Data.Height = props.Height;
        m_Data.VSync = props.VSync;
        m_Data.GraphicsBackend = props.GraphicsBackend;

        if (s_GLFWWindowCount == 0) {
            glfwSetErrorCallback(GLFWErrorCallback);
            if (!glfwInit()) {
                AXLE_CORE_CRITICAL("Failed to initialize GLFW!");
                return;
            }
            s_GLFWInitialized = true;
        }

        if (props.GraphicsBackend == GraphicsAPI::OpenGL) {
            // OpenGL context hints (Core Profile 4.5)
            glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        } else {
            // Direct3D 12 / Vulkan / NRI: do not create an OpenGL context
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        }

        if (props.Maximized) {
            glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
        }

        m_Window = glfwCreateWindow(
            static_cast<int>(m_Data.Width),
            static_cast<int>(m_Data.Height),
            m_Data.Title.c_str(),
            nullptr,
            nullptr
        );

        if (!m_Window) {
            AXLE_CORE_CRITICAL("Failed to create GLFW window '{0}'!", m_Data.Title);
            glfwTerminate();
            return;
        }

        int actualWidth = 0, actualHeight = 0;
        glfwGetFramebufferSize(m_Window, &actualWidth, &actualHeight);
        if (actualWidth > 0 && actualHeight > 0) {
            m_Data.Width = static_cast<u32>(actualWidth);
            m_Data.Height = static_cast<u32>(actualHeight);
        }

        AXLE_CORE_INFO("Created window '{0}' ({1}x{2}) (Maximized: {3})", 
                       m_Data.Title, m_Data.Width, m_Data.Height, props.Maximized ? "Yes" : "No");

        ++s_GLFWWindowCount;

        if (m_Data.GraphicsBackend == GraphicsAPI::OpenGL) {
            glfwMakeContextCurrent(m_Window);
            SetVSync(m_Data.VSync);
        }

        // Bind WindowData pointer to GLFW window user pointer for callbacks
        glfwSetWindowUserPointer(m_Window, &m_Data);

        // Bind Input context
        Input::SetContext(m_Window);

        // ----------------- GLFW Callbacks Setup -----------------

        // Window size callback
        glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int width, int height) {
            auto* data = static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            data->Width = static_cast<u32>(width);
            data->Height = static_cast<u32>(height);

            if (data->OnWindowResize) {
                data->OnWindowResize({ data->Width, data->Height });
            }
        });

        // Window close callback
        glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window) {
            auto* data = static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            if (data->OnWindowClose) {
                data->OnWindowClose({});
            }
        });

        // Key callback
        glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int scancode, int action, int mods) {
            (void)scancode;
            (void)mods;
            auto* data = static_cast<WindowData*>(glfwGetWindowUserPointer(window));

            switch (action) {
                case GLFW_PRESS: {
                    if (data->OnKeyPressed) data->OnKeyPressed({ static_cast<KeyCode>(key), false });
                    break;
                }
                case GLFW_RELEASE: {
                    if (data->OnKeyReleased) data->OnKeyReleased({ static_cast<KeyCode>(key) });
                    break;
                }
                case GLFW_REPEAT: {
                    if (data->OnKeyPressed) data->OnKeyPressed({ static_cast<KeyCode>(key), true });
                    break;
                }
                default: break;
            }
        });

        // Mouse button callback
        glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int mods) {
            (void)mods;
            auto* data = static_cast<WindowData*>(glfwGetWindowUserPointer(window));

            switch (action) {
                case GLFW_PRESS: {
                    if (data->OnMouseButtonPressed) data->OnMouseButtonPressed({ static_cast<MouseCode>(button) });
                    break;
                }
                case GLFW_RELEASE: {
                    if (data->OnMouseButtonReleased) data->OnMouseButtonReleased({ static_cast<MouseCode>(button) });
                    break;
                }
                default: break;
            }
        });

        // Mouse scroll callback
        glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double xOffset, double yOffset) {
            Input::SetMouseScrollY(Input::GetMouseScrollY() + static_cast<f32>(yOffset));
            auto* data = static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            if (data->OnMouseScrolled) {
                data->OnMouseScrolled({ static_cast<f32>(xOffset), static_cast<f32>(yOffset) });
            }
        });

        // Cursor position callback
        glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double xPos, double yPos) {
            auto* data = static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            if (data->OnMouseMoved) {
                data->OnMouseMoved({ static_cast<f32>(xPos), static_cast<f32>(yPos) });
            }
        });

        // Window drag-and-drop callback from OS
        glfwSetDropCallback(m_Window, [](GLFWwindow* window, int count, const char** paths) {
            auto* data = static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            if (data && data->OnFileDrop) {
                std::vector<std::string> droppedPaths;
                droppedPaths.reserve(count);
                for (int i = 0; i < count; ++i) {
                    droppedPaths.emplace_back(paths[i]);
                }
                data->OnFileDrop(droppedPaths);
            }
        });
    }

    Window::~Window() {
        if (m_Window) {
            glfwDestroyWindow(m_Window);
            --s_GLFWWindowCount;
        }

        if (s_GLFWWindowCount == 0) {
            glfwSetErrorCallback(nullptr);
            glfwTerminate();
            s_GLFWInitialized = false;
        }
    }

    void Window::PollEvents() {
        glfwPollEvents();
    }

    void Window::SwapBuffers() {
        if (m_Data.GraphicsBackend == GraphicsAPI::OpenGL) {
            glfwSwapBuffers(m_Window);
        }
    }

    bool Window::ShouldClose() const {
        return glfwWindowShouldClose(m_Window);
    }

    void Window::SetVSync(bool enabled) {
        m_Data.VSync = enabled;
        if (m_Data.GraphicsBackend == GraphicsAPI::OpenGL && HasActiveContext()) {
            glfwSwapInterval(enabled ? 1 : 0);
        }
    }

    void Window::Maximize() {
        if (m_Window) {
            glfwMaximizeWindow(m_Window);
        }
    }

    void Window::Restore() {
        if (m_Window) {
            glfwRestoreWindow(m_Window);
        }
    }

    bool Window::IsMaximized() const {
        if (m_Window) {
            return glfwGetWindowAttrib(m_Window, GLFW_MAXIMIZED) == GLFW_TRUE;
        }
        return false;
    }

    void Window::SetTitle(const std::string& title) {
        m_Data.Title = title;
        if (m_Window) {
            glfwSetWindowTitle(m_Window, title.c_str());
        }
    }

    void Window::SetCursorMode(CursorMode mode) {
        if (!m_Window) return;
        m_Data.CurrentCursorMode = mode;
        int glfwMode = GLFW_CURSOR_NORMAL;
        switch (mode) {
            case CursorMode::Normal: glfwMode = GLFW_CURSOR_NORMAL; break;
            case CursorMode::Hidden: glfwMode = GLFW_CURSOR_HIDDEN; break;
            case CursorMode::Locked: glfwMode = GLFW_CURSOR_DISABLED; break;
        }
        glfwSetInputMode(m_Window, GLFW_CURSOR, glfwMode);

        if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(m_Window, GLFW_RAW_MOUSE_MOTION, (mode == CursorMode::Locked) ? GLFW_TRUE : GLFW_FALSE);
        }
    }

    void Window::SetCursorPosition(f32 x, f32 y) {
        if (!m_Window) return;
        glfwSetCursorPos(m_Window, static_cast<double>(x), static_cast<double>(y));
    }

}