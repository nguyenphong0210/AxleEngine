#include <Axle/Application.hpp>
#include <Axle/Input.hpp>
#include <Axle/Log.hpp>
#include <Axle/JobSystem.hpp>
#include <GLFW/glfw3.h>

namespace Axle {

    Application* Application::s_Instance = nullptr;

    Application::Application(const ApplicationSpecification& spec)
        : m_Specification(spec) {
        s_Instance = this;

        // 1. Initialize logging
        Log::Init();

        // 2. Initialize unified Job System
        JobSystem::Init();

        // 3. Create platform window
        WindowProps props;
        props.Title = spec.Name;
        props.Width = spec.Width;
        props.Height = spec.Height;
        props.VSync = spec.VSync;
        props.Maximized = spec.Maximized;
        props.GraphicsBackend = spec.GraphicsBackend;

        m_Window = std::make_unique<Window>(props);

        // 4. Initialize renderer context if host callback provided
        if (m_Specification.GraphicsBackend == GraphicsAPI::OpenGL && m_Specification.RendererInitCallback) {
            if (!m_Specification.RendererInitCallback(reinterpret_cast<void*>(glfwGetProcAddress))) {
                AXLE_CORE_CRITICAL("Failed to initialize renderer context in Application!");
            }
            if (m_Specification.ViewportResizeCallback) {
                m_Specification.ViewportResizeCallback(m_Window->GetWidth(), m_Window->GetHeight());
            }
        }

        // 5. Setup window resize callback
        m_Window->SetWindowResizeCallback([this](const WindowResizeEvent& e) {
            if (e.Width > 0 && e.Height > 0) {
                if (m_Specification.GraphicsBackend == GraphicsAPI::OpenGL && m_Specification.ViewportResizeCallback) {
                    m_Specification.ViewportResizeCallback(e.Width, e.Height);
                }
                for (auto& layer : m_LayerStack) {
                    layer->OnWindowResize(e.Width, e.Height);
                }
            }
        });

        // 5.1 Setup OS file drop callback
        m_Window->SetWindowDropCallback([this](const std::vector<std::string>& paths) {
            for (auto& layer : m_LayerStack) {
                layer->OnFileDrop(paths);
            }
        });

        AXLE_CORE_INFO("Application '{0}' initialized successfully ({1}x{2})", spec.Name, spec.Width, spec.Height);
    }

    Application::~Application() {
        AXLE_CORE_INFO("Application::~Application -> [1/6] Clear LayerStack");
        m_LayerStack.Clear();

        AXLE_CORE_INFO("Application::~Application -> [2/6] Drain deferred completions");
        JobSystem::PumpMainThreadCompletions();

        AXLE_CORE_INFO("Application::~Application -> [3/6] Shutdown UIContext");
        if (m_UIContext) {
            m_UIContext->Shutdown();
            m_UIContext.reset();
        }

        AXLE_CORE_INFO("Application::~Application -> [4/6] Shutdown Services in reverse registration order");
        for (auto it = m_Services.rbegin(); it != m_Services.rend(); ++it) {
            if (*it) {
                AXLE_CORE_INFO("Application::~Application -> Shutdown service '{0}'", (*it)->GetName());
                (*it)->OnShutdown();
            }
        }
        m_Services.clear();

        if (m_Specification.RendererShutdownCallback) {
            m_Specification.RendererShutdownCallback();
        }

        AXLE_CORE_INFO("Application::~Application -> [5/6] Shutdown JobSystem");
        JobSystem::Shutdown();

        AXLE_CORE_INFO("Application::~Application -> [6/6] Complete ('{0}' shut down)", m_Specification.Name);
        s_Instance = nullptr;
    }

    void Application::RegisterService(std::shared_ptr<IApplicationService> service) {
        if (!service) return;
        service->OnInit();
        m_Services.push_back(std::move(service));
    }

    void Application::UnregisterService(std::string_view name) {
        for (auto it = m_Services.begin(); it != m_Services.end(); ++it) {
            if (*it && (*it)->GetName() == name) {
                (*it)->OnShutdown();
                m_Services.erase(it);
                return;
            }
        }
    }

    std::shared_ptr<IApplicationService> Application::GetService(std::string_view name) const {
        for (const auto& svc : m_Services) {
            if (svc && svc->GetName() == name) {
                return svc;
            }
        }
        return nullptr;
    }

    void Application::SetUIContext(std::unique_ptr<ApplicationUIContext> uiContext) {
        if (m_UIContext) {
            m_UIContext->Shutdown();
        }
        m_UIContext = std::move(uiContext);
        if (m_UIContext && m_Window) {
            m_UIContext->Init(m_Window->GetNativeWindow());
        }
    }

    void Application::Close() {
        m_Running = false;
    }

    void Application::PushLayer(std::shared_ptr<Layer> layer) {
        m_LayerStack.PushLayer(layer);
    }

    void Application::PopLayer(std::shared_ptr<Layer> layer) {
        m_LayerStack.PopLayer(layer);
    }

    void Application::Run() {
        while (m_Running && !m_Window->ShouldClose()) {
            Input::SetMouseScrollY(0.0f);
            m_Window->PollEvents();
            JobSystem::RunPinnedTasks();
            JobSystem::PumpMainThreadCompletions();

            const auto time = static_cast<f32>(glfwGetTime());
            Timestep ts = time - m_LastFrameTime;
            m_LastFrameTime = time;

            // Update registered application services
            for (auto& service : m_Services) {
                service->OnUpdate(ts);
            }

            // Update layers
            for (auto& layer : m_LayerStack) {
                layer->OnUpdate(ts);
            }

            // Render UI overlay if present
            if (m_UIContext) {
                m_UIContext->Begin();
                for (auto& layer : m_LayerStack) {
                    layer->OnImGuiRender();
                }
                m_UIContext->End();
            }

            m_Window->SwapBuffers();
        }
    }

}
