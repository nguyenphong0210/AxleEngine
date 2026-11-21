#include "TestWorkspace.hpp"
#include "EditorStartup.hpp"
#include <Axle/Scene.hpp>
#include <Axle/Components.hpp>
#include <Axle/UUID.hpp>
#include <Axle/ProjectSerializer.hpp>
#include <Axle/UIPalettePanel.hpp>
#include <imgui.h>
#include <fstream>
#include <iostream>

#define CHECK(condition, message) do { if (!(condition)) { std::cerr << message << '\n'; return 1; } } while (false)

int main() {
    Axle::Tests::InitializeWorkspace();
    using Axle::Project;
    using Axle::ProjectSerializer;
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
    // Verify ProjectRenderSettings roundtrip, clamping, and legacy omission preservation (M15-U10-03.1..03.3)
    {
        const auto rtPath = source / "RenderSettingsParity.axleproj";
        auto proj = Project::New(source / "RenderSettingsProj");
        auto& rs = proj->GetConfig().RenderSettings;
        rs.EnableShadows = false;
        rs.EnableSkybox = false;
        rs.EnableGTAO = false;
        rs.EnableBloom = false;
        rs.FogOverride = 1;
        rs.FogQuality = 3;
        rs.FogTemporalWeight = 0.75f;
        rs.DDGIOverride = 0;
        rs.EnableToneMapping = true;
        rs.ToneMappingMode = 2;
        rs.Exposure = 2.5f;
        rs.EnableAutoExposure = true;
        rs.AutoExposureMin = 0.25f;
        rs.AutoExposureMax = 12.0f;
        rs.AutoExposureSpeed = 4.5f;
        rs.AutoExposureKeyValue = 0.22f;
        rs.EnableFXAA = false;
        rs.EnableTAA = true;
        rs.MSAASamples = 4;
        rs.EnableSSR = false;
        rs.EnablePlanarReflections = true;
        rs.PlanarReflectionResolutionScale = 0.75f;
        rs.PlanarReflectionStrength = 1.25f;
        rs.PlanarReflectionMaxRoughness = 0.6f;
        rs.ReflectionCaptureMaxProbesPerFrame = 4;
        rs.ReflectionCaptureMaxFacesPerFrame = 18;
        rs.ReflectionCaptureCacheBudgetBytes = 128ull * 1024ull * 1024ull;
        rs.QualityPreset = 3;
        rs.GTAOSliceCount = 8;
        rs.GTAOStepCount = 8;
        rs.ContactShadowSteps = 24;
        rs.SSRMaxSteps = 96;
        CHECK(ProjectSerializer(proj).Serialize(rtPath), "Save project with RenderSettings");

        auto loadedProj = std::make_shared<Project>();
        CHECK(ProjectSerializer(loadedProj).Deserialize(rtPath), "Reload project with RenderSettings");
        const auto& lrs = loadedProj->GetConfig().RenderSettings;
        CHECK(loadedProj->GetConfig().HasAuthoredRenderSettings &&
              !lrs.EnableShadows && !lrs.EnableSkybox && !lrs.EnableGTAO && !lrs.EnableBloom &&
              lrs.FogOverride == 1 && lrs.FogQuality == 3 && std::abs(lrs.FogTemporalWeight - 0.75f) < 1e-4f &&
              lrs.DDGIOverride == 0 && lrs.EnableToneMapping && lrs.ToneMappingMode == 2 &&
              std::abs(lrs.Exposure - 2.5f) < 1e-4f && lrs.EnableAutoExposure &&
              std::abs(lrs.AutoExposureMin - 0.25f) < 1e-4f && std::abs(lrs.AutoExposureMax - 12.0f) < 1e-4f &&
              !lrs.EnableFXAA && lrs.EnableTAA && lrs.MSAASamples == 4 &&
              !lrs.EnableSSR && lrs.EnablePlanarReflections &&
              std::abs(lrs.PlanarReflectionResolutionScale - 0.75f) < 1e-4f &&
              lrs.ReflectionCaptureMaxProbesPerFrame == 4 && lrs.ReflectionCaptureMaxFacesPerFrame == 18 &&
              lrs.ReflectionCaptureCacheBudgetBytes == 128ull * 1024ull * 1024ull &&
              lrs.QualityPreset == 3 && lrs.GTAOSliceCount == 8 && lrs.GTAOStepCount == 8 &&
              lrs.ContactShadowSteps == 24 && lrs.SSRMaxSteps == 96,
              "ProjectSerializer preserves all U10-B1 Environment/Exposure/AA/Reflection & U10-C preset settings cleanly");
        std::filesystem::remove(rtPath);
    }

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
