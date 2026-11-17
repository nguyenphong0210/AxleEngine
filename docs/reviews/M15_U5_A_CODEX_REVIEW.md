# M15 U5-A independent Codex review

Delivery: `968cab6b`, based on `a746397b`; reply to
`CG-20261117T070850437254Z-GEMINI-4e70b7`. Scope: U5-05.1 through U5-07.2.

**Verdict: automated checkpoint accepted after corrections.** Interactive
Editor captures remain deferred as recorded in the released task; U5-07.1's
visual checkbox remains open. This does not release U5-B or accept the whole U5.
One consolidated Codex commit includes code, tests, PLAN and exchange; no push.

## Findings

- **U5A-R1, high, FIXED:** visualization selected the RGBA8 pipeline for every
  attachment except RGBA16. BGRA8 and RGBA32 therefore received an incompatible
  pipeline. Preparation now obtains the cached PSO for the actual attachment
  format and passes it explicitly into recording. Native BGRA8/RGBA32 draws
  succeed and match RGBA8 depth tones within 1/255. Pipeline creation/failure
  stays in preparation; command recording retains the pipeline lease.
- **U5A-R2, medium, FIXED:** backend capability resolution left visualization
  enabled without compute/storage support. It now resolves the level to -1 and
  emits `hiz_visualization_disabled:compute_or_storage_unavailable`.
  Focused CPU coverage checks disabling and fallback on compatibility backend.
- **U5A-R3, low, FIXED:** Editor selection stopped at 11 although 16 levels are
  supported. It now uses `MaxHiZLevels - 1`, exposing levels 0 through 15.
- **U5A-R4, coverage, FIXED:** rendered pyramid comparison used HiZ0 as its own
  source oracle. It now also checks HiZ0 exactly against actual opaque D32
  depth, with nonempty geometry and clear-sky controls at larger resolutions.
  Added native 1x5/5x1 trailing-border cases, complementing 1x1/3x5/1919x1079.

The conservative odd-border reduction, min-depth convention, useful-versus-SSR
allocated counts, transactional extra-level ownership and declared UAV-to-SRV
dependencies were independently inspected. Final native reductions have
**max_error=0** across standalone and actual-renderer fixtures; 1919x1079
allocates 11 levels ending at 1x1, while tiny views retain eight SSR bindings.

Visualization is a **scene-pipeline depth-tone mode**, not raw metric-depth
output: fog and subsequent postprocessing (including tone mapping) may change
displayed tones. Raw Hi-Z values remain unchanged. Existing intentional HDR
composition is preserved; no new reflection traversal/composition policy is
introduced. Level selection, clamped/tail diagnostics and normal-render recovery
are verified. The per-level descriptor sets remain distinct during recording.

## Independently executed checks

All commands below exit **0**, from `D:/WorkSpace/AxleEngine`. `<Config>` expands
to Debug and Release; `<backend>` expands to dx12 and vulkan. GPU runs were
serial on hardware, with native validation enabled and zero validation errors.
Changed executables were rebuilt before use.

```text
cmake --build build --config <Config> --target AxleRenderUnitTests AxleEnvironmentRenderingTests AxleRHIResourceViewTests AxleEditor --parallel 8
build/Tests/<Config>/AxleRenderUnitTests.exe --m15-hiz-contract
build/Tests/<Config>/AxleRenderUnitTests.exe
build/Tests/<Config>/AxleRHIResourceViewTests.exe
ctest --test-dir build -C <Config> -R ^(M15HiZContract|M15HiZReference)$ --output-on-failure
python scripts/test_m15_hiz_reference.py
build/Tests/<Config>/AxleEnvironmentRenderingTests.exe <backend> build/Tests/EnvironmentRendering/Codex-U5A-<Config>-<backend>
build/Editor/<Config>/AxleEditor.exe --rhi=<backend> --verify-empty-startup
python scripts/check_architecture.py
git diff --check
```

Evidence:

- Builds: `build/Codex-U5A-build-<Config>.log`.
- CPU/resource/CTest: `build/Codex-U5A-<Config>-{focused,full,resource,ctest}.log`.
- Native runs: `build/Codex-U5A-<Config>-<backend>.log`.
- Numeric reductions and visualization PPMs:
  `build/Tests/EnvironmentRendering/Codex-U5A-<Config>-<backend>/M15_U5A_hiz.txt`
  and `M15_U5A_vis-level{0,3,6}.ppm` in the same directory.
- Editor startup: `build/Codex-U5A-Editor-<Config>-<backend>.log`.

All four Editor startup requests run five empty-project UI frames and exit
cleanly. Startup is not a per-level visual capture. Interactive capture remains
**NOT RUN / user-deferred**, including earlier deferred visual gates.
Existing full CTest working-directory limitations in older material source tests
are not used to claim a full CTest run; the full unit executable ran from the
repository root and the two focused Hi-Z CTests passed.

The protocol-required separate-context reviewer checked attachment-format
preparation/lifetime, capability fallback, selector bounds, reduction tests and
depth oracle. No blocking source finding remains. Its recheck was source-only;
runtime results above were executed independently by Codex. Original Gemini
self-verification remains historical reported evidence.
