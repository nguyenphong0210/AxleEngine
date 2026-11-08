#include "TestWorkspace.hpp"
#include "EditorStartup.hpp"
#include <Axle/Scene.hpp>
#include <Axle/Components.hpp>
#include <Axle/UUID.hpp>
#include <Axle/UIPalettePanel.hpp>
#include <imgui.h>
#include <fstream>
#include <iostream>

#define CHECK(condition, message) do { if (!(condition)) { std::cerr << message << '\n'; return 1; } } while (false)

int main() {
    Axle::Tests::InitializeWorkspace();
    using Axle::Project;
    const auto workspace = std::filesystem::current_path();
    const auto sandboxPath = std::filesystem::path(AXLE_TEST_SOURCE_DIR) / "Samples/Sandbox/Sandbox.axleproj";
    auto sandbox = Axle::OpenEditorStartupProject(Axle::ResolveEditorStartupProject({}, sandboxPath));
    CHECK(sandbox && sandbox->GetConfig().Name == "Sandbox", "Development default opens the renamed Sandbox project");
    CHECK(sandbox->GetConfig().SelectedPlugins.empty(), "Sandbox opens with no optional plugins selected");
    CHECK(sandbox->GetConfig().StartScene == "scenes/Test.axlescene" &&
          Project::GetAssetDirectory() == sandboxPath.parent_path() / "assets",
          "Sandbox keeps its authored scene and asset root");
    Project::SetActive(nullptr);
    Axle::FileSystem::SetAssetDirectory(workspace / "assets");
    const auto source = workspace / "startup-fixtures" / std::to_string(static_cast<uint64_t>(Axle::UUID{}));
    std::filesystem::create_directories(source);
    std::ofstream(source / "AxleEditor.axleproj") << "Project:\n  Name: Must not auto-open\n  StartScene: scenes/Sample.axlescene\n  AssetDirectory: unrelated-assets\n";
    std::filesystem::current_path(source);
    const auto defaultProject = source / "AxleEditor.axleproj";
    CHECK(Axle::ResolveEditorStartupProject({}, defaultProject) == defaultProject, "Available configured development project is selected");
    CHECK(Axle::ResolveEditorStartupProject({}, defaultProject, true).empty(), "Empty-project mode suppresses development sample");
    CHECK(Axle::ResolveEditorStartupProject({}, source / "missing.axleproj").empty(), "Missing development default falls back to empty");
    CHECK(Axle::ResolveEditorStartupProject({}, source).empty(), "A directory cannot be a startup project");
    const auto explicitProject = source / "explicit.axleproj";
    CHECK(Axle::ResolveEditorStartupProject(explicitProject, defaultProject) == explicitProject, "Explicit project takes priority over development default");
    CHECK(Axle::ResolveEditorStartupProject(explicitProject, defaultProject, true) == explicitProject, "Explicit project also takes priority over empty-project convenience flag");
    CHECK(!Axle::OpenEditorStartupProject(explicitProject), "Missing explicit project fails rather than selecting the sample");
    CHECK(Axle::ResolveEditorStartupProject({}, {}).empty(), "Disabling the default keeps startup empty without nearby discovery");
    auto project = Axle::OpenEditorStartupProject();
    CHECK(project && project->GetConfig().StartScene.empty(), "Default startup has no sample start scene");
    const auto firstRoot = Project::GetProjectDirectory();
    CHECK(firstRoot != workspace && std::filesystem::is_empty(Project::GetAssetDirectory()), "Untitled assets are empty and outside the checkout workspace");
    std::ofstream(Project::GetAssetDirectory() / "authored.txt") << "preserve authored data";
    auto second = Axle::OpenEditorStartupProject();
    const auto secondRoot = Project::GetProjectDirectory();
    CHECK(firstRoot != secondRoot && std::filesystem::is_empty(Project::GetAssetDirectory()), "Each default startup is fresh");
    const auto missing = source / "missing-project-root";
    project = Project::New(missing);
    Project::SetActive(project);
    CHECK(Project::GetAssetDirectory() == missing / "assets", "A missing project asset root cannot select another project's assets");
    CHECK(Axle::FileSystem::GetAssetDirectory() == missing / "assets", "Explicit FileSystem root remains authoritative when absent");
    Project::SetActive(Project::New(firstRoot));
    std::filesystem::create_directories(firstRoot / "Plugins/Mover");
    std::ofstream(firstRoot / "Plugins/Mover/Mover.cpp") << "authored plugin source";
    const auto target = source / "SavedProject" / "Project.axleproj";
    CHECK(Project::SaveActive(target), "Save As preserves untitled assets");
    CHECK(std::filesystem::exists(target.parent_path() / "assets/authored.txt"), "Saved project owns its asset copy");
    CHECK(std::filesystem::exists(target.parent_path() / "Plugins/Mover/Mover.cpp"), "Save As preserves project plugin sources");
    CHECK(Axle::FileSystem::GetAssetDirectory() == target.parent_path() / "assets", "Save As updates active asset root");
    CHECK(Project::SaveActive(target), "Saving an existing project is repeatable");
    const auto blockedTarget = source / "OccupiedProject" / "Project.axleproj";
    std::filesystem::create_directories(blockedTarget.parent_path() / "assets");
    std::ofstream(blockedTarget.parent_path() / "assets/existing.txt") << "keep existing data";
    CHECK(!Project::SaveActive(blockedTarget), "Save As rejects nonempty destination assets");
    CHECK(Project::GetProjectDirectory() == target.parent_path(), "Rejected Save As keeps the active project root");
    CHECK(!std::filesystem::exists(blockedTarget), "Rejected Save As does not write the target project");
    CHECK(!Project::SaveActive(target.parent_path() / "assets/nested/Project.axleproj"), "Save As cannot recursively copy its own asset tree");
    auto loaded = Axle::OpenEditorStartupProject(target.lexically_relative(source));
    CHECK(loaded && loaded->GetConfig().StartScene.empty(), "Only an explicit project is loaded");
    Axle::Scene emptyScene;
    CHECK(emptyScene.GetRegistry().view<Axle::TagComponent>().empty(), "An empty authored scene has no injected camera, light or vehicle");
    // Drawing the actual authoring panel is passive, including after canvas deletion.
    auto* context = ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(800, 600);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID((ImTextureID)1);
    Axle::UIPalettePanel palette;
    palette.SetContext(&emptyScene);
    auto drawLibrary = [&] {
        ImGui::NewFrame();
        palette.OnImGuiRender();
        ImGui::Render();
    };
    for (int frame = 0; frame < 5; ++frame) drawLibrary();
    CHECK(emptyScene.GetRegistry().view<Axle::TagComponent>().empty(), "Visible UI Library cannot auto-create entities");
    auto canvas = Axle::UIPalettePanel::GetOrCreateCanvasEntity(&emptyScene);
    CHECK(canvas && canvas.HasComponent<Axle::UICanvasComponent>(), "Explicit UI authoring still creates a canvas");
    const auto document = canvas.GetComponent<Axle::UICanvasComponent>().Document;
    CHECK(Axle::UIPalettePanel::SpawnWidget(document.get(), Axle::UIWidgetType::Button), "Explicit widget creation still works");
    drawLibrary();
    CHECK(emptyScene.GetRegistry().view<Axle::TagComponent>().size() == 1, "Drawing an authored canvas adds no entities");
    CHECK(Axle::UIPalettePanel::GetOrCreateCanvasEntity(&emptyScene) == canvas, "Explicit authoring reuses the existing canvas");
    emptyScene.DestroyEntity(canvas);
    for (int frame = 0; frame < 5; ++frame) drawLibrary();
    CHECK(emptyScene.GetRegistry().view<Axle::TagComponent>().empty(), "Deleted UI canvas stays deleted while library is visible");
    ImGui::DestroyContext(context);
    Project::SetActive(nullptr);
    Axle::FileSystem::SetAssetDirectory(workspace / "assets");
    std::filesystem::current_path(workspace);
    // Only remove the unique temporary workspaces created by this test.
    const auto ownedParent = std::filesystem::temp_directory_path() / "AxleEngine" / "UntitledProjects";
    for (const auto& path : {firstRoot, secondRoot}) {
        CHECK(path.parent_path() == ownedParent, "Temporary cleanup stays inside the owned project container");
        std::filesystem::remove_all(path);
    }
    std::cout << "Empty startup, explicit roots and Save As preservation: PASS\n";
    return 0;
}
