# M14 Render Architecture Repair Status

Updated: 2026-11-14 (Asia/Saigon)

This addendum records sequential repairs after the independent review in
`M14_RENDER_ARCHITECTURE_CODEX_REVIEW.md`. M15 remains unreleased.

## Completed checkpoints

### Policy service integration â€” commit `9c08a3a`

`RHISceneRenderer` now owns `LightingService`, `ReflectionService` and
`GIService` collaborators and invokes them from the live frame path. Their
fallback diagnostics are published to `RenderDiagnostics`, and DDGI scheduling
now follows the GI service decision. `RHIViewTargets::Validate` checks
`ScreenStorage` extent, format and UAV contract.

Verified:

- Debug build of `AxleRenderUnitTests` and `AxleEnvironmentRenderingTests`: exit 0.
- `AxleRenderUnitTests`: exit 0.
- D3D12 environment fixture: exit 0.

### Graph descriptor lifetime â€” commit `a9c2a04`

`RenderGraph::DeclareDescriptorRead` retains descriptor transitive leases, and
the live renderer routes graphics/compute descriptor-set binds through this
graph seam. The helper deliberately retains lifetime without inserting an
implicit barrier inside an active rendering scope; layout transitions remain
declared graph `Use` entries. An initial implementation that injected barriers
was rejected by the D3D12 TAA fixture and removed before commit.

Verified:

- Debug build of `AxleRenderUnitTests` and `AxleEnvironmentRenderingTests`: exit 0.
- `AxleRenderUnitTests`: exit 0.
- D3D12 environment fixture: exit 0.

### Live adapter lifecycle â€” commit `9626aaf`

The five legacy pass adapters now validate immutable world/camera/settings/view
context, native capability and active command recording during Configure/Prepare
and Record. Failures return pass-qualified diagnostics through the scheduler.
The concrete shader/recording body remains a legacy facade implementation and
is still a follow-up extraction boundary; this checkpoint does not claim full
U2 pass relocation.

Verified:

- Debug build of `AxleRenderUnitTests` and `AxleEnvironmentRenderingTests`: exit 0.
- `AxleRenderUnitTests`: exit 0.
- D3D12 environment fixture: exit 0.

### Runtime evidence â€” current rerun

The previous runtime launch failure (`0xC0000135`) was an execution-environment
issue: the test binary requires the Debug test/runtime/plugin DLL directories
on the DLL search path. With the built binary run from `build/Tests/Debug` and
PATH including `build/Tests/Debug`, `build/Engine/PluginSDK/Debug`,
`build/Runtime/Debug` and `build/Editor/Debug`:

```text
AxleRuntimeRenderTests.exe dx12
OPT05 Runtime PASS
exit=0
```

The fixture completed startup-scene load, simulation pause/resume, rendering
and clean shutdown on native D3D12.

### Canonical custom-material boundary â€” commit `9bf7da2`

Each live `DrawItem` now resolves and stores one `ResolvedSurface` before the
custom shader selection branch. Built-in GPU constants and custom-material
diagnostics therefore share the same validity and generation identity instead
of silently resolving only the built-in path. Custom graph property lowering
still owns its graph-specific packed buffers; this checkpoint closes the
canonical CPU status boundary without claiming advanced graph field parity.

Verified:

- Debug build of `AxleRenderUnitTests` and `AxleEnvironmentRenderingTests`: exit 0.
- `AxleRenderUnitTests`: exit 0.
- D3D12 environment fixture: exit 0.

### Live scheduler group dispatch â€” commits `92d149a`, `00ad942` and `115dfb2`

`RenderPassScheduler::RecordOne` now dispatches named groups at their actual
recording boundaries instead of running all adapters before the renderer body.
The renderer invokes `LightingAndShadows`, `OpaqueAndSky`, `ScreenSpace`,
`ReflectionAndGI` and `PostAndTemporal` in the live sequence. The concrete `SurfaceNormal` body is owned by the `ScreenSpace` adapter callback and shadow recording is owned by the `LightingAndShadows` adapter callback; the remaining large legacy bodies are still being migrated group by group.

Verified:

- Debug build of `AxleRenderUnitTests` and `AxleEnvironmentRenderingTests`: exit 0.
- `AxleRenderUnitTests`: exit 0, including named `RecordOne` dispatch coverage.
- D3D12 environment fixture: exit 0.

### Complete live pass schedule - commit `7f5deb7`

All eight groups now own their live Configure/Declare/Prepare/Record path in
`RHISceneRendererPasses.cpp`; the facade no longer manually declares/records
these bodies. The scheduler runs one ordered frame lifecycle, finalizes on
failure/exception and destroys all borrowed callbacks before return. Shadow and
probe setup happens before atlas imports, while light/environment and material
queue preparation happen before recording. Disabled is restricted to Configure;
fallback is retained across phases.

