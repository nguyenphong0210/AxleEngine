# M15 U4 Batch 3 — Independent Codex Review

## Scope

Reviewed Gemini delivery `1416a066` for `M15-U4-05.1` through
`M15-U4-05.4`, based on `8505a6f7`, including the delivery report and
exchange metadata. The review covers half-resolution GTAO allocation and
dispatch, bilateral reconstruction, depth-edge protection, odd-resolution
handling, and preservation of the U4-A/U4-B composition contracts.

## Findings and corrections

### U4C-R1 — HIGH: AO and depth were blended before edge rejection

`GTAO.hlsl` sampled both full-resolution depth and half-resolution visibility
with a linear sampler. A visibility sample could contain foreground and
background contributions before its single depth weight was calculated.
Exact depth loads alone did not correct the mixed AO sample.

**FIXED:** compute selects one real full-resolution depth/normal texel for each
half-resolution output. Reconstruction loads individual AO texels and pairs
each with exactly that compute-source depth before rejecting and accumulating
the tap. Both stages use the same integer center mapping for floor-half odd
extents. `GTAOApply` receives full-resolution scene dimensions and queries the
AO texture dimensions separately. A 5x5 half-resolution kernel is retained;
half-resolution tap spacing itself is a valid design.

The new native fixture executes production `VSMain`/`PSMain` against authored
step-depth and visibility images. It reproduced the incomplete repair at 8x4
with maximum final-color error `0.00747687` (exit 1). The corrected shader
passes with maximum error `1.78814e-7` (tolerance `2e-5`).

### U4C-R2 — HIGH: the isolated-surface fallback imported unrelated visibility

If a thin foreground surface had no representative half-resolution texel,
the low-weight fallback sampled the linearly filtered center AO. That AO could
belong entirely to another surface, so the fallback reintroduced the edge leak.

**FIXED:** invalid/background and rejected depth taps contribute no weight;
an unmatched surface receives neutral visibility `(1,1)`. This intentionally
loses occlusion on unrepresented subpixel surfaces instead of importing
foreign-surface shadows. Native tests cover a single unrepresented foreground
pixel and minimal views where one side of a depth edge has no representative.

### U4C-R3 — MEDIUM: tests did not exercise actual texture reconstruction

The delivered CPU tests combined already-paired AO/depth scalars. Native
environment tests used 128x128 average luminance and unrelated center parity,
so both passed despite silhouette leakage and absent odd-resolution coverage.
Source inspection also depended on the process working directory.

**FIXED:** native tests now run the production apply shader on 14 synthetic
fixtures, covering vertical/horizontal edges, reversed AO/contact visibility,
flat fields, alpha preservation, thin fallback, odd sizes and minimal extents.
Real renderer allocation/recording/compute/apply is additionally exercised at
1x1, 3x5, 127x63 and 1919x1079, reading and checking every half-resolution
visibility texel. Source paths now come from CMake, so the focused CTest also
passes from the build directory. CPU helpers reject zero extents and invalid
depths and match the shader's zero-weight edge rejection.

No unresolved findings remain in U4-C. Allocation retains
`max(1, full >> 1)`, including `1919x1079 -> 959x539`; no additional target or
VRAM allocation is introduced. U4-A/U4-B composition contracts and the bounded
post-composite fallback remain unchanged.

## Verification

The following commands passed with exit 0 for **both Debug and Release**:

```powershell
cmake --build build --config <Config> --target AxleRenderUnitTests AxleEnvironmentRenderingTests --parallel 8
build/Tests/<Config>/AxleRenderUnitTests.exe --m15-occlusion-contract
build/Tests/<Config>/AxleRenderUnitTests.exe
ctest --test-dir build -C <Config> -R '^M15OcclusionContract$' --output-on-failure
```

All four native commands passed with exit 0, production edge/dispatch assertions
passing, no validation errors, and SDR/HDR parity `diff=(0,0,0)`:

```powershell
build/Tests/Debug/AxleEnvironmentRenderingTests.exe dx12 build/Tests/EnvironmentRendering/Codex-U4C-Final-Debug-DX12
build/Tests/Debug/AxleEnvironmentRenderingTests.exe vulkan build/Tests/EnvironmentRendering/Codex-U4C-Final-Debug-Vulkan
build/Tests/Release/AxleEnvironmentRenderingTests.exe dx12 build/Tests/EnvironmentRendering/Codex-U4C-Final-Release-DX12
build/Tests/Release/AxleEnvironmentRenderingTests.exe vulkan build/Tests/EnvironmentRendering/Codex-U4C-Final-Release-Vulkan
```

Each native artifact directory contains `M15_U4C_reconstruction.txt` (per-case
maximum pixel error) and `M15_U4C_dispatch.txt` (actual dimensions and checked
texel counts). Adjacent `.log` files contain full native output. CPU logs use
`build/Tests/EnvironmentRendering/Codex-U4C-Final-<Config>-Focused.log` and
`Codex-U4C-Final-<Config>-Full.log`. The failing pre-correction proof is in
`Codex-U4C-EdgeRegression-Before.log` in the same parent directory.

`python scripts/check_architecture.py` passed (`611` files, `25` CMake files),
and `git diff --check` passed. A separate-context read-only correction review
found no remaining blocking issues. Broader milestone Editor visual captures
remain user-provided; this verdict records automated/source acceptance of U4-C.

## Verdict

**ACCEPTED after correction.** M15-U4-05.1 through M15-U4-05.4 satisfy the
requested contract after the corrections above. The
corrections, tests, report, and exchange evidence are delivered together in
one Codex commit.
