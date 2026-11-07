#pragma once
#include <Axle/Types.hpp>
#include <Axle/Timestep.hpp>
#include <Axle/Layer.hpp>
#include <Axle/LayerStack.hpp>
#include <Axle/Window.hpp>
#include <Axle/ApplicationUI.hpp>

#include <string>
#include <string_view>
#include <memory>
#include <vector>
#include <functional>

namespace Axle {

    class IApplicationService {
    public:
        virtual ~IApplicationService() = default;
        virtual const char* GetName() const = 0;
        virtual void OnInit() {}
        virtual void OnUpdate(Timestep ts) {}
        virtual void OnShutdown() {}
    };

    struct ApplicationSpecification {
        std::string Name = "Axle Application";
        u32 Width = 1280;
        u32 Height = 720;
        bool VSync = true;
        bool Maximized = true;
        GraphicsAPI GraphicsBackend = GraphicsAPI::OpenGL;

        // Optional host service callbacks for renderer initialization / viewport sync
        std::function<bool(void* procAddress)> RendererInitCallback = nullptr;
        std::function<void(u32 width, u32 height)> ViewportResizeCallback = nullptr;
        std::function<void()> RendererShutdownCallback = nullptr;
    };

    class Application {
    public:
        Application(const ApplicationSpecification& spec = ApplicationSpecification());
        virtual ~Application();

        // Run the main game loop
        bool Run(uint32_t frameLimit = 0);

        // Request graceful shutdown
        void Close();

        void PushLayer(std::shared_ptr<Layer> layer);
        void PopLayer(std::shared_ptr<Layer> layer);

        void SetUIContext(std::unique_ptr<ApplicationUIContext> uiContext);
        ApplicationUIContext* GetUIContext() { return m_UIContext.get(); }
        void SetRenderContext(std::unique_ptr<ApplicationRenderContext> context);

        Window& GetWindow() { return *m_Window; }
        const Window& GetWindow() const { return *m_Window; }

        // Explicit service ownership
        void RegisterService(std::shared_ptr<IApplicationService> service);
        void UnregisterService(std::string_view name);
        std::shared_ptr<IApplicationService> GetService(std::string_view name) const;

        template<typename T>
        std::shared_ptr<T> GetService() const {
            for (const auto& svc : m_Services) {
                if (auto casted = std::dynamic_pointer_cast<T>(svc)) {
                    return casted;
                }
            }
            return nullptr;
        }

        const std::vector<std::shared_ptr<IApplicationService>>& GetServices() const { return m_Services; }

        static Application& Get() { return *s_Instance; }

    private:
        ApplicationSpecification m_Specification;
        std::unique_ptr<Window> m_Window;
        std::unique_ptr<ApplicationUIContext> m_UIContext;
        std::unique_ptr<ApplicationRenderContext> m_RenderContext;
        LayerStack m_LayerStack;
        std::vector<std::shared_ptr<IApplicationService>> m_Services;

        bool m_Running = true;
        f32 m_LastFrameTime = 0.0f;

        static Application* s_Instance;
    };

}