Debug CPU tests, D3D12/Vulkan/OpenGL environment fixtures and D3D12/Vulkan
acceptance measurements passed. Architecture/dependency checks passed. A focused
separate-context review accepted the U2 migration seam conditional on native
shadow gates. Those gates exposed a pre-existing U3 conversion defect (fixed
shading mode/cutoff); U2 validation stays partial until its sequential U3 repair
and shadow rerun. See the current U2 evidence for exact commands and limits.

### Canonical material policy preservation - commit `421cc49`

The expanded U2 shadow gates exposed a pre-existing U3 loss of textured shader
mode and alpha cutoff. The new CPU parity test failed before repair and passed
afterward. ResolvedSurface now carries shading/PackedORM mode, cutoff, detail
normal transform/strength and raster/queue policy. Its identity covers omitted
material policies, source paths and explicitly assigned native generations;
hashing does not allocate fallback textures. Live draws consume the resolved
raster/queue state.

Verified after a fresh Debug build: CPU suite; cascaded and local shadows on
D3D12 and Vulkan; environment on D3D12 and Vulkan; acceptance measurements on
D3D12 and Vulkan, all exit 0. Both native cascade tests measured 48401 solid vs
38617 alpha-hole depth pixels. This closes the outstanding U2 native gate.
Separate-context correction reviews found no remaining U2 blocker or blocker
in this U3 prerequisite, with the latter explicitly retaining the custom graph
gap and texture-content revision boundary.

### Particle snapshot direction and product gates - commit `06a6c4d`

The soft-particle product fixture exposed an existing reversed CopyTexture call:
ParticleDepth was used as an uninitialized source instead of the destination.
The corrected call matches the declared scene-depth -> snapshot dependency.
Actual D3D12/Vulkan soft-particle preview fixtures now pass, including soft
intersection fading. A focused separate-context source check confirmed the
remaining copy/resolve directions match their declarations.

Fresh Debug product builds and runs passed: surface previews on D3D12, Vulkan
and OpenGL; particles on D3D12/Vulkan; physics preview on D3D12; Runtime D3D12
loose/cooked startup/input/simulation/presentation/shutdown. Final Debug and
Release CPU builds/suites also passed (exit 0); native/product runs were Debug. Exact commands/logs and Runtime
DLL search paths are recorded in U2 evidence.

### Canonical shader surface API - commit `86feca7`

The shared resolver now owns common custom HLSL/graph engine inputs, property
packing, texture requests, evaluation domain and immutable material/program
identity. Invalid numeric/policy inputs reject resolution. Unsupported controls
have an explicit feature mask and diagnostic. Focused CPU contracts passed after
a fresh Debug build before committing this API checkpoint.

### Live canonical material integration - commit `f241eb0`

Scene built-in/custom draws, shadow alpha binding and typed MaterialGraphRuntime
now consume canonical surface inputs and texture owners. Snapshot compatibility
mirrors no longer form a second common GPU packing path. Actual native bindings
are folded into each fresh effective identity after cache/fallback selection;
same-path reload and neutral/last-good recovery are observable. Owners increment
`RenderMaterialData::ContentRevision` for in-place pixel writes. Per-material
validity/fallback/unsupported/program identity is published to frame diagnostics;
late material changes invalidate history contents without changing imported slots.
Custom draw failure reports the built-in fallback with shader generation zero.

Focused separate-context review exposed texture identity, unsupported-map/emission,
native validation, procedural union-packing and test-wrapper lifetime defects.
All reported defects were corrected before the final gates. Earlier corrections
and temporal ordering received intermediate rechecks; the last Texture2D/SRV
compatibility correction still requires its final independent recheck because
the reviewer invocation failed at its usage limit. No final independent PASS is
claimed.

Current verification (all exit 0): Debug/Release render CPU suite; Debug material
unit suite (5/5); Debug/Release graph compiler/runtime/resource policy and native
D3D12/Vulkan fixtures (18 runs, graph/reference image max error 0); Debug OpenGL
graph unsupported policy; Debug environment D3D12/Vulkan/OpenGL, cascade/local
shadows D3D12/Vulkan, surface preview D3D12/Vulkan/OpenGL, particle preview
D3D12/Vulkan and Runtime D3D12 loose/cooked. Architecture check passed (600 files,
25 CMake files), dependency map reported zero architecture/caller violations, and
`git diff --check` passed. Exact commands/logs/limits and source anchors are in
[U3 evidence](../milestones/MILESTONE_14_RENDER_ARCHITECTURE/U3_MATERIAL_SURFACE_EVIDENCE.md).
Release product runs and interactive Editor UI were not rerun in U3.

## Current acceptance state

- **PASS:** U2 live orchestration, including its explicit legacy cache ownership
  boundary. Previously verified U0, U1, U6, U7, U8 remain accepted.
- **PASS (implemented/self-verified):** U3 canonical shader/graph integration and
  its current CPU/native/product gates. **U3 acceptance remains PARTIAL** pending
  the final separate-context correction recheck required by the working protocol.
- **PARTIAL:** U4 complete graph resource declarations beyond descriptor
  retention; U5 policy ownership/integration review.
- **HOLD:** U9 until the remaining integration gaps and their native gates close.
