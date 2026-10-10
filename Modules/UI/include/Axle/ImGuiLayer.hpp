#pragma once
#include <Axle/Types.hpp>

struct GLFWwindow;

namespace Axle {

    class ImGuiLayer {
    public:
        ImGuiLayer() = default;
        ~ImGuiLayer();

        void Init(GLFWwindow* window);
        void Shutdown();

        void Begin();
        void End();

        void SetDarkThemeColors();

    private:
        bool m_Initialized = false;
    };

}
