#include "EditorStartup.hpp"
#include "EditorStartupConfig.hpp"
#include <Axle/Application.hpp>
#include <Axle/Log.hpp>
#include <Axle/ImGuiLayer.hpp>
#include <Axle/AssetCooker.hpp>
#include <Axle/Project.hpp>
#include <Axle/ProjectSerializer.hpp>
#include <Axle/Path.hpp>
#include <Axle/RHI/RHISwitcher.hpp>
#include <Axle/RHI/RHIFaultInjection.hpp>
#include <Axle/RHIMeshRegistry.hpp>
#include <Axle/RenderCommand.hpp>
#include <Axle/RenderService.hpp>
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

int EditorMain(int argc, char** argv) {
#ifdef _WIN32
    AddVectoredExceptionHandler(1, VectoredCrashHandler);
    SetUnhandledExceptionFilter(VectoredCrashHandler);
    std::set_terminate(TerminateHandler);
    std::signal(SIGABRT, AbortHandler);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    std::string renderPolicyError;
    if (!Axle::RenderService::Get().ConfigureProductArguments(argc, argv, renderPolicyError)) {
        fprintf(stderr, "%s\n", renderPolicyError.c_str()); return 1;
    }
    const auto renderPolicy = Axle::RenderService::Get().GetProductRollout();
    fprintf(stdout, "%s\n", renderPolicy.Summary.c_str());
    for (const auto& reason : renderPolicy.Reasons) fprintf(stderr, "Render fallback: %s\n", reason.c_str());
    // Explicit projects override the build-configured development sample.
    std::filesystem::path requestedProject;
    bool emptyProject = false;
    bool verifyEmptyStartup = false;
    bool verifyStartup = false;
    bool verifySimulation = false;
    bool verifyPluginWorkflow = false;
    bool verifyPluginRecovery = false;
    bool safeMode = false;
    bool verifyRestartExit = false, verifyRestartFrameFailure = false;
    std::string expectedPluginPlan, restartChannel;
    Axle::PluginRestartSnapshot expectedPluginSnapshot;
    bool hasPluginGeneration = false;
    unsigned long restartParent = 0;
    std::string startupRejection;
    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];
        if (argument == "--empty-project") emptyProject = true;
        else if (argument == "--safe-mode" || argument == "--recovery") safeMode = true;
        else if (argument == "--verify-restart-exit") verifyRestartExit = true;
        else if (argument == "--verify-restart-frame-failure") verifyRestartFrameFailure = true;
        else if (argument.rfind("--expected-plugin-plan=", 0) == 0) expectedPluginPlan = argument.substr(23);
        else if (argument.starts_with("--expected-plugin-generation=")) {
            const auto path = argument.substr(std::string("--expected-plugin-generation=").size());
            hasPluginGeneration = true; expectedPluginSnapshot.Generation = path == "none" ? std::filesystem::path{} : Axle::PathFromUtf8(path);
        }
        else if (argument.starts_with("--expected-plugin-receipts=")) expectedPluginSnapshot.ReceiptSet = argument.substr(std::string("--expected-plugin-receipts=").size());
        else if (argument.rfind("--restart-channel=", 0) == 0) restartChannel = argument.substr(18);
        else if (argument.rfind("--restart-parent=", 0) == 0) {
            try { std::size_t consumed = 0; auto text = argument.substr(17); restartParent = std::stoul(text, &consumed); if (consumed != text.size()) return 1; }
            catch (...) { return 1; }
        }
        else if (argument == "--verify-empty-startup") verifyEmptyStartup = true;
        else if (argument == "--verify-startup") verifyStartup = true;
        else if (argument == "--verify-simulation") verifySimulation = true;
        else if (argument == "--verify-plugin-workflow") verifyPluginWorkflow = true;
        else if (argument == "--verify-plugin-recovery") { verifyPluginRecovery = true; verifyStartup = true; }
        else if (argument.rfind("--verify-startup-rejection=", 0) == 0) startupRejection = argument.substr(27);
        else if (argument == "--project" && i + 1 < argc) requestedProject = Axle::PathFromUtf8(argv[++i]);
        else if (argument.rfind("--project=", 0) == 0) requestedProject = Axle::PathFromUtf8(argument.substr(10));
        else {
            const auto candidate = Axle::PathFromUtf8(argument);
            if (candidate.extension() == ".axleproj" || candidate.extension() == ".axleproject")
                requestedProject = candidate;
        }
    }
    Axle::PluginRestartChild restartChild;
    const bool restarting = !restartChannel.empty() || restartParent != 0 || !expectedPluginPlan.empty();
    if (restarting) {
        std::string error;
        if (expectedPluginPlan.empty() || !hasPluginGeneration || expectedPluginSnapshot.ReceiptSet.empty() || requestedProject.empty() || safeMode || !restartChild.Connect(restartChannel, restartParent, error)) {
            fprintf(stderr, "Invalid restart handoff: %s\n", error.c_str()); return 1;
        }
    }

    // Headless CLI Cooker / Exporter
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--export" || arg == "--cook") {
            Axle::Log::Init();
            std::filesystem::path outDir = "Export";
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                outDir = Axle::PathFromUtf8(argv[++i]);
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
            settings.CopyExecutables = arg == "--export";
            const auto environment = Axle::GetPluginSessionEnvironment();
            const auto pluginRoot = Axle::Project::GetProjectDirectory() / "Plugins";
            const auto catalog = Axle::Plugin::Discover({{environment.EnginePlugins, pluginRoot}, {pluginRoot}});
            const auto selected = Axle::Plugin::Resolve(catalog, Axle::Project::GetActive()->GetConfig().SelectedPlugins, Axle::Plugin::ProductKind::Editor);
            if (!selected.IsValid()) { AXLE_ERROR("CLI export rejected: selected plugins cannot be resolved"); return 1; }
            Axle::JobSystem::Init(4);
            struct StopJobs { ~StopJobs() { Axle::JobSystem::Shutdown(); } } stopJobs;
            ImGuiContext* validationContext = ImGui::CreateContext();
            struct DestroyContext { ImGuiContext* Context; ~DestroyContext() { ImGui::DestroyContext(Context); } } destroyContext{validationContext};
            Axle::PluginBuildRequest pluginBuild;
            pluginBuild.CMake = environment.CMake; pluginBuild.SdkDirectory = environment.SdkDirectory;
            pluginBuild.ProjectDirectory = Axle::Project::GetProjectDirectory();
            pluginBuild.Generator = environment.Generator; pluginBuild.GeneratorToolset = environment.GeneratorToolset; pluginBuild.GeneratorInstance = environment.GeneratorInstance;
            pluginBuild.Plan = Axle::Plugin::CreateBuildPlan(selected, Axle::Plugin::ProductKind::Editor, environment.Configuration, environment.SdkFingerprint, environment.ToolchainFingerprint);
            std::atomic_bool cancel{false};
            const auto validationBuild = Axle::BuildPlugins(pluginBuild, cancel);
            if (!validationBuild.Success) { AXLE_ERROR("Plugin validation build failed: {0}", validationBuild.Error); return 1; }
            Axle::PluginProjectSession validationSession(environment);
            std::string error;
            if (!validationSession.Bind(error) || !validationSession.Open(Axle::Project::GetActive(), requestedProject)) { AXLE_ERROR("Cannot activate plugin data preflight: {0} {1}", error, validationSession.Error()); return 1; }
            settings.ValidatePluginDocument = Axle::ValidatePluginAssetDocument;
            auto request = Axle::GetRuntimeBuildRequest(); request.ProjectPath = requestedProject;
            for (int option = 1; option < argc; ++option) {
                const std::string value = argv[option];
                if (value.starts_with("--runtime-configuration=")) request.Configuration = value.substr(24);
            }
            if (!Axle::FreezeRuntimeBuildRequest(request, error) || !Axle::ValidateRuntimeBuildDocuments(request, settings.ValidatePluginDocument, error)) { AXLE_ERROR("Product preflight failed: {0}", error); return 1; }
            settings.OutputDirectory = std::filesystem::absolute(outDir.is_absolute() ? outDir : requestedProject.parent_path() / outDir);
            if (settings.CopyExecutables) {
                const auto built = Axle::BuildRuntime(request, cancel);
                if (!built.Success || !Axle::BindRuntimeExport(request, built, settings, error)) { AXLE_ERROR("Runtime export build failed: {0} {1}; log: {2}", built.Error, error, Axle::PathToUtf8(built.LogPath)); return 1; }
                AXLE_INFO("Runtime product build: {0}; generation: {1}", Axle::PathToUtf8(built.ProductBuildDirectory), Axle::PathToUtf8(built.GenerationDirectory));
            } else if (!Axle::BindRuntimeCook(request, settings, error)) { AXLE_ERROR("Snapshot cook failed: {0}", error); return 1; }

            auto res = Axle::AssetCooker::CookProject(settings);
            if (res.Success) {
                AXLE_INFO("CLI Export Succeeded! Created {0} ({1} assets, {2} KB)",
                    Axle::PathToUtf8(res.PackagePath), res.TotalAssetsCooked, res.PackageSizeBytes / 1024);
                return 0;
            } else {
                AXLE_ERROR("CLI Export Failed: {0}", res.ErrorMessage);
                return 1;
            }
        }
    }
    const auto startupPath = Axle::ResolveEditorStartupProject(
        requestedProject, AXLE_EDITOR_DEFAULT_PROJECT, emptyProject || verifyEmptyStartup || verifySimulation);
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

    Axle::GraphicsAPI activeAPI = Axle::GraphicsAPI::None;
    bool startupRejected = false;
    bool productReady = false;
    Axle::RHI::ScopedRHIFault startupFault([&](std::string_view operation) -> std::optional<nri::Result> {
        if (restarting && verifyRestartFrameFailure && productReady && operation == "QueuePresent" &&
            Axle::Application::Get().GetCompletedFrameCount() == 4) return nri::Result::FAILURE;
        if (!startupRejection.empty() && operation == startupRejection &&
            (!startupRejected || startupRejection == "CreateDevice")) {
            startupRejected = true;
            AXLE_INFO("STARTUP_FAULT_REACHED {0}", operation);
            return nri::Result::FAILURE;
        }
        return {};
    });

    // Enclose every product return/exception, including rejected restart
    // candidates. Application locals unwind before device callbacks retire;
    // process caches must still exist when those callbacks run.
    struct ProductDeviceCleanup {
        ~ProductDeviceCleanup() {
            Axle::RHIMeshRegistry::Get().Shutdown();
            Axle::RHI::RHIDevice::Get().Shutdown();
        }
    } deviceCleanup;
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
            std::unique_ptr<Axle::Application> app;
            std::shared_ptr<EditorLayer> editorLayer;
            const bool initialized = Axle::RHI::RHISwitcher::InitializeProduct(rhiConfig, [&](Axle::GraphicsAPI api) {
                try {
                    activeAPI = api;
                    spec.GraphicsBackend = api;
                    app = std::make_unique<Axle::Application>(spec);
                    app->RegisterService(std::make_shared<PhysicsApplicationService>());
                    app->SetUIContext(std::make_unique<Axle::ImGuiLayer>(Axle::ToImGuiBackend(api)));
                    auto* ui = static_cast<Axle::ImGuiLayer*>(app->GetUIContext());
                    app->SetRenderContext(ui->CreateRenderContext(app->GetWindow()));
                    if (Axle::RHI::ScopedRHIFault::Invoke("ProductUI", [] { return nri::Result::SUCCESS; }) != nri::Result::SUCCESS)
                        throw std::runtime_error("Injected rejection after native UI readiness");
                    editorLayer = std::make_shared<EditorLayer>(startupPath);
                    if (restarting) editorLayer->SetExpectedPluginSnapshot(expectedPluginSnapshot);
                    if (safeMode) editorLayer->GetPluginsPanel().SetSafeMode(true);
                    app->PushLayer(editorLayer);
                    if (Axle::RHI::ScopedRHIFault::Invoke("ProductRenderer", [] { return nri::Result::SUCCESS; }) != nri::Result::SUCCESS)
                        throw std::runtime_error("Injected rejection after Editor renderer readiness");
                    return true;
                } catch (const std::exception& error) {
                    AXLE_ERROR("Product backend candidate failed: {0}", error.what());
                    editorLayer.reset();
                    app.reset();
                    Axle::RHIMeshRegistry::Get().Shutdown();
                    return false;
                }
            });
            if (!initialized) {
                AXLE_ERROR("No requested backend could initialize the complete Editor");
                return 1;
            }
            if (restarting) {
                const auto* session = editorLayer->GetPluginSession();
                if (!session || session->GetStatus() != Axle::PluginProjectSession::Status::Active ||
                    session->ActiveFingerprint() != expectedPluginPlan) {
                    AXLE_ERROR("Restart rejected: plugin plan or verified generation changed before child startup"); return 1;
                }
                const auto expectedBackend = rhiConfig.PreferredBackend;
                if (Axle::ToImGuiBackend(activeAPI) != Axle::ToImGuiBackend(Axle::RHI::RHISwitcher::ToGraphicsAPI(expectedBackend)))
                    { AXLE_ERROR("Restart rejected: requested graphics backend did not initialize"); return 1; }
                struct RestartHandshake final : Axle::Layer {
                    Axle::PluginRestartChild& Child;
                    bool Committed = false;
                    bool ExitAfterCommit;
                    RestartHandshake(Axle::PluginRestartChild& child, bool exitAfterCommit) : Child(child), ExitAfterCommit(exitAfterCommit) {}
                    void OnImGuiRender() override {
                        if (Committed) return;
                        std::string error;
                        if (Axle::Application::Get().GetCompletedFrameCount() >= 5 && !Child.Ready(error)) throw std::runtime_error(error);
                        const auto state = Child.Poll(error);
                        if (state == Axle::PluginRestartChild::Result::Failed) throw std::runtime_error(error);
                        Committed = state == Axle::PluginRestartChild::Result::Committed;
                        if (Committed && ExitAfterCommit) Axle::Application::Get().Close();
                    }
                };
                app->PushLayer(std::make_shared<RestartHandshake>(restartChild, verifyRestartExit));
            }
            productReady = true;
            if (!startupRejection.empty() && !startupRejected) throw std::runtime_error("Startup rejection probe did not reach the requested operation");
            if (verifyEmptyStartup || verifyStartup) {
                // Check after the Editor's UI each frame: panels may mutate the scene.
                struct StartupVerifier final : Axle::Layer {
                    std::shared_ptr<EditorLayer> Editor;
                    unsigned Frames = 0;
                    bool RequireEmpty;
                    bool RequireRecovery;
                    std::filesystem::path ExpectedProjectDirectory = Axle::Project::GetProjectDirectory();
                    StartupVerifier(std::shared_ptr<EditorLayer> editor, bool requireEmpty, bool requireRecovery)
                        : Editor(std::move(editor)), RequireEmpty(requireEmpty), RequireRecovery(requireRecovery) {}
                    void OnImGuiRender() override {
                        if (Axle::Project::GetProjectDirectory() != ExpectedProjectDirectory)
                            throw std::runtime_error("Editor startup project changed unexpectedly");
                        if (RequireEmpty && (Editor->GetSceneEntityCount() != 0 ||
                            !Axle::Project::GetActive()->GetConfig().StartScene.empty() ||
                            !std::filesystem::is_empty(Axle::Project::GetAssetDirectory())))
                            throw std::runtime_error("Editor empty startup verification failed after UI frame");
                        if (RequireRecovery && (!Editor->GetPluginSession() || !Editor->GetPluginSession()->IsSafeMode() || Editor->GetPluginSession()->HasNativeModules()))
                            throw std::runtime_error("Plugin recovery startup did not disable native modules");
                        if (++Frames == 5) {
                            if (RequireEmpty)
                                AXLE_INFO("Editor empty startup verification: PASS (0 entities, 0 assets, no start scene; 5 UI frames)");
                            else
                                AXLE_INFO("Editor startup verification: PASS (project '{0}', {1} entities; 5 UI frames)",
                                    Axle::PathToUtf8(ExpectedProjectDirectory), Editor->GetSceneEntityCount());
                            Axle::Application::Get().Close();
                        }
                    }
                };
                if (verifyEmptyStartup && !requestedProject.empty()) throw std::runtime_error("Empty startup verification requires an untitled project");
                app->PushLayer(std::make_shared<StartupVerifier>(editorLayer, verifyEmptyStartup, verifyPluginRecovery));
            }
            if (verifyPluginWorkflow) { editorLayer->VerifyPluginWorkflow(); app->Close(); }
            else if (verifySimulation) editorLayer->VerifySimulationContract();
            else if (!app->Run()) throw std::runtime_error("Editor rendering or presentation failed");
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

#ifdef _WIN32
int wmain(int argc, wchar_t** arguments) {
    std::vector<std::string> storage; storage.reserve(argc);
    std::vector<char*> argv; argv.reserve(argc + 1);
    for (int i = 0; i < argc; ++i) storage.push_back(Axle::PathToUtf8(arguments[i]));
    for (auto& argument : storage) argv.push_back(argument.data());
    argv.push_back(nullptr); return EditorMain(argc, argv.data());
}
#else
int main(int argc, char** argv) { return EditorMain(argc, argv); }
#endif
