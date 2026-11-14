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

## Current acceptance state

- **PASS:** U0, U1, U6, U7, U8 and the previously rerun runtime evidence gate.
- **PARTIAL:** U2 implementation is repaired, with native shadow verification
  pending the U3 material conversion fix. U3 also retains the custom graph
  canonical lowering gap; U4 retains incomplete graph resource declarations;
  U5 still needs review of actual policy/service ownership after integration.
- **HOLD:** U9 until the remaining integration gaps and native gates are closed.
