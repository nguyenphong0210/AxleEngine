# 🤖 AI Agent & Developer Engineering Handbook

This guide is written specifically for **AI coding assistants** (e.g. Antigravity, Gemini, Claude, GPT) and **software engineers** contributing to or extending **AxleEngine**. It outlines the non-negotiable architectural rules, step-by-step implementation recipes, and common pitfalls to avoid.

For implemented behavior, ownership and thread boundaries, consult the [architecture documentation](architecture/README.md).

---

## ⚠️ Core Architectural Invariants (Non-Negotiable Rules)

1. **Graphics Boundaries**:
   Keep new raw graphics operations inside the relevant graphics backend implementation. GLFW window/input operations belong in Platform or explicit integration code. Current legacy graphics calls also exist in VFX and other integration paths; do not treat this existing debt as a reason to add more leaks. See [current boundaries](architecture/SAD.md) before changing either rendering path.
2. **Layered Dependency Direction**:
   New dependencies should follow the [proposed layered policy](architecture/TARGET_LAYERED_ARCHITECTURE.md). The current engine is not strictly layered: Core/Application uses Platform/Renderer/Physics, and runtime plugins pull editor UI. Do not add reverse dependencies or conceal them with include paths. Check both CMake targets and includes; the [dependency map](architecture/DEPENDENCY_MAP.md) records the current exceptions.
3. **Explicit Ownership and RAII**:
   Prefer value members and unique_ptr for exclusive ownership and shared_ptr for shared resources. Observer pointers must have a documented lifetime. Existing wrappers can own native GPU/OS handles represented as raw pointers and must release them explicitly. Destroy script instances through their matching callbacks before unloading their DLL.
4. **Public Visibility for Script Reflection**:
   The reflection macro `AXLE_SCRIPT_FIELDS` **must always be declared under `public:` visibility**. AxleEngine uses a C++20 concept to verify field registration:
   ```cpp
   template<typename T>
   concept HasScriptFields = requires(ScriptFieldRegistry& reg) { T::RegisterFields(reg); };
   ```
   If declared under `private:` or `protected:`, this concept evaluates to `false`, causing the Inspector reflection to be silently skipped.
5. **Headless ImGui Safety in Automated Tests**:
   When testing code that interacts with ImGui (such as scripts overriding `OnImGuiRender()` or HUDs), always initialize a headless `ImGuiContext`, configure `io.DeltaTime > 0.0f`, build the font texture with `io.Fonts->GetTexDataAsRGBA32(...)`, and destroy the context at test completion.
6. **Quaternion Rotation Integrity**:
   `TransformComponent.Rotation` is stored as a normalized `glm::quat (w, x, y, z)`. Never manipulate angles directly as arbitrary float additions. Always construct rotation deltas using `glm::angleAxis(glm::radians(angle), axis)` to prevent gimbal lock.

---

## 🛠️ Implementation Recipes

### Recipe 1: Adding a New ECS Component

When introducing a new component:

1. Define its data and ownership. Current built-in types are in [Components.hpp](../Engine/Scene/include/Axle/Components.hpp); a future domain module should not create another dependency from generic Scene data to optional gameplay tooling.
2. Update the actual text-writing/parsing paths in [SceneSerializer.cpp](../Engine/Scene/src/SceneSerializer.cpp) and [AssemblySerializer.cpp](../Engine/Scene/src/AssemblySerializer.cpp). Follow the implementation in the current checkout rather than assuming a YAML library API.
3. Check Scene::Copy, duplicate/destroy and runtime start/update/stop behavior. Preserve persistent UUIDs where appropriate and remap registry handles/references when cloning.
4. Add inspection and add-component UI through the current inspector/hierarchy implementation. Consider asset/prefab editing paths separately.
5. For plugin components, use SceneSerializerContext callbacks where possible. Add a behavioral regression test covering the component's meaningful lifecycle or persistence contract.

See [Scene design](architecture/SDD.md#4-scene-and-entity) and [class relationships](architecture/CLASS_RELATIONSHIPS.md) for the current integration points.

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

The test runner is [Editor/src/Test.cpp](../Editor/src/Test.cpp); test groups are implemented in [Editor/src/Tests](../Editor/src/Tests) and share TestCommon.hpp. Register new groups in the runner/build definition as required. Test builds currently depend on AxleEditor resource staging.

```cpp
bool RunTest_MyNewFeature() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << " [TEST] MY NEW FEATURE VERIFICATION                     " << std::endl;
    std::cout << "========================================================" << std::endl;

    auto scene = std::make_shared<Axle::Scene>();
    auto physicsScene = std::make_unique<Axle::PhysicsScene>();

    // Setup entities and components
    auto entity = scene->CreateEntity("TestSubject");
    entity.AddComponent<Axle::TransformComponent>();

    // Simulate runtime steps
    scene->OnRuntimeStart(physicsScene.get());
    for (int frame = 0; frame < 30; ++frame) {
        physicsScene->Step(0.016f);
        scene->OnUpdateRuntime(0.016f, physicsScene.get());
    }
    scene->OnRuntimeStop(physicsScene.get());

    // Assert conditions
    if (/* check condition */ false) {
        std::cerr << "FAIL: Expected condition not met!" << std::endl;
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
| **Windows DLL Locking (`LNK1104`)** | Building `AxleScripts.dll` while `AxleEditor.exe` has it directly open locks the file. | Never load the primary `AxleScripts.dll` directly. Use `ScriptHotReloadManager` which copies to a timestamped shadow DLL first. |
| **Degree vs Radian Confusion** | Passing degrees directly to GLM trigonometric functions (`glm::sin`, `glm::cos`, `glm::angleAxis`). | Always convert via `glm::radians(degrees)`. |
| **Missing `io.DeltaTime` in Tests** | Calling `ImGui::NewFrame()` without setting `io.DeltaTime` triggers division by zero. | Explicitly assign `io.DeltaTime = 1.0f / 60.0f;` in headless test harnesses. |
| **Overwriting Asset UUIDs** | Generating a new UUID upon saving an existing asset breaks scene cross-references. | Preserve the existing `UUID` in `.meta` files; only generate new UUIDs on asset duplication or fresh creation. |
