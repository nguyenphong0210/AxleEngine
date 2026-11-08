#include "PluginsPanel.hpp"
#include "PluginBuild.hpp"
#include "PluginSDKConfig.hpp"
#include <Axle/ProjectSerializer.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/UUID.hpp>
#include <Axle/Path.hpp>
#include <fstream>
#include <iostream>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define CHECK(value) do { if (!(value)) { std::cerr << "FAIL " << __LINE__ << ": " << #value << '\n'; return 1; } } while(false)
int wmain(int argc, wchar_t** argv) {
    using namespace Axle;
    CHECK(argc == 3);
    const auto root = std::filesystem::temp_directory_path()/"AxleEngine"/(L"Create \u0111\u1ed3 "+std::to_wstring(static_cast<std::uint64_t>(Axle::UUID{})));
    std::filesystem::create_directories(root);
    auto project = Project::New(root); const auto file = root/L"Project \u0111\u1ed3.axleproj";
    CHECK(ProjectSerializer(project).Serialize(file));
    const auto before = FileSystem::ReadTextFile(file);
    PluginsPanel panel; panel.SetEnginePluginRoot(root/"EnginePlugins"); panel.SetPluginTemplateDirectory(argv[2]);
    CHECK(!panel.CreatePlugin("Mover","project.mover","Mover"));
    panel.SetActiveProject(project);
    CHECK(!panel.CreatePlugin("Mover","project.mover","Mover"));
    panel.SetProjectPath(file);
    panel.SetBusy(true); CHECK(!panel.CreatePlugin("Mover","project.mover","Mover")); panel.SetBusy(false);
    CHECK(!panel.CreatePlugin("Mover","project.mover","Mover",true));
    CHECK(!std::filesystem::exists(root/"Plugins"));
    CHECK(!panel.CreatePlugin("Mover","../outside","Mover"));
    CHECK(!panel.CreatePlugin("Mover","project.mover","../outside"));
    CHECK(!panel.CreatePlugin("Mover","project.mover","CON"));
    CHECK(!panel.CreatePlugin("Bad\"Name","project.mover","Mover"));
    panel.SetPluginTemplateDirectory(root/"MissingTemplate");
    CHECK(!panel.CreatePlugin("Mover","project.mover","Mover"));
    CHECK(!std::filesystem::exists(root/"Plugins/Mover"));
    panel.SetPluginTemplateDirectory(argv[2]);
    CHECK(panel.CreatePlugin("Mover \xC4\x91\xE1\xBB\x93","project.mover","Mover"));
    const auto mover = root/"Plugins/Mover";
    CHECK(std::filesystem::exists(mover/"Mover.axleplugin") && std::filesystem::exists(mover/"Runtime/Mover.cpp") && std::filesystem::exists(mover/"Editor/Inspector.cpp"));
    CHECK(panel.GetCatalog().Plugins.size()==1 && panel.GetCatalog().Plugins[0].Id=="project.mover");
    CHECK(panel.GetSearchFilter()=="project.mover" && panel.GetSourceFilter()==PluginsPanel::SourceFilter::Project);
    CHECK(project->GetConfig().SelectedPlugins.empty() && !panel.HasDraftChanges() && !panel.NeedsRestart());
    const auto source = FileSystem::ReadTextFile(mover/"Runtime/Mover.cpp");
    CHECK(!panel.CreatePlugin("Other","project.other","Mover"));
    CHECK(!panel.CreatePlugin("Other","project.mover","Other"));
    CHECK(FileSystem::ReadTextFile(mover/"Runtime/Mover.cpp")==source && !std::filesystem::exists(root/"Plugins/Other"));
    CHECK(panel.TogglePluginDraft("project.mover",true));
    panel.SetRefreshCallback([] { throw std::runtime_error("refresh probe"); });
    CHECK(panel.CreatePlugin("Second","project.second","Second"));
    CHECK(panel.GetErrorMessage().find("Plugin created")!=std::string::npos);
    CHECK(panel.GetSelectionModel()->IsDraft("project.mover") && !panel.GetSelectionModel()->IsDraft("project.second"));
    CHECK(project->GetConfig().SelectedPlugins.empty() && FileSystem::ReadTextFile(file)==before);
    const auto broken = root/"BrokenTemplate";
    std::filesystem::copy(argv[2],broken,std::filesystem::copy_options::recursive);
    CHECK(FileSystem::WriteTextFileAtomically(broken/"Plugin.axleplugin.in","invalid JSON"));
    panel.SetPluginTemplateDirectory(broken);
    CHECK(!panel.CreatePlugin("Broken","project.broken","Broken"));
    CHECK(!std::filesystem::exists(root/"Plugins/Broken"));
    CHECK(std::filesystem::is_empty(root/"Intermediate/Plugins/Creation"));
    const auto lock = CreateFileW((root/"Intermediate/Plugins/CreatePlugin.lock").c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,
        0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
    CHECK(lock!=INVALID_HANDLE_VALUE);
    panel.SetPluginTemplateDirectory(argv[2]);
    const bool concurrent = panel.CreatePlugin("Locked","project.locked","Locked");
    CloseHandle(lock);
    CHECK(!concurrent && !std::filesystem::exists(root/"Plugins/Locked"));
    // Compile the actual native-created Runtime and dependent Editor modules.
    PluginBuildRequest request; request.ProjectDirectory=root; request.CMake=argv[1]; request.SdkDirectory=AXLE_PLUGIN_SDK_DIRECTORY;
    request.Generator=AXLE_PLUGIN_CMAKE_GENERATOR; request.GeneratorToolset=AXLE_PLUGIN_CMAKE_TOOLSET; request.GeneratorInstance=AXLE_PLUGIN_CMAKE_INSTANCE;
    request.Plan=Plugin::CreateBuildPlan(Plugin::Resolve(panel.GetCatalog(),{"project.mover"},Plugin::ProductKind::Editor),
        Plugin::ProductKind::Editor,AXLE_PLUGIN_BUILD_CONFIGURATION,AXLE_PLUGIN_SDK_FINGERPRINT,AXLE_PLUGIN_TOOLCHAIN_FINGERPRINT);
    std::atomic_bool cancel{false}; const auto built=BuildPlugins(request,cancel);
    if (!built.Success) std::cerr << built.Error << '\n';
    CHECK(built.Success && std::filesystem::exists(built.GenerationDirectory/"Mover_Runtime.dll") && std::filesystem::exists(built.GenerationDirectory/"Mover_Editor.dll"));
    std::cout << "Native project creation, atomic failure preservation, panel state and actual SDK DLL build PASS\n";
    std::cout << "Creation fixture: " << PathToUtf8(root) << '\n';
    return 0;
}
