#include "PluginSession.hpp"
#include "PluginSDKConfig.hpp"
#include "OwnedProcess.hpp"
#include <Axle/Scene.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <Axle/ExtensionDocument.hpp>
#include <Axle/ProjectSerializer.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/Log.hpp>
#include <Axle/AssetCooker.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/Path.hpp>
#include <Axle/UI/UIParser.hpp>
#include <Axle/AssemblyEditorPanel.hpp>
#include <imgui.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#define CHECK(value) do { if (!(value)) { std::cerr << "FAIL " << __LINE__ << ": " << #value << '\n'; return 1; } } while (false)
int Run(int argc, wchar_t** argv) {
    CHECK(argc == 4);
    using namespace Axle;
    Log::Init(); JobSystem::Init(2); PhysicsEngine::Init(); ImGui::CreateContext();
    auto& io = ImGui::GetIO(); io.IniFilename = nullptr; io.DisplaySize = {800,600}; io.DeltaTime = 1.0f/60;
    unsigned char* pixels; int width, height; io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    struct CleanupEngine { ~CleanupEngine() { ImGui::DestroyContext(); PhysicsEngine::Shutdown(); JobSystem::Shutdown(); } } cleanupEngine;
    const auto root = std::filesystem::temp_directory_path() / "AxleEngine" / (L"Session \u0111\u01b0\u1eddng " + std::to_wstring(static_cast<std::uint64_t>(UUID{})));
    std::filesystem::create_directories(root / "assets/scenes");
    std::cout << "Session fixture: " << Plugin::PathUtf8(root) << '\n';
    auto project = Project::New(root); project->GetConfig().SelectedPlugins = {"studio.mover"}; Project::SetActive(project);
    const auto projectPath = root / L"Sandbox \u0111\u1ed3.axleproj";
    CHECK(ProjectSerializer(project).Serialize(projectPath));
    std::atomic_bool cancel{false};
    auto generated = RunOwnedProcess(argv[2], {argv[3], projectPath.wstring(), L"--id", L"studio.mover", L"--module", L"StudioMover", L"--name", L"Studio Mover"}, root, root / "generator.log", cancel);
    CHECK(generated.ExitCode == 0);
    CHECK(RunOwnedProcess(argv[2], {argv[3], projectPath.wstring(), L"--id", L"studio.mover", L"--module", L"StudioMover", L"--name", L"Studio Mover"}, root, root / "overwrite.log", cancel).ExitCode != 0);
    PluginSessionEnvironment environment{{}, AXLE_PLUGIN_SDK_DIRECTORY, argv[1], AXLE_PLUGIN_BUILD_CONFIGURATION,
        AXLE_PLUGIN_SDK_FINGERPRINT, AXLE_PLUGIN_TOOLCHAIN_FINGERPRINT, AXLE_PLUGIN_CMAKE_GENERATOR, AXLE_PLUGIN_CMAKE_TOOLSET, AXLE_PLUGIN_CMAKE_INSTANCE};
    auto finishBuild = [](PluginProjectSession& session) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
        while (session.IsBuilding() && std::chrono::steady_clock::now() < deadline) { session.Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        return !session.IsBuilding() && session.LastBuild().Success;
    };
    std::string error, fingerprint;
    PluginRestartSnapshot staged;
    {
        PluginProjectSession session(environment); CHECK(session.Bind(error));
        CHECK(!session.Open(project, {})); CHECK(session.GetStatus() == PluginProjectSession::Status::NeedsBuild);
        CHECK(!session.BeginBuild());
        CHECK(session.SetProjectFile(projectPath));
        CHECK(session.BeginBuild()); CHECK(finishBuild(session));
        CHECK(session.GetStatus() == PluginProjectSession::Status::NeedsRestart && !session.HasNativeModules());
        std::vector<std::string> modules; for(const auto& module : session.DesiredPlan().Artifacts) if(module.Kind!=Plugin::ModuleKind::Content) modules.push_back(module.ModuleName);
        CHECK(CapturePluginRestartSnapshot(session.LastBuild().GenerationDirectory,modules,staged,error));
        CHECK(!session.Open(project, projectPath)); // No late native activation, even from an initially empty graph.
    }
    {
        auto expected = staged; expected.ReceiptSet = "changed-receipts";
        PluginProjectSession session(environment); CHECK(session.Bind(error)); CHECK(!session.Open(project,projectPath,false,expected));
        CHECK(session.IsSafeMode() && !session.HasNativeModules());
    }
    {
        const auto index = root/"Intermediate/Plugins"/(Plugin::CreateBuildPlan(Plugin::Resolve(Plugin::Discover({{root/"Plugins"},{root/"Plugins"}}),{"studio.mover"},Plugin::ProductKind::Editor),
            Plugin::ProductKind::Editor,environment.Configuration,environment.SdkFingerprint,environment.ToolchainFingerprint).Fingerprint+".generation");
        const auto originalIndex = FileSystem::ReadTextFile(index);
        const auto alternate = staged.Generation.parent_path()/("alternate-"+std::to_string(static_cast<std::uint64_t>(UUID{})));
        std::filesystem::copy(staged.Generation,alternate,std::filesystem::copy_options::recursive);
        CHECK(FileSystem::WriteTextFileAtomically(index,PathToUtf8(alternate)));
        {
            PluginProjectSession session(environment); CHECK(session.Bind(error)); CHECK(!session.Open(project,projectPath,false,staged));
            CHECK(session.IsSafeMode() && !session.HasNativeModules());
        }
        CHECK(FileSystem::WriteTextFileAtomically(index,originalIndex));
    }
    {
        auto scene = std::make_shared<Scene>(); auto entity = scene->CreateEntityWithUUID(UUID(100), "Mover");
        ExtensionDocument document{"studio.mover/Mover", 1, true, false, "3.5 \"Private string\""};
        std::ostringstream text; WriteExtensionDocument(text, document);
        std::istringstream input(text.str()); std::string line; std::getline(input, line);
        const auto section = line.substr(4, line.size()-5); std::vector<std::string> lines;
        while (std::getline(input, line)) lines.push_back(line);
        scene->StorePassiveSection(entity, section, lines);
        std::vector<std::string> errors; CHECK(!scene->ExtensionsReady(errors));
        PluginProjectSession session(environment); CHECK(session.Bind(error));
        CHECK(session.Open(project, projectPath));
        CHECK(session.ActiveIds() == std::vector<std::string>{"studio.mover"} && session.MatchesDesired());
        fingerprint = session.ActiveFingerprint(); CHECK(scene->ExtensionsReady(errors));
        CHECK(scene->DuplicateEntity(entity));
        SceneSerializer serializer(scene.get()); CHECK(serializer.Serialize(root / "assets/scenes/Mover.axlescene"));
        CHECK(ValidatePluginAssetDocument(root / "assets/scenes/Mover.axlescene", error));
        auto ui = scene->CreateEntityWithUUID(UUID(500), "Unicode UI"); auto& canvas = ui.AddComponent<UICanvasComponent>();
        canvas.AssetPath = "ui/" + PathToUtf8(std::filesystem::path(L"\u0110\u1ed3.axleui"));
        canvas.Document = std::make_shared<UIDocument>();
        CHECK(UIParser::ParseSource("Canvas { Text { text: \"first\" } }", *canvas.Document, &error)); canvas.Document->SetDirty();
        const auto uiPath = SceneSerializer::CanvasDocumentPath(ui);
        const auto capture = serializer.SerializeToString();
        CHECK(!capture.empty() && canvas.Document->IsDirty() && !std::filesystem::exists(uiPath));
        CHECK(serializer.Serialize(root / "assets/scenes/UnicodeUI.axlescene") && !canvas.Document->IsDirty());
        const auto savedUI = FileSystem::ReadTextFile(uiPath); canvas.Document->SetDirty();
        CHECK(!serializer.SerializeToString().empty() && canvas.Document->IsDirty() && FileSystem::ReadTextFile(uiPath) == savedUI);
        canvas.Document->SetDirty(false);
        auto uiReloaded = std::make_shared<Scene>();
        CHECK(SceneSerializer(uiReloaded.get()).Deserialize(root / "assets/scenes/UnicodeUI.axlescene"));
        CHECK(uiReloaded->GetEntityByUUID(UUID(500)).GetComponent<UICanvasComponent>().Document);
        // A clean alias preceding two dirty writers cannot hide their conflict.
        auto secondUI = scene->CreateEntityWithUUID(UUID(501), "Other canvas"); auto thirdUI = scene->CreateEntityWithUUID(UUID(502), "Third canvas");
        auto& secondCanvas = secondUI.AddComponent<UICanvasComponent>(); auto& thirdCanvas = thirdUI.AddComponent<UICanvasComponent>();
        secondCanvas.AssetPath = thirdCanvas.AssetPath = canvas.AssetPath;
        secondCanvas.Document = std::make_shared<UIDocument>(); thirdCanvas.Document = std::make_shared<UIDocument>();
        CHECK(secondCanvas.Document->LoadFromFile(PathToUtf8(uiPath)) && thirdCanvas.Document->LoadFromFile(PathToUtf8(uiPath)));
        secondCanvas.Document->SetDirty(); canvas.Document->SetDirty(); // Registry view visits newest (clean third) first.
        CHECK(!serializer.Serialize(root / "assets/scenes/Conflict.axlescene") && FileSystem::ReadTextFile(uiPath) == savedUI);
        scene->DestroyEntity(secondUI); scene->DestroyEntity(thirdUI); canvas.Document->SetDirty(false);
        {
            AssemblyData data; data.IsValid = true; data.Name = "Wide and deep native data"; data.RootNode.AuthoredUUID = 10000;
            auto addData = [&](AssemblyNode& node, std::uint64_t id) {
                node.AuthoredUUID = id; std::ostringstream encoded;
                WriteExtensionDocument(encoded,{"studio.mover/Mover",1,true,false,std::to_string(id)+" \"node"+std::to_string(id)+"\""});
                std::istringstream source(encoded.str()); std::string header; std::getline(source,header);
                UnknownComponentSection section{header.substr(4,header.size()-5),{}};
                std::string line; while(std::getline(source,line)) section.Lines.push_back(line);
                node.PassiveSections.push_back(std::move(section));
            };
            for(unsigned i=0;i<101;++i) { AssemblyNode child; addData(child,10100+i); data.RootNode.Children.push_back(std::move(child)); }
            AssemblyNode nested; addData(nested,20000); data.RootNode.Children[0].Children.push_back(std::move(nested));
            auto* deep = &data.RootNode.Children[1];
            for(unsigned i=0;i<20;++i) { AssemblyNode child; addData(child,30000+i); deep->Children.push_back(std::move(child)); deep=&deep->Children.back(); }
            const auto file = root / "assets/Wide.axle"; CHECK(AssemblySerializer::SaveAssemblyData(file,data));
            AssemblyEditorPanel panel; panel.SetAssembly(file); CHECK(panel.GetAssemblyData().IsValid);
            const auto preview = panel.GetPreviewScene(); CHECK(preview && preview->GetEntityByUUID(UUID(20000)) && preview->GetEntityByUUID(UUID(10200)));
            CHECK(panel.SaveDocument()); auto saved = AssemblySerializer::LoadAssemblyData(file);
            ExtensionDocument first, last;
            const auto& a = saved.RootNode.Children[0].Children[0].PassiveSections.front();
            const auto& b = saved.RootNode.Children[100].PassiveSections.front();
            CHECK(ParseExtensionDocument(a.SectionName,a.Lines,first) && ParseExtensionDocument(b.SectionName,b.Lines,last));
            CHECK(first.Payload == "20000 \"node20000\"" && last.Payload == "10200 \"node10200\"");
            CHECK(saved.RootNode.Children[0].Children[0].AuthoredUUID == 20000 && saved.RootNode.Children[100].AuthoredUUID == 10200);
            panel.ShutdownPreview();
        }
        uiReloaded.reset(); scene->DestroyEntity(ui);
        CookSettings cook; cook.ProjectPath = projectPath; cook.AssetDirectory = root / "assets";
        cook.BakeMeshesToBinary = false;
        cook.OutputDirectory = root / "Cooked"; cook.CopyExecutables = false; cook.IncludeShaders = false;
        cook.ValidatePluginDocument = ValidatePluginAssetDocument;
        CHECK(AssetCooker::CookProject(cook).Success);
        auto reopened = std::make_shared<Scene>(); SceneSerializer reload(reopened.get());
        CHECK(reload.Deserialize(root / "assets/scenes/Mover.axlescene")); CHECK(reopened->ExtensionsReady(errors));
        auto play = Scene::Copy(reopened); CHECK(play);
        play->OnRuntimeStart(); play->ExecuteFrameScriptPhase(0.5f);
        CHECK(play->GetEntityByUUID(UUID(100)).GetComponent<TransformComponent>().Translation.x == 1.75f);
        play->OnRuntimeStop(); CHECK(entity.GetComponent<TransformComponent>().Translation.x == 0);
        ImGui::NewFrame(); ImGui::SetNextWindowSize({600,400}); ImGui::Begin("Template Inspector");
        session.EditorServices().DrawComponents(entity); ImGui::End(); ImGui::Render();
        CHECK(ImGui::GetDrawData()->CmdListsCount > 0);
        project->GetConfig().SelectedPlugins.clear();
        CHECK(!session.MatchesDesired()); CHECK(session.BeginBuild()); CHECK(finishBuild(session));
        CHECK(session.HasNativeModules() && session.ActiveFingerprint() == fingerprint && session.ActiveIds().size() == 1);
        CHECK(session.GetStatus() == PluginProjectSession::Status::NeedsRestart);
        project->GetConfig().SelectedPlugins = {"studio.mover"};
        CHECK(session.MatchesDesired() && session.GetStatus() == PluginProjectSession::Status::Active);
        session.Quiesce(); play.reset(); reopened.reset(); scene->Clear(); scene.reset(); session.Shutdown();
    }
    CHECK(!ValidatePluginAssetDocument(root / "assets/scenes/Mover.axlescene", error));
    project->GetConfig().SelectedPlugins = {"studio.mover"};
    const auto markers = root / "Intermediate/Plugins/Sessions";
    std::filesystem::create_directories(markers); std::ofstream(markers / "abandoned.pending") << "failed session";
    {
        PluginProjectSession session(environment); CHECK(session.Bind(error)); CHECK(!session.Open(project, projectPath));
        CHECK(session.IsSafeMode() && !session.HasNativeModules());
        CHECK(session.ClearAbandonedSessions()); CHECK(!std::filesystem::exists(markers / "abandoned.pending"));
    }
    {
        PluginProjectSession session(environment); CHECK(session.Bind(error)); CHECK(session.Open(project, projectPath));
        CHECK(session.ActiveFingerprint() == fingerprint); session.Shutdown();
    }
    CHECK(std::filesystem::is_empty(markers));
    CHECK(std::filesystem::remove(markers)); std::ofstream(markers) << "not a directory";
    {
        PluginProjectSession session(environment); CHECK(session.Bind(error)); CHECK(!session.Open(project,projectPath));
        CHECK(session.IsSafeMode() && !session.HasNativeModules());
    }
    CHECK(std::filesystem::remove(markers)); std::filesystem::create_directories(markers);
    {
        PluginProjectSession session(environment); CHECK(session.Bind(error)); CHECK(session.Open(project,projectPath));
        const auto copyPath = root / "Relocated/Relocated.axleproj";
        CHECK(Project::SaveActive(copyPath)); CHECK(std::filesystem::exists(copyPath.parent_path()/"Plugins/StudioMover/Runtime/Mover.cpp"));
        CHECK(session.SetProjectFile(copyPath)); CHECK(session.ProjectPath() == copyPath);
        CHECK(session.HasNativeModules() && session.ActiveFingerprint() == fingerprint && !session.MatchesDesired());
        CHECK(std::filesystem::is_empty(markers));
        CHECK(!std::filesystem::is_empty(copyPath.parent_path()/"Intermediate/Plugins/Sessions"));
        session.Shutdown(); CHECK(std::filesystem::is_empty(copyPath.parent_path()/"Intermediate/Plugins/Sessions"));
    }
    project->GetConfig().StartScene = "scenes/Mover.axlescene";
    CHECK(ProjectSerializer(project).Serialize(projectPath));
    std::ofstream(root / "workflow.state") << fingerprint << '\n';
    std::cout << "Generated external SDK plugin and Editor session build, activation, persistence, copy, simulation, frozen Active/Desired and recovery: PASS\n";
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 4 && std::wstring(argv[1]) == L"--build-project") {
            using namespace Axle; Log::Init(); auto project = Project::Load(argv[3]); CHECK(project);
            const auto root = project->GetProjectDirectoryPath(); CHECK(std::filesystem::exists(root/"workflow.state"));
            PluginBuildRequest request; request.CMake = argv[2]; request.ProjectDirectory = root; request.SdkDirectory = AXLE_PLUGIN_SDK_DIRECTORY;
            request.Generator = AXLE_PLUGIN_CMAKE_GENERATOR; request.GeneratorToolset = AXLE_PLUGIN_CMAKE_TOOLSET; request.GeneratorInstance = AXLE_PLUGIN_CMAKE_INSTANCE;
            const auto catalog = Plugin::Discover({{root/"Plugins"},{root/"Plugins"}});
            request.Plan = Plugin::CreateBuildPlan(Plugin::Resolve(catalog,project->GetConfig().SelectedPlugins,Plugin::ProductKind::Editor),
                Plugin::ProductKind::Editor,AXLE_PLUGIN_BUILD_CONFIGURATION,AXLE_PLUGIN_SDK_FINGERPRINT,AXLE_PLUGIN_TOOLCHAIN_FINGERPRINT);
            std::atomic_bool cancel{false}; const auto result = BuildPlugins(request,cancel);
            if (!result.Success) { std::cerr << result.Error << '\n'; return 1; }
            std::ofstream(root/"workflow.state") << request.Plan.Fingerprint << '\n'; return 0;
        }
        return Run(argc, argv);
    } catch (const std::exception& error) { std::cerr << error.what() << std::endl; return 1; }
}
