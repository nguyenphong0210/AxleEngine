#include <Axle/Input.hpp>
#include <GLFW/glfw3.h>

namespace Axle {

    static InputContext s_LocalContext;
    static f32 s_MouseScrollY = 0.0f;

    void Input::SetContext(GLFWwindow* window) {
        s_WindowContext = window;

        s_LocalContext.Window = window;
        s_LocalContext.IsKeyPressedFn = [](KeyCode key) -> bool {
            if (!s_LocalContext.Window) return false;
            const int state = glfwGetKey(s_LocalContext.Window, static_cast<int>(key));
            return state == GLFW_PRESS || state == GLFW_REPEAT;
        };
        s_LocalContext.IsMouseButtonPressedFn = [](MouseCode button) -> bool {
            if (!s_LocalContext.Window) return false;
            const int state = glfwGetMouseButton(s_LocalContext.Window, static_cast<int>(button));
            return state == GLFW_PRESS;
        };
        s_LocalContext.GetMousePositionFn = []() -> std::pair<f32, f32> {
            if (!s_LocalContext.Window) return { 0.0f, 0.0f };
            double xPos, yPos;
            glfwGetCursorPos(s_LocalContext.Window, &xPos, &yPos);
            return { static_cast<f32>(xPos), static_cast<f32>(yPos) };
        };
        s_LocalContext.GetMouseScrollYFn = []() -> f32 {
            return s_MouseScrollY;
        };

        s_SharedContext = &s_LocalContext;
    }

    void Input::SetInputContext(const InputContext* context) {
        s_SharedContext = context;
        if (context) {
            s_WindowContext = context->Window;
        }
    }

    const InputContext* Input::GetInputContext() {
        return s_SharedContext ? s_SharedContext : &s_LocalContext;
    }

    GLFWwindow* Input::GetContext() {
        return s_SharedContext ? s_SharedContext->Window : s_WindowContext;
    }

    bool Input::IsKeyPressed(KeyCode key) {
        if (s_SharedContext && s_SharedContext->IsKeyPressedFn) {
            return s_SharedContext->IsKeyPressedFn(key);
        }
        if (!s_WindowContext) return false;
        const int state = glfwGetKey(s_WindowContext, static_cast<int>(key));
        return state == GLFW_PRESS || state == GLFW_REPEAT;
    }

    bool Input::IsMouseButtonPressed(MouseCode button) {
        if (s_SharedContext && s_SharedContext->IsMouseButtonPressedFn) {
            return s_SharedContext->IsMouseButtonPressedFn(button);
        }
        if (!s_WindowContext) return false;
        const int state = glfwGetMouseButton(s_WindowContext, static_cast<int>(button));
        return state == GLFW_PRESS;
    }

    std::pair<f32, f32> Input::GetMousePosition() {
        if (s_SharedContext && s_SharedContext->GetMousePositionFn) {
            return s_SharedContext->GetMousePositionFn();
        }
        if (!s_WindowContext) return { 0.0f, 0.0f };
        double xPos, yPos;
        glfwGetCursorPos(s_WindowContext, &xPos, &yPos);
        return { static_cast<f32>(xPos), static_cast<f32>(yPos) };
    }

    f32 Input::GetMouseX() {
        return GetMousePosition().first;
    }

    f32 Input::GetMouseY() {
        return GetMousePosition().second;
    }

    f32 Input::GetMouseScrollY() {
        if (s_SharedContext && s_SharedContext->GetMouseScrollYFn) {
            return s_SharedContext->GetMouseScrollYFn();
        }
        return s_MouseScrollY;
    }

    void Input::SetMouseScrollY(f32 scrollY) {
        s_MouseScrollY = scrollY;
    }

}