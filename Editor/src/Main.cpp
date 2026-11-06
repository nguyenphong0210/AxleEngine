#include "EditorStartup.hpp"
#include "EditorStartupConfig.hpp"
#include <Axle/Application.hpp>
#include <Axle/Log.hpp>
#include <Axle/ImGuiLayer.hpp>
#include <Axle/AssetCooker.hpp>
#include <Axle/Project.hpp>
#include <Axle/ProjectSerializer.hpp>
#include <Axle/RHI/RHISwitcher.hpp>
#include <Axle/RHIMeshRegistry.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/PhysicsEngine.hpp>
#include "EditorLayer.hpp"
#include <exception>
#include <csignal>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

static void WriteCrashInfo(const char* title, DWORD code, void* addr, PEXCEPTION_POINTERS ep) {
    FILE* f = fopen("D:\\WorkSpace\\AxleEngine\\crash.log", "w");
    if (!f) f = fopen("crash.log", "w");
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

        HANDLE hProcess = GetCurrentProcess();
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
        SymInitialize(hProcess, "D:\\WorkSpace\\AxleEngine\\build\\Editor\\Release;.;", TRUE);

        void* stack[64];
        unsigned short frames = CaptureStackBackTrace(0, 64, stack, NULL);
        fprintf(f, "\nCallstack Backtrace (%u frames):\n", frames);
        for (unsigned short i = 0; i < frames; ++i) {
            DWORD64 displacement = 0;
            char buffer[sizeof(SYMBOL_INFO) + 256 * sizeof(TCHAR)];
            PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;
            pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            pSymbol->MaxNameLen = 255;

            IMAGEHLP_LINE64 lineInfo = {};
            lineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
            DWORD displacementLine = 0;

            if (SymFromAddr(hProcess, (DWORD64)stack[i], &displacement, pSymbol)) {
                fprintf(f, "  [%02u] 0x%p %s + 0x%llX", i, stack[i], pSymbol->Name, displacement);
                if (SymGetLineFromAddr64(hProcess, (DWORD64)stack[i], &displacementLine, &lineInfo)) {
                    fprintf(f, " (%s:%lu)", lineInfo.FileName, lineInfo.LineNumber);
                }
                fprintf(f, "\n");
            } else {
                fprintf(f, "  [%02u] 0x%p\n", i, stack[i]);
            }
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
    // Intercept fatal hardware exceptions and access violations
    if (code == EXCEPTION_ACCESS_VIOLATION ||
        code == EXCEPTION_ILLEGAL_INSTRUCTION ||
        code == EXCEPTION_PRIV_INSTRUCTION ||
        code == EXCEPTION_STACK_OVERFLOW ||
        code == EXCEPTION_DATATYPE_MISALIGNMENT ||
        code == EXCEPTION_ARRAY_BOUNDS_EXCEEDED ||
        code == EXCEPTION_FLT_DIVIDE_BY_ZERO ||
        code == EXCEPTION_INT_DIVIDE_BY_ZERO) {

        WriteCrashInfo("HARDWARE/SEH CRASH DETECTED VIA VEH", code, pExceptionPointers->ExceptionRecord->ExceptionAddress, pExceptionPointers);
        AXLE_CRITICAL("CRASH DETECTED! Code: 0x{0:X}, Address: 0x{1:X}", code, (uint64_t)pExceptionPointers->ExceptionRecord->ExceptionAddress);
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
        FILE* f = fopen("D:\\WorkSpace\\AxleEngine\\crash.log", "a");
        if (f) {
            fprintf(f, "Exception what(): %s\n", e.what());
            fclose(f);
        }
    } catch (...) {
        FILE* f = fopen("D:\\WorkSpace\\AxleEngine\\crash.log", "a");
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

    // Explicit projects override the build-configured development sample.
    std::filesystem::path requestedProject;
    bool emptyProject = false;
    bool verifyEmptyStartup = false;
    bool verifyStartup = false;
    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];
        if (argument == "--empty-project") emptyProject = true;
        else if (argument == "--verify-empty-startup") verifyEmptyStartup = true;
        else if (argument == "--verify-startup") verifyStartup = true;
        else if (argument == "--project" && i + 1 < argc) requestedProject = argv[++i];
        else if (argument.rfind("--project=", 0) == 0) requestedProject = argument.substr(10);
        else {
            const std::filesystem::path candidate(argument);
            if (candidate.extension() == ".axleproj" || candidate.extension() == ".axleproject")
                requestedProject = candidate;
        }
    }

    // Headless CLI Cooker / Exporter
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--export" || arg == "--cook") {
            Axle::Log::Init();
            std::filesystem::path outDir = "Export";
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                outDir = argv[++i];
            }
            Axle::CookSettings settings;
            if (requestedProject.empty() || !Axle::Project::Load(requestedProject)) {
                AXLE_ERROR("CLI export requires an explicit valid --project path.");
                return 1;
            }
            settings.ProjectPath = requestedProject;
            settings.AssetDirectory = Axle::Project::GetAssetDirectory();
            settings.OutputDirectory = outDir;
            settings.BakeMeshesToBinary = true;
            settings.IncludeShaders = true;
            settings.CopyExecutables = true;

            auto res = Axle::AssetCooker::CookProject(settings);
            if (res.Success) {
                AXLE_INFO("CLI Export Succeeded! Created {0} ({1} assets, {2} KB)",
                    res.PackagePath.string(), res.TotalAssetsCooked, res.PackageSizeBytes / 1024);
                return 0;
            } else {
                AXLE_ERROR("CLI Export Failed: {0}", res.ErrorMessage);
                return 1;
            }
        }
    }
    const auto startupPath = Axle::ResolveEditorStartupProject(
        requestedProject, AXLE_EDITOR_DEFAULT_PROJECT, emptyProject || verifyEmptyStartup);
    if (requestedProject.empty() && startupPath.empty() && !emptyProject && !verifyEmptyStartup &&
        std::string(AXLE_EDITOR_DEFAULT_PROJECT).size() != 0)
        AXLE_WARN("Configured development project is unavailable; opening an empty project: {0}", AXLE_EDITOR_DEFAULT_PROJECT);

    // Parse RHI Switcher command line flags (--rhi=dx12, --rhi=vulkan, --rhi=opengl)
    Axle::RHI::RHIBackendConfig rhiConfig = Axle::RHI::RHISwitcher::ParseCommandLine(argc, argv);

    // If no explicit CLI backend was provided, inspect Project configuration
    if (rhiConfig.RawArgument.empty()) {
        const auto& projPath = startupPath;
        if (std::filesystem::exists(projPath)) {
            auto tempProj = Axle::Project::New();
            Axle::ProjectSerializer serializer(tempProj);
            if (serializer.Deserialize(projPath)) {
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

#if defined(_WIN32)
#if !defined(AXLE_HAS_VULKAN)
    if (rhiConfig.PreferredBackend == Axle::RHI::RHIType::Vulkan) {
        AXLE_WARN("Vulkan 1.3 is supported for headless RHI compute/tests, but Editor UI requires the Vulkan SDK (not installed). Defaulting to Direct3D 12.");
        rhiConfig.PreferredBackend = Axle::RHI::RHIType::Direct3D12;
    }
#endif
#endif

    Axle::GraphicsAPI activeAPI = Axle::GraphicsAPI::OpenGL;

    if (rhiConfig.PreferredBackend != Axle::RHI::RHIType::OpenGL) {
        if (Axle::RHI::RHISwitcher::ResolveAndInitialize(rhiConfig)) {
            activeAPI = Axle::RHI::RHISwitcher::GetActiveGraphicsAPI();
        } else {
            AXLE_WARN("RHI Switcher failed to initialize preferred backend; falling back to OpenGL.");
            activeAPI = Axle::GraphicsAPI::OpenGL;
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

        Axle::ApplicationSpecification spec{
            .Name = "Axle Editor",
            .Width = 1280,
            .Height = 720,
            .VSync = true,
            .Maximized = true,
            .GraphicsBackend = activeAPI,
            .RendererInitCallback = [](void* proc) {
                return Axle::RenderCommand::Init(reinterpret_cast<Axle::GLProcAddressFn>(proc));
            },
            .ViewportResizeCallback = [](Axle::u32 width, Axle::u32 height) {
                Axle::RenderCommand::SetViewport(0, 0, width, height);
            }
        };

        {
            auto startupProject = Axle::OpenEditorStartupProject(startupPath);
            if (!startupProject) {
                AXLE_ERROR("Could not open the selected startup project.");
                return 1;
            }
            auto app = std::make_unique<Axle::Application>(spec);
            app->RegisterService(std::make_shared<PhysicsApplicationService>());
            app->SetUIContext(std::make_unique<Axle::ImGuiLayer>(Axle::ToImGuiBackend(activeAPI)));
            auto editorLayer = std::make_shared<EditorLayer>(startupPath);
            app->PushLayer(editorLayer);
            if (verifyEmptyStartup || verifyStartup) {
                // Check after the Editor's UI each frame: panels may mutate the scene.
                struct StartupVerifier final : Axle::Layer {
                    std::shared_ptr<EditorLayer> Editor;
                    unsigned Frames = 0;
                    bool RequireEmpty;
                    std::filesystem::path ExpectedProjectDirectory = Axle::Project::GetProjectDirectory();
                    StartupVerifier(std::shared_ptr<EditorLayer> editor, bool requireEmpty)
                        : Editor(std::move(editor)), RequireEmpty(requireEmpty) {}
                    void OnImGuiRender() override {
                        if (Axle::Project::GetProjectDirectory() != ExpectedProjectDirectory)
                            throw std::runtime_error("Editor startup project changed unexpectedly");
                        if (RequireEmpty && (Editor->GetSceneEntityCount() != 0 ||
                            !Axle::Project::GetActive()->GetConfig().StartScene.empty() ||
                            !std::filesystem::is_empty(Axle::Project::GetAssetDirectory())))
                            throw std::runtime_error("Editor empty startup verification failed after UI frame");
                        if (++Frames == 5) {
                            if (RequireEmpty)
                                AXLE_INFO("Editor empty startup verification: PASS (0 entities, 0 assets, no start scene; 5 UI frames)");
                            else
                                AXLE_INFO("Editor startup verification: PASS (project '{0}', {1} entities; 5 UI frames)",
                                    ExpectedProjectDirectory.string(), Editor->GetSceneEntityCount());
                            Axle::Application::Get().Close();
                        }
                    }
                };
                if (verifyEmptyStartup && !requestedProject.empty()) throw std::runtime_error("Empty startup verification requires an untitled project");
                app->PushLayer(std::make_shared<StartupVerifier>(editorLayer, verifyEmptyStartup));
            }
            app->Run();
            AXLE_INFO("Main: Application run loop completed, now destroying app object...");
        }

        AXLE_INFO("Main: Application destroyed, now checking RHIDevice::Shutdown...");
        if (Axle::RHI::RHIDevice::Get().IsInitialized()) {
            Axle::RHIMeshRegistry::Get().Shutdown();
            Axle::RHI::RHIDevice::Get().Shutdown();
        }
        AXLE_INFO("Main: Axle Editor exited cleanly!");
    } catch (const std::exception& e) {
        AXLE_CRITICAL("Unhandled C++ Exception: {0}", e.what());
        fprintf(stderr, "[FATAL] Unhandled C++ Exception: %s\n", e.what());
        FILE* f = fopen("D:\\WorkSpace\\AxleEngine\\crash.log", "w");
        if (f) {
            fprintf(f, "Unhandled C++ Exception: %s\n", e.what());
            fclose(f);
        }
        fflush(stderr);
        return 1;
    } catch (...) {
        AXLE_CRITICAL("Unhandled unknown exception caught in main!");
        fprintf(stderr, "[FATAL] Unhandled unknown exception caught in main!\n");
        FILE* f = fopen("D:\\WorkSpace\\AxleEngine\\crash.log", "w");
        if (f) {
            fprintf(f, "Unhandled unknown exception caught in main!\n");
            fclose(f);
        }
        fflush(stderr);
        return 1;
    }

    return 0;
}