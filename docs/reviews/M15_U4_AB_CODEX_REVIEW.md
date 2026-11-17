# M15 U4 A/B — Codex Runtime Re-review

Date: 2026-11-17. Reviewed current base `9028e8d7`, including Gemini U4-A
delivery `83ef0286`, U4-B delivery `ccc4ba61`, earlier Codex corrections, and
the accepted U4-C reconstruction. Scope: U4-03.1/03.2 and U4-04.1/04.2.

## Findings and corrections

### U4AB-R1 — BLOCKER, fixed: pre-lighting existed only as CPU contract math

The native graph ran GTAO after color lighting and its apply shader multiplied
the already-combined scene color. `EvaluateShading` and broad parity tests did
not establish direct/specular/emissive invariance in production. The previous
contract-only acceptance did not demonstrate the requested runtime behavior.

The corrected single-sample native graph runs `SurfaceNormalPrepass`, then
`GTAOCompute`, then `OpaqueAndSky`. The prepass reuses existing opaque depth and
normal targets; the compute output remains half resolution. ForwardPBR and the
canonical MaterialSurface shader consume reconstructed visibility while their
lighting terms are still separate. AO scales only ambient/indirect lighting.
Direct diffuse, direct specular and authored emission remain invariant.

No separate indirect target or MRT is added: forward shaders keep indirect
radiance in registers. At 1080p this avoids a prospective 16,588,800-byte
RGBA16_FLOAT indirect target. Existing auxiliary target allocation remains
available. The bounded legacy composite implementation is retained and shader
tested; it is not applied in addition to the separated forward path. Unsupported
MSAA/depth/storage combinations report neutral visibility explicitly.

### U4AB-R2 — HIGH, fixed: post-composite contact attenuated unrelated light

Contact was applied to completed scene color, affecting local lights and other
terms and compounding existing CSM attenuation. Forward shaders now compute
`min(CSM visibility, contact visibility)` on the matching sun radiance only.
Ambient, emission and local point/spot direct terms do not receive directional
contact attenuation. Separated shading uses raw visibility; the old soft floors
remain confined to the bounded fallback composite.

### U4AB-R3 — HIGH, fixed: graph parity could silently exercise built-in fallback

The canonical surface shader's renderer-owned lighting structs/resources were
passed to editable material planning, and the custom-source loader did not
compile its normal companion. Existing center-pixel parity could pass after a
built-in fallback. This concealed additional graph-path defects: a regular
sampler bound to `SamplerComparisonState`, and CSM split selection using clip
depth as view depth.

The shared reserved-binding classifier now preserves physical reflection while
excluding exact engine bindings from editable schemas. Logical engine buffer
sizes are validated. Graph layout validation handles engine structs/arrays
separately and retains them in its physical layout key. Optional normal
companions are compiled, reflected and both normal PSOs validated before a new
generation is published. Engine comparison and environment samplers now use
their correct policies; graph CSM reconstructs linear view depth.

Native environment tests require a valid custom shader generation with no
fallback, verify nonzero direct output, and read back graph-produced normals.
A stale dispersion setting in the old parity fixture is reset so its authored
surface is actually equivalent. Graph compiler/resource policy fixtures now
use the same engine-binding separation as production.

### U4AB-R4 — MEDIUM, fixed: guide lifetime and normal replay needed explicit rules

Visibility lives in a separate descriptor set (space 4), excluded from normal
prepass bindings. This prevents a graph read-before-write cycle. The prepass
matches material culling/cutout and skips non-depth-writers. SSR-only custom
normal replay uses equal-depth testing without depth writes. Custom shaders
without a normal companion are excluded from guides with a diagnostic.

Forward fragments whose depth does not match the guide receive neutral
visibility, so an unrepresented fragment cannot inherit background AO/contact.
Native tests cover this guard and graph depth-write-disabled rendering. Shared
reconstruction preserves U4-C's exact AO/depth pairing, odd-size mapping, edge
rejection and neutral thin-surface fallback.

### U4AB-R5 — LOW, fixed: CPU verifier disagreed with explicit light separation

`VerifySunOnlyContactModulation` could interpret legacy direct fields as sun
even when explicit local fields made `EvaluateShading` ignore those fields.
Both now use the same separation predicate; a focused regression covers the
mixed legacy/explicit input.

