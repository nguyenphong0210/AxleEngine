# Phase 41: Implementation Guide for Codex and Gemini

> **Execution:** follow [OPT-00..11](../ROADMAP.md). This document is a
> technical contract, not a separate phase queue. Old Phase/G labels and
> source-audit baselines are retained for traceability. Early Vehicle retirement
> is delivered; independent review and new plugin implementation remain open.

Date: 2026-11-03 (Asia/Saigon). Source baseline: `492e6d1`.
Status: **coding blueprint; proposed APIs, not shipped interfaces**.

Read root `AGENTS.md` or `GEMINI.md`, the [working protocol](../agents/WORKING_PROTOCOL.md),
[live assignment](../agents/CODEX_GEMINI_EXCHANGE.json),
[architecture](RHI_PRIMARY_RENDERER_ARCHITECTURE.md) and
[package plan](RHI_PRIMARY_RENDERER_IMPLEMENTATION_PLAN.md). Implement only the
user's released package/range. An old prompt or this entire guide is not a task.
This documentation delivery releases no production code changes.

## 1. Starting procedure and source map

1. Read current HEAD/status/inbox and actual source. Preserve peer/user edits.
2. Identify the released package, dependencies and missing acceptance evidence.
3. Post your own STATUS naming owned files. Use the shared checkout sequentially.
4. Add a failing production-path regression for the defect/contract, implement the
   smallest complete change, rebuild and run the relevant checks.
5. Commit coherent behavior/tests and record real evidence. Continue through an
   already authorized range; stop at its boundary. Routine decisions within that
   range do not require repetitive permission. No push unless requested.

| Contract | Starting files |
| --- | --- |
| Startup/readiness | `Editor/src/Main.cpp`, `Engine/RHI/src/RHISwitcher.cpp`, `RHIDevice.cpp`, `Engine/Core/src/Application.cpp`, `Engine/Platform/src/Window.cpp` |
| Resource creation | `Engine/RHI/src/RHITexture.cpp`, `RHIBuffer.cpp`, `RHIMesh.cpp`, `RHIPipeline.cpp`, `RHIDescriptorPool.cpp` |
| Frame/state/present | `Engine/RHI/src/RHICommandBuffer.cpp`, `RHIBarrier.cpp`, `RHISwapChain.cpp`, `Engine/UI/src/ImGuiLayer.cpp` |
| Scene extraction/resolution | `Engine/Scene/include/Axle/Components.hpp`, `Engine/Scene/src/RHIMeshRegistry.cpp`, `RHISceneRenderer.cpp`, `RHIObjectPicker.cpp`, `SceneSerializer.cpp` |
| Asset decode/upload | `Engine/Asset/src/AssetSerializer.cpp`, `AssetManager.cpp`, `Engine/Scene/src/MaterialTextureCache.cpp` |
| View consumers | `Editor/src/EditorLayer.cpp`, `Engine/UI/src/{AssetPreviewPanel,VFXEditorPanel,AssemblyEditorPanel,AssetThumbnailManager,RHIViewportDescriptor,AssetInspector}.cpp` |
| Runtime/build boundaries | `Runtime/src/{Main,RuntimeLayer}.cpp`, root/module/plugin CMake, `Editor/CMakeLists.txt` |
| Existing GPU regression | `Tests/src/Test_VFXRendering.cpp`, `VFXGPUTestMain.cpp`, material rendering tests |
| Animation bridge | `Engine/Animation`, `Engine/Scene/src/AnimationSceneBinding.cpp`, Phase 40 F1-F3 contracts |

Brace notation above groups paths; it is not a literal filename or shell command.
Move code only when its replacement caller/build boundary is working.

## 2. Proposed minimum contracts

These sketches express semantics; adapt names/types to the repository. Do not
create a framework of unused interfaces or duplicate a production facility.

```cpp
struct BackendStatus {
    GraphicsAPI Selected;
    bool ContextReady;
    bool NativeDeviceReady;
    RendererCapabilities Features; // Implemented renderer features, not GPU marketing tiers.
    RenderDiagnostic Diagnostic;
};

struct ResourceHandle {
    uint32_t Slot;
    uint32_t Generation;
    uint64_t DeviceEpoch;
};

struct CompletionToken {
    uint64_t DeviceEpoch;
    uint32_t Queue;
    uint64_t Value;
};

struct ViewRequest {
    ViewId View;
    CameraData Camera;
    Extent2D Extent;
    RenderOptions Options;
    RenderWorldRef World; // Immutable until recording leases are released.
};

struct RenderResult {
    RenderStatus Status;
    ViewTextureLease Output; // Acquired before handing the image to UI recording.
    RenderDiagnostic Diagnostic;
};
```

