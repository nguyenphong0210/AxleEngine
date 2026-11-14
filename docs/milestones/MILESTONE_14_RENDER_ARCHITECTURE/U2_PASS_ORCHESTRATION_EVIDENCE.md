# M14-U2 - Pass Interface and Renderer Orchestration Evidence

Updated: 2026-11-14 (Asia/Saigon)

**Status: implementation repaired; native shadow validation remains PARTIAL pending U3.**
Implementation commit: `7f5deb7`. Supersedes the original no-op adapter acceptance.
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

## Open native gate

Broadened shadow fixtures exposed an existing canonical material conversion
fault: ResolvedSurface hardcodes shading mode 3 and alpha cutoff 0.5. The shadow
consumer only samples cutout alpha for modes 5/6, so textured alpha edits are
ignored and local shadow caching incorrectly remains enabled. Both source paths
predate this U2 repair (introduced in `b1f9a97`); no source-baseline native rerun
was performed, so this attribution is based on source comparison.

Observed failures: CascadedShadowRendering dx12 at the alpha-hole depth
assertion; LocalShadowRendering dx12/vulkan at the same-generation alpha redraw
assertion. Logs: `build/m14-u2-cascades-dx12.log` and
`build/m14-u2-local-{dx12,vulkan}.log`.

Separate-context review found no remaining U2 structural blocker, conditional on
the native gates. Acceptance remains PARTIAL until U3 preserves these material
fields and both shadow fixtures pass again. Overall M14 remains HOLD.
