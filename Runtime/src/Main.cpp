#include <Axle/Application.hpp>
#include <Axle/ImageSource.hpp>
#include <Axle/ImageTextureCache.hpp>
#include <Axle/Log.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/RHI/RHISwitcher.hpp>
#if defined(AXLE_BUILD_RHI)
#include <Axle/RHI/RHIPresentationContext.hpp>
#include <Axle/RenderService.hpp>
#endif
#include <Axle/Project.hpp>
#include <Axle/ProjectSerializer.hpp>
#include "RuntimeLayer.hpp"
#include <exception>
#include <csignal>
#include <cstdio>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

static void WriteCrashInfo(const char* title, DWORD code, void* addr, PEXCEPTION_POINTERS ep) {
    FILE* f = fopen("crash_runtime.log", "w");
    if (f) {
        fprintf(f, "=== %s ===\n", title);
        fprintf(f, "Exception Code: 0x%08lX\n", (unsigned long)code);
        fprintf(f, "Faulting Address: 0x%p\n", addr);
        if (ep && ep->ExceptionRecord && code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
            ULONG_PTR accessType = ep->ExceptionRecord->ExceptionInformation[0];
            ULONG_PTR targetAddr = ep->ExceptionRecord->ExceptionInformation[1];
            fprintf(f, "Access Violation: Attempted to %s memory at 0x%p\n",
                accessType == 0 ? "READ" : (accessType == 1 ? "WRITE" : "EXECUTE"),
                (void*)targetAddr);
        }

        void* stack[64];
        unsigned short frames = CaptureStackBackTrace(0, 64, stack, NULL);
        fprintf(f, "\nCallstack Backtrace (%u frames):\n", frames);
        for (unsigned short i = 0; i < frames; ++i) {
            fprintf(f, "  [%02u] 0x%p\n", i, stack[i]);
        }
        fflush(f);
        fclose(f);
    }
}

