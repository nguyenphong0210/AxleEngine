# UI Canvas Architecture & Scoped MVVM Data Binding Specification

**Document Version:** 1.1.0  
**Target Subsystem:** `Engine/UI`, `Engine/Scene`, `Editor`  
**Pattern:** Hybrid MVVM (Model-View-ViewModel) with "Convention over Configuration"  
**Related Documents:** `Phase34_QML_Canvas2D_Guide.md`, `ARCHITECTURE.md`, `ECS_AND_SCENE_GRAPH.md`, `ROADMAP.md`

---

## 1. Executive Summary & Problem Analysis

AxleEngine's 2D Canvas UI subsystem provides hardware-accelerated declarative interface rendering via `.axleui` markup and `UICanvasComponent`. While the rendering pipeline operates with high visual fidelity and 180+ FPS throughput, user testing and architectural analysis have revealed key ergonomic and data-flow requirements:

1. **Global Data Collision (The Multi-Instance Problem):**
   - `UIDataContext` was originally scoped only at the global `Scene` level.
   - Scripts published metrics using fixed global keys (e.g. `"Vehicle.SpeedKmh"`).
   - When multiple vehicle instances exist in a scene (e.g. Player vs. AI racers, Split-Screen, or Multiplayer), scripts overwrite each other's telemetry, causing UI flicker.
   - Hardcoding entity IDs into `.axleui` markup destroys template reusability across instances.

2. **Inspector UX & Data Source Binding Gap:**
   - On the Inspector panel, `UICanvasComponent` lacked an intuitive link to scripts (`NativeScriptComponent`).
   - Users needed a standard mechanism to connect declarative `.axleui` Views with C++ ViewModels/Scripts.
   - The UX should adopt the industry-standard **"Convention over Configuration"** pattern (as seen in Unity UI Toolkit, Unreal Engine UMG MVVM, and Godot).

3. **Scene Hierarchy Visual Pollution:**
   - Expanding `UICanvas` on the Scene Hierarchy auto-expanded the internal DOM tree (30+ micro-nodes: `Row`, `Column`, `Rectangle`, `Text`, etc.).
   - This cluttered the 3D spatial hierarchy and buried world objects (`Car`, `Track`, `Lights`, `Camera`).

4. **Manual Asset Path Input & Component Discovery:**
   - `Asset Path` was a raw text input field rather than a searchable project asset picker.
   - `UICanvasComponent` was missing from the categorized "Add Component" menu due to the absence of a `"User Interface"` category.

---

## 2. Core Architectural Design: Scoped MVVM with "Convention over Configuration"

```mermaid
flowchart TD
    subgraph View [View Layer (.axleui)]
        Markup["RacingHUD.axleui<br/>text: '{SpeedKmh}'<br/>value: '{RPMNormalized}'<br/>color: '{IsRedline ? #FF1E42 : #00F5D4}'"]
    end

    subgraph Bridge [Bridge Layer (UICanvasComponent)]
        CanvasComp["UICanvasComponent<br/>Asset Path: 'RacingHUD.axleui'<br/>Data Source: [ Self | EntityRef | None ]"]
    end

    subgraph ViewModel [ViewModel / Data Layer]
        direction TB
        Script1["CarController.hpp (NativeScriptComponent)<br/>• Reflected Fields: Horsepower, SteerSpeed...<br/>• Scoped UIDataContext: SpeedKmh, RPMNormalized..."]
        Phys1["VehiclePhysicsComponent / TransformComponent"]
    end

    Markup -.->|Evaluates Bindings| CanvasComp
    CanvasComp ==>|Auto-Detected (Same Entity)| Script1
    CanvasComp -.->|Fallback or Drag-and-Drop Override| Phys1
```

---

### 2.1. Two-Tier Inspector UX: The "Data Source" Mechanism

On the Inspector panel of `UICanvasComponent`, the UI exposes an interactive `Data Source` field:

```
┌────────────────────────────────────────────────────────────────────────┐
│ ▼ 🖼️ UI Canvas Component                                            [...]
├────────────────────────────────────────────────────────────────────────┤
│ Asset Path:   [ 📄 assets/ui/RacingHUD.axleui                  ▼ ] [ 🔍 ]│
│                                                                        │
│ Data Source:  [ ⚙️ Self (NativeScript: CarController)          ▼ ] [ 🎯 ]│
│               *(Auto-detects NativeScript residing on the same Entity)*│
│                                                                        │
│ Sort Order:   [ 0 ]                                                    │
│ [x] Visible                                                            │
│                                                                        │
│ Nodes in DOM: 18 | Reference Resolution: 1920 x 1080                   │
│ [ Save .axleui Document ] [ Reload from Disk ]                         │
└────────────────────────────────────────────────────────────────────────┘
```

