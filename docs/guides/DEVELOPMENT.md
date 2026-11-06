# Development Conventions and Extension Guide

Navigation: [documentation hub](../README.md).

This guide is written specifically for **AI coding assistants** (e.g. Antigravity, Gemini, Claude, Codex) and **software engineers** contributing to or extending **AxleEngine**. It outlines architectural rules, step-by-step implementation recipes, and common pitfalls to avoid.

For implemented behavior, ownership and thread boundaries, consult the [architecture documentation](../architecture/README.md).

---

## ⚠️ Core Architectural Invariants (Non-Negotiable Rules)

1. **Clean Leaf Foundation (`Engine/Core`)**:
   `Engine/Core` is a strict Level-0 leaf library containing primitive types, logging, hashing, assertions, time steps, and memory utilities. It must **never** depend upward on `Platform`, `Renderer`, `Scene`, or `Physics`. `Application` is owned by `Engine/Platform` (Level 1).
2. **Graphics Boundaries**:
   Keep raw graphics operations inside the relevant graphics backend implementation. GLFW window/input operations belong in `Platform` or explicit integration code. Do not leak low-level API calls into generic scene or asset logic.
3. **Layered Dependency Direction**:
   Follow the [target layered architecture](../architecture/TARGET_LAYERED_ARCHITECTURE.md). Check both CMake targets and include paths. No downward target may include or link an upward target.
4. **Explicit Ownership and RAII**:
   Prefer value members and `std::unique_ptr` for exclusive ownership, and `std::shared_ptr` only for shared asset resources. Observer pointers must have a documented lifetime. Destroy script instances through their matching callbacks before unloading their DLL.
5. **Pure CPU Asset Pipeline**:
   `AssetManager` is a pure CPU registry decoupled from GPU device resources. Native asset documents embed their persistent 64-bit UUID directly within the `.axleasset` / `.axlescene` payload. No `.meta` companion sidecars are used. Raw media files (textures, audio, meshes) are resolved via canonical root-relative paths (`assets/...`).
6. **Unified Simulation Driver**:
   Both Editor (Play mode) and standalone Runtime step simulation via the shared `SimulationDriver` and `SimulationClock`. Frame ordering strictly follows: `FramePhase::FixedControls` $\to$ `FramePhase::Physics` $\to$ `FramePhase::PostStepSync` $\to$ `FramePhase::FrameScripts` $\to$ `FramePhase::AnimationVFX` $\to$ `FramePhase::LatePresentation`.
7. **Public Visibility for Script Reflection**:
   The reflection macro `AXLE_SCRIPT_FIELDS` **must always be declared under `public:` visibility**. AxleEngine uses a C++20 concept to verify field registration:
   ```cpp
   template<typename T>
   concept HasScriptFields = requires(ScriptFieldRegistry& reg) { T::RegisterFields(reg); };
   ```
   If declared under `private:` or `protected:`, this concept evaluates to `false`, causing the Inspector reflection to be silently skipped.
8. **Headless ImGui Safety in Automated Tests**:
   When testing code that interacts with ImGui (such as scripts overriding `OnImGuiRender()` or HUDs), always initialize a headless `ImGuiContext`, configure `io.DeltaTime > 0.0f`, build the font texture with `io.Fonts->GetTexDataAsRGBA32(...)`, and destroy the context at test completion.
9. **Quaternion Rotation Integrity**:
   `TransformComponent.Rotation` is stored as a normalized `glm::quat (w, x, y, z)`. Never manipulate angles directly as arbitrary float additions. Always construct rotation deltas using `glm::angleAxis(glm::radians(angle), axis)` to prevent gimbal lock.

---

## 🛠️ Implementation Recipes

### Recipe 1: Adding a New ECS Component

When introducing a new component:

1. Define its data and ownership in [Components.hpp](../../Engine/Scene/include/Axle/Components.hpp).
2. Update the human-readable text-writing/parsing paths in [SceneSerializer.cpp](../../Engine/Scene/src/SceneSerializer.cpp) and [AssemblySerializer.cpp](../../Engine/Scene/src/AssemblySerializer.cpp).
3. Check `Scene::Copy`, duplicate/destroy, and runtime start/update/stop behavior. Preserve persistent UUIDs and remap registry handles/references when cloning.
4. Add inspection and add-component UI in the Editor properties panel.
5. For plugin components, use `SceneSerializerContext` callbacks. Add a behavioral regression test covering the component's lifecycle and persistence contract.