static LONG WINAPI VectoredCrashHandler(PEXCEPTION_POINTERS pExceptionPointers) {
    if (!pExceptionPointers || !pExceptionPointers->ExceptionRecord) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    DWORD code = pExceptionPointers->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_ACCESS_VIOLATION ||
        code == EXCEPTION_ILLEGAL_INSTRUCTION ||
        code == EXCEPTION_PRIV_INSTRUCTION ||
        code == EXCEPTION_STACK_OVERFLOW ||
        code == EXCEPTION_DATATYPE_MISALIGNMENT ||
        code == EXCEPTION_ARRAY_BOUNDS_EXCEEDED ||
        code == EXCEPTION_FLT_DIVIDE_BY_ZERO ||
        code == EXCEPTION_INT_DIVIDE_BY_ZERO) {

        WriteCrashInfo("HARDWARE/SEH CRASH DETECTED VIA VEH", code, pExceptionPointers->ExceptionRecord->ExceptionAddress, pExceptionPointers);
        AXLE_CRITICAL("RUNTIME CRASH DETECTED! Code: 0x{0:X}, Address: 0x{1:X}", code, (uint64_t)pExceptionPointers->ExceptionRecord->ExceptionAddress);
        fflush(stderr);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void TerminateHandler() {
    WriteCrashInfo("CRT TERMINATE CALLED (unhandled exception or abort)", 0xE0000001, nullptr, nullptr);
    try {
        auto ex = std::current_exception();
        if (ex) std::rethrow_exception(ex);
    } catch (const std::exception& e) {
        FILE* f = fopen("crash_runtime.log", "a");
        if (f) {
            fprintf(f, "Exception what(): %s\n", e.what());
            fclose(f);
        }
    } catch (...) {
        FILE* f = fopen("crash_runtime.log", "a");
        if (f) {
            fprintf(f, "Exception what(): Unknown exception\n");
            fclose(f);
        }
    }
}

static void AbortHandler(int sig) {
    WriteCrashInfo("CRT SIGABRT CALLED", static_cast<DWORD>(sig), nullptr, nullptr);
}
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    AddVectoredExceptionHandler(1, VectoredCrashHandler);
    SetUnhandledExceptionFilter(VectoredCrashHandler);
    std::set_terminate(TerminateHandler);
    std::signal(SIGABRT, AbortHandler);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    std::filesystem::path projectPath;
    bool verifySimulation = false;
    auto rhiConfig = Axle::RHI::RHISwitcher::ParseCommandLine(argc, argv);
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--verify-simulation") verifySimulation = true;
        else if (arg == "--project" && i + 1 < argc) projectPath = argv[++i];
        else if (arg.rfind("--project=", 0) == 0) projectPath = arg.substr(10);
        else if (!arg.empty() && arg[0] != '-') projectPath = arg;
    }

    // Apply consistent project / CLI precedence:
    // If no explicit CLI backend was provided, inspect Project configuration
    if (rhiConfig.RawArgument.empty()) {
        std::filesystem::path inspectPath = projectPath;
        if (inspectPath.empty()) {
            if (std::filesystem::exists("Sandbox.axleproj")) inspectPath = "Sandbox.axleproj";
            else if (std::filesystem::exists("Project.axleproj")) inspectPath = "Project.axleproj";
        }
        if (!inspectPath.empty() && std::filesystem::exists(inspectPath)) {
            auto tempProj = Axle::Project::New();
            Axle::ProjectSerializer serializer(tempProj);
            if (serializer.Deserialize(inspectPath)) {
                const std::string& b = tempProj->GetConfig().RHIBackend;
                if (b == "Direct3D12" || b == "dx12" || b == "d3d12") {
                    rhiConfig.PreferredBackend = Axle::RHI::RHIType::Direct3D12;
                } else if (b == "Vulkan" || b == "vulkan" || b == "vk") {
                    rhiConfig.PreferredBackend = Axle::RHI::RHIType::Vulkan;
                } else if (b == "OpenGL" || b == "opengl" || b == "gl") {
                    rhiConfig.PreferredBackend = Axle::RHI::RHIType::OpenGL;
                }
            }
        }
    }

    try {
        class PhysicsApplicationService final : public Axle::IApplicationService {
        public:
            const char* GetName() const override { return "Physics"; }
            void OnInit() override {
                if (!Axle::PhysicsEngine::IsInitialized()) {
                    Axle::PhysicsEngine::Init();
                }
            }
            void OnShutdown() override {
                if (Axle::PhysicsEngine::IsInitialized()) {
                    Axle::PhysicsEngine::Shutdown();
                }
            }
        };

#if defined(AXLE_BUILD_RHI)
        class RuntimeRenderContext final : public Axle::ApplicationRenderContext {
        public:
            RuntimeRenderContext(const Axle::Window& window) {
                Axle::RHI::RHISwapChainDesc desc;
                desc.VSync = window.IsVSync();
                desc.FramesInFlight = 3;
                if (!m_Presenter.Init(window.GetNativeWindow(), desc)) {
                    throw std::runtime_error("Runtime application presentation initialization failed");
                }
            }

            bool Present(const Axle::Window& window) override {
                nri::Texture* blitSource = nullptr;
                if (auto* target = Axle::RenderService::Get().GetDefaultMainColorTarget()) {
                    blitSource = target->GetNative();
                }
                const auto outcome = m_Presenter.RenderFrame(
                    window.GetWidth(),
                    window.GetHeight(),
                    window.IsVSync(),
                    {},
                    {},
                    blitSource
                );
                return outcome != Axle::RHI::RHIPresentOutcome::Failed;
            }

            void Shutdown() override {
                m_Presenter.Shutdown();
            }

        private:
            Axle::RHI::RHIPresentationContext m_Presenter;
        };
#endif

        Axle::ApplicationSpecification spec{
            .Name = "Axle Game Player",
            .Width = 1280,
            .Height = 720,
            .VSync = true,
            .Maximized = true,
            .RendererInitCallback = [](void* proc) {
                return Axle::RenderCommand::Init(reinterpret_cast<Axle::GLProcAddressFn>(proc));
            },
            .ViewportResizeCallback = [](Axle::u32 width, Axle::u32 height) {
                Axle::RenderCommand::SetViewport(0, 0, width, height);
            }
        };

        Axle::ImageTextureCache::SetSourceResolver(Axle::LoadImageSource);
        std::unique_ptr<Axle::Application> app;
        std::shared_ptr<Axle::RuntimeLayer> layer;
        if (!Axle::RHI::RHISwitcher::InitializeProduct(rhiConfig, [&](Axle::GraphicsAPI api) {
            try {
                spec.GraphicsBackend = api;
                app = std::make_unique<Axle::Application>(spec);
                app->RegisterService(std::make_shared<PhysicsApplicationService>());
                layer = std::make_shared<Axle::RuntimeLayer>(projectPath);
                app->PushLayer(layer);
#if defined(AXLE_BUILD_RHI)
                if (api != Axle::GraphicsAPI::OpenGL) {
                    app->SetRenderContext(std::make_unique<RuntimeRenderContext>(app->GetWindow()));
                }
#endif
                return true;
            } catch (const std::exception& error) {
                AXLE_ERROR("Runtime backend candidate failed: {0}", error.what());
                layer.reset();
                app.reset();
                return false;
            }
        })) throw std::runtime_error("No requested backend could initialize the Runtime");
        if (verifySimulation) layer->VerifySimulationContract();
        else app->Run();
    } catch (const std::exception& e) {
        AXLE_CRITICAL("Unhandled Runtime Exception: {0}", e.what());
        fprintf(stderr, "[FATAL] Unhandled Runtime Exception: %s\n", e.what());
        return 1;
    } catch (...) {
        AXLE_CRITICAL("Unhandled unknown exception caught in Runtime main!");
        fprintf(stderr, "[FATAL] Unhandled unknown exception caught in Runtime main!\n");
        return 1;
    }

    Axle::RHI::RHIDevice::Get().Shutdown();
    return 0;
}