#### Mode A: Convention over Configuration (Auto-Detection / Zero Setup)
- **Same Entity:** When `UICanvasComponent` and `NativeScriptComponent` (e.g. `CarController` or `RacingHUD`) coexist on the same Entity:
  - The `Data Source` property automatically detects the script and displays: `[ ⚙️ Self (NativeScript: <ClassName>) ]`.
  - Both components establish a bidirectional link automatically without requiring the user to perform any drag-and-drop or manual configuration.
  - Eliminates unlinked references, null-pointer errors, and configuration fatigue.

#### Mode B: Drag-and-Drop Retargeting (External Entity Target)
- **Different Entity:** When the UI Canvas is an independent 2D Screen-space overlay decoupled from the 3D vehicle in the world:
  - The user simply drags the target Entity (e.g. `Car`) from the **Scene Hierarchy** and drops it onto the `Data Source` field.
  - The display updates to: `[ 🚗 Car (NativeScript: CarController) ]`.
  - An adjacent `[ 🎯 ]` / `[ x ]` button provides rapid entity selection via popup and quick reset to default `[ Self (Auto) ]`.

---

### 2.2. Dual-Channel Data Exposure: Reflected Fields + Scoped UIDataContext

When `UICanvasComponent` binds to a `NativeScriptComponent`, the binding engine (`UIDocument::EvaluateBindings`) has access to two distinct data streams:

1. **Reflected Script Fields (`AXLE_SCRIPT_FIELDS`):**
   - Any public script variables exposed via the reflection macro (e.g., `Horsepower`, `MaxSteerAngle`, `BrakePower`, `SteerSpeed`, `BoostMultiplier`) are automatically exposed to `.axleui` templates.
   - Declarative markup can directly bind to reflected field identifiers:
     ```qml
     Text { text: "Power: {Horsepower} HP" }
     ```

2. **Scoped `UIDataContext` Telemetry:**
   - Scripts update local metrics using `GetEntity().GetLocalUIDataContext().Set("SpeedKmh", ...)` for high-frequency runtime telemetry.
   - This telemetry is strictly encapsulated within the target Entity instance, avoiding collisions in multi-car simulations.

3. **Fallback to Scene Globals:**
   - If an evaluated `{...}` token is not found in the local Data Source, the engine falls back to `Scene::GetUIDataContext()` for global engine metrics (`{Engine.FPS}`, `{MatchTime}`, `{NetworkPing}`).

---

### 2.3. Scene Hierarchy Cleanliness: DOM Decoupling

Following the architectural standards of Unreal Engine (Widget Blueprints) and Unity (UI Toolkit `UIDocument`):

- **Single-Row Representation:** The Scene Hierarchy renders `UICanvas` as an atomic scene entity, identical to `Directional Light`, `Camera`, or `Car`.
- **DOM Tree Suppression:** Internal layout micro-nodes (`Row`, `Column`, `Rectangle`, `Text`, `RadialGauge`) are **NOT** auto-expanded inside the 3D scene tree.
- **Dedicated Editing Surfaces:** Fine-grained widget inspection, visual layout tweaking, and QML editing are routed to:
  1. The **UI Canvas Designer** panel (`.axleui` visual workspace with live interactive gizmos).
  2. The **Entity Inspector**, which displays high-level canvas status and properties.

---

### 2.4. Searchable Asset Picker for `AssetPath`

The raw string input field in `EntityInspector.cpp` is replaced by an interactive **Searchable Asset Picker**:

```
┌───────────────────────────────────────────────────────────────┐
│ UI Canvas Component                                           │
├───────────────────────────────────────────────────────────────┤
│ Asset Path:    [ 📄 assets/ui/RacingHUD.axleui     ▼ ] [ 🔍 ] │
│                                                               │
│   ┌── SEARCHABLE POPUP (Opens on Click) ───────────────────┐  │
│   │ 🔍 Filter .axleui: [ racing                           ]│  │
│   ├────────────────────────────────────────────────────────┤  │
│   │  [ None / Clear ]                                      │  │
│   │  📄 RacingHUD.axleui      (assets/ui/RacingHUD.axleui) │  │
│   │  📄 PauseMenu.axleui      (assets/ui/PauseMenu.axleui) │  │
│   └────────────────────────────────────────────────────────┘  │
└───────────────────────────────────────────────────────────────┘
```

- **Project Scanning:** Automatically discovers all `.axleui` files across `assets/` and `Editor/assets/`.
- **Interactive Search:** Real-time character filtering.
- **Automatic Loading:** Selecting an item immediately updates `AssetPath` and invokes `uic.Document->LoadFromFile()`.
- **Drag & Drop Retained:** Dragging an `.axleui` item from the Content Browser into the field remains fully functional.

