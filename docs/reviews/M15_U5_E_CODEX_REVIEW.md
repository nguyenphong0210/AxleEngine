# M15-U5-E independent Codex review

Reviewed 2026-11-17. Scope: S5.5, M15-U5-27.1 through U5-31.5 and the P5 exit.
Gemini delivery: `32144635`, based on accepted U5-D `b77f70d9`;
reply to `CG-20261117T143543452697Z-GEMINI-67672c`.
The [Gemini report](M15_U5_E_REVIEW.md) is historical self-verification; this
document records Codex source review, corrections and independently executed
verification. The user's GTAO halo investigation was canceled before code edits
and is outside this review.

**Verdict: automated U5-E/P5 scope accepted after repairs.** Interactive Editor fixture
inspection remains user-deferred. This does not activate U5-F/G or accept all U5.

## Findings and corrections

| ID | Severity | Original trigger/behavior and corrected result |
| --- | --- | --- |
| U5E-R1 | P1 | SSRApply subtracted a newly sampled sharp environment using fixed dielectric F0=.04, although forward rendering had added a material-dependent, roughness-filtered contribution. Metallic/color/BRDF/LUT compensation, reflection flags, AO and extra lobes therefore disagreed; SSR could alter emission/direct light even when environment reflection was disabled. The opaque forward draw now exports its actual environment specular and material response to two RGBA16 targets. SSR resolves `scene + confidence*strength*(hit*response - previousSpecular)` and preserves other lighting terms. Misses return the original pixel exactly. No sharp environment is independently reconstructed by the resolve. |
| U5E-R2 | P1 | Selection truncated to four candidates before readiness was known, so four cold/missing probes hid a valid fifth. Camera priority values were collected but not used, and approximate float tie comparisons did not define a strict ordering. Ranking is now deterministic and camera-first, retaining ranked overflow for native readiness fallback. Up to four ready probes are packed with their matching publication receipts; missing counts remain accurate. Scheduler admission protects ready view-selected/overflow dynamic generations. |
| U5E-R3 | P2 | A collapsed transform with local X parallel to world Z normalized a zero fallback cross product, producing an invalid basis. Box-ray parallax also substituted small nonzero components for parallel directions, incorrectly rejecting a ray on a slab face. Basis fallbacks now use a nonparallel reference axis; finite normalization is checked. CPU/HLSL slab intersection handles true parallel axes without invented directions. Canonical graph sky sampling now applies the same authored rotation as forward PBR, while local-probe directions remain in world space. |
| U5E-R4 | P2 | Mesh/probe sample generation produced O(M*P) temporary points which every probe rescanned, giving O(M*P²) influence work. The world-nearest sample could also miss a rotated slender box intersecting a large mesh. One bounds record per mesh now feeds O(M*P) influence work; box bounds are conservatively enclosed in probe space, sphere distance uses the world AABB, and exact per-fragment influence still controls actual shading. NativeMesh-only snapshots are supported and invalid bounds are skipped. |
| U5E-R5 | P2 evidence | The U5-E `renderHDR` helper disabled tone mapping and read a FP16 output containing the legacy ACES/gamma result. Tests advertised as HDR energy evidence were comparing display-mapped values. They now explicitly select scene-linear rendering and read the intermediate HDR target. The unit-radiance energy tolerance is tightened from .18 to .03; HDR emission checks require values above one. |

The R1 repair adds `PSReflectionMain` companions for built-in and canonical
material shaders. Their normal `PSMain` entry retains a single color output,
avoiding unused-MRT validation warnings when SSR is disabled. MRT layouts,
clear/load rules, sky write masks, graph dependencies, view allocation and
Editor/RenderService target descriptions are updated together. SSR uses a
linear HDR scene even with legacy output settings, then applies the legacy
ACES/gamma conversion once. It resolves opaque radiance before transparency,
refraction, particles and later GI/fog composition.

Separate-context rechecks found and drove repairs in the **Codex corrections**:
cached single-target pipelines had to be bypassed for MRT draws; custom-shader
SSR eligibility had to be decided in Configure before graph declaration; and
reflection-companion-only bindings had to be merged into shader validation,
binding plans and layouts before publication. Runtime tests additionally caught
the missing mandatory custom-program fingerprint in preflight. These are fixed.
Legacy custom shaders without the companion now produce a named
`ssr_unavailable` fallback and a complete non-SSR render graph.

The final read-only source recheck found no remaining P1/P2 blocker. It inspected
the corrected source and affected callers; all execution evidence below is from
the primary reviewer, not copied or delegated runs.

## Meaningful regressions

- Degenerate basis, exact box-face parallel ray, camera priority with five
  equal mesh-region candidates, large mesh intersection and the reviewer's
  concrete rotated-slender box/AABB counterexample.
- Four missing high-ranked probes plus a ready fifth: require one active/four
  missing publications and unchanged reflected radiance.
- Actual confident SSR hits on an emissive receiver with environment reflection
  disabled: preserve HDR emission and color. The canonical material counterpart
  uses AO=0, requires real SSRApply/hits and preserves graph-authored emission.