Selection IDs, authored UUIDs and GPU handles are distinct types. Serialization
writes authored identities, not `ResourceHandle` slots, native addresses or fences.
Keep ownership in RAII objects; tokens identify completion but do not themselves
keep a device or native allocation alive.

## 3. Algorithms and invariants

### Backend startup

Resolve CLI/project/default precedence once. Try an allowed backend as a complete
startup transaction: device/context -> required interfaces -> presentation ->
renderer -> optional UI bridge. Publish readiness only after that product's
required stages succeed. On failure, destroy partial owners in reverse order.

Honor strict/no-fallback even when Vulkan UI support was not compiled. Unsupported
UI is a startup diagnostic, not permission to silently change a strict backend.
Explicit GL and fallback GL share the same context/asset/resource factory path.
Do not call NRI functions merely because `IsInitialized()` is true.

### Transactional resource creation

1. Validate requested descriptor and queried native support before allocation.
2. Allocate into a local candidate, including all required views and upload work.
3. If any step fails, destroy every partial native wrapper; do not expose it.
4. If the device remains usable, keep the previous published generation.
5. On success, publish the new generation and retire the replaced generation
   through recording/submission completion. Do not call immediate `Shutdown()` on
   a resource that another frame still references.

Inject failure after a real NRI wrapper has been created to test allocation failure
without exhausting VRAM. Restore injected callbacks using scoped RAII even when
a test fails. Tests may control native-operation results; GPU acceptance still
uses real textures, commands and pixel readback.

### Primitive and asset resolution

Use `PrimitiveKind -> canonical builtin asset identity -> CPU geometry -> selected
backend resource` through one resolver. Always add a render component, even when
no GL context exists. Do not fix the bug by creating GL buffers on Vulkan/D3D12.

Render/picking/serialization/assembly/plugin consumers share this resolution.
Legacy name-based inference may be used only in an explicit, tested migration
for a known legacy schema, never as ongoing render-time geometry selection.

### Recording leases and retirement

```text
Acquire generation for view/UI recording
    -> hold recording lease
    -> successful submit associates the lease with completion token(s)
    -> after all relevant GPU completions, release submission references
    -> destroy only when no recording, submission or persistent owner remains
```

On abandoned/failed recording, release recording references without claiming GPU
completion. A failed submit must not fabricate a signaled token or advance known
resource states. Lost-device cleanup uses an explicit terminal policy and owning
device context; it must not wait on a fence that can never signal.

Test the same-frame case: queue a red image, invalidate it, allocate green before
submission, then submit the original image and read red pixels. Also test prior
frames in flight; these are different lifetime obligations.

### Frame execution

```text
Poll completion and errors
Acquire a reusable frame slot (wait only when it is still busy)
Acquire presentation image, or skip when minimized
Freeze scene snapshots and resolve retained resource generations
Record uploads/views/UI with declared resource usage
Submit checked work and commit submitted states
Present through the owning coordinator
Retire completed generations and release abandoned work
```

ImGui backend recording may remain native. Its descriptor heap/pool and commands
must be integrated with the same slot and lifetime policy. UI does not create a
second independent application swapchain/present loop. Keep one graphics queue
initially; do not add cross-queue synchronization before it is needed and tested.

### Resource states and graph

Represent resource generation plus subresource range and usage. New allocations
start from the real initial state; imports declare their actual externally owned
state. Compile dependencies/barriers from pass declarations, then record commands.
Keep submitted state unchanged when graph compilation/recording/submission fails.

The first graph needs attachment/load-store validation, dependency ordering,
optional passes and clear diagnostics. Do not add aliasing, async compute or a
shader graph as prerequisites. Persistent/UI resources are imported leases, not
temporary graph resources destroyed at graph end.

### Scene extraction and coordinate/color behavior

Extract on the scene-owning thread into immutable records. Compute complete world
transforms for meshes, lights and cameras. Capture the actual geometry/pose
generation and stable entity mapping. A RenderWorld must not store references
into a mutable EnTT view or mutate authored material data while drawing.

Define one engine convention for matrices/depth and linear-light values. Translate
API clip/UV differences behind adapters and name the single tone-map/output-sRGB
owner. Prove orientation/depth/color with asymmetric fixtures; a white symmetric
sphere cannot detect flipped UVs or incorrect handedness.