See [Scene design](../architecture/SDD.md#4-scene-and-entity) and [class relationships](../architecture/CLASS_RELATIONSHIPS.md) for current integration points.

---

### Recipe 2: Authoring a New Gameplay Script

1. Create a new header in `Editor/assets/scripts/` (e.g., `RotateObject.hpp`).
2. Inherit from `Axle::ScriptableEntity`.
3. Add fields and the `AXLE_SCRIPT_FIELDS` macro under `public:`.
4. Register the script at file scope with `AXLE_REGISTER_SCRIPT`.

```cpp
#pragma once
#include <Axle/ScriptableEntity.hpp>
#include <Axle/ScriptRegistry.hpp>

class RotateObject : public Axle::ScriptableEntity {
public:
    float Speed = 45.0f;

    void OnUpdate(Axle::Timestep ts) override {
        auto& tc = GetComponent<Axle::TransformComponent>();
        tc.Rotation = glm::angleAxis(glm::radians(Speed * ts.GetSeconds()), glm::vec3(0, 1, 0)) * tc.Rotation;
    }

    AXLE_SCRIPT_FIELDS(RotateObject,
        AXLE_FIELD(Speed, -180.0f, 180.0f, "Rotation speed in degrees per second")
    )
};

AXLE_REGISTER_SCRIPT(RotateObject);
```
5. Compile via `cmake --build build --config Release --target AxleScripts` or press `F9` in the Editor.

---

### Recipe 3: Writing an Automated Headless Test in `AxleTests.exe`

Suites live in [Tests/src](../../Tests/src) and are registered in [Tests/CMakeLists.txt](../../Tests/CMakeLists.txt).

```cpp
bool RunTest_MyNewFeature() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [TEST] MY NEW FEATURE VERIFICATION                     " << std::endl;
    std::cout << "========================================================" << std::endl;

    auto scene = std::make_shared<Axle::Scene>();
    auto physicsScene = std::make_unique<Axle::PhysicsScene>();
    Axle::SimulationDriver driver;

    // Setup entities and components
    auto entity = scene->CreateEntity("TestSubject");
    entity.AddComponent<Axle::TransformComponent>();

    // Advance simulation via the unified SimulationDriver
    driver.Start(*scene, physicsScene.get());
    for (int frame = 0; frame < 30; ++frame) {
        driver.AdvanceFrame(1.0f / 60.0f, *scene, physicsScene.get());
    }
    driver.Stop(*scene, physicsScene.get());

    // Assert conditions
    if (!entity.GetComponent<Axle::TransformComponent>().Valid()) {
        std::cerr << "FAIL: Expected transform to remain valid!" << std::endl;
        return false;
    }

    std::cout << "  -> PASS: Feature verified successfully." << std::endl;
    return true;
}
```

---

## 🚫 Common Pitfalls & Traps

| Pitfall | Problem | Solution |
| :--- | :--- | :--- |
| **Unchecked EnTT Entity** | Calling `entity.GetComponent<T>()` on a null or deleted entity causes memory corruption. | Always check `if (entity)` or `if (registry.valid(e))` before accessing components. |
| **Direct Mutation in Parallel ECS** | Adding or removing components inside `ParallelECS::ForEach` corrupts entity pools. | Record structural changes in a `SceneCommandBuffer` and execute them synchronously at the frame boundary. |
| **Windows DLL Locking (`LNK1104`)** | Building `AxleScripts.dll` while `AxleEditor.exe` has it directly open locks the file. | Never load the primary `AxleScripts.dll` directly. Use `ScriptHotReloadManager` which copies to a timestamped shadow DLL first. |
| **Degree vs Radian Confusion** | Passing degrees directly to GLM trigonometric functions (`glm::sin`, `glm::cos`, `glm::angleAxis`). | Always convert via `glm::radians(degrees)`. |
| **Missing `io.DeltaTime` in Tests** | Calling `ImGui::NewFrame()` without setting `io.DeltaTime` triggers division by zero. | Explicitly assign `io.DeltaTime = 1.0f / 60.0f;` in headless test harnesses. |
| **Overwriting Asset UUIDs** | Generating a new UUID upon saving an existing asset breaks scene cross-references. | Preserve the existing embedded `UUID` in `.axleasset` / `.axlescene`; only generate new UUIDs on asset duplication or fresh creation. |
