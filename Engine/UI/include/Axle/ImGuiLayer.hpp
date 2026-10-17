#pragma once
#include <Axle/Types.hpp>
#include <Axle/ApplicationUI.hpp>

struct GLFWwindow;

namespace Axle {

    class ImGuiLayer : public ApplicationUIContext {
    public:
        ImGuiLayer() = default;
        ~ImGuiLayer() override;

        void Init(GLFWwindow* window) override;
        void Shutdown() override;

        void Begin() override;
        void End() override;

        void SetDarkThemeColors();

    private:
        bool m_Initialized = false;
    };

}
