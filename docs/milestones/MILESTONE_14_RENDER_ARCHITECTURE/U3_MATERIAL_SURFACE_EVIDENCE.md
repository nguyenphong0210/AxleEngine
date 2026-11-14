# M14-U3 - Canonical Material and Surface Contract Evidence

Updated: 2026-11-14 (Asia/Saigon)

**Implementation / self-verification:** PASS at `f241eb0`, following API checkpoint `86feca7`.
**Acceptance:** PASS - final separate-context correction recheck completed at `b476686`; no remaining U3 blocker.
**Scope:** canonical material inputs, GPU packing, effective binding identity,
explicit shader/graph support status, preview/runtime integration and temporal
invalidation. M15 lighting/material fidelity is excluded.

## 1. Contract and ownership

`MaterialResolver::Resolve(RenderMaterialData)` is the snapshot boundary. An
authored `RHIMaterial` owns common material values; duplicated snapshot mirrors
cannot override its resolved values. An unmaterialized procedural mesh uses an
explicit adapter preserving pattern colors and the existing GPU marker. It
always writes opaque `MaterialExtra`, avoiding the old sun-data union alias.

`ResolveShaderSurface` derives custom HLSL and typed Material Graph packing from
the same canonical base. It replaces material-owned engine uniforms with
resolved base color, UVs and roughness/metallic/AO; view/object/light uniforms
remain caller inputs. Typed graph instances retain transactional named overrides.
The existing 256-byte built-in GPU material ABI is unchanged.

For custom HLSL/graph evaluation, `ResolvedSurface` numeric fields describe CPU
engine inputs, **not CPU evaluation of per-pixel shader expressions**. Evaluation
domain, immutable shader generation, material generation, validity, fallback,
reason and unsupported feature mask are explicit. Advanced graph fields keep
neutral defaults and `FeatureFlags=0`. Unsupported base-material controls
(including emission and non-base maps) are reported instead of silently claimed
as supported. This does not implement M15 feature nodes or visual fidelity.

| Check | Result | Source anchor |
|---|---|---|
| Canonical schema and texture owners | PASS (self-verified) | `Engine/Renderer/include/Axle/ResolvedSurface.hpp:16` |
| Snapshot/material validation and procedural adapter | PASS (self-verified) | `Engine/Renderer/src/ResolvedSurface.cpp:81`, `:153` |
| Common shader inputs, effective properties, unsupported status | PASS (self-verified) | `Engine/Renderer/src/ShaderSurface.cpp:37` |
| Live custom/built-in lowering | PASS (self-verified) | `Engine/Scene/src/RHISceneRendererPasses.cpp:728`, `:765` |
| Typed graph generation/epoch and binding resolution | PASS (self-verified) | `Engine/Scene/src/MaterialGraphRuntime.cpp:160` |
| Shadow alpha binding from canonical surface | PASS (self-verified) | `Engine/Scene/src/RHIShadowDepthAtlas.cpp:139` |
| Actual texture identity and history invalidation | PASS (self-verified) | `Engine/Renderer/src/ShaderSurface.cpp:107`, `Engine/Scene/src/RHISceneRendererPasses.cpp:1352`, `Engine/Renderer/src/ViewContext.cpp:102` |
| Final independent correction recheck | PASS | [Correction recheck report](../../reviews/M14_U3_CORRECTION_RECHECK.md); see section 4 |

## 2. Effective resource identity and failure behavior

Canonical texture bindings retain authored paths and optional const native
owners. Native maps must expose a live, single-sample float Texture2D SRV;
uninitialized/stale, cube, array, multisample and incompatible integer bindings
cannot be published as a valid surface. Missing source files use explicit
neutral/last-good fallback diagnostics.

Each fresh draw surface is finalized once after actual cache/fallback binding
selection. Its generation includes actual native handles and effective texture
flags. Same-path reloads, fallback recovery, program reloads and effective
parameter edits therefore change identity. Keeping the same last-good texture
preserves effective identity while publishing fallback status. Identity excludes
model/view/light constants.

`RenderMaterialData::ContentRevision` is a runtime owner contract: increment it
after in-place native pixel writes, which preserve the resource handle. Path
reload/native replacement is tracked automatically. No asset format, per-asset
sidecar or content-pixel hash was introduced.

