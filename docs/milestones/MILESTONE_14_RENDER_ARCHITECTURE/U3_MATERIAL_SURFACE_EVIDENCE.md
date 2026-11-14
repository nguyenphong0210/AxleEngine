# M14-U3 - Canonical Material and Surface Contract Evidence

**Status:** `PARTIAL - built-in policy repaired; custom shader/graph boundary pending`
**Unit:** M14-U3  
**Scope:** one resolved CPU surface contract, deterministic GPU packing, material generation identity and Material Graph/preview compatibility.

## 1. Implementation

The canonical contract is defined in:

- `Engine/Renderer/include/Axle/ResolvedSurface.hpp`
- `Engine/Renderer/src/ResolvedSurface.cpp`

`ResolvedSurface` contains the shared values required by built-in PBR, shadow depth, preview and later screen-space consumers:

```text
BaseColor, Metallic, Roughness, AO, Emission
GeometricNormal, ShadingNormal, Tangent + handedness
UV and detail transforms/strength, shading mode and alpha cutoff
Clearcoat, anisotropy, sheen
Subsurface, absorption, IOR, refraction and dispersion
Surface/cull/depth/queue policy, reflection/specular flags
Texture mask, generation identity and validity
```

`MaterialResolver` converts `RHI::RHIMaterial` into this contract, clamps values at the boundary and produces a stable generation hash. `RHISceneRenderer` and `RHIShadowDepthAtlas` now consume the resolver for push constants and material constant packing instead of calling `RHIMaterial` packing methods directly.

## 2. GPU and graph contract

- `ResolvedSurface::ToMaterialConstants()` preserves the existing 256-byte `RHIMaterialConstants` layout and shader register contract.
- `ResolvedSurface::ApplyToPushConstants()` supplies the same base/emission/roughness/metallic policy to the built-in draw path.
- `MaterialSurface.hlsl` now declares advanced surface fields alongside the original six graph fields.
- `MaterialGraphHLSL` initializes advanced fields to explicit neutral/unsupported defaults when the graph emits only the currently authored six fields. `FeatureFlags=0` is the explicit unsupported marker; fields are not silently read as enabled advanced features.
- Material Graph compilation still uses the same template and reflection contract, so preview/runtime can consume the same generated surface structure.

This closes the architecture contract without claiming that M15 advanced graph nodes are implemented. M15 will add graph-authored advanced expressions by replacing explicit defaults and setting the corresponding feature flags.

## 3. Verification

### Canonical surface CPU fixture

Command:

```text
build/Tests/Debug/AxleRenderUnitTests.exe
```

Result: **exit code 0**. The `[ResolvedSurface]` suite verified clamping, advanced-field preservation, 256-byte GPU packing, generation changes after material edits and graph-template field exposure. All existing RenderContracts, ViewContext, RenderPass, RenderWorld, worker and transient-graph suites passed.

### Material Graph compiler fixture

Command:

```text
build/Tests/Debug/AxleMaterialGraphCompilerTests.exe policy <absolute-path-to-Tests/Fixtures/MaterialGraph/M_RustMetal.axleasset> build/Tests/MaterialGraphCompiler/Debug/policy
```

Result: **exit code 0**. DXIL/SPIR-V compilation, metadata, determinism, hostile names, rollback, sampler rejection and registered-lowering diagnostics passed. The fixture must receive an absolute asset path because its workspace bootstrap changes the asset root before reading the input file.

### Native renderer fixture

Command:

```text
build/Tests/Debug/AxleEnvironmentRenderingTests.exe dx12 build/Tests/Work/M14-U3-baseline
```

Result: **exit code 0**. Native D3D12 ForwardPBR/environment output remained valid with the 128x128 RGBA8/D32 baseline, two draw calls and one rendered entity.

### Build and architecture checks

```text
cmake --build build --config Debug --target AxleRenderUnitTests AxleEnvironmentRenderingTests --parallel 4  => exit 0
cmake --build build --config Debug --target AxleMaterialGraphCompilerTests --parallel 4                 => exit 0
python scripts/check_architecture.py                                                                         => PASS
git diff --check                                                                                              => PASS
```

## 4. Known boundary

`RenderMaterialData` and `MaterialComponent` remain compatibility snapshot structures during migration. Their conversion into `RHIMaterial` is now consumed through the canonical resolver at render/shadow use sites, while U8 will remove remaining duplicated authoring mirrors and add explicit generation propagation through the full asset/preview pipeline.

## 5. Current correction checkpoint (2026-11-14)

Commit `421cc49` repairs existing built-in lowering losses: textured/PackedORM
mode, authored alpha cutoff, detail normal transform/strength and raster/queue
policy now survive the canonical boundary. Its hash covers omitted policies,
source paths and explicitly assigned native generations. An unassigned slot
never allocates a fallback during identity calculation.

A new CPU parity fixture failed on the pre-fix canonical conversion
(`build/m14-u3-repro.log`, exit 1), then passed after repair
(`build/m14-u3-unit.log`, exit 0). Native fixtures confirm no fallback allocation
while hashing and a changed identity after assigned texture generation
replacement. Native cascaded/local shadows and environment fixtures passed on
D3D12/Vulkan, including alpha cutout and same-generation pixel upload redraw.
Detailed commands/results are in the current U2 evidence and repair addendum.

This is material assignment/source-policy identity. In-place pixel mutation and
same-path content reload require consumer invalidation or a separate content
revision; the hash does not claim to identify every texture pixel change.

## 6. U3 decision

**U3 PARTIAL.** Built-in/shadow policy lowering is repaired and native-verified.
The custom HLSL draw still packs common engine material inputs from compatibility
snapshot fields instead of the canonical surface; explicit graph/custom shader
resolution status and generation must be completed and tested. Generated
Material Graph advanced fields keep their neutral defaults and FeatureFlags=0;
adding M15 feature nodes/fidelity remains excluded. Historical fixture claims
above are retained as history and do not establish full current acceptance.
