# M15-U5-F independent Codex review

Reviewed 2026-11-18. Gemini delivery `21a3e25e`, based on `c11e6edb`;
reply to `CG-20261117T163203278299Z-GEMINI-64701c`.
Reviewed scope follows the delivered task card: S5.6, U5-32.1 through U5-39.6.
The batch table originally splits these leaves between U5-F and U5-G; this
review covers the combined delivery actually submitted as U5-F. It does not
release S5.7 or later implementation.

**Verdict: automated U5-F delivery accepted after repairs.** No unresolved source
blocker remains. All final Debug/Release checks below pass; interactive Editor
visual gates remain open and user-deferred. One local Codex commit contains the
repairs, tests, review, plan and exchange; no push.

## Findings and repairs

| ID | Severity | Original failure | Repair and regression |
| --- | --- | --- | --- |
| U5F-R1 | P1 | SSR DDA located coarse cells with `uv * mipDimensions`. Hi-Z reduction instead folds odd trailing pixels into the last cell. At odd viewport sizes these footprints disagree, allowing a one-pixel blocker to be skipped. | CPU and HLSL now locate cells in base-pixel space using `2^mip` and extend only the final cell to the full extent. A blocker sweep at 63x47 and 95x63 compares hierarchical traversal with mip-0 DDA across both axes and directions. The original implementation failed this new test; the repair passes. |
| U5F-R2 | P1 | Mip-0 binary refinement always moved the upper endpoint when ray depth was behind scene depth. This assumes increasing-depth rays and can reject a genuine decreasing-depth crossing toward the camera. | Within a mip-0 cell, UVZ is linear and sampled depth is constant: solve their depth-plane crossing exactly for either direction. Thickness-only acceptance starts at cell entry. A decreasing-depth crossing with a per-cell span larger than thickness fails before this repair and passes afterward. Native tilted-mirror rays also exercise the camera-facing direction. |
| U5F-R3 | P2 | Claimed CPU/GPU parity omitted camera/distance/disocclusion confidence, used a different minimum distance, applied SSR strength during tracing, measured distance from the biased origin, and linearly interpolated view-space endpoints with a projective UVZ parameter. | Reconstruct the clipped endpoint from UVZ; retain the unbiased origin and configured distance limit; match normal-facing, step-budget and five-meter minimum-distance policy; compute the same four-neighbor/disocclusion, screen, distance, camera and step fades. Strength belongs in resolve only. Exit-only clipping rejects exterior/nonfinite starts. Added endpoint/min-distance checks and real CPU/GPU per-ray comparisons. |

Native parity reads the production Hi-Z levels, normal target and SSR output,
then traces the same 25 source pixels on CPU. It compares hit UV (0.002),
normalized distance (0.003) and confidence (0.025), rather than comparing unrelated
CPU and GPU hit examples. Fixtures cover a square view, an odd 255x191 view and
a tilted mirror whose view-space ray depth decreases. The camera-facing resolve
also checks bilinear pre-resolve scene radiance times exported BRDF response and
measured confidence, with a 10% relative bound. Its cone is explicitly bounded
below the single-sample threshold; a silhouette hit is not assumed to contain
a full emitter texel. No fixed HDR peak is required when confidence is low.

## Architecture review and correction recheck

Inspected the complete delivery diff, CPU contracts, Hi-Z reduction, shader
projection/traversal/refinement, reflection MRT producers, material resolve,
target allocation, descriptor/pipeline layouts and RenderGraph declarations.
The existing architecture is retained: scene color is copied before resolve;
four separate HDR pyramid levels receive declared writes/read transitions;
per-pass constant slices and descriptor sets avoid reuse within the recording;
resolve replaces only exported indirect specular. Material roughness, enabled
flags, dielectric/conductor response and probe/sky fallback remain part of the
material companion contract. Single-sample SSR requests use the renderer's
linear HDR route even when tone mapping was originally disabled.

A separate-context reviewer checked significant Codex corrections and affected
callers, identified the decreasing-depth issue, and rechecked its repair. That
review was source-only; execution evidence below is from the primary Codex run.
No GTAO halo work, vendor changes or later package implementation is included.