The live frame collects final per-entity identities and diagnostics after draws,
before temporal resolve. A material change invalidates history contents while
preserving already imported read/write slots; successful submission remains the
only history commit/flip point. Invalid canonical inputs fail preparation before
graph recording. Shader resolution/draw failure reports a built-in fallback
with shader generation zero rather than claiming custom execution succeeded.

## 3. Verification on the final source

The commands below ran in the original Codex U3 implementation session before
`b476686`. Every build and test invocation returned **exit 0**. Log/output paths
are ignored build artifacts. Fresh closeout runs are recorded in section 4.

### Builds

```text
cmake --build build --config Debug --target AxleRenderUnitTests AxleMaterialGraphRuntimeTests AxleMaterialGraphCompilerTests AxleMaterialGraphResourceTests --parallel 6
cmake --build build --config Debug --target AxleEnvironmentRenderingTests AxleCascadedShadowRenderingTests AxleLocalShadowRenderingTests AxlePreviewRenderTests AxleRuntimeRenderTests AxleMaterialGraphOpenGLPolicyTests --parallel 6
cmake --build build --config Release --target AxleRenderUnitTests AxleMaterialGraphRuntimeTests AxleMaterialGraphCompilerTests AxleMaterialGraphResourceTests --parallel 6
cmake --build build --config Debug --target AxleMaterialUnitTests --parallel 6
```

Logs: `build/m14-u3-integration-build.log`,
`build/m14-u3-regression-build.log`, `build/m14-u3-release-build.log`,
`build/m14-u3-material-build.log`. The last build emitted a non-fatal LNK4098
default-library warning; linking and all five material suites succeeded.

### CPU and graph fixtures

```text
build/Tests/{Debug,Release}/AxleRenderUnitTests.exe
build/Tests/Debug/AxleMaterialUnitTests.exe
build/Tests/Debug/AxleMaterialGraphOpenGLPolicyTests.exe
build/Tests/{Debug,Release}/{AxleMaterialGraphCompilerTests,AxleMaterialGraphRuntimeTests,AxleMaterialGraphResourceTests}.exe {policy,dx12,vulkan} <fixture> <output>
```

The braces above denote separately executed matrix entries, not literal shell
syntax. Fixture:
`D:/WorkSpace/AxleEngine/Tests/Fixtures/MaterialGraph/M_RustMetal.axleasset`.
An absolute input is required because fixture bootstrap changes the asset root.
Output is under `build/Tests/Work/M14-u3-final`, with Release runs in its
`Release` subdirectory. Native GPU runs were sequential.

- CPU render contracts passed in Debug/Release, including canonical byte packing,
  finite/policy validation, property/program/content identity, procedural packing
  and late history invalidation without slot changes. Material unit tests: 5/5.
- All three graph executables passed `policy`, D3D12 and Vulkan in both Debug
  and Release (18 invocations). The compiler's graph/reference image maximum
  pixel error was zero on both native backends/configurations.
- New live Scene native cases prove canonical inputs beat stale snapshot mirrors;
  model changes preserve identity; effective property edits change identity;
  native base-map binding, in-place upload plus ContentRevision, same-path reload,
  corrupt-file last-good retention, neutral fallback/recovery, unsupported status,
  NaN rejection/fallback and incompatible texture rejection behave as declared.
- Runtime/resource fixtures retain immutable-generation, rollback, device-epoch,
  sampler/color-space/mip/alpha/orientation and recorded-command lease checks.
  Resolved wrapper owners are dropped before lifetime callbacks so those checks
  still exercise native command leases.
- OpenGL graph policy passed with an explicit unsupported result; native graph
  execution on OpenGL remains unsupported by design.

Logs: `build/m14-u3-final-unit.log`, `build/m14-u3-release-unit.log`,
`build/m14-u3-material-unit.log`, `build/m14-u3-opengl-policy.log`,
`build/m14-u3-AxleMaterialGraph*Tests-{policy,dx12,vulkan}.log` and
`build/m14-u3-release-AxleMaterialGraph*Tests-{policy,dx12,vulkan}.log`.
Source: `Tests/rendering/Test_ResolvedSurfaceUnit.cpp:105`,
`Tests/rendering/Test_ViewContextUnit.cpp:64`,
`Tests/src/Test_MaterialGraphCompiler.cpp:290`,
`Tests/src/Test_MaterialGraphRuntime.cpp:60`.

