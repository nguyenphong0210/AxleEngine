# M14-U3 Final Correction Recheck

Date: 2026-11-14 (Asia/Saigon)

**Verdict: PASS. No remaining M14-U3 blocker found.**

Reviewed source: implementation checkpoints `86feca7` and `f241eb0`, unchanged
at checkout `b476686`. Codex records the actual final result from the reused
separate-context reviewer `/root/u2_correction_review`. This closes the focused
correction recheck required by `docs/agents/WORKING_PROTOCOL.md`; it is not a
Gemini response or a consolidated U9 review.

## Inspected corrections and affected callers

| Check | Result | Source anchor |
|---|---|---|
| Texture liveness/SRV/dimension/sample/format validation | PASS | `Engine/Renderer/src/ResolvedSurface.cpp:8`, `:142` |
| Actual custom/built-in binding finalization before draw | PASS | `Engine/Scene/src/RHISceneRendererPasses.cpp:361`, `:442` |
| Typed graph binding finalization | PASS | `Engine/Scene/src/MaterialGraphRuntime.cpp:187` |
| Shadow canonical validation before descriptor preparation | PASS | `Engine/Scene/src/RHIShadowDepthAtlas.cpp:139` |
| Late material invalidation preserves imported history slots | PASS | `Engine/Renderer/src/ViewContext.cpp:102` |
| Wrapper owners released before command-lease lifetime checks | PASS | `Tests/src/MaterialGraphTestDraw.hpp:59` |
| Native cubemap and missing-SRV rejection cases | PASS | `Tests/src/Test_MaterialGraphCompiler.cpp:313` |

Assigned native maps are screened before canonical surface publication. The
actual path-cache and typed graph producers create compatible float Texture2D
resources. The raw custom finalizer runs after descriptor-table binding but
before GPU draw; invalid texture access cannot occur before rejection/fallback.
Built-in finalization precedes descriptor-set update, and typed graph resolution
finalizes bindings before the descriptor writer. The reviewer found no remaining
blocker in these paths or the earlier identity/status/lifetime corrections.

## Fresh closeout verification

Codex ran the following against the unchanged final source; all returned exit 0:

```text
cmake --build build --config Debug --target AxleRenderUnitTests AxleMaterialGraphCompilerTests --parallel 6
build/Tests/Debug/AxleRenderUnitTests.exe
build/Tests/Debug/AxleMaterialGraphCompilerTests.exe dx12 D:/WorkSpace/AxleEngine/Tests/Fixtures/MaterialGraph/M_RustMetal.axleasset D:/WorkSpace/AxleEngine/build/Tests/Work/M14-u3-close/Compiler/dx12
build/Tests/Debug/AxleMaterialGraphCompilerTests.exe vulkan D:/WorkSpace/AxleEngine/Tests/Fixtures/MaterialGraph/M_RustMetal.axleasset D:/WorkSpace/AxleEngine/build/Tests/Work/M14-u3-close/Compiler/vulkan
```

Logs: `build/m14-u3-close-build.log`, `build/m14-u3-close-unit.log`,
`build/m14-u3-close-compiler-dx12.log`,
`build/m14-u3-close-compiler-vulkan.log`. Native graph/reference maximum image
error was zero on both backends. The negative binding cases and final unchanged
RHI-validation-error-count assertion passed.

The separate-context reviewer inspected these logs and found them consistent
with the reported results. It independently inspected source; it did **not**
execute the closeout commands. The broader Debug/Release and product gate results
remain the earlier implementer-run evidence recorded in
[U3 evidence](../milestones/MILESTONE_14_RENDER_ARCHITECTURE/U3_MATERIAL_SURFACE_EVIDENCE.md).
No source/test changes were needed during closeout.

## Acceptance and limits

Recommendation: **accept M14-U3**. The previously pending final correction gate
is closed; there are no open U3 blockers.

Acceptance covers the canonical CPU material/interface, packing, status,
identity and affected caller/lifetime contract. In-place native pixel writes
require owner-provided `ContentRevision`; shader/graph CPU input metadata does
not evaluate per-pixel GPU expressions. Native Material Graph execution on OpenGL
remains explicitly unsupported. U4/U5 and consolidated U9 acceptance remain open;
overall M14 stays HOLD. M15 lighting/material fidelity remains excluded.
