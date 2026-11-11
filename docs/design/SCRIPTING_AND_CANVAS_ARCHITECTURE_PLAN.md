# Scripting & Canvas UI Architecture Plan: Decoupling ImGui and Establishing Declarative In-Game UI

Date: **2026-11-11**\
Author / Proposer: **Gemini (Arch Plan)**\
Reviewer: **Codex (Pending Review)**\
Scope: **Architecture & Design Plan only (No code modified)**

---

## 1. Executive Summary

This document specifies the architectural plan to eliminate immediate-mode Editor UI (`OnImGuiRender`) from the gameplay scripting layer (`ScriptableEntity`), and complete the bridge between **Scripting** and **Canvas 2D UI** (`UICanvasComponent` / `UIDocument`).

### Key Objectives:
1. **Purity of Game Logic:** Remove `virtual void OnImGuiRender() {}` from `ScriptableEntity` and `Scene`. Game scripts must have zero dependency on ImGui or Editor UI code.
2. **Standardized In-Game UI Pipeline:** In-game HUDs, menus, and overlays must be authored declaratively in `.axleasset` and rendered by `CanvasRenderer` (`Canvas2D.hlsl`) via the RenderGraph.
3. **Ergonomic Two-Way Bridge:**
   - **Data Flow (Script → UI):** Scripts publish state (e.g., Health, Speed) into `UIDataContext` with zero layout knowledge.
   - **Event Flow (UI → Script):** Interactive UI elements (buttons, sliders) dispatch typed action events back to registered script callbacks.
4. **Standalone Runtime Purity:** Ensure `AxleRuntime` (shipping game builds) can execute with ImGui completely disabled/stripped.

---

## 2. Current State Audit

An exhaustive audit of the codebase reveals that `ScriptableEntity::OnImGuiRender` is narrowly confined to only **4 files**:

| File | Location | Current Usage |
| :--- | :--- | :--- |
| `Engine/Scene/include/Axle/ScriptableEntity.hpp` | Line 45 | Declares `virtual void OnImGuiRender() {}` |
| `Engine/Scene/include/Axle/Scene.hpp` | Line 98 | Declares `void OnImGuiRender();` |
| `Engine/Scene/src/Scene.cpp` | Lines 404–413 | Iterates all `NativeScriptComponent` instances and dispatches `OnImGuiRender()` |
| `Editor/src/EditorLayer.cpp` | Lines 3022–3025 | Calls `m_Scene->OnImGuiRender()` during `Play` and `Simulate` states |
| `Tests/Fixtures/Assets/scripts/RacingHUD.hpp` | Line 258 | Legacy sample HUD using `ImDrawList` as a fallback when `UICanvasComponent` is missing |