## Verification

Debug/Release builds of `AxleRenderUnitTests` and
`AxleEnvironmentRenderingTests` passed. Focused and full CPU suites plus
focused CTest passed with exit 0 in both configurations:

```powershell
build/Tests/<Config>/AxleRenderUnitTests.exe --m15-occlusion-contract
build/Tests/<Config>/AxleRenderUnitTests.exe
ctest --test-dir build -C <Config> -R '^M15OcclusionContract$' --output-on-failure
```

The additional Debug compiler/resource/runtime policy checks passed with exit 0:

```powershell
build/Tests/Debug/AxleMaterialGraphCompilerTests.exe policy D:/WorkSpace/AxleEngine/Tests/Fixtures/MaterialGraph/M_RustMetal.axleasset D:/WorkSpace/AxleEngine/build/Tests/EnvironmentRendering/Codex-U4AB-GraphCompiler
build/Tests/Debug/AxleMaterialGraphResourceTests.exe policy D:/WorkSpace/AxleEngine/Tests/Fixtures/MaterialGraph/M_RustMetal.axleasset D:/WorkSpace/AxleEngine/build/Tests/EnvironmentRendering/Codex-U4AB-GraphResource
build/Tests/Debug/AxleMaterialGraphRuntimeTests.exe policy D:/WorkSpace/AxleEngine/Tests/Fixtures/MaterialGraph/M_RustMetal.axleasset D:/WorkSpace/AxleEngine/build/Tests/EnvironmentRendering/Codex-U4AB-GraphRuntime
```

All four native final gates (D3D12/Vulkan, Debug/Release) **passed, exit 0**, with
no validation errors and SDR/HDR center parity `diff=(0,0,0)`. Output is in the
adjacent
`build/Tests/EnvironmentRendering/Codex-U4AB-Final-<Config>-<backend>.log`
files. Each invocation uses:

```powershell
build/Tests/<Config>/AxleEnvironmentRenderingTests.exe <backend> build/Tests/EnvironmentRendering/Codex-U4AB-Final-<Config>-<backend>
```

`M15_U4AB_forward.txt` in each native artifact directory records 64 draws: the
same radiance matrix runs on actual ForwardPBR and independently compiled
canonical MaterialSurface VS/PS bytecodes, with reflected graph descriptors.
All 40 comparison assertions per native run reported maximum error **0**.
The matrix checks AO 0/0.5/1, contact 0/0.5/1, disabled features, nonzero isolated
direct/specular/emissive/local lighting, mixed sun/local light, real comparison
PCF at CSM 0/0.5/1, minimum composition, and guide-depth mismatch. The environment
fixture additionally proves actual custom generation selection and normal
prepass/replay, including depth-write-disabled materials.

U4-C's 14 reconstruction fixtures and real compute/readback at 1x1, 3x5,
127x63 and 1919x1079 remain part of every native run. Their scorecards are
`M15_U4C_reconstruction.txt` and `M15_U4C_dispatch.txt` in the same directories.
CPU output uses `Codex-U4AB-Final-<Config>-{Focused,Full}.log`; graph policy
output uses `Codex-U4AB-Graph{Compiler,Resource,Runtime}.log`.

The separate-context correction reviewer found no remaining blocking issue.
`python scripts/check_architecture.py` passed (`611` files, `25` CMake files),
and `git diff --check HEAD` passed. Both Editor rebuilds passed with exit 0:

```powershell
cmake --build build --config Debug --target AxleEditor --parallel 8
cmake --build build --config Release --target AxleEditor --parallel 8
```

Logs are `Codex-U4AB-Editor-{Debug,Release}-Build.log` in the same artifact
parent directory. Interactive Editor visual capture remains user-provided.

## Verdict

**ACCEPTED after correction for U4-A and U4-B automated/source requirements.**
The corrected native path now implements the requested lighting separation,
with direct GPU evidence rather than CPU-only contract math. Earlier
contract-only acceptance is superseded by this runtime review. No user Editor
visual acceptance or later milestone package is claimed. All corrections,
tests and review evidence are delivered in one final Codex commit.
