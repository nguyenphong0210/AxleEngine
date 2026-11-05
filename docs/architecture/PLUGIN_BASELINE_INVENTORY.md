# PLG-00: plugin baseline and retirement inventory

Date: **2026-11-05**. Original delivery: Gemini `179d66e`, baseline `8d1fece`.
Reviewed and corrected by Codex under the user's combined OPT-01 review request.
This is current source inspection plus explicit CPU baseline coverage, not a
plugin-framework implementation or historical Vehicle compatibility guarantee.

## Current source and build topology

[Root CMake](../../CMakeLists.txt) includes Engine modules, both Spline/Terrain,
Editor, Runtime and explicit Tests. No Engine/Vehicle directory or target remains.
[Spline](../../Plugins/Spline/CMakeLists.txt) is a STATIC library combining Curve3D,
TrackGenerator/component/serializer code and SplineEditor tooling. It links Core,
Renderer, Physics, Scene and UI. [Terrain](../../Plugins/Terrain/CMakeLists.txt) is
STATIC, combines TerrainData/mesh/collider/sculpting and TerrainEditor code, and
links Core, Renderer, Physics, Scene, UI and Spline. This is not selected DLL loading.

Both [Editor](../../Editor/CMakeLists.txt) and [Runtime](../../Runtime/CMakeLists.txt)
link Spline and Terrain unconditionally. Runtime thus still carries their static
UI-linked dependencies; its log describing a Zero-ImGui Shipping Runtime is not
proof of a clean binary boundary. No module disabling/build-exclusion acceptance
has been established. CPU tests do not initialize graphics, but link existing
production module libraries; they are not proof of graphics-free library linkage.

## Startup and actual update order

[Application](../../Engine/Core/src/Application.cpp) owns Log::Init, JobSystem::Init
and PhysicsEngine::Init before product layers attach. On teardown it currently
shuts down PhysicsEngine before JobSystem. That global ownership is later AF/EJ
work; neither EditorLayer nor RuntimeLayer constructs an m_PhysicsEngine object.

[EditorLayer::OnAttach](../../Editor/src/EditorLayer.cpp) initializes audio, then
SplinePlugin::Initialize and TerrainPlugin::Initialize, synchronizes script input
context and initializes the script reload manager. Its active scene member is
m_Scene, not m_ActiveScene. [RuntimeLayer::OnAttach](../../Runtime/src/RuntimeLayer.cpp)
initializes audio, script context/module, Canvas, PhysicsScene and SceneRenderer,
then loads the selected project/start scene. It has no matching explicit SplinePlugin
or TerrainPlugin initialization call. Do not invent a shared startup service here.

Current Editor Play calls Scene::OnUpdateRuntime before PhysicsScene::Step(simTs).
Runtime calls PhysicsScene::Step(ts) before Scene::OnUpdateRuntime. Editor paused
single-step takes a separate physics-only branch. This is the known ECSJ-05 schedule
mismatch, not parity established by PLG-00. Later EJ-11..14 own shared simulation.

[Scene](../../Engine/Scene/src/Scene.cpp) still owns native script, Character,
animation/audio/VFX and transform/readback phases. General physics and character
behavior remains Engine code; the removed Vehicle runtime collection/query/tick
is absent. Inspect actual declarations rather than relying on imagined wrapper
APIs such as SynchronizePhysicsTransforms or CreateBody/SetTransform.

## Retirement ledger

The historical implementation is available from `503715e`; retirement commits are
`fa74981` and `be7b071`. Historical names are reference facts, not supported APIs.

| Category | Current disposition | Future owner |
| --- | --- | --- |
| General PhysicsScene, rigid bodies, collision shapes and Character | Retained production Engine behavior; covered selectively by existing suites | EJ physics/lifecycle fixes, then generic plugin seams |
| Old Vehicle simulation/drivetrain/suspension | Built-in module/targets/lifecycle removed | Fresh Vehicle plugin PLG-14..18 |
| VehicleComponent, WheelComponent/VehicleWheel3D, VehicleAudioComponent, VehicleVFXComponent | Removed, including old passive compatibility holders | New component identities/codecs in PLG-14; no old-schema support required |
| Vehicle Inspector/debug/telemetry bindings | Hardcoded runtime ownership removed; generic UIDataContext remains | PLG-16 and PLG-18 through generic interfaces |
| CarController/RacingHUD/VehicleChaseCamera fixture scripts | Preserved as historical fixture files, explicitly excluded by Tests CMake | PLG-21/sample accounting if later assigned |
| Old authored CarTest/SportsCar and similar fixture data | Historical files retained; no promise of legacy load/save preservation or old simulation | No legacy reader/migration authorized |
| Current generic Spline/Terrain tooling and runtime data | Still statically bundled with UI dependencies | Spline PLG-19, Terrain PLG-20 |

The latest user identity policy is raw PNG/external resource paths and UUIDs inside
Engine-owned asset files. No per-asset sidecars or authored central registry are
required. Retired unknown data must not be revived to turn a baseline test green.
The original inventory incorrectly promised safe legacy interpretation and named
VehicleWheelComponent; that contract is superseded by the user and corrected here.

## CPU baseline coverage and limits

Tests/plugin/Test_PluginBaselineUnit.cpp calls the real Spline/Terrain lifecycle
functions, verifies reentry state, operates Curve3D and TerrainData, simulates an
ordinary falling rigid body, verifies Jolt-owned debug-renderer singleton lifetime, exercises a vehicle-free Scene runtime lifecycle and
checks arbitrary inherited UIDataContext keys. It does not load a project DLL,
prove reflected plugin component registration/unregistration, verify GPU renders
or produce a selected packaged Runtime. The original description overstated
reflection coverage; Initialize/Shutdown flag checks are lifecycle evidence only.

The always-active test executable and exact intentional-failure probe run in
Debug/Release. The deliberate-failure CTest wrapper validates its expected event
and exact exit, so unrelated errors cannot masquerade as proof. Full current
commands/results and original-vs-correction evidence are recorded in the combined
OPT-01 review report. Physical render readback and automated Editor startup/UI-frame smoke are recorded there; manual UI/Play and selected packaged export remain NOT RUN.

## Subsequent package mapping

PLG-01 discovery, PLG-02 resolution, PLG-03 choices and PLG-04 product receipts
precede host/loader and generic lifecycle work in PLG-05..13. Fresh Vehicle is
PLG-14..18; Spline/Terrain conversion is PLG-19/20; scripts/static Runtime/export
are PLG-21..23. PLG-24/25 consolidate acceptance/docs. These are contracts, not
assignments. Follow [the sequential roadmap](../ROADMAP.md) and
[plugin package contract](../design/PLUGIN_SYSTEM_IMPLEMENTATION_PLAN.md).