### Module migration

Do not start by renaming the existing `Engine/Renderer` directory. Establish the
contracts and service callers, then relocate implementations behind real CMake
boundaries. Move EnTT extraction and material/light domain policy above RHI.
Split runtime/editor plugin libraries; inspect transitive links, public includes
and post-build dependencies, not just the executable's direct link list.

During transition, GL and modern adapters may retain their private resource caches.
They must consume the common CPU identity/RenderWorld contract. Remove old fields
and old submission paths only after their final consumers and tests have moved.

## 4. Test layout and commands

P41-00 should introduce isolated render contracts/tests under proposed
`Tests/rendering/` rather than adding every new test to the Editor executable.
The following names/options are **proposed; not available at the planning baseline**:

| Proposed target | Coverage |
| --- | --- |
| `AxleRenderUnitTests` | Backend policy, identities, extraction, graph validation and retirement decisions; no GPU/window/UI initialization |
| `AxleRenderGPUTests` | Real D3D12/Vulkan resource/pixel/state/lifetime checks with explicit backend selection |
| `AxleRenderProductTests` | Real window/present/GL compatibility and standalone fixtures; process exit/timeout evidence |

Add an always-active failure self-test, backend-explicit test selection, labels and
nonzero failure exits. Missing prerequisites are NOT RUN; required acceptance CI
must treat an unavailable required row as incomplete. Keep physical hardware
acceptance distinct from software-adapter policy tests.

Once the corresponding targets/options really exist, commands may look like:

```powershell
cmake --build build --config Debug --target AxleRenderUnitTests
ctest --test-dir build -C Debug -L '^render$' -L '^cpu$' --output-on-failure
cmake --build build --config Release --target AxleRenderUnitTests AxleRenderGPUTests AxleRenderProductTests
ctest --test-dir build -C Release -L '^render$' -L '^cpu$' --output-on-failure
cmake -S . -B build/render-runtime -DAXLE_BUILD_EDITOR=OFF -DAXLE_BUILD_RUNTIME=ON
cmake --build build/render-runtime --config Release --target AxleRuntime
```

Verify actual test label definitions/selection and consumed CMake options before
using these examples. An unused `AXLE_BUILD_EDITOR=OFF` cache variable does not
prove an Editor-free build. Run backend-specific hardware/windowed checks through
the harness flags implemented in P41-00; do not invent passing commands in reports.

Small fixture requirements: five builtins, an asymmetric UV mesh, nested
transforms/lights, alpha-cutout geometry, two distinct environment maps, a weighted
animated mesh with two independent poses, a neutral multi-emitter effect and a
minimal cooked start scene. Generate them outside user asset directories and
record deterministic inputs; no bundled fake success or hardcoded example scene
may stand in for the production importer/creation/service path.

## 5. Acceptance and delivery discipline

- Rebuild the executable that will be run. Do not call old binaries proof of new code.
- Report exact commands, configs, backend/hardware, exit codes and artifact paths.
- Separate source review, CPU checks, offscreen GPU readback, native UI behavior,
  standalone execution and performance measurement.
- Preserve material/VFX/animation regressions; a shared renderer migration is not
  permission to weaken their assertions or silently replace shaders/effects.
- Review relevant native validation output. Expected injected failures are labeled;
  unexpected validation/device errors are failures requiring investigation.
- Update only your own messages/cursor and the package ledger you actually own.
  No fabricated peer ACK/review. Use local atomic commits and explicit staging.

## 6. Scoped assignment template

This template is reference text, **not an active release**. Use it only when the
user actually assigns the named package/range:

```text
Read GEMINI.md, the shared protocol/inbox, and the three Phase 41 documents.
Implement P41-00 only: always-active render regression harness and an evidence
baseline. Inspect actual APIs/CMake; proposed targets are not already shipped.
Preserve user changes and existing tests. Do not implement P41-01..29, Phase 40
D1/D2/G2, renderer feature expansion or a full architecture rewrite in this task.
Use coherent local commits, production-path checks and actual Debug/Release
evidence. Hardware/native checks unavailable in your environment are NOT RUN.
Write your own delivery to the exchange, with commits/commands/exits/limits.
Stop at the released task boundary. Do not push.
```

When a different package/range is assigned, replace the entire scope paragraph
and its dependencies/acceptance, rather than appending conflicting instructions.
