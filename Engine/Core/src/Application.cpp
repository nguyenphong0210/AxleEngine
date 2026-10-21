#include <Axle/Application.hpp>
#include <Axle/Input.hpp>
#include <Axle/Log.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/PhysicsEngine.hpp>
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

        // 3. Initialize physics engine core
        if (!PhysicsEngine::IsInitialized()) {
            PhysicsEngine::Init();
        }

        // 3. Create platform window
        WindowProps props;
        props.Title = spec.Name;
        props.Width = spec.Width;
        props.Height = spec.Height;
        props.VSync = spec.VSync;
        props.Maximized = spec.Maximized;
        props.GraphicsBackend = spec.GraphicsBackend;

        m_Window = std::make_unique<Window>(props);

        // 4. Initialize renderer context (for OpenGL legacy / fallback)
        if (m_Window->GetGraphicsBackend() == GraphicsAPI::OpenGL) {
            if (!RenderCommand::Init(reinterpret_cast<GLProcAddressFn>(glfwGetProcAddress))) {
                AXLE_CORE_CRITICAL("Failed to initialize renderer context in Application!");
            }
            RenderCommand::SetViewport(0, 0, m_Window->GetWidth(), m_Window->GetHeight());
        }

        // 5. Setup window resize callback
        m_Window->SetWindowResizeCallback([this](const WindowResizeEvent& e) {
            if (e.Width > 0 && e.Height > 0) {
                if (m_Window->GetGraphicsBackend() == GraphicsAPI::OpenGL) {
                    RenderCommand::SetViewport(0, 0, e.Width, e.Height);
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
        m_LayerStack.Clear();

        if (m_UIContext) {
            m_UIContext->Shutdown();
            m_UIContext.reset();
        }

        if (PhysicsEngine::IsInitialized()) {
            PhysicsEngine::Shutdown();
        }

        JobSystem::Shutdown();

        AXLE_CORE_INFO("Application '{0}' shut down", m_Specification.Name);
        s_Instance = nullptr;
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

            const auto time = static_cast<f32>(glfwGetTime());
            Timestep ts = time - m_LastFrameTime;
            m_LastFrameTime = time;

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
