# M14-U2 - Pass Interface and Renderer Orchestration Evidence

Updated: 2026-11-14 (Asia/Saigon)

**Status: PASS for U2 live orchestration and the documented legacy cache boundary.**
Implementation commit: `7f5deb7`; canonical surface prerequisite: `421cc49`;
particle snapshot correction: `06a6c4d`.
Supersedes the original no-op adapter acceptance.
M15 lighting/material fidelity is excluded.

## Live ownership

`RenderPassScheduler::Execute` now runs Configure, Declare, strict graph compile,
Prepare, Record and Finalize in one deterministic order. Every actual graph
node is declared by its adapter and every live recording body executes through
that adapter. The synchronous frame schedule is:

1. LightingAndShadows
2. OpaqueAndSky
3. ScreenSpace (surface normal)
4. Transparent (refraction snapshot and transparent queue)
5. ParticlesAndResolve
6. ReflectionAndGI (DDGI, GTAO, Hi-Z/SSR and fog)
7. PostAndTemporal (bloom, exposure, TAA, tone mapping and FXAA)
8. Export

The implementation is in `Engine/Scene/src/RHISceneRendererPasses.cpp`.
`RHISceneRenderer.cpp` retains view acquisition, target selection, policy
resolution and immutable camera/jitter input construction. Probe/shadow setup
runs in LightingAndShadows Configure, before atlas generations are imported in
Declare. Light/environment preparation, descriptor publication and uniform
uploads run in Prepare; opaque material queues are also built in Prepare.

`FramePassInputs` explicitly borrows synchronous view inputs. Execute finalizes
and destroys all adapters before returning, on explicit failures and exceptions.
An outer frame guard also covers registration failure and closes open rendering
scopes. GPU dependencies remain retained by the command journal through queue
completion. ViewContext owns submission and temporal commit after successful
recording; Finalize receives the recording outcome before submission.

Disabled is valid only during Configure, before graph declarations. Late
Disabled is rejected. Fallback survives later successful phases. Failure stops
subsequent work with a pass-qualified diagnostic. Native execution also rejects
an incompletely consumed graph.

Persistent shader/pipeline/descriptor cache relocation remains the explicit
legacy adapter boundary in PLAN S2.3. This checkpoint establishes live pass
orchestration, not independent ownership of every cached RHI object.

## Verification at this checkpoint

Fresh Debug builds passed for AxleRenderUnitTests,
AxleEnvironmentRenderingTests, AxleRenderAcceptanceTests,
AxleCascadedShadowRenderingTests, AxleLocalShadowRenderingTests,
AxlePreviewRenderTests and AxleRuntimeRenderTests.

- `AxleRenderUnitTests.exe`: exit 0. Covers full phase order, Configure/compile/
  Prepare/Record failure, exception finalization, capture release, disabled and
  fallback policy. Separate-context correction review independently reran it:
  exit 0 (`build/m14-u2-review-cpu.log`).
- `AxleEnvironmentRenderingTests.exe dx12 build/Tests/Work/M14-u2-final/dx12`:
  exit 0, including exact graph diagnostic order, injected preparation failure,
  no-recording/no-completion rejection and recovery in the same renderer.
- Same environment fixture on Vulkan and OpenGL: exit 0.
- `AxleRenderAcceptanceTests.exe` on D3D12 and Vulkan: exit 0.
- `python scripts/check_architecture.py`: PASS, 598 files / 25 CMake files.
- `python scripts/generate_m14_dependency_map.py --output-dir build/Tests/Work/M14-u2-architecture`:
  exit 0, no architecture or product caller violations.
- `git diff --check`: exit 0.

The GPU CTest options are disabled in this build configuration; binaries above
were invoked directly. A CTest invocation that found no tests is not evidence.

## Closed native gate - canonical surface prerequisite

The broadened shadow fixtures initially exposed an existing U3 conversion fault:
ResolvedSurface hardcoded shading mode 3 and cutoff 0.5. The unchanged source
paths predated U2 (`b1f9a97`). A new CPU parity regression reproduced the fault
before the fix (`build/m14-u3-repro.log`, exit 1). No separate source-baseline
native run was performed.