### Native and product regressions (Debug)

```text
build/Tests/Debug/AxleEnvironmentRenderingTests.exe {dx12,vulkan,gl} <output>
build/Tests/Debug/{AxleCascadedShadowRenderingTests,AxleLocalShadowRenderingTests}.exe {dx12,vulkan} <output>
build/Tests/Debug/AxlePreviewRenderTests.exe {dx12,vulkan,gl} surfaces
build/Tests/Debug/AxlePreviewRenderTests.exe {dx12,vulkan} particles
AxleRuntimeRenderTests.exe dx12
AxleRuntimeRenderTests.exe dx12 cooked
```

Environment, cascade/local shadow cache/cutout/resize/retry, surface preview,
soft-particle preview and Runtime startup/input/simulation/presentation/shutdown
passed. Runtime ran from `build/Tests/Debug`; product runs prepend the absolute
Debug test, Engine/PluginSDK, Runtime and Editor binary directories to PATH.
Logs: `build/m14-u3-regression-Axle*Tests-*.log`,
`build/m14-u3-preview-{dx12,vulkan,gl}-surfaces.log`,
`build/m14-u3-preview-{dx12,vulkan}-particles.log`,
`build/m14-u3-runtime-dx12.log`, `build/m14-u3-runtime-dx12-cooked.log`.
Release product/preview runs and interactive Editor UI were **NOT RUN** in U3;
their historical results are not counted as current verification.

### Architecture and migration

```text
python scripts/check_architecture.py
  PASS: 600 source files, 25 CMake files
python scripts/generate_m14_dependency_map.py --output-dir build/Tests/Work/M14-u3-final/dependencies
  PASS: 0 architecture violations, 0 caller violations
git diff --check
  PASS
```

No production `Engine/Scene` caller packs custom material uniforms or resolves
texture requests independently of the shared shader surface resolver.

## 4. Review and acceptance recommendation

Focused separate-context review found effective texture identity, unsupported
emission/non-base maps, invalid assigned textures, procedural union packing,
test-wrapper lifetime and Texture2D/SRV compatibility gaps. These findings were
corrected before the final builds/native gates. Intermediate rechecks confirmed
the earlier corrections and temporal ordering. The final texture compatibility
correction and affected callers have now passed the resumed independent recheck.

The original delivery was PARTIAL because the reviewer reached its usage limit.
On user-requested closeout, the same separate-context reviewer resumed, inspected
the final guard, live/custom/typed graph/shadow callers, temporal behavior and
lifetime/negative fixtures at `b476686`, and explicitly returned **PASS: no
remaining U3 blocker; the pending correction gate may close**. This satisfies the
working protocol's final correction recheck. No Gemini approval is inferred.

Fresh Debug build of `AxleRenderUnitTests` and `AxleMaterialGraphCompilerTests`,
the CPU suite and native compiler fixtures on D3D12/Vulkan all returned exit 0.
Both native image comparisons had maximum error zero; cube/no-SRV negative cases
and the unchanged validation-error-count assertion passed. Logs are
`build/m14-u3-close-{build,unit}.log` and
`build/m14-u3-close-compiler-{dx12,vulkan}.log`. The reviewer inspected source and
these logs, but did not execute those commands. No source/test edits were needed.
Exact commands, source anchors and review limits are recorded in the
[final correction recheck](../../reviews/M14_U3_CORRECTION_RECHECK.md).

**Recommendation: accept U3.** No open U3 gate remains. Overall M14/U9 stay HOLD
for U4 resource-declaration and U5 policy-ownership work and consolidated final
acceptance. M15 is unreleased.

Historical prerequisites: `421cc49` repaired built-in shading/PackedORM/cutoff,
detail and raster policy; its pre-fix CPU reproduction failed, then passed after
repair. Those earlier U2/U3 results remain historical and are not substituted
for the current runs above. Compatibility authoring/snapshot fields remain a
migration boundary, but they no longer form a second common GPU packing path.
