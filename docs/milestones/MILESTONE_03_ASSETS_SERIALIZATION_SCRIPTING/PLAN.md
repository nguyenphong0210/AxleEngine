# Milestone 03 — Asset Pipeline, Scene Serialization & Scripting Plan

**Status:** Completed Historical Milestone.
**Parent Document:** [`ROADMAP.md`](../../ROADMAP.md).

---

## Work Breakdown & Historical Checklist

- [x] **Phase 8: C++ Native Scripting & Vehicle Component (`Engine/Scene`)**
  - [x] **VehicleComponent in ECS**: Encapsulated vehicle configuration and physics handle as an entity component.
  - [x] **`ScriptableEntity` Lifecycle**: Base C++ class exposing `OnCreate()`, `OnUpdate(Timestep)`, and `OnDestroy()`.
  - [x] **`NativeScriptComponent`**: Template-based script binding to entities with zero runtime overhead.
  - [x] **Modular Gameplay Scripts**: Decoupled vehicle input (`CarPlayerScript`) into clean, reusable C++ scripts.
  - [x] **Editor Component Management**: Interactive `+ Add Component` popup in Properties panel, component removal options (`Remove Component`), prominent `+ Create` button with Search filter in Scene Hierarchy, and File menu actions (`Ctrl+S`, `Ctrl+O`, `Ctrl+N`).
  - [x] **Window Work Area & Fullscreen**: Automatic maximized window startup matching OS work area (zero taskbar overlap) and `F11` Maximize/Restore toggle.
- [x] **Phase 9: Asset System & 3D Model Ingest Pipeline (`Engine/Asset` & `Sandbox/assets`)**
  - [x] **Project Asset Architecture**: Standardized `Sandbox/assets/` structure (`models/`, `textures/`, `scenes/`, `materials/`, `shaders/`) and `Axle::FileSystem` resolver - separating game project assets from engine core.
  - [x] **Content Browser Panel (`Engine/UI`)**: ImGui-based visual asset browser with directory navigation, file type icons, search filter, path breadcrumbs, Explorer integration, and Drag & Drop payload support.
  - [x] **Drag & Drop Workflow**: Drag 3D model files from Content Browser directly into Viewport or Properties panel to instantiate meshes with zero-gap ground snapping and centered pivots.
  - [x] **3D Asset Thumbnails & Interactive Asset Preview (`Engine/UI`)**: On-demand studio-lit 3D thumbnails rendered directly on Content Browser cards with frame-sliced caching, plus a dedicated interactive 3D Asset Preview window with turntable auto-orbit, wireframe inspection, geometry stats, and direct scene instantiation.
  - [x] **Unified `.axle` Asset Pipeline & Auto-Conversion**:
    - [x] **Step 1: Core Engine/Asset Module & AssetSerializer**: Structured text format `.axle` (AI-friendly & human-editable) + Automated flat binary caching `.axlebin` (<1ms GPU loading) with smart timestamp validation (`last_write_time`) for automatic re-baking when text is modified. Seamless drag & drop support for `.axle` assets into Viewport, Scene Hierarchy, and Mesh Inspector.
    - [x] **Step 2: Ingest & Auto-Conversion Pipeline**: Dragging external 3D files (`.obj`, `.gltf`, `.glb`, `.fbx`) directly from Windows Explorer into Content Browser or Viewport automatically ingests and converts them into engine-standard `.axle` format with instant background `.axlebin` cache generation and seamless scene instantiation.
    - [x] **Step 3: AI Mesh Mutation & Hot-Reload**: AI agents and developers can edit `.axle` files as text; the Engine features a dedicated Editor Compiler button (`F9`), auto-compile before simulation Play (`F5`), and instant GPU VertexArray and material hot-reloading in real time without restart.
    - [x] **Step 4: Reverse Mesh Export (`.obj`)**: Export `.axle` / `.axlebin` meshes back into standard Wavefront `.obj` format with full material libraries (`.mtl`), grouped subparts, and smooth vertex normals for external DCC (Blender, Maya) interop and mesh inspection via right-click context menu.
- [x] **Phase 10: Scene Serialization & Project Management (`Engine/Scene` & `Engine/Project`)**
  - [x] **Step 1: Core SceneSerializer (`.axlescene`)**: Built-in structured YAML-like scene serializer saving and restoring complete ECS scene state (UUIDs, TagComponents, TransformComponents, `MeshInstance3D` with builtin/custom models, Jolt `RigidBody3D` with decoupled `CollisionShape3D` shapes and backward-compatible auto-migration, HierarchyComponents parent-child links, and NativeScriptComponents; backward-compatible with legacy names).
  - [x] **Step 2: Interactive Save/Open Workflows**: Connect `Ctrl+S` (Save Scene), `Ctrl+Shift+S` (Save Scene As), `Ctrl+O` (Open Scene), `Ctrl+N` (New Scene) with native OS file dialogs (`Axle::FileDialogs`), dynamic window title indicator (`Axle Engine - Project - [SceneName.axlescene]`), and full Main Menu Bar integration (`File -> New / Open / Save / Save As / Exit`).
  - [x] **Step 3: Content Browser Scene Loading**: Double-click scene files (`.axlescene`) in Content Browser to instantly load them into the active Viewport, with right-click context menu ("Open Scene in Viewport"), dedicated Scene clapperboard icon (`SCN`), and drag-and-drop scene loading onto the Viewport.
  - [x] **Step 4: Project Configuration System (`.axleproj`)**: Project file storing project name, asset directory root, and start scene with `File -> New Project`, `File -> Open Project` (`Ctrl+Shift+O`), `File -> Save Project`, Content Browser `.axleproj` project blueprint icon (`PRJ`), and dynamic window title synchronization.