Commit `421cc49` preserves textured/PackedORM mode, authored cutoff, detail
normal parameters and raster/queue policy. Material identity includes those
policies, source paths and explicitly assigned native generations. Hashing an
unassigned slot performs no fallback allocation; the native fixture verifies
this and native generation replacement. In-place pixel uploads are not material
assignment changes; textured cutout consumers deliberately redraw instead of
caching them.

Fresh Debug builds and direct runs after that prerequisite:

| Executable | Backend | Result |
|---|---|---|
| AxleRenderUnitTests | CPU | PASS, exit 0 |
| AxleCascadedShadowRenderingTests | D3D12 / Vulkan | PASS / PASS, exit 0 |
| AxleLocalShadowRenderingTests | D3D12 / Vulkan | PASS / PASS, exit 0 |
| AxleEnvironmentRenderingTests | D3D12 / Vulkan | PASS / PASS, exit 0 |
| AxleRenderAcceptanceTests | D3D12 / Vulkan | PASS / PASS, exit 0 |

Fixtures used `build/Tests/Work/M14-u3-surface/<case>`; logs are
`build/m14-u3-{cascades,local,environment,acceptance}-{dx12,vulkan}.log`.
Cascade cutout depth changed from 48401 solid pixels to 38617 with alpha holes
on both native backends. Local shadow same-generation alpha redraw, six faces,
seams, cache reuse/invalidation and allocation retry passed.

The focused separate-context recheck found no remaining U2 blocker, conditional
on the native gates now satisfied above. U2 is accepted at its live legacy
adapter migration boundary. Full U3 remains PARTIAL for the custom shader/graph
canonical lowering/status boundary; U4/U5 and overall U9 remain on hold.

## Product caller verification and particle snapshot correction

The extended soft-particle preview exposed a second pre-existing fault: the
ParticleDepthCopy call reversed the RHI destination/source arguments despite a
correct graph declaration. D3D12 validation rejected the uninitialized particle
snapshot as a CopyResource source. The reversed call also exists in `58a2d85`.
Commit `06a6c4d` copies scene depth into the particle snapshot, matching Declare.
The separate-context reviewer checked every other texture-copy/resolve direction
in the moved module and found no other mismatch.

After fresh Debug product builds:

- `AxlePreviewRenderTests.exe dx12 surfaces`: PASS, exit 0.
- `AxlePreviewRenderTests.exe vulkan surfaces`: PASS, exit 0.
- `AxlePreviewRenderTests.exe gl surfaces`: PASS, exit 0.
- `AxlePreviewRenderTests.exe dx12 particles`: PASS, exit 0 after the copy fix.
- `AxlePreviewRenderTests.exe vulkan particles`: PASS, exit 0 after the copy fix.
- `AxlePreviewRenderTests.exe dx12 physics`: PASS, exit 0.
- `AxleRuntimeRenderTests.exe dx12` and `dx12 cooked`: PASS, exit 0, including
  loose/cooked startup scene, simulation/input, eight presented frames and clean shutdown.

Preview logs: `build/m14-u2-preview-<backend>-<mode>.log`.
Runtime logs: `build/m14-u2-runtime-dx12.log` and
`build/m14-u2-runtime-cooked-dx12.log`. Runtime ran from
`build/Tests/Debug`, with the Debug Tests, Engine/PluginSDK, Runtime and Editor
DLL directories on PATH; no DLL-launch failure was waived.

Final Debug/Release CPU gate on the corrected source:

```text
cmake --build build --config Release --target AxleRenderUnitTests --parallel 6  => exit 0
build/Tests/Release/AxleRenderUnitTests.exe                                    => exit 0
build/Tests/Debug/AxleRenderUnitTests.exe                                      => exit 0
```

Logs: `build/m14-u2-release-build.log`, `build/m14-u2-release-unit.log` and
`build/m14-u2-final-unit.log`. Release native/product executables were not rerun
for this checkpoint; the native/product matrix above is Debug.
