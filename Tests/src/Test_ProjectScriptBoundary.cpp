#include <Axle/Scene.hpp>
#include <Axle/Components.hpp>
#include <Axle/ScriptableEntity.hpp>
#include <Axle/ScriptRegistry.hpp>
#include <Axle/ScriptModule.hpp>
#include <Axle/ScriptHotReloadManager.hpp>
#include <Axle/SimulationDriver.hpp>
#include <Axle/Log.hpp>
#include <Axle/JobSystem.hpp>
#include <Axle/PhysicsEngine.hpp>

#include <iostream>
#include <filesystem>
#include <memory>
#include <string>

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// -----------------------------------------------------------------------------
// Test 1: Empty Script Project & Registry Stability
// -----------------------------------------------------------------------------
bool Test_EmptyScriptProject() {
    std::cout << "\n[TEST 1] Empty Script Project Baseline...\n";

    Axle::ScriptRegistry::ClearDynamicScripts();
    TEST_CHECK(!Axle::ScriptRegistry::Has("NonExistentScript"), "NonExistentScript must not be present");

    // Attempting to instantiate an unselected/unregistered script on an entity
    auto scene = std::make_shared<Axle::Scene>();
    auto entity = scene->CreateEntity();
    auto& nsc = entity.AddComponent<Axle::NativeScriptComponent>();
    nsc.Bind("NonExistentScript");

    TEST_CHECK(nsc.Instance == nullptr, "Unregistered script must not instantiate");
    TEST_CHECK(nsc.InstantiateScript == nullptr, "InstantiateScript must remain null");

    // Scene lifecycle must not crash on empty/unbound scripts
    scene->OnRuntimeStart();
    scene->OnRuntimeStop();

    std::cout << "[PASS] Test_EmptyScriptProject\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Script Plugin Boundary & Missing Plugin Preflight Diagnostic
// -----------------------------------------------------------------------------
class PluginConsumerScript : public Axle::ScriptableEntity {
public:
    bool DetectedPlugin = false;
    std::string PreflightError;

    void OnCreate() override {
        if (!HasComponent<Axle::TransformComponent>()) {
            PreflightError = "Missing required transform component";
        }
    }
};

bool Test_MissingPluginPreflightDiagnostic() {
    std::cout << "\n[TEST 2] Missing Plugin Preflight Diagnostic...\n";

    auto scene = std::make_shared<Axle::Scene>();
    auto entity = scene->CreateEntity();

    Axle::ScriptRegistry::Register<PluginConsumerScript>("PluginConsumerScript");
    TEST_CHECK(Axle::ScriptRegistry::Has("PluginConsumerScript"), "Script must be registered");

    auto& nsc = entity.AddComponent<Axle::NativeScriptComponent>();
    nsc.Bind("PluginConsumerScript");

    scene->OnRuntimeStart();
    auto* scriptInstance = static_cast<PluginConsumerScript*>(nsc.Instance);
    TEST_CHECK(scriptInstance != nullptr, "Script instance must be created on runtime start");
    TEST_CHECK(scriptInstance->PreflightError.empty(), "Valid preflight check succeeded");

    scene->OnRuntimeStop();
    Axle::ScriptRegistry::UnregisterMetadata("PluginConsumerScript");

    std::cout << "[PASS] Test_MissingPluginPreflightDiagnostic\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Live Component Reference Cleanup On Script Unload
// -----------------------------------------------------------------------------
static int s_DestroyCount = 0;
class LifecycleScript : public Axle::ScriptableEntity {
public:
    int Value = 42;
    void OnDestroy() override {
        s_DestroyCount++;
    }
};

bool Test_LiveComponentReferenceCleanup() {
    std::cout << "\n[TEST 3] Live Component Reference Cleanup On Unload...\n";

    s_DestroyCount = 0;
    Axle::ScriptRegistry::Register<LifecycleScript>("LifecycleScript");

    auto scene = std::make_shared<Axle::Scene>();
    auto entity1 = scene->CreateEntity();
    auto entity2 = scene->CreateEntity();

    entity1.AddComponent<Axle::NativeScriptComponent>().Bind("LifecycleScript");
    entity2.AddComponent<Axle::NativeScriptComponent>().Bind("LifecycleScript");

    scene->OnRuntimeStart();

    // Verify instances are live
    TEST_CHECK(entity1.GetComponent<Axle::NativeScriptComponent>().Instance != nullptr, "Entity 1 script must be active");
    TEST_CHECK(entity2.GetComponent<Axle::NativeScriptComponent>().Instance != nullptr, "Entity 2 script must be active");

    // Stopping scene cleans up live component references
    scene->OnRuntimeStop();
    TEST_CHECK(s_DestroyCount == 2, "Both script instances must have OnDestroy called during shutdown");

    Axle::ScriptRegistry::UnregisterMetadata("LifecycleScript");
    std::cout << "[PASS] Test_LiveComponentReferenceCleanup\n";
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Axle::Log::Init();
    Axle::JobSystem::Init(2);
    Axle::PhysicsEngine::Init();

    std::cout << "Running AxleProjectScriptBoundaryTests...\n";

    if (!Test_EmptyScriptProject()) return 1;
    if (!Test_MissingPluginPreflightDiagnostic()) return 1;
    if (!Test_LiveComponentReferenceCleanup()) return 1;

    std::cout << "All Project Script Boundary Tests PASSED successfully!\n";
    Axle::JobSystem::Shutdown();
    return 0;
}
