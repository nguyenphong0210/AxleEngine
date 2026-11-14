# M14-U5 Service Ownership Correction Recheck

Date: 2026-11-14 (Asia/Saigon)

**Verdict: PASS at `3af60f6`. No remaining M14-U5 blocker.**

The final source follows initial corrections `2f71cae` and `7568546`. A
separate-context reviewer inspected the correction diff, production callers,
CPU/native regressions and final verification evidence. This closes U5 service
ownership only. U9 consolidated acceptance and M15 fidelity remain separate.

## Findings and disposition

| Finding | Severity | Disposition and final source |
|---|---|---|
| U5-R1: unit conversion and light packing remained in the pass callback | P2 | FIXED: bounded CPU frame produced by `Engine/Renderer/src/LightingService.cpp:28`; native mapping consumes it in `Engine/Scene/src/RHISceneRendererPasses.cpp:600` |
| U5-R2: no-sun normalization and light-capacity metadata drift | P2 | FIXED: `LightingService.cpp:21`, `:32`, `:65`; CPU conversion, caps and two-sided-light assertions |
| U5-R3: nearest-probe migration changed existing influence behavior and ID-only lookup could select the wrong probe | P2 | FIXED: `Engine/Renderer/src/ReflectionService.cpp:5` preserves existing box/sphere weights; exact immutable snapshot consumed by `Engine/Scene/src/RHISceneRenderer.cpp:2422` |
| U5-R4: authored path claimed ready prefilter and synthetic generation before load | P2 | FIXED: pending candidate plus native receipt; `ReflectionService.cpp:40`, `:44`; missing/recovery/replacement assertions verify real generation readiness |
| U5-R5: GI ignored disabled/invalid grid and advertised no fallback without usable feedback | P2 | FIXED: `Engine/Renderer/src/GIService.cpp:4`; immutable grid data consumed by `RHISceneRendererPasses.cpp:1059`; disabled grid emits no update node and experimental state keeps IBL fallback |
| U5-R6: shadow generation was the constant availability sentinel `1` | P2 | FIXED: real epoch/slot/generation handles at `RHISceneRendererPasses.cpp:551`; native cascade replacement/local cache checks |
| U5-R7: requested SSR could be reported as fallback while MSAA prevented execution | P2 | FIXED: native availability reconciled by `ReflectionService.cpp:44` and `RHISceneRendererPasses.cpp:568`; actual SSR+MSAA regression |
| U5-R8: that regression exposed single-sample normal RTV with multisampled DSV | P2 | FIXED: shared executable-consumer predicate gates Declare/Prepare/Record at `RHISceneRendererPasses.cpp:89`, `:840`, `:849`, `:893` |

The typed service identities are metadata. Native owners, graph declarations and
command leases remain responsible for resource lifetime. Shared persistent caches
and callback adapters retain the explicit U2 boundary. No M15 capture, blending,
DDGI feedback or material fidelity implementation is accepted by this recheck.

## Final verification inspected

- Debug and Release builds of seven affected render/product targets: exit 0.
- Debug manifest `build/m14-u5-final-debug-results.json`: 13/13 exit 0 (unit,
  graph D3D12/Vulkan, environment D3D12/Vulkan, cascaded/local shadows on both,
  preview surfaces on both, Runtime D3D12 loose/cooked).
- Release manifest `build/m14-u5-final-release-results.json`: 5/5 exit 0
  (unit, graph D3D12/Vulkan, Runtime D3D12 loose/cooked).
- Final native logs end PASS, with no NRI validation errors. Environment includes
  probe publication and availability regressions, disabled GI and 270 TAA frames.
- Architecture checker: PASS, 600 files / 25 CMake files. Dependency map:
  `build/Tests/Work/M14-u5-final/dependencies`, zero architecture/caller violations.
- Source whitespace check: PASS.

The source/log reviewer was `/root/u5_final_review`. It did not run GPU commands;
Codex executed the builds and tests sequentially against the corrected source.
The reviewer checked final evidence chronology and the archived before-fix MSAA
failure separately from final passing logs. Release environment/shadow/preview,
OpenGL and interactive Editor UI were not rerun. Exact commands, log paths and
limits are in [U5 evidence](../milestones/MILESTONE_14_RENDER_ARCHITECTURE/U5_SERVICE_BOUNDARIES_EVIDENCE.md).

**Recommendation: accept M14-U5. Keep U9 on HOLD for consolidated acceptance.**
