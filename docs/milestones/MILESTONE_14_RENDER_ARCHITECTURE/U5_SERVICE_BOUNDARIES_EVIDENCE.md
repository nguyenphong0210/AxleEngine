# M14-U5 - Lighting, Reflection and GI Service Ownership Evidence

Updated: 2026-11-14 (Asia/Saigon)

**Implementation / self-verification:** PASS at `2f71cae`, `7568546` and final correction `3af60f6`.
**Acceptance:** PASS after the separate-context correction recheck.
**Scope:** immutable service decisions, CPU light conversion/packing, native publication receipts, fallback and migration. M15 fidelity algorithms and pass-class extraction are excluded.

## Ownership and acceptance matrix

| Boundary | Result | Source anchor |
|---|---|---|
| Light selection, limits and artist/physical intensity conversion | PASS | `Engine/Renderer/src/LightingService.cpp:28`; `LightingService.hpp:59` |
| Exact shadow publication identity | PASS | `LightingService.hpp:16`; `LightingService.cpp:97`; `Engine/Scene/src/RHISceneRendererPasses.cpp:551` |
| Authored probe influence selection and exact snapshot | PASS | `Engine/Renderer/src/ReflectionService.cpp:5`; `ReflectionService.hpp:19`; `Engine/Scene/src/RHISceneRenderer.cpp:2422` |
| Pending vs native-ready probe generation | PASS | `ReflectionService.cpp:40`, `:44`; `RHISceneRendererPasses.cpp:568` |
| SSR/probe/sky/neutral fallback matches native availability | PASS | `ReflectionService.cpp:44`; `RHISceneRendererPasses.cpp:85`, `:568` |
| GI enablement, validation and immutable grid inputs | PASS | `Engine/Renderer/src/GIService.cpp:4`; `RHISceneRendererPasses.cpp:124`, `:1059` |
| Separate-context final correction recheck | PASS | [U5 correction recheck](../../reviews/M14_U5_CORRECTION_RECHECK.md) |

Services consume immutable `RenderWorld` and settings snapshots. They do not access
ECS, command buffers, descriptor pools or queues. Native environment/shadow owners
prepare GPU resources; the graph and command journal retain their exact leases.
Service publication handles are identity metadata and do not replace that ownership.
The existing callback adapter and persistent native caches retain the documented U2
migration boundary.

## Corrected service behavior

`LightingService` produces the bounded directional/point/spot/rect frame consumed
by live light-buffer mapping. It owns the existing artist/physical intensity
thresholds, cone-angle conversion, rectangular two-sided sign and extra-directional
slot policy. `PointCount` denotes the occupied native point-array prefix, including
extra directionals. The pass maps the resulting fields to the existing GPU ABI;
capacity static assertions guard that mapping. The no-primary-sun fallback remains
normalized and has zero direct illuminance. This is a behavior-preserving migration
of the existing unit convention, not a new physical lighting model.

Shadow publications carry the actual `(Epoch, Slot, Generation)` atlas identity.
Availability without a valid identity cannot report a ready atlas. Disabling shadows
clears publications while preserving direct light inputs. Native shadow replacement
and cached reuse are covered by the existing shadow fixtures plus new identity checks.

`ReflectionService` owns the existing box/sphere influence policy, including source
order on equal weights and fallback outside influence. It carries an exact copied
probe snapshot; duplicate/default entity IDs cannot redirect the native owner to a
different probe. Selection reports `AuthoredPending`, with no ready prefilter or
synthetic generation. Native preparation publishes a receipt only for a complete
live environment generation. The service then reports `AuthoredReady` with the real
prefilter handle, or fallback with no ready generation. Same-source last-good reuse
continues through the native environment cache with its diagnostic.

Fallback is reconciled after native predicates and environment preparation: usable
local probe, otherwise executable SSR, available sky IBL, or neutral ambient. SSR
requested with MSAA cannot advertise an SSR fallback. Its single-sample normal
prepass is also omitted when neither SSR nor GTAO can execute. Declare, input
preparation and Record use the same predicate.