- A custom cbuffer used only in the reflection companion's output alpha checks
  that the companion interface participates in native layout/binding creation.
  A valid legacy variant requires named fallback and no SSRApply, without
  leaving graph passes unconsumed.
- Existing box parallax, added native sphere parallax, overlap/boundary/sky,
  missing/stale generation, P5 captured-red-object and roughness tests remain.
  Native LRU assertions remain intact; its static emitter is placed outside
  probe influence to isolate cache-use order from visible-surface pinning.
- Existing odd/minimal Hi-Z fixture descriptions now include the required
  linear/term targets; every depth-pyramid readback assertion is retained.
  Pass-order assertions verify opaque SSR precedes transparent composition.

Debug native examples (both backends agree within normal FP16 tolerance):

| Check | Executed result |
| --- | --- |
| Unit scene radiance | SSR off `.99988`, on `1.0121` |
| Actual colored hit | Red/Green off `(1.99976,.0312463)`, on `(.313255,5.14158)` |
| Miss inside probe | `(1.99968,.0312453,.031246)` |
| Miss outside probe | Red/Blue `(.0312463,1.99976)` |
| P5 captured object, SSR off | Cubemap -Z `(4,.159912,.159912)` |
| P5 linear roughness response | Red peak `3.29863 -> 1.09219 -> .138735` |

## Executed validation

Both final configurations pass all 14 commands each, **28/28 exit 0**:

| Command/check | Debug | Release |
| --- | --- | --- |
| `AxleRenderUnitTests.exe --m15-reflection-capture-contract` | 0 | 0 |
| `AxleRenderUnitTests.exe` (full, including service/math/bounds regressions) | 0 | 0 |
| `AxleEnvironmentBakeTests.exe` | 0 | 0 |
| `ctest --test-dir build -C C -R "M15ReflectionCaptureContract\|M15EnvironmentContract\|MaterialGraph" --output-on-failure` | 0, 8/8 | 0, 8/8 |
| `AxleRHIResourceViewTests.exe {dx12,vulkan} storage` | 0 each | 0 each |
| `AxleRHIResourceViewTests.exe {dx12,vulkan} targets` | 0 each | 0 each |
| `AxleEnvironmentRenderingTests.exe {dx12,vulkan} <artifact-directory>` | 0 each | 0 each |
| `AxleEditor.exe --verify-empty-startup --rhi={dx12,vulkan} --no-fallback` | 0 each | 0 each |
| `AxleEditor.exe --verify-startup --project Tests/Fixtures/M15/U4E/U4E.axleproj --rhi={dx12,vulkan} --no-fallback` | 0 each | 0 each |

`C` is `Debug` or `Release`; the matrix below records expanded paths/arguments.
All native rendering runs retain U4/U5-A/B/C/D regressions and exercise U5-E,
P5, actual device reset, publication rollback and new material/selection tests.
Editor runs report the requested actual backend, `fallback=none`, five UI frames
and (for the loaded fixture) three entities. `check_architecture.py` exits 0
(613 source/25 CMake), exchange validation exits 0, and whitespace checks exit 0.

Exact command arguments, exit codes and log paths are retained in
`build/Codex-U5E-results.json`. Full build logs:
`build/Codex-U5E-Debug-build-verified.log` and
`build/Codex-U5E-Release-build-serialized.log`. Both configurations rebuild
`AxleRenderUnitTests`, `AxleEnvironmentRenderingTests`, `AxleEnvironmentBakeTests`,
`AxleRHIResourceViewTests`, `AxleEditor` and the six MaterialGraph test targets.
An intermediate overlapping Release build hit an object-file lock; the final
serialized build exits 0. Its failed log is retained separately.

## Explicit boundaries

- Four ready probes are bound per view. Selection uses conservative mesh bounds;
  exact influence/weights are evaluated per fragment. It is not an unlimited
  per-pixel probe list or a guarantee of seamless slot reassignment beyond four.
- Exact environment replacement is repaired here; Hi-Z traversal quality,
  roughness-cone/color-mip filtering and remaining S5.6 items stay open for U5-F/G.
  Transparent/particle layers composite after opaque SSR and are not hit sources
  in this resolve. Raw custom shaders without the reflection companion disable
  SSR for that view through explicit diagnostics.
- The two reflection-term targets cost 16 bytes/pixel (about 31.6 MiB at 1080p),
  in addition to the linear scene/storage targets. They are allocated for SSR
  views and released with their target generation when disabled.
- Native runs assert zero RHI validation **errors**; logs retain existing unused
  vertex-attribute and GTAO typed-storage **warnings**. This is not a claim of a
  warning-free engine or verification of the canceled GTAO halo report. No GTAO
  shader was modified in this task.
- Editor startup with a real three-entity fixture exercises project/viewport
  integration, not interactive visual inspection. U5-27.2/28/29/30.1/31.5 remain
  NOT RUN/user-deferred until the consolidated Editor visual pass.
