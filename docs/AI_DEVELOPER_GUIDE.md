# 🤖 AI Agent & Developer Engineering Handbook

This guide is written specifically for **AI coding assistants** (e.g. Antigravity, Gemini, Claude, GPT) and **software engineers** contributing to or extending **AxleEngine**. It outlines the non-negotiable architectural rules, step-by-step implementation recipes, and common pitfalls to avoid.

---

## ⚠️ Core Architectural Invariants (Non-Negotiable Rules)

1. **Zero Raw Graphics Leaks**:
   Never include OpenGL headers (`glad.h`, `glfw3.h`) or call `gl*` functions outside `Engine/Renderer`. If a higher layer (such as `Engine/Scene` or `Editor`) needs a rendering operation, expose a clean abstraction method on `Axle::SceneRenderer` or `Axle::RenderCommand`.
2. **Strict Layered Dependency Hierarchy**:
   Dependencies flow strictly downward (`Core` $\rightarrow$ `Platform` $\rightarrow$ `Renderer`/`Physics`/`Audio`/`Asset` $\rightarrow$ `Scene` $\rightarrow$ `Editor`/`Runtime`). Lower layers must **never** include or link against higher layers.
3. **RAII Ownership Semantics**:
   Do not use raw pointers for resource ownership. Always use `std::unique_ptr` for exclusive ownership and `std::shared_ptr` for shared resource caches. Raw pointers are strictly non-owning observers.
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

When introducing a new component (e.g., `HealthComponent`):

1. **Declare the struct** in [`Engine/Scene/include/Axle/Components.hpp`](file:///d:/WorkSpace/AxleEngine/Engine/Scene/include/Axle/Components.hpp):
   ```cpp
   struct HealthComponent {
       float CurrentHealth = 100.0f;
       float MaxHealth = 100.0f;
       bool Invulnerable = false;

       HealthComponent() = default;
       HealthComponent(float maxHp) : CurrentHealth(maxHp), MaxHealth(maxHp) {}
   };
   ```
2. **Add YAML serialization** in [`Engine/Scene/src/SceneSerializer.cpp`](file:///d:/WorkSpace/AxleEngine/Engine/Scene/src/SceneSerializer.cpp):
   * Inside `SerializeEntity()`:
     ```cpp
     if (entity.HasComponent<HealthComponent>()) {
         out << YAML::Key << "HealthComponent";
         out << YAML::BeginMap;
         out << YAML::Key << "CurrentHealth" << YAML::Value << entity.GetComponent<HealthComponent>().CurrentHealth;
         out << YAML::Key << "MaxHealth" << YAML::Value << entity.GetComponent<HealthComponent>().MaxHealth;
         out << YAML::Key << "Invulnerable" << YAML::Value << entity.GetComponent<HealthComponent>().Invulnerable;
         out << YAML::EndMap;
     }
     ```
   * Inside `Deserialize()`: parse the `HealthComponent` YAML node and attach to the entity via `deserializedEntity.AddComponent<HealthComponent>()`.
   * Repeat for [`Engine/Scene/src/AssemblySerializer.cpp`](file:///d:/WorkSpace/AxleEngine/Engine/Scene/src/AssemblySerializer.cpp) for prefab support.
3. **Add Editor UI Inspection** in [`Engine/UI/src/SceneHierarchyPanel.cpp`](file:///d:/WorkSpace/AxleEngine/Engine/UI/src/SceneHierarchyPanel.cpp):
   * Add a `DrawComponent<HealthComponent>(...)` block rendering sliders and checkboxes.
   * Add an entry in the `+ Add Component` popup menu.

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

All regression tests are maintained in [`Editor/src/Test.cpp`](file:///d:/WorkSpace/AxleEngine/Editor/src/Test.cpp).

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