`GIService` respects both the setting and authored grid `Enabled` flag. Disabled,
missing, invalid or compute-unavailable grids schedule no update. The service
validates consumed grid fields and supplies immutable dimensions, origin, spacing
and energy to the existing experimental pass. Experimental scheduling keeps the
IBL diffuse fallback; irradiance, visibility and sampling readiness remain false.
Native resource/sample constraints can reject the update without claiming feedback.
Dynamic probe capture, multi-probe blending and DDGI feedback remain M15 scope.

## Final-source verification

Both commands returned exit 0 at source `3af60f6`:

```text
cmake --build build --config Debug --target AxleRenderUnitTests AxleEnvironmentRenderingTests AxleCascadedShadowRenderingTests AxleLocalShadowRenderingTests AxleRuntimeRenderTests AxlePreviewRenderTests AxleRenderGraphTests --parallel 4
cmake --build build --config Release --target AxleRenderUnitTests AxleEnvironmentRenderingTests AxleCascadedShadowRenderingTests AxleLocalShadowRenderingTests AxleRuntimeRenderTests AxlePreviewRenderTests AxleRenderGraphTests --parallel 4
```

| Executed suite | Debug | Release |
|---|---|---|
| Render unit contracts | PASS | PASS |
| RenderGraph authority, D3D12 / Vulkan | PASS / PASS | PASS / PASS |
| Environment, D3D12 / Vulkan | PASS / PASS | NOT RERUN |
| Cascaded and local shadows, D3D12 / Vulkan | PASS / PASS for both suites | NOT RERUN |
| Preview surfaces, D3D12 / Vulkan | PASS / PASS | NOT RERUN |
| Runtime D3D12 loose / cooked | PASS / PASS | PASS / PASS |

Final test manifests: `build/m14-u5-final-debug-results.json` (13/13 exit 0) and
`build/m14-u5-final-release-results.json` (5/5 exit 0). They list each case, exact
log path, duration and exit code. Build logs are
`build/m14-u5-final-{debug,release}-build.log`; individual logs use the same prefix.
GPU cases were run sequentially. Tests run from the repository root except Runtime,
which runs from `build/Tests/<config>` with the absolute Tests, Engine/PluginSDK,
Runtime and Editor directories for that configuration on `PATH`.

CPU assertions cover conversion, light capacities, two-sided sign, normalized
fallback, shadow identity validation, probe candidate/publication/failure,
influence and duplicate IDs, actual SSR availability and disabled/invalid GI.
Native assertions cover missing-probe fallback/recovery/replacement, exact live
prefilter generation, duplicate-ID influence selection, SSR+MSAA without an SSR
node, disabled GI without an update node, and shadow atlas identity/replacement.
The existing 270-frame TAA and injected descriptor-arena retry remain in the
successful environment runs.

The new SSR+MSAA regression first reproduced a D3D12 attachment sample mismatch
(exit 2170, `build/m14-u5-ssr-msaa-before.log`). The shared normal-prepass predicate
corrects it; the final D3D12/Vulkan runs pass without an NRI validation error.
An initial cascade launch from the executable directory could not resolve the
PhysicsWireframe shader; the final invocation uses the repository-root working
directory and passes. Expected fault-injection messages are not successful-frame
validation errors.

Architecture checker: PASS, 600 source files / 25 CMake files. Dependency map:
`python scripts/generate_m14_dependency_map.py --output-dir build/Tests/Work/M14-u5-final/dependencies`
reported zero architecture and caller violations. Final whitespace checks pass.
OpenGL and interactive Editor UI were not rerun in this U5 correction. No M15
fidelity or performance-budget acceptance is claimed from these regression runs.

## Recommendation

Accept U5 service ownership at source `3af60f6`. The final separate-context reviewer
inspected source, diffs and final logs; it did not execute GPU commands. See the
[correction recheck](../../reviews/M14_U5_CORRECTION_RECHECK.md). U9 remains on HOLD
for consolidated final acceptance; this U5 closeout does not release M15.
