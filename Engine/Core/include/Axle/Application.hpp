#pragma once
#include <Axle/Types.hpp>
#include <Axle/Timestep.hpp>
#include <Axle/Layer.hpp>
#include <Axle/LayerStack.hpp>
#include <Axle/Window.hpp>
#include <Axle/ImGuiLayer.hpp>

#include <string>
#include <memory>

namespace Axle {

    struct ApplicationSpecification {
        std::string Name = "Axle Application";
        u32 Width = 1280;
        u32 Height = 720;
        bool VSync = true;
    };

    class Application {
    public:
        Application(const ApplicationSpecification& spec = ApplicationSpecification());
        virtual ~Application();

        // Run the main game loop
        void Run();

        // Request graceful shutdown
        void Close();

        void PushLayer(std::shared_ptr<Layer> layer);
        void PopLayer(std::shared_ptr<Layer> layer);

        Window& GetWindow() { return *m_Window; }
        const Window& GetWindow() const { return *m_Window; }

        static Application& Get() { return *s_Instance; }

    private:
        ApplicationSpecification m_Specification;
        std::unique_ptr<Window> m_Window;
        std::unique_ptr<ImGuiLayer> m_ImGuiLayer;
        LayerStack m_LayerStack;

        bool m_Running = true;
        f32 m_LastFrameTime = 0.0f;

        static Application* s_Instance;
    };

}
