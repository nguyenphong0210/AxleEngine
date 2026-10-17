#pragma once
#include <Axle/KeyCodes.hpp>
#include <utility>

struct GLFWwindow;

namespace Axle {

    struct InputContext {
        GLFWwindow* Window = nullptr;
        bool (*IsKeyPressedFn)(KeyCode key) = nullptr;
        bool (*IsMouseButtonPressedFn)(MouseCode button) = nullptr;
        std::pair<f32, f32> (*GetMousePositionFn)() = nullptr;
    };

    class Input {
    public:
        static void SetContext(GLFWwindow* window);
        static void SetInputContext(const InputContext* context);
        static const InputContext* GetInputContext();
        static GLFWwindow* GetContext();

        static bool IsKeyPressed(KeyCode key);
        static bool IsMouseButtonPressed(MouseCode button);

        static std::pair<f32, f32> GetMousePosition();
        static f32 GetMouseX();
        static f32 GetMouseY();

    private:
        inline static GLFWwindow* s_WindowContext = nullptr;
        inline static const InputContext* s_SharedContext = nullptr;
    };

}