---

### 2.5. Component Registry: "User Interface" Category

In [`Engine/UI/src/EntityInspector.cpp`](file:///d:/WorkSpace/AxleEngine/Engine/UI/src/EntityInspector.cpp#L2442), the hardcoded list of root categories is updated:

```cpp
// Before:
const std::vector<std::string> categories = { "Rendering", "Physics", "Scripts", "Audio/VFX" };

// After:
const std::vector<std::string> categories = { "Rendering", "Physics", "Scripts", "Audio/VFX", "User Interface" };
```

This immediately surfaces:
`Add Component -> User Interface -> UICanvasComponent`

---

## 3. Comparison with Commercial Game Engines

| Feature / UX | **AxleEngine** (Current Design) | **Unity** (UI Toolkit 2023/6) | **Unreal Engine 5** (UMG MVVM) | **Godot 4** |
| :--- | :--- | :--- | :--- | :--- |
| **Default Binding** | `Self` (Auto-detects `NativeScript` on same Entity) | Contextual Fallback to `GetComponent<T>()` on same GameObject | `Owner Actor` (Actor owning `UWidgetComponent`) | Node's attached script or `owner` |
| **Retargeting UX** | Drag-and-drop Entity from Hierarchy into `Data Source` | Drag GameObject / Component / ScriptableObject into `dataSource` | Select `Source Type: Object Reference` & drag Actor / ViewModel | Drag Node into `@export var target` field |
| **Reflection Fields** | Exposes `AXLE_SCRIPT_FIELDS` automatically | Exposes `[CreateProperty]` / serialized C# properties | Exposes `UPROPERTY(FieldNotify)` | Exposes `@export` script variables |
| **Hierarchy Representation** | Single atomic Entity row (DOM hidden from 3D tree) | Single `UIDocument` GameObject (UXML DOM hidden) | Single `UWidgetComponent` (Widget tree in UMG editor) | Separate Control tree or SubViewport |
| **Asset Selection** | Searchable popup picker (`.axleui`) + Drag & Drop | Asset field with circle picker (`.uxml`) + Drag & Drop | Asset drop-down with thumbnail picker (`WBP_`) | QuickOpen resource dropdown (`.tscn`) |

---

## 4. Technical Implementation Roadmap

### Phase 1: Hierarchy Cleanliness & Add Component Registration
1. In `Engine/UI/src/EntityInspector.cpp`, add `"User Interface"` to `categories` vector so `UICanvasComponent` appears in the menu.
2. In `Engine/UI/src/SceneHierarchyPanel.cpp` (lines 1066-1075 & 1097), remove `ImGuiTreeNodeFlags_DefaultOpen` or suppress DOM tree auto-expansion so `UICanvas` remains a single clean row.
3. Verify clean display in Scene Hierarchy.

### Phase 2: Searchable Asset Picker for `AssetPath`
1. Implement a helper scanning `.axleui` files across `assets/` and `Editor/assets/`.
2. Replace raw `ImGui::InputText` in `EntityInspector.cpp` with a combo/popup search picker with live filter.
3. Retain drag-and-drop support from Content Browser.

### Phase 3: Scoped Data Source Mechanism on `UICanvasComponent`
1. In `Engine/Scene/include/Axle/Components.hpp`:
   - Add `UUID DataSourceEntityID = 0;` (0 = Self / Auto-detect).
   - Add helper methods `Entity GetDataSourceEntity(Scene* scene, Entity currentEntity) const;`.
2. In `Engine/UI/src/EntityInspector.cpp`:
   - Render `Data Source: [ Self (NativeScript: ClassName) ]` if `DataSourceEntityID == 0` and current entity has script.
   - Accept drag-and-drop payload `AXLE_DND_ENTITY` from Scene Hierarchy into `Data Source`.
   - Provide a reset/clear button to restore `Self (Auto)`.
3. In `Engine/UI/src/UI2D/UIDocument.cpp` (`UIDocument::EvaluateBindings`):
   - Query the target entity's `NativeScriptComponent` reflected fields and local `UIDataContext`.
   - If token is found, return the local value; otherwise, fallback to global `Scene::GetUIDataContext()`.

### Phase 4: Automated Verification & Unit Test Suite
1. Run `cmake --build build --config Release --target AxleEditor AxleTests`.
2. Execute `AxleTests.exe --summary` to verify that all 96 unit test suites pass with 0 failures.
3. Validate runtime HUD telemetry binding with `RacingHUD.axleui`.
