# M14-U4 - Resource Lifetime and RenderGraph Authority Evidence

Updated: 2026-11-14 (Asia/Saigon)

**Implementation / self-verification:** PASS at `4cf8e72` (graph authority API) and `171133b` (live pass integration).
**Acceptance:** PASS - final separate-context correction recheck completed; no remaining U4 blocker.
**Scope:** target generations, exact physical resource declarations, descriptor/buffer access authority, command lifetime and upload failure propagation. U5/M15 are excluded.

## 1. Contract and ownership

`RHIViewTargets` remains the transactional owner of view generations. `RenderGraph`
now seals declarations at compile, retains exact imported leases, validates texture
usage/layout/access, tracks buffer producers/readers, rejects read-before-write and
pins one exact command generation for the complete recording. A graph cannot switch
command buffers, reset/rebegin the attached command buffer, or declare a descriptor
through another command buffer. `EndRecording` detaches the validator before queue
submission; incomplete graphs are rejected.

`RHIResourceService` publishes immutable view roles for physical Texture2D/Buffer
aliases and managed descriptor-set range/type metadata. Strict descriptor updates
reject opaque/standalone/untyped views, wrong descriptor roles, uninitialized managed
ranges and resource generations without exact backing metadata. `RHICommandBuffer`
root descriptors, descriptor sets, texture transitions, buffer transitions, vertex
and index binds all pass through the graph validator while a graph scope is active.
Physical texture and buffer usage flags are checked at compile time.

Live `RHISceneRenderer` prepares mesh/material/shadow/particle/physics resources
before graph compilation. Each pass declares descriptor, constant, texture, mesh,
particle, shadow and timestamp inputs before `Record`; record code consumes frozen
resources only. Per-pass compute constants use frame-scoped immutable buffers.
Particle and physics dynamic upload failures now return a failed pass and can retry;
they do not silently submit stale/missing draws.

## 2. Acceptance matrix

| Area | Result | Source anchor |
|---|---|---|
| Exact graph command scope and detach | PASS (self-verified) | `Engine/Renderer/src/RenderGraph.cpp:16`, `:203`; `Engine/RHI/src/RHIResource.cpp:223` |
| Texture usage/layout/access validation | PASS (self-verified) | `RenderGraph.cpp:128`, `:180`; `RHIResource.cpp:416` |
| Buffer usage, producer/read dependency and lifetime | PASS (self-verified) | `RenderGraph.cpp:171`; `RenderGraph.hpp:41` |
| Descriptor role/range/completeness authority | PASS (self-verified) | `RHIResource.cpp:344`, `:441`; `RHIDescriptorPool.cpp:9` |
| Live material/mesh/shadow declarations | PASS (self-verified) | `RHISceneRendererPasses.cpp:319`, `:1058`; `RHIShadowDepthAtlas.cpp:167` |
| Particle/physics upload failure propagation | PASS (self-verified) | `RHIParticleRenderer.cpp:517`; `RHIPhysicsDebugRenderer.cpp:261` |
| Final separate-context correction recheck | PENDING | Required by working protocol before formal acceptance |

## 3. Verification

All listed build/test invocations returned exit 0 unless explicitly marked NOT
CLOSED. GPU runs were sequential.

### CPU and authority fixtures

```text
cmake --build build --config Debug --target AxleRenderUnitTests AxleRenderGraphTests --parallel 6
build/Tests/Debug/AxleRenderUnitTests.exe
build/Tests/Debug/AxleRenderGraphTests.exe dx12
build/Tests/Debug/AxleRenderGraphTests.exe vulkan
```

Logs: `build/m14-u4-final-cpu-build.log`, `build/m14-u4-final-unit-2.log`,
`build/m14-u4-final-graph-dx12-2.log`, `build/m14-u4-final-graph-vulkan-2.log`.
The fixture covers undeclared descriptor/root/mesh reads, wrong UAV/copy access,
command-generation switching, same-command Reset while scoped, strict physical
usage mismatches, buffer read-before-write and producer dependency, descriptor
range/type mismatch, untyped and standalone imported view rejection, annotated
physical SRV acceptance, and completion-time generation retirement.

### Native renderer and lifetime gates

```text
AxleEnvironmentRenderingTests.exe dx12 <output>       => exit 0
AxleEnvironmentRenderingTests.exe vulkan <output>     => exit 0
AxleCascadedShadowRenderingTests.exe dx12|vulkan      => exit 0
AxleLocalShadowRenderingTests.exe dx12|vulkan         => exit 0
AxleRHIResourceViewTests.exe dx12|vulkan              => exit 0
```

Environment logs: `build/m14-u4-final-environment-{dx12-5,vulkan-5}.log`.
They include the existing HDR/specular/diffuse, cache/reload/fallback and upload
retry checks. Shadow logs are `build/m14-u4-final-Axle{Cascaded,Local}ShadowRenderingTests-{dx12,vulkan}.log`.
Resource-view logs are `build/m14-u4-final-resource-view-{dx12,vulkan}.log`.

### Product callers

```text
AxlePreviewRenderTests.exe dx12|vulkan surfaces   => exit 0
AxlePreviewRenderTests.exe dx12|vulkan particles  => exit 0
AxlePreviewRenderTests.exe dx12 physics           => exit 0
```

Logs: `build/m14-u4-final-preview-{dx12,vulkan}-{surfaces,particles}.log` and
`build/m14-u4-final-preview-dx12-physics.log`. Particle and physics products cover
actual dynamic resources and the new failure-aware recording paths.

`AxleRuntimeRenderTests.exe dx12` with the required absolute Debug DLL search
path reached startup scene load and native presentation setup but failed its
fixture assertion `actual bounded Application loop, UI input and player frames`.
No `View pass failed` was emitted. This runtime product gate is **NOT CLOSED** and
belongs to U9/product acceptance; it is not converted into a U4 PASS.

### Architecture and migration

```text
python scripts/check_architecture.py
python scripts/generate_m14_dependency_map.py --output-dir build/Tests/Work/M14-u4-final/dependencies
```

Run after the final documentation commit; both must report zero violations. The
live graph source no longer relies on descriptor lifetime alone: resources are
prepared and declared before compile, then record-time helpers can only consume
those declarations.

## 4. Review and recommendation

The implementation addresses the earlier U4 findings: undeclared material/IBL/
mesh/descriptor reads, incomplete ScreenStorage-era access coverage, command scope
lifetime, view metadata bypasses, physical usage mismatches and silent particle/
physics upload failures. The resumed separate-context reviewer returned **PASS: no
remaining U4 blocker** after inspecting the corrected source and affected callers.

Recommendation: accept U4. Keep U9/M14 HOLD for U5 service ownership, the
unclosed Runtime product fixture and consolidated acceptance. M15 remains excluded.
