#include "PluginBuild.hpp"
#include "PluginSDKConfig.hpp"
#include "PluginEditorServices.hpp"
#include <imgui.h>
#include <Axle/PluginServices.hpp>
#include <Axle/Scene.hpp>
#include <Axle/SceneSerializer.hpp>
#include <Axle/SceneDataDocument.hpp>
#include <Axle/AssemblySerializer.hpp>
#include <Axle/SceneExtraction.hpp>
#include <Axle/ExtensionDocument.hpp>
#include <Axle/Entity.hpp>
#include <Axle/Components.hpp>
#include <Axle/PhysicsEngine.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/Project.hpp>
#include <Axle/Log.hpp>
#include <Axle/FileSystem.hpp>
#include <Axle/UUID.hpp>
#include <chrono>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#define CHECK(value) do { if (!(value)) { std::cerr << "FAIL " << __LINE__ << ": " << #value << '\n'; return 1; } } while(false)
struct HookCounts { int Clear = 0, Destroy = 0; };
struct CountingExtension final : Axle::SceneExtension {
    std::shared_ptr<HookCounts> Counts;
    explicit CountingExtension(std::shared_ptr<HookCounts> counts) : Counts(std::move(counts)) {}
    void Clear() override { ++Counts->Clear; }
    void Destroy(Axle::Entity) override { ++Counts->Destroy; }
    bool CanCopy(Axle::Entity) const override { return true; }
    bool Copy(Axle::Entity, const Axle::SceneExtension&, Axle::Entity, const std::unordered_map<std::uint64_t,std::uint64_t>&) override { return true; }
    bool Serialize(Axle::Entity, std::ostream&) const override { return true; }
    bool Deserialize(Axle::Entity, const std::string&, const std::vector<std::string>&) override { return false; }
    bool Ready(std::vector<std::string>&) const override { return true; }
};
int Run(int argc, wchar_t** argv) {
    CHECK(argc == 3);
    Axle::Log::Init(); Axle::JobSystem::Init(1); Axle::PhysicsEngine::Init();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO(); io.IniFilename = nullptr; io.DisplaySize = {800,600}; io.DeltaTime = 1.0f / 60;
    unsigned char* pixels; int width, height; io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    struct EngineCleanup { ~EngineCleanup() { ImGui::DestroyContext(); Axle::PhysicsEngine::Shutdown(); Axle::JobSystem::Shutdown(); } } engineCleanup;
    const auto root = std::filesystem::temp_directory_path() / "AxleEngine" / ("Services-" + std::to_string(static_cast<std::uint64_t>(Axle::UUID{})));
    std::filesystem::create_directories(root / "Plugins/Custom");
    bool done = false;
    struct Cleanup { std::filesystem::path Root; bool& Done; ~Cleanup() { if (Done) { std::error_code ec; std::filesystem::remove_all(Root, ec); } } } cleanup{root, done};
    const auto plugin = root / "Plugins/Custom";
    std::filesystem::copy(argv[2], plugin, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
    std::ofstream(plugin / "Custom.axleplugin") << R"({"schema":1,"id":"custom.plugin","version":"1.0.0","name":"Custom","modules":[{"name":"CustomPlugin_Base","kind":"runtime","sourceRoot":"Base"},{"name":"CustomPlugin_Runtime","kind":"runtime","sourceRoot":"Runtime","dependencies":["CustomPlugin_Base"]},{"name":"CustomPlugin_Editor","kind":"editor","sourceRoot":"Editor","dependencies":["CustomPlugin_Runtime"]}]})";
    Axle::PluginBuildRequest request;
    request.CMake = argv[1]; request.ProjectDirectory = root; request.SdkDirectory = AXLE_PLUGIN_SDK_DIRECTORY;
    request.Generator = AXLE_PLUGIN_CMAKE_GENERATOR; request.GeneratorToolset = AXLE_PLUGIN_CMAKE_TOOLSET; request.GeneratorInstance = AXLE_PLUGIN_CMAKE_INSTANCE;
    const auto catalog = Axle::Plugin::Discover({{root / "Plugins"}, {root / "Plugins"}});
    const auto resolution = Axle::Plugin::Resolve(catalog, {"custom.plugin"}, Axle::Plugin::ProductKind::Tests);
    request.Plan = Axle::Plugin::CreateBuildPlan(resolution, Axle::Plugin::ProductKind::Tests, AXLE_PLUGIN_BUILD_CONFIGURATION, AXLE_PLUGIN_SDK_FINGERPRINT, AXLE_PLUGIN_TOOLCHAIN_FINGERPRINT);
    std::atomic_bool cancel{false}; const auto built = Axle::BuildPlugins(request, cancel);
    if (!built.Success) std::cerr << built.Error << " log=" << Axle::Plugin::PathUtf8(built.LogPath) << '\n';
    CHECK(built.Success);
    Axle::Project::SetActive(Axle::Project::New(root));
    CHECK(Axle::Project::GetActive());
    auto edit = std::make_shared<Axle::Scene>();
    Axle::PluginRuntime::RegistrationRegistry registrations;
    Axle::PluginServiceSession services(registrations); std::string error;
    CHECK(services.Bind(error));
    Axle::PluginEditorServices editorServices(services, registrations); CHECK(editorServices.Bind(error));
    Axle::PluginRuntime::LoadRequest load;
    load.ModulePath = built.GenerationDirectory / "CustomPlugin_Runtime.dll"; load.ReceiptPath = built.GenerationDirectory / "CustomPlugin_Runtime.build.receipt";
    const auto& artifact = *std::find_if(request.Plan.Artifacts.begin(), request.Plan.Artifacts.end(), [](const auto& item) { return item.ModuleName == "CustomPlugin_Runtime"; });
    load.Expected.PluginId = artifact.PluginId; load.Expected.ModuleName = artifact.ModuleName; load.Expected.Kind = artifact.Kind;
    load.Expected.Product = request.Plan.Product; load.Expected.Configuration = request.Plan.Configuration;
    load.Expected.SourceRoot = artifact.SourceRoot; load.Expected.Fingerprint = request.Plan.Fingerprint;
    load.Expected.SdkFingerprint = request.Plan.SdkFingerprint; load.Expected.ToolchainFingerprint = request.Plan.ToolchainFingerprint;
    services.Attach(load);
    auto entity = edit->CreateEntityWithUUID(Axle::UUID(100), "Mover");
    {
        const auto setup = LoadLibraryExW(load.ModulePath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS); CHECK(setup);
        const auto startup = reinterpret_cast<void(*)(std::uint64_t,bool)>(GetProcAddress(setup, "ax_fixture_startup"));
        const auto count = reinterpret_cast<int(*)()>(GetProcAddress(setup, "ax_fixture_components"));
        const auto contexts = reinterpret_cast<int(*)()>(GetProcAddress(setup, "ax_fixture_contexts"));
        CHECK(startup && count && contexts); startup(edit->GetInstanceId(), true);
        Axle::PluginRuntime::NativePlugin rejected;
        CHECK(!rejected.Load(load, registrations, error));
        CHECK(registrations.Size() == 0 && count() == 0 && contexts() == 0);
        startup(0, false); FreeLibrary(setup);
        CHECK(!GetModuleHandleW(load.ModulePath.c_str()));
    }
    auto baseLoad = load; baseLoad.ModulePath = built.GenerationDirectory / "CustomPlugin_Base.dll";
    baseLoad.ReceiptPath = built.GenerationDirectory / "CustomPlugin_Base.build.receipt";
    baseLoad.Expected.ModuleName = "CustomPlugin_Base";
    baseLoad.Expected.SourceRoot = std::find_if(request.Plan.Artifacts.begin(), request.Plan.Artifacts.end(), [](const auto& item) { return item.ModuleName == "CustomPlugin_Base"; })->SourceRoot;
    Axle::PluginRuntime::NativePlugin baseModule; CHECK(baseModule.Load(baseLoad, registrations, error));
    Axle::PluginRuntime::NativePlugin module;
    CHECK(module.Load(load, registrations, error));
    auto editorLoad = load; editorLoad.ModulePath = built.GenerationDirectory / "CustomPlugin_Editor.dll";
    editorLoad.ReceiptPath = built.GenerationDirectory / "CustomPlugin_Editor.build.receipt";
    editorLoad.Expected.ModuleName = "CustomPlugin_Editor"; editorLoad.Expected.Kind = Axle::Plugin::ModuleKind::Editor;
    editorLoad.Expected.SourceRoot = std::find_if(request.Plan.Artifacts.begin(), request.Plan.Artifacts.end(), [](const auto& item) { return item.ModuleName == "CustomPlugin_Editor"; })->SourceRoot;
    Axle::PluginRuntime::NativePlugin editorModule; CHECK(editorModule.Load(editorLoad, registrations, error));
    const auto editorNative = GetModuleHandleW(editorLoad.ModulePath.c_str()); CHECK(editorNative);
    const auto context = reinterpret_cast<const void*(*)()>(GetProcAddress(editorNative, "ax_fixture_editor_context"));
    const auto runtimeTable = reinterpret_cast<const void*(*)()>(GetProcAddress(editorNative, "ax_fixture_editor_runtime_table"));
    const auto draws = reinterpret_cast<int(*)()>(GetProcAddress(editorNative, "ax_fixture_editor_draws"));
    CHECK(context && runtimeTable && draws && context() == ImGui::GetCurrentContext() && runtimeTable() == AxleRuntimeHost_GetV1());
    const auto native = GetModuleHandleW(load.ModulePath.c_str()); CHECK(native);
    const auto components = reinterpret_cast<int(*)()>(GetProcAddress(native, "ax_fixture_components"));
    const auto contexts = reinterpret_cast<int(*)()>(GetProcAddress(native, "ax_fixture_contexts"));
    const auto observedBase = reinterpret_cast<int(*)(const void*)>(GetProcAddress(native, "ax_fixture_observed_base"));
    const auto totalStarts = reinterpret_cast<int(*)()>(GetProcAddress(native, "ax_fixture_total_starts"));
    const auto totalStops = reinterpret_cast<int(*)()>(GetProcAddress(native, "ax_fixture_total_stops"));
    const auto dependencyStops = reinterpret_cast<int(*)()>(GetProcAddress(native, "ax_fixture_dependency_stops"));
    const auto reference = reinterpret_cast<std::uint64_t(*)(const void*)>(GetProcAddress(native, "ax_fixture_reference"));
    const auto starts = reinterpret_cast<int(*)(const void*)>(GetProcAddress(native, "ax_fixture_starts"));
    const auto stops = reinterpret_cast<int(*)(const void*)>(GetProcAddress(native, "ax_fixture_stops"));
    const auto phase = reinterpret_cast<int(*)(const void*,int)>(GetProcAddress(native, "ax_fixture_phase"));
    const auto submit = reinterpret_cast<bool(*)(std::uint64_t)>(GetProcAddress(native, "ax_fixture_submit"));
    const auto completed = reinterpret_cast<int(*)()>(GetProcAddress(native, "ax_fixture_completed"));
    const auto jobsDestroyed = reinterpret_cast<int(*)()>(GetProcAddress(native, "ax_fixture_jobs_destroyed"));
    const auto startReference = reinterpret_cast<std::uint64_t(*)(const void*)>(GetProcAddress(native, "ax_fixture_start_reference"));
    const auto startPhysics = reinterpret_cast<bool(*)(const void*)>(GetProcAddress(native, "ax_fixture_start_physics"));
    const auto mutate = reinterpret_cast<bool(*)(std::uint64_t,std::uint64_t)>(GetProcAddress(native, "ax_fixture_submit_mutator"));
    const auto workAccess = reinterpret_cast<int(*)()>(GetProcAddress(native, "ax_fixture_work_access"));
    const auto workOnOwner = reinterpret_cast<bool(*)()>(GetProcAddress(native, "ax_fixture_work_on_owner"));
    const auto completionAccess = reinterpret_cast<bool(*)()>(GetProcAddress(native, "ax_fixture_completion_access"));
    CHECK(components && contexts && reference && starts && stops && phase && submit && completed && jobsDestroyed && startReference && startPhysics && mutate && workAccess && workOnOwner && completionAccess && observedBase && totalStarts && totalStops && dependencyStops);
    const auto baseTable = reinterpret_cast<const void*(*)()>(GetProcAddress(GetModuleHandleW(baseLoad.ModulePath.c_str()), "ax_fixture_base_runtime_table"));
    CHECK(baseTable && baseTable() == AxleRuntimeHost_GetV1());
    const auto& api = services.Services(); const auto owner = module.GetHostApi()->owner_id;
    CHECK(api.Address(api.User, owner, Axle::SDK::ServiceAddress::Project, 0) == Axle::Project::GetActive().get());
    CHECK(api.Address(api.User, owner, Axle::SDK::ServiceAddress::Jobs, 0) == Axle::JobSystem::GetScheduler());
    const Axle::SDK::EntityRef ref{edit->GetInstanceId(), 100};
    const std::string payload = "3 100 \"payload with nested Children: and Unicode text\"";
    CHECK(api.SetComponent(api.User, owner, ref, "custom.plugin/Mover", payload.data(), static_cast<std::uint32_t>(payload.size())));
    auto* data = api.GetComponent(api.User, owner, ref, "custom.plugin/Mover"); CHECK(data && components() == 1);
    ImGui::NewFrame(); ImGui::SetNextWindowSize({500,400}); ImGui::Begin("Plugin inspector");
    editorServices.DrawComponents(entity); ImGui::End(); ImGui::Render();
    CHECK(draws() == 1 && ImGui::GetDrawData()->CmdListsCount > 0);
    auto duplicated = edit->DuplicateEntity(entity); CHECK(duplicated);
    const auto duplicateRef = Axle::SDK::EntityRef{edit->GetInstanceId(), static_cast<std::uint64_t>(duplicated.GetUUID())};
    CHECK(reference(api.GetComponent(api.User, owner, duplicateRef, "custom.plugin/Mover")) == duplicateRef.Entity);
    edit->DestroyEntity(duplicated); CHECK(components() == 1);
    auto child = edit->CreateEntityWithUUID(Axle::UUID(200), "Child"); child.SetParent(entity, false);
    auto set = [&](std::uint64_t scene, std::uint64_t id, const std::string& text) { return api.SetComponent(api.User, owner, {scene,id}, "custom.plugin/Mover", text.data(), static_cast<std::uint32_t>(text.size())); };
    CHECK(set(ref.Scene, 100, "3 200 \"Root\"") && set(ref.Scene, 200, "1 100 \"Child\""));
    auto tree = edit->DuplicateEntity(entity); CHECK(tree && tree.GetChildren().size() == 1);
    const auto treeChild = tree.GetChildren().front();
    CHECK(reference(api.GetComponent(api.User, owner, {ref.Scene, static_cast<std::uint64_t>(tree.GetUUID())}, "custom.plugin/Mover")) == static_cast<std::uint64_t>(treeChild.GetUUID()));
    CHECK(reference(api.GetComponent(api.User, owner, {ref.Scene, static_cast<std::uint64_t>(treeChild.GetUUID())}, "custom.plugin/Mover")) == static_cast<std::uint64_t>(tree.GetUUID()));
    edit->DestroyEntity(tree); CHECK(components() == 2);
    CHECK(set(ref.Scene, 200, "1 100 \"fail-clone\""));
    CHECK(!edit->DuplicateEntity(entity) && components() == 2 && edit->GetRegistry().view<Axle::IDComponent>().size() == 2);
    edit->DestroyEntity(child); CHECK(set(ref.Scene, 100, payload));
    auto bare = edit->CreateEntity("No transform"); bare.RemoveComponent<Axle::TransformComponent>();
    Axle::SDK::Transform missing{}; const Axle::SDK::EntityRef bareRef{ref.Scene, static_cast<std::uint64_t>(bare.GetUUID())};
    CHECK(!api.GetTransform(api.User, owner, bareRef, &missing) && !api.SetTransform(api.User, owner, bareRef, &missing)); edit->DestroyEntity(bare);
    {
        auto badStart = Axle::Scene::Copy(edit); CHECK(badStart && set(badStart->GetInstanceId(), 100, "3 100 \"throw-start\""));
        badStart->OnRuntimeStart(); CHECK(!badStart->IsRuntimeActive() && badStart->IsSimulationFaulted());
        auto badUpdate = Axle::Scene::Copy(edit); CHECK(badUpdate && set(badUpdate->GetInstanceId(), 100, "3 100 \"throw-update\""));
        badUpdate->OnRuntimeStart(); CHECK(badUpdate->IsRuntimeActive()); badUpdate->StepSimulation(1.0f / 60);
        const auto* badData = api.GetComponent(api.User, owner, {badUpdate->GetInstanceId(),100}, "custom.plugin/Mover");
        CHECK(!badUpdate->IsRuntimeActive() && badUpdate->IsSimulationFaulted() && badData && phase(badData, 1) == 0);
    }
    entity.AddComponent<Axle::CollisionShape3D>(); entity.AddComponent<Axle::RigidBody3D>();
    CHECK(api.SetComponent(api.User, owner, ref, "custom.plugin/ZBase", nullptr, 0));
    const auto serialized = Axle::SceneSerializer(edit.get()).SerializeToString(); CHECK(serialized.find("AxlePlugin_") != std::string::npos);
    auto reopened = std::make_shared<Axle::Scene>(); CHECK(Axle::SceneSerializer(reopened.get()).DeserializeFromString(serialized));
    CHECK(api.GetComponent(api.User, owner, {reopened->GetInstanceId(), 100}, "custom.plugin/Mover"));
    auto dataOnly = std::make_shared<Axle::SceneData>(); CHECK(Axle::SceneDataDocument::Deserialize(serialized, *dataOnly));
    CHECK(Axle::SceneDataDocument::Serialize(*dataOnly).find("AxlePlugin_") != std::string::npos);
    const auto assemblyFile = root / "Mover.axle";
    CHECK(Axle::AssemblySerializer::Serialize(assemblyFile, entity));
    auto assemblyData = Axle::AssemblySerializer::LoadAssemblyData(assemblyFile); CHECK(assemblyData.IsValid && assemblyData.RootNode.AuthoredUUID == 100);
    CHECK(Axle::AssemblySerializer::SaveAssemblyData(assemblyFile, assemblyData));
    auto assemblyScene = std::make_shared<Axle::Scene>();
    auto instantiated = Axle::AssemblySerializer::Deserialize(assemblyFile, assemblyScene.get()); CHECK(instantiated);
    const Axle::SDK::EntityRef instantiatedRef{assemblyScene->GetInstanceId(), static_cast<std::uint64_t>(instantiated.GetUUID())};
    CHECK(reference(api.GetComponent(api.User, owner, instantiatedRef, "custom.plugin/Mover")) == instantiatedRef.Entity);
    {
        const auto validText = Axle::FileSystem::ReadTextFile(assemblyFile);
        const auto invalidFile = root / "Invalid.axle";
        const auto before = assemblyScene->GetRegistry().view<Axle::IDComponent>().size();
        for (const auto& invalidText : {validText + "  - Entity: 100\n", std::string("RootEntityID: 0\nEntities:\n  - Entity: 0\n")}) {
            std::ofstream(invalidFile) << invalidText;
            CHECK(!Axle::AssemblySerializer::Deserialize(invalidFile, assemblyScene.get()));
            CHECK(!Axle::AssemblySerializer::LoadAssemblyData(invalidFile).IsValid);
            CHECK(assemblyScene->GetRegistry().view<Axle::IDComponent>().size() == before);
        }
    }
    auto play = Axle::Scene::Copy(edit); CHECK(play);
    auto playEntity = play->GetEntityByUUID(Axle::UUID(100));
    playEntity.AddComponent<Axle::CollisionShape3D>(); playEntity.AddComponent<Axle::RigidBody3D>();
    Axle::PhysicsSettings settings; settings.Gravity = {0,0,0};
    auto physics = std::make_unique<Axle::PhysicsScene>(settings);
    play->OnRuntimeStart(physics.get()); CHECK(play->IsRuntimeActive());
    const Axle::SDK::EntityRef playRef{play->GetInstanceId(), 100}; auto* playData = api.GetComponent(api.User, owner, playRef, "custom.plugin/Mover"); CHECK(playData && starts(playData) == 1);
    CHECK(api.Address(api.User, owner, Axle::SDK::ServiceAddress::Physics, playRef.Scene) == physics.get());
    CHECK(api.Address(api.User, owner, Axle::SDK::ServiceAddress::Components, playRef.Scene) == &play->GetRegistry());
    play->StepSimulation(1.0f / 60, physics.get());
    for (int i = 0; i < 5; ++i) CHECK(phase(playData, i) == 1);
    CHECK(observedBase(playData) == 1);
    CHECK(physics->GetLinearVelocity(playEntity.GetComponent<Axle::RigidBody3D>().RuntimeBodyID).x > 2.9f);
    CHECK(playEntity.GetComponent<Axle::TransformComponent>().Translation.x > 0.04f);
    CHECK(entity.GetComponent<Axle::TransformComponent>().Translation.x == 0);
    auto snapshot = Axle::ExtractRenderWorld(*play); CHECK(snapshot.DebugLines.size() == 1 && snapshot.DebugLines.front().To.x == 3);
    const auto startsBeforeImport = totalStarts(), stopsBeforeImport = totalStops();
    auto runningImport = Axle::AssemblySerializer::Deserialize(assemblyFile, play.get(), physics.get()); CHECK(runningImport);
    const Axle::SDK::EntityRef importRef{play->GetInstanceId(), static_cast<std::uint64_t>(runningImport.GetUUID())};
    const auto* importData = api.GetComponent(api.User, owner, importRef, "custom.plugin/Mover");
    CHECK(importData && starts(importData) == 1 && stops(importData) == 0 && startReference(importData) == importRef.Entity && startPhysics(importData));
    CHECK(totalStarts() == startsBeforeImport + 1 && totalStops() == stopsBeforeImport);
    play->DestroyEntity(runningImport, physics.get());
    CHECK(set(ref.Scene, 100, "3 100 \"fail-clone\""));
    const auto failedImportFile = root / "FailedImport.axle";
    CHECK(Axle::AssemblySerializer::Serialize(failedImportFile, entity)); CHECK(set(ref.Scene, 100, payload));
    const auto startsBeforeFailure = totalStarts(), stopsBeforeFailure = totalStops();
    const auto entitiesBeforeFailure = play->GetRegistry().view<Axle::IDComponent>().size();
    const auto bodiesBeforeFailure = physics->GetNumBodies();
    CHECK(!Axle::AssemblySerializer::Deserialize(failedImportFile, play.get(), physics.get()));
    CHECK(play->IsRuntimeActive() && totalStarts() == startsBeforeFailure && totalStops() == stopsBeforeFailure);
    CHECK(play->GetRegistry().view<Axle::IDComponent>().size() == entitiesBeforeFailure && physics->GetNumBodies() == bodiesBeforeFailure);
    CHECK(mutate(playRef.Scene, playRef.Entity)); Axle::JobSystem::WaitForAll(); services.Drain(); Axle::JobSystem::PumpMainThreadCompletions();
    CHECK(workOnOwner() && workAccess() == 0 && completionAccess());
    CHECK(submit(playRef.Scene)); Axle::JobSystem::WaitForAll(); services.Drain();
    CHECK(completed() == 0); Axle::JobSystem::PumpMainThreadCompletions(); CHECK(completed() == 1 && jobsDestroyed() == 1);
    CHECK(submit(playRef.Scene)); Axle::JobSystem::WaitForAll(); services.Drain();
    services.Quiesce(); CHECK(!submit(playRef.Scene));
    Axle::JobSystem::PumpMainThreadCompletions(); CHECK(completed() == 1 && jobsDestroyed() == 2);
    services.ResumeJobs(); CHECK(submit(playRef.Scene)); services.Quiesce(); services.ResumeJobs();
    auto privateScene = std::make_shared<Axle::Scene>(); const auto oldId = privateScene->GetInstanceId();
    CHECK(submit(oldId)); privateScene->Clear(); CHECK(privateScene->GetInstanceId() != oldId && !Axle::SceneData::FindInstance(oldId));
    Axle::JobSystem::WaitForAll(); services.Drain(); Axle::JobSystem::PumpMainThreadCompletions(); CHECK(completed() == 1 && jobsDestroyed() == 4);
    play->OnRuntimeStop(); CHECK(stops(playData) == 1 && dependencyStops() > 0 && Axle::ExtractRenderWorld(*play).DebugLines.empty() && snapshot.DebugLines.size() == 1);
    CHECK(submit(playRef.Scene)); editorModule.Stop(); module.Stop(); baseModule.Stop(); CHECK(jobsDestroyed() == 5);
    Axle::JobSystem::PumpMainThreadCompletions(); CHECK(completed() == 1);
    std::vector<std::string> errors; CHECK(!edit->ExtensionsReady(errors));
    CHECK(!api.GetComponent(api.User, owner, ref, "custom.plugin/Mover"));
    // Values and even an empty host wrapper pool retain their descriptors/code.
    edit->Clear(); reopened->Clear(); dataOnly->Clear(); assemblyScene->Clear(); play->Clear();
    CHECK(components() == 0 && contexts() == 1 && GetModuleHandleW(load.ModulePath.c_str()));
    edit.reset(); reopened.reset(); dataOnly.reset(); assemblyScene.reset(); play.reset(); privateScene.reset(); physics.reset();
    CHECK(contexts() == 0 && GetModuleHandleW(load.ModulePath.c_str())); snapshot.Clear();
    Axle::PluginRuntime::NativePlugin::CollectRetiredModules(); CHECK(!GetModuleHandleW(load.ModulePath.c_str()));
    // An unavailable required schema remains byte-preserved and blocks Play;
    // opaque entity references cannot be duplicated without a remapper.
    auto unavailable = std::make_shared<Axle::Scene>();
    CHECK(Axle::SceneSerializer(unavailable.get()).DeserializeFromString(serialized));
    errors.clear(); CHECK(!unavailable->ExtensionsReady(errors));
    CHECK(!unavailable->DuplicateEntity(unavailable->GetEntityByUUID(Axle::UUID(100))));
    CHECK(Axle::SceneSerializer(unavailable.get()).SerializeToString().find("AxlePlugin_") != std::string::npos);
    unavailable->OnRuntimeStart(); CHECK(!unavailable->IsRuntimeActive() && unavailable->IsSimulationFaulted());
    unavailable.reset(); editorServices.Stop(); services.Stop(); Axle::Project::SetActive(nullptr);
    {
        Axle::PluginRuntime::RegistrationRegistry lateRegistry;
        Axle::PluginServiceSession lateServices(lateRegistry); CHECK(lateServices.Bind(error));
        auto lateLoad = load; lateServices.Attach(lateLoad);
        Axle::PluginRuntime::NativePlugin lateModule; CHECK(lateModule.Load(lateLoad, lateRegistry, error));
        auto lateScene = std::make_shared<Axle::Scene>(); auto lateEntity = lateScene->CreateEntity();
        const auto& lateApi = lateServices.Services(); const auto lateOwner = lateModule.GetHostApi()->owner_id;
        CHECK(lateApi.SetComponent(lateApi.User, lateOwner, {lateScene->GetInstanceId(), static_cast<std::uint64_t>(lateEntity.GetUUID())}, "custom.plugin/Mover", nullptr, 0));
        lateServices.Stop(); CHECK(lateApi.Log && lateApi.SetComponent && !lateApi.User);
        lateModule.Stop(); lateScene->Clear(); lateScene.reset();
        Axle::PluginRuntime::NativePlugin::CollectRetiredModules(); CHECK(!GetModuleHandleW(load.ModulePath.c_str()));
    }
    {
        auto counts = std::make_shared<HookCounts>();
        CHECK(Axle::SceneExtensionRegistry::Register("test.lifecycle", [counts](Axle::SceneData&) { return std::make_shared<CountingExtension>(counts); }));
        auto scene = std::make_unique<Axle::Scene>(); auto actor = scene->CreateEntity();
        scene->DestroyEntity(actor); CHECK(counts->Destroy == 1);
        scene->CreateEntity(); scene->Clear(); CHECK(counts->Clear == 1);
        scene.reset(); CHECK(counts->Clear == 2);
        Axle::SceneExtensionRegistry::Unregister("test.lifecycle");
    }
    done = true; std::cout << "Native components, clone/remap, persistence, phases/physics, owned jobs and retained rendering: PASS\n";
    return 0;
}
int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    try { return Run(argc, argv); } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
