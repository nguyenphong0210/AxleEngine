# M14-U3 - Canonical Material and Surface Contract Evidence

**Status:** `ACCEPTED`  
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
UV scale/offset and normal strength
Clearcoat, anisotropy, sheen
Subsurface, absorption, IOR, refraction and dispersion
Surface/cull/depth policy, reflection/specular flags
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

## 5. U3 decision

**U3 ACCEPTED.** The canonical surface contract, deterministic GPU packing, generation identity and explicit Material Graph unsupported-field policy are implemented and verified. The next released Unit is **M14-U4 - Resource lifetime and RenderGraph authority**.
