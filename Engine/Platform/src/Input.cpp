#include <Axle/Input.hpp>
#include <GLFW/glfw3.h>

namespace Axle {

    void Input::SetContext(GLFWwindow* window) {
        s_WindowContext = window;
    }

    bool Input::IsKeyPressed(KeyCode key) {
        if (!s_WindowContext) return false;
        const int state = glfwGetKey(s_WindowContext, static_cast<int>(key));
        return state == GLFW_PRESS || state == GLFW_REPEAT;
    }

    bool Input::IsMouseButtonPressed(MouseCode button) {
        if (!s_WindowContext) return false;
        const int state = glfwGetMouseButton(s_WindowContext, static_cast<int>(button));
        return state == GLFW_PRESS;
    }

    std::pair<f32, f32> Input::GetMousePosition() {
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

}