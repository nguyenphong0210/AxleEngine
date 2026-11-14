# M14-U5 - Lighting, Reflection and GI Service Boundaries Evidence

**Status:** `ACCEPTED`  
**Unit:** M14-U5  
**Scope:** stable service contracts, immutable snapshot inputs, shadow/probe/GI status and explicit fallback policy. M15 fidelity algorithms remain deferred.

## 1. Service contracts

Added Renderer-layer services:

- `LightingService`: light counts, unit/selection boundary and shadow publication metadata.
- `ReflectionService`: authored probe selection, generation/invalidation metadata, dynamic-capture boundary and SSR/probe/sky fallback.
- `GIService`: DDGI grid/update/irradiance/visibility/sampling boundary with experimental status and IBL fallback.

Files:

```text
Engine/Renderer/include/Axle/LightingService.hpp
Engine/Renderer/src/LightingService.cpp
Engine/Renderer/include/Axle/ReflectionService.hpp
Engine/Renderer/src/ReflectionService.cpp
Engine/Renderer/include/Axle/GIService.hpp
Engine/Renderer/src/GIService.cpp
```

All service inputs are immutable `RenderWorld`/settings snapshots. Services do not access ECS, command buffers, descriptor pools or native queues. GPU packing remains in RHI and pass recording remains in the pass layer.

## 2. Explicit policies

### Lighting

- Direct light counts are derived from the snapshot.
- Shadow atlas/cascade availability is published as generation metadata.
- Disabling shadows removes receiver atlas publication but keeps direct light inputs.
- Missing shadow publication returns a named fallback diagnostic; direct lighting remains valid.

### Reflection

- Authored probes can report `AuthoredReady`, cubemap path, parallax policy and generation identity.
- Selection is deterministic nearest-probe selection for the migration boundary.
- Invalidation tracks transform, scene revision, geometry, material, lighting and asset changes.
- Fallback order is explicit: SSR, local probe, sky IBL or neutral ambient.
- Dynamic six-face capture, prefilter scheduling and multi-probe blending are declared interfaces but remain M15 implementation scope.

### GI

- Disabled/no-grid state returns `IBL` diffuse fallback.
- Missing compute capability returns `IBL` diffuse fallback.
- A valid grid with compute support returns `Experimental` and `UpdateScheduled`, but irradiance/visibility/sampling remain false until M15 feedback is implemented.
- DDGI state does not enter `ResolvedSurface` directly.

## 3. Verification

Command:

```text
build/Tests/Debug/AxleRenderUnitTests.exe
```

Result: **exit code 0**. The `[LightingServices]` suite verified light/shadow publication, shadow disable policy, nearest authored probe selection, reflection invalidation, SSR/sky fallback, GI disabled/experimental/compute-fallback states and explicit absence of DDGI feedback claims.

Additional checks:

```text
cmake --build build --config Debug --target AxleRenderUnitTests --parallel 4  => exit 0
python scripts/check_architecture.py                                           => PASS
git diff --check                                                                => PASS
```

## 4. U5 decision

**U5 ACCEPTED.** Lighting, reflection and GI now have independent Renderer-layer service boundaries and explicit fallback/status contracts. M15 owns the concrete reflection capture/prefilter/blend and DDGI feedback implementations.
