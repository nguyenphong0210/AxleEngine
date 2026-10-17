#pragma once
#include <Axle/KeyCodes.hpp>
#include <utility>

struct GLFWwindow;

namespace Axle {

    class Input {
    public:
        static void SetContext(GLFWwindow* window);
        static GLFWwindow* GetContext() { return s_WindowContext; }

        static bool IsKeyPressed(KeyCode key);
        static bool IsMouseButtonPressed(MouseCode button);

        static std::pair<f32, f32> GetMousePosition();
        static f32 GetMouseX();
        static f32 GetMouseY();

    private:
        inline static GLFWwindow* s_WindowContext = nullptr;
    };

}