#include <Axle/Application.hpp>
#include <Axle/ImageSource.hpp>
#include <Axle/ImageTextureCache.hpp>
#include <Axle/Log.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/RHI/RHISwitcher.hpp>
#include <Axle/RHI/RHIPresentationContext.hpp>
#include <Axle/RenderService.hpp>
#include <Axle/Project.hpp>
#include <Axle/ProjectSerializer.hpp>
#include "RuntimeLayer.hpp"
#include "RuntimeStartup.hpp"
#include "RuntimeRenderContext.hpp"
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

    try {
        std::filesystem::path projectPath;
        uint32_t frameLimit = 0;
        bool verifySimulation = false;
        auto rhiConfig = Axle::RHI::RHISwitcher::ParseCommandLine(argc, argv);
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--verify-simulation") verifySimulation = true;
            else if (arg.rfind("--frames=", 0) == 0) {
                size_t consumed = 0; const auto value = std::stoul(arg.substr(9),&consumed);
                if (consumed != arg.size()-9 || value == 0 || value > 1000000) throw std::invalid_argument("--frames requires an integer from 1 to 1000000");
                frameLimit = static_cast<uint32_t>(value);
            }
            else if (arg == "--project" && i + 1 < argc) projectPath = argv[++i];
            else if (arg.rfind("--project=", 0) == 0) projectPath = arg.substr(10);
            else if (!arg.empty() && arg[0] != '-') projectPath = arg;
        }

        const auto startup = Axle::PrepareRuntimeStartup(projectPath,rhiConfig);
        projectPath = startup.ProjectPath; rhiConfig = startup.Backend;

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
        Axle::RuntimeRenderContext* nativePresenter=nullptr;
        if (!Axle::RHI::RHISwitcher::InitializeProduct(rhiConfig, [&](Axle::GraphicsAPI api) {
            try {
                nativePresenter=nullptr;
                spec.GraphicsBackend = api;
                app = std::make_unique<Axle::Application>(spec);
                app->RegisterService(std::make_shared<PhysicsApplicationService>());
                layer = std::make_shared<Axle::RuntimeLayer>(projectPath,startup.ProjectDefinition);
                app->PushLayer(layer);
                if (api != Axle::GraphicsAPI::OpenGL) {
                    auto presenter=std::make_unique<Axle::RuntimeRenderContext>(app->GetWindow());
                    nativePresenter=presenter.get();app->SetRenderContext(std::move(presenter));
                }
                return true;
            } catch (const std::exception& error) {
                AXLE_ERROR("Runtime backend candidate failed: {0}", error.what());
                layer.reset();
                app.reset();
                return false;
            }
        })) throw std::runtime_error("No requested backend could initialize the Runtime");
        AXLE_INFO("Runtime render context: {0}", app->GetWindow().GetGraphicsBackend()==Axle::GraphicsAPI::OpenGL ? "OpenGL window" : "Native sampled presentation");
        if (verifySimulation) layer->VerifySimulationContract();
        else if (!app->Run(frameLimit)) throw std::runtime_error("Runtime rendering or presentation failed");
        if(!verifySimulation&&nativePresenter) {
            AXLE_INFO("Runtime frames presented: {0}",nativePresenter->GetPresentCount());
            if(frameLimit&&nativePresenter->GetPresentCount()!=frameLimit)throw std::runtime_error("Bounded Runtime loop did not present all frames");
        }
    } catch (const std::exception& e) {
        AXLE_CRITICAL("Unhandled Runtime Exception: {0}", e.what());
        fprintf(stderr, "[FATAL] Unhandled Runtime Exception: %s\n", e.what());
        Axle::RHI::RHIDevice::Get().Shutdown();
        return 1;
    } catch (...) {
        AXLE_CRITICAL("Unhandled unknown exception caught in Runtime main!");
        fprintf(stderr, "[FATAL] Unhandled unknown exception caught in Runtime main!\n");
        Axle::RHI::RHIDevice::Get().Shutdown();
        return 1;
    }

    return Axle::RHI::RHIDevice::Get().Shutdown() ? 0 : 1;
}