## Verification

Both final eleven-target builds exited 0. All **32/32** final executable/CTest
invocations exited 0 across Debug/Release and DX12/Vulkan. Each CTest run passed
9/9. Native rendering includes the environment/U4/U5-A..F regressions and asserts
zero RHI validation errors. Strict empty/U4E Editor startup passed on both actual
backends with five UI frames and fallback disabled. Architecture, exchange
validation and whitespace checks also exited 0 (613 source / 25 CMake files).
Existing LNK4098 linker warnings remain visible in the build logs.
Historical Gemini results remain in
[the self-verification report](M15_U5_F_REVIEW.md) and are not counted as Codex execution.

Build command, for each `C = Debug, Release`:

```text
cmake --build build --config C --target AxleRenderUnitTests AxleEnvironmentRenderingTests AxleEnvironmentBakeTests AxleRHIResourceViewTests AxleEditor AxleMaterialGraphTests AxleMaterialGraphCompilerTests AxleMaterialGraphOpenGLPolicyTests AxleMaterialGraphSlangTests AxleMaterialGraphRuntimeTests AxleMaterialGraphResourceTests --parallel 8
```

The final matrix uses the following commands for both configurations and every
`B = dx12, vulkan` combination:

| Gate | Command |
| --- | --- |
| Focused CPU | `build/Tests/C/AxleRenderUnitTests.exe --m15-hiz-contract`; `--m15-reflection-capture-contract`; `--lighting-services` |
| Full CPU | `build/Tests/C/AxleRenderUnitTests.exe` |
| Bake | `build/Tests/C/AxleEnvironmentBakeTests.exe` |
| Registered contracts/material graphs | `ctest --test-dir build -C C --output-on-failure -R "M15HiZContract\|M15ReflectionCaptureContract\|M15EnvironmentContract\|MaterialGraph"` |
| Native RHI | `build/Tests/C/AxleRHIResourceViewTests.exe B storage`; `B targets` |
| Native rendering | `build/Tests/C/AxleEnvironmentRenderingTests.exe B build/Tests/EnvironmentRendering/Codex-U5F-C-B` |
| Strict empty Editor startup | `build/Editor/C/AxleEditor.exe --verify-empty-startup --rhi=B --no-fallback` |
| Strict U4E Editor startup | `build/Editor/C/AxleEditor.exe --verify-startup --project Tests/Fixtures/M15/U4E/U4E.axleproj --rhi=B --no-fallback` |
| Architecture | `python scripts/check_architecture.py` |
| Exchange / whitespace | `python scripts/agent_exchange.py validate`; `git diff --check` |

Evidence paths: `build/Codex-U5F-build-{Debug,Release}.log`,
`build/Codex-U5F-results.json` (exact final executable/arguments/exit/log mapping),
`build/Codex-U5F-*.log`, and native artifacts under
`build/Tests/EnvironmentRendering/Codex-U5F-{C}-{B}`.
Fail-before regression logs: `build/Codex-U5F-before-CPU.log` (odd footprint) and
`build/Codex-U5F-decreasing-before-CPU.log` (decreasing-depth refinement);
both test commands exited 1 after successful builds. Their corrected focused
runs exited 0.

The 75 CPU/GPU ray comparisons pass in each of four native runs (300 comparisons
total). Camera-facing red resolves to 1.43395 versus predicted 1.43428; the
roughness sweep's green peak falls 11.3359 -> 8.92734 -> 2.65883 at roughness
0.05/0.30/0.65. These displayed measurements agree on both configurations and
backends. Tests also retain hit/miss/probe/sky fallback, thickness, reflection
flags, gold/chrome/dielectric, object motion and camera motion coverage. The
comparisons establish these controlled fixtures, not equivalence for every
possible depth buffer or interactive visual acceptance.

Interactive Editor visuals U5-34.1, 35.1, 35.3, 37.2, 38.1 and 39.4 remain
**NOT RUN / user-deferred**. Their PLAN checkboxes stay open. Automated startup
does not substitute for those captures or accept the complete visual gate.