- [x] **Phase 11: Lighting, Dynamic Shadows & PBR Materials**
  - [x] **Directional Sun Light & Real-Time Shadow Mapping**:
    - [x] Depth Framebuffer (`GL_DEPTH_COMPONENT32F`, 2048x2048, border clamped), Light Space Matrix calculation, and `Scene::RenderShadowPass()`.
    - [x] Two-pass rendering pipeline with 3x3 Percentage-Closer Filtering (PCF) soft shadows and hardware polygon offset slope-scaled depth bias to eliminate shadow acne.
    - [x] ECS `DirectionalLightComponent`, Sun Light entity management, interactive Inspector controls (Sun Azimuth & Elevation sliders, Color picker, Intensity, Ambient fill, Cast Shadows toggle, Atmosphere presets), and full scene serialization.
  - [x] **Texture Mapping (`stb_image`)**:
    - [x] Standard `stb_image.h` integration, OpenGL 4.5 Core Profile `Axle::Texture2D` abstraction (PNG/JPG/TGA/BMP, RGBA8/RGB8/RG8/R8, auto mipmaps, configurable wrap/filter modes), with pre-baked 1x1 fallback textures.
    - [x] Upgraded Vertex Layout & UV coordinates (`a_TexCoord`) across Primitive Meshes and `.axle` loaders; Texture Sampler Slots (`u_AlbedoMap`, `u_ShadowMap`, `u_NormalMap`, `u_RoughnessMap`, `u_MetallicMap`) with screen-space cotangent-frame normal perturbation.
    - [x] `MaterialComponent` in ECS, Inspector UI with texture map slots, live thumbnail previews, and drag-and-drop assignment.
  - [x] **PBR Material System (Cook-Torrance Microfacet BRDF)**:
    - [x] Physically based Cook-Torrance microfacet specular BRDF (Trowbridge-Reitz GGX distribution, Smith-Schlick shadowing, Fresnel-Schlick reflectance), energy conservation, metallic workflow, and directional sun radiance integration.
    - [x] HDR Filmic Tone Mapping (ACES Narkowicz approximation, Reinhard, Uncharted 2 Hable, and Linear Bypass modes) & display gamma correction ($1/\gamma$), with real-time Exposure and Tone Mapping controls.
  - [x] **Atmospheric Skybox**:
    - [x] Procedural atmospheric Rayleigh / Mie scattering skybox pipeline (`Axle::Skybox`), camera-centric unit cube renderer at infinity, analytical sharp sun disk with anti-aliasing, sunset/sunrise golden horizon warming, night darkening with procedural twinkling stars.
    - [x] ECS `SkyboxComponent`, dynamic Sun Light orientation synchronization, interactive Inspector controls, and ambient hemisphere irradiance calculation.
- [x] **Phase 12: Assembly Pipeline & Unified `.axle` Prefab System (`Engine/Asset`, `Engine/Scene` & `Engine/UI`)**
  - [x] Standardize unified asset specification in `.axle` format with `AssetType: Assembly` header (YAML-like structured text, optimized for both human editing and AI prompt mutation).
  - [x] `AssemblySerializer::Serialize()` to recursively save complete entity hierarchies preserving all ECS components.
  - [x] `AssemblySerializer::Deserialize()` with automatic UUID Remapping to guarantee collision-free instantiation when spawning multiple assembly instances in the same scene.
  - [x] Drag & drop prefab workflows, dedicated `AssemblyEditorPanel` isolated 3D viewport, and scene instantiation.
- [x] **Phase 13: C++ Native Scripting Ecosystem & Mechanical Assembly Architecture (`Engine/Scene` & `Engine/UI`)**
  - [x] Core Static Reflection & Script Registry (`ScriptField` & `ScriptRegistry`) supporting `Float`, `Int`, `Bool`, `Vec3`, and `String` with min/max slider ranges and tooltips.
  - [x] Automated `AXLE_REGISTER_SCRIPT(ClassName)` macro registration.
  - [x] Equipping `Entity` and `ScriptableEntity` with hierarchy traversal helpers (`FindChildByTag`, `GetParent`, `GetChildren`, `ForEachChild`) utilizing `HierarchyComponent`.
  - [x] Editor Workflow: Right-click **`Create -> C++ Script (.hpp)`** in Content Browser with auto-generated boilerplate.

---
