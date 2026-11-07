#include <Axle/Scene.hpp>
#include <Axle/Components.hpp>
#include <Axle/ScriptRegistry.hpp>
#include <Axle/ScriptHotReloadManager.hpp>
#include <Axle/SimulationDriver.hpp>
#include <Axle/Log.hpp>
#include <Axle/JobSystem.hpp>
#include <windows.h>
#include <iostream>
#include <thread>
namespace {
    int destroyed = 0, destructed = 0, failures = 0;
    void Sink(int event, int generation) {
        if (generation == 1) { destroyed += event == 1; destructed += event == 2; }
    }
    void Check(bool value, int line) { if (!value) { ++failures; std::cerr << "SCRIPT_REVIEW failure line=" << line << '\n'; } }
#define CHECK(expr) Check(bool(expr), __LINE__)
}
int main(int argc, char** argv) {
    if (argc != 8) return 2;
    Axle::Log::Init();
    Axle::JobSystem::Init(2);
    auto& reload = Axle::ScriptHotReloadManager::Get();
    CHECK(reload.Initialize(argv[1]));
    auto edit = std::make_shared<Axle::Scene>();
    auto play = std::make_shared<Axle::Scene>();
    auto preview = std::make_shared<Axle::Scene>();
    auto editEntity = edit->CreateEntity();
    auto playEntity = play->CreateEntity();
    auto previewEntity = preview->CreateEntity();
    editEntity.AddComponent<Axle::NativeScriptComponent>().Bind("ReviewScript");
    playEntity.AddComponent<Axle::NativeScriptComponent>().Bind("ReviewScript");
    previewEntity.AddComponent<Axle::NativeScriptComponent>().Bind("ReviewScript");
    play->OnRuntimeStart();
    preview->OnRuntimeStart();
    auto oldFactory = editEntity.GetComponent<Axle::NativeScriptComponent>().InstantiateScript;
    auto oldInstance = playEntity.GetComponent<Axle::NativeScriptComponent>().Instance;
    const auto oldModulePath = reload.GetCurrentRuntimeDllPath();
    auto module = GetModuleHandleW(oldModulePath.c_str());
    CHECK(module != nullptr);
    auto setSink = reinterpret_cast<void(*)(void(*)(int, int))>(GetProcAddress(module, "AxleReview_SetSink"));
    if (!setSink) return 2;
    setSink(Sink);
    const auto generation = reload.GetModuleGeneration();
    bool externalInitialize = true, externalReload = true;
    std::thread external([&] {
        externalInitialize = reload.Initialize(argv[2]);
        externalReload = reload.Reload(nullptr, false).Success;
        reload.Shutdown();
    });
    external.join();
    CHECK(!externalInitialize && !externalReload);
    CHECK(reload.IsLoaded() && reload.GetModuleGeneration() == generation);
    for (int candidate : {3, 4, 5, 6}) {
        CHECK(!reload.Initialize(argv[candidate]));
        CHECK(reload.GetModuleGeneration() == generation);
        CHECK(playEntity.GetComponent<Axle::NativeScriptComponent>().Instance == oldInstance);
        CHECK(editEntity.GetComponent<Axle::NativeScriptComponent>().InstantiateScript == oldFactory);
    }
    auto* metadata = Axle::ScriptRegistry::GetMetadata("ReviewScript");
    metadata->Fields[0].SetValue<float>(oldInstance, 23.0f);
    CHECK(reload.Initialize(argv[2]));
    CHECK(destroyed == 2 && destructed == 2);
    CHECK(GetModuleHandleW(oldModulePath.c_str()) == nullptr);
    CHECK(editEntity.GetComponent<Axle::NativeScriptComponent>().InstantiateScript != oldFactory);
    CHECK(playEntity.GetComponent<Axle::NativeScriptComponent>().Instance != nullptr);
    CHECK(previewEntity.GetComponent<Axle::NativeScriptComponent>().Instance != nullptr);
    metadata = Axle::ScriptRegistry::GetMetadata("ReviewScript");
    CHECK(metadata->Fields[0].GetValue<float>(playEntity.GetComponent<Axle::NativeScriptComponent>().Instance) == 23.0f);
    Axle::InputContext hardware;
    hardware.IsKeyPressedFn = [](Axle::KeyCode key) { return key == Axle::KeyCode::W; };
    Axle::Input::SetInputContext(&hardware);
    Axle::SimulationDriver driver;
    driver.AdvanceFrame(1.0f / 60.0f, *play);
    CHECK(metadata->Fields[0].GetValue<float>(playEntity.GetComponent<Axle::NativeScriptComponent>().Instance) == 24.0f);
    CHECK(!reload.Initialize(argv[7]));
    CHECK(reload.IsLoaded());
    CHECK(!play->IsRuntimeActive() && !preview->IsRuntimeActive());
    CHECK(play->IsSimulationFaulted());
    CHECK(driver.AdvanceFrame(1.0f, *play) == 0);
    CHECK(driver.IsPaused());
    reload.Shutdown();
    CHECK(!reload.IsLoaded());
    CHECK(!editEntity.GetComponent<Axle::NativeScriptComponent>().InstantiateScript);
    Axle::Input::SetInputContext(nullptr);
    edit.reset(); play.reset(); preview.reset();
    Axle::JobSystem::Shutdown();
    std::cout << "SCRIPT_LIFECYCLE_REVIEW failures=" << failures << '\n';
    return failures ? 1 : 0;
}