### Existing Canvas Foundation:
The engine already possesses a robust, declarative Retained-Mode UI framework:
- [`UICanvasComponent`](file:///d:/WorkSpace/AxleEngine/Engine/Scene/include/Axle/Components.hpp#L1783): Holds `AssetPath` and `std::shared_ptr<UIDocument>`.
- [`UIDocument`](file:///d:/WorkSpace/AxleEngine/Engine/UI/include/Axle/UI/UIDocument.hpp): Node hierarchy (`UINode`, `UIButton`, `UIText`, `UIImage`), layout solver, and input processing.
- [`UIDataContext`](file:///d:/WorkSpace/AxleEngine/Engine/UI/include/Axle/UI/UIDataContext.hpp): Key-value store supporting direct, interpolated (`"Speed: {Vehicle.SpeedKmh}"`), and ternary bindings (`"{IsRedline ? #FF0000 : #00FF00}"`).
- [`CanvasRenderer`](file:///d:/WorkSpace/AxleEngine/Engine/Renderer/include/Axle/CanvasRenderer.hpp): Batch-renders UI primitives to GPU using `Canvas2D.hlsl` / `Canvas2D.vert` / `Canvas2D.frag`.

**The Missing Piece:** The ergonomics of connecting `ScriptableEntity` to `UIDataContext` and handling button click events back into gameplay scripts.

---

## 3. Architecture Specification

```mermaid
flowchart TD
    subgraph SCRIPT_LAYER["Gameplay Scripting (Pure C++)"]
        SE[ScriptableEntity\ne.g., PlayerController]
        SE -->|1. SetUI("Player.HP", 85)| DC[UIDataContext\nObservable State Store]
        EVT_REG[Script Event Handlers\ne.g., OnPauseClicked] <--|4. Dispatch Action| SE
    end

    subgraph UI_SYSTEM["Declarative Canvas UI (.axleasset)"]
        DC -->|2. Reactive Property Evaluation| Doc[UIDocument\nUINode Tree]
        Doc -->|3. Button Clicked / Action Triggered| EVT_REG
    end

    subgraph RENDER_PIPELINE["Rendering Pipeline"]
        Doc -->|Draw Command Generation| CR[CanvasRenderer]
        CR -->|RenderGraph Pass| RT[Main Viewport / Present Target]
    end

    subgraph EDITOR_ISOLATION["Editor UI (Isolated)"]
        ImGui[ImGui Context] -->|Inspector, Hierarchy, Canvas Designer| EditorPanels[Editor Only]
    end
```

### 3.1 Non-Negotiable Invariants
1. **Zero ImGui in Game Logic:** No file in `Engine/Scene/` or gameplay scripts under `Tests/` or game projects may include `<imgui.h>`.
2. **Main-Thread Dispatch:** All UI interactions and script callbacks run deterministically on the engine Main Thread.
3. **No Memory Leaks / Dangling Callbacks:** UI event registrations are strictly scoped to the `ScriptableEntity` lifecycle and automatically unregistered on `OnDestroy()`.
4. **Hot-Reload Safety:** When scripts or Canvas assets hot-reload, bindings and event listeners must safely re-attach without null-pointer dereferences.

---

## 4. Phased Implementation Plan (Commit Units)

### Package SC-01: Decouple ImGui from ScriptableEntity
* **Scope:**
  - Remove `virtual void OnImGuiRender() {}` from `ScriptableEntity.hpp`.
  - Remove `Scene::OnImGuiRender()` from `Scene.hpp` and `Scene.cpp`.
  - Remove the dispatch block in `EditorLayer.cpp` (lines 3022–3025).
  - Clean up `RacingHUD.hpp`: Remove the `OnImGuiRender()` override and its `ImDrawList` code.
* **Verification:**
  - Rebuild `AxleEngine`, `AxleEditor`, `AxleRuntime`, and `AllTests`.
  - Verify zero compiler warnings regarding missing overrides.

### Package SC-02: Script-to-Canvas Data Binding Ergonomics
* **Scope:**
  - Enhance `ScriptableEntity` with safe, cached access to the entity's (or target's) `UIDataContext`:
    ```cpp
    // In ScriptableEntity.hpp:
    void SetUI(std::string_view key, UIDataValue value);
    void SetUI(std::string_view key, float value);
    void SetUI(std::string_view key, int32_t value);
    void SetUI(std::string_view key, bool value);
    void SetUI(std::string_view key, std::string_view value);
    ```
  - Cache the pointer to `UIDataContext` during `OnCreate()`, resolving lazily if `UICanvasComponent` is added dynamically.
* **Verification:**
  - Unit test in `Tests/src/Test_ScriptCanvasBridge.cpp` verifying that calling `SetUI()` updates `UIDataContext` and properly triggers `UIDocument::EvaluateBindings()`.

### Package SC-03: Canvas-to-Script Event Dispatching (Two-Way Interaction)
* **Scope:**
  - Add `Action` string property to `UIButton` / interactive nodes (e.g., `Action: "Game.Resume"`).
  - In `ScriptableEntity`, introduce typed event binding:
    ```cpp
    void RegisterUIAction(std::string_view action, std::function<void()> handler);
    void UnregisterUIAction(std::string_view action);
    ```
  - `ScriptableEntity::~ScriptableEntity()` automatically unregisters all actions.
  - In `UIDocument::ProcessInput`, when an interactive node with an `Action` is clicked, invoke the registered callback via a central or scene-level UI Action Registry.
* **Verification:**
  - Automated test simulating a synthetic mouse click on a `UIButton` and asserting that the script's member callback executed.

### Package SC-04: Runtime Canvas Render Pass Verification
* **Scope:**
  - Verify that `AxleRuntime` (cooked & loose modes) compiles and runs without any ImGui initialization.
  - Ensure the `CanvasRenderer` pass executes seamlessly after scene post-processing in `SceneRenderer` / `RenderGraph`.
* **Verification:**
  - Run `scripts/run_opt11_product_acceptance.py` to confirm that Editor and Runtime tests continue passing 100% on D3D12, Vulkan, and OpenGL.

---

## 5. Review Guidelines for Codex

When reviewing this plan, Codex should specifically check:
1. **ABI / Header Hygiene:** Does removing `OnImGuiRender` impact any existing plugin or script DLL boundaries?
2. **Event Lifetime:** Does `RegisterUIAction` handle edge cases such as entity destruction mid-event dispatch?
3. **Performance:** Does `SetUI()` avoid heap allocations for common scalar types (`float`, `int`, `bool`)? *(Note: `UIDataValue` uses `std::variant`, which is heap-free for primitive types).*
4. **Compatibility:** Does this leave the existing `UICanvasDesignerPanel` in Editor 100% intact? (Yes, the designer operates on `UIDocument` directly).

---

## 6. Acceptance Criteria

1. Zero references to `ImGui` inside `ScriptableEntity.hpp` and `Scene.cpp`.
2. Full build and test pass on Debug & Release for `AxleEditor`, `AxleRuntime`, and `AllTests`.
3. Working end-to-end sample demonstrating data binding (HP bar / Speedometer) and action callbacks (Pause/Play button) without a single line of immediate-mode GUI code.
