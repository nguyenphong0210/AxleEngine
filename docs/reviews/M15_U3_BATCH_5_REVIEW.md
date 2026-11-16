# M15 U3 batch 5 — surface normal and material data contract

The user authorized implementation of milestone 15 package M15-U3-14 through M15-U3-19.4. CPU and GPU native verification evidence is recorded below. Independent review and acceptance remains pending for Codex and the user.

## U3-14 — Normal producer with normal/detail-normal decoding and strength

**PASS (self-verification).** `SurfaceNormal.hlsl` and `RHISceneRendererPasses.cpp` were upgraded to bring the normal pass into parity with `ForwardPBR.hlsl`:
- Binds `ObjectConstants` (b1), `MaterialConstants` (b2), and texture descriptor set (t0 Albedo, t1 Normal, t8 Detail Normal, t9 Detail Mask, s0 Sampler).
- Implements `DecodeMaterialNormal` supporting primary normal mapping, strength scaling, detail normal maps with mask weighting, and Reoriented Normal Mapping (RNM) blending.
- Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`: flat map decodes to unit +Z, normal strength attenuates tangent displacement smoothly, and RNM blending maintains base/detail identity.

## U3-15.1, U3-15.2, U3-15.3 — Inverse-transpose, mirrored transforms and normal-map transform

**PASS (self-verification).** Verified mathematically and contractually in `Test_M15_SurfaceAndMaterialContractUnit.cpp`:
- **15.1:** Nonuniform scale combined with oblique rotation verified using cofactor transform; normal and tangent remain strictly orthogonal ($n' \cdot t' = 0$, error $< 10^{-5}$).
- **15.2:** Negative-determinant transform (e.g. reflection along X) flips tangent handedness sign ($t.w \to -t.w$) while preserving outward-facing normal direction ($N.z > 0$), ensuring bitangent $B = (N \times T) \cdot w$ aligns with texture coordinate V direction.
- **15.3:** Tangent-space normal map perturbation transforms through the constructed TBN basis to displace the world normal towards the world tangent.

## U3-16 — Alpha-cutout/depth/normal coverage and background validity

**PASS (self-verification).**
- `SurfaceNormal.hlsl` now evaluates `u_SurfaceType` and `u_AlphaCutoff`. When `surfaceType == SurfaceType_Cutout` (1), fragments failing the alpha cutoff test execute `discard`, preventing depth and normal buffer writes.
- Covered geometry returns `float4(normalize(N), 1.0)`, providing an explicit `1.0` validity marker in the alpha channel, whereas cleared background pixels retain `0.0` validity.

## U3-17.1 through U3-17.6 — Material parameter unification

**PASS (self-verification).** Parity between `MaterialSurface.hlsl` and canonical `ForwardPBR.hlsl` verified:
- **17.1 (Base Color):** Lambertian diffuse normalization using `INV_PI`.
- **17.2 (Metallic/Roughness):** Clamped roughness bounds `[0.04, 1.0]` and saturated metallic `[0.0, 1.0]`.
- **17.3 (AO/Emission):** Ambient accumulation scaled by `saturate(surface.AO)` while direct lighting and emissive radiance are preserved.
- **17.4 (Normal/Tangent):** Shared TBN construction and normal decoding contract.
- **17.5 (Clearcoat/Anisotropy):** Clearcoat layer evaluation attenuating base specular and diffuse by $(1 - coat \cdot coatF)$.
- **17.6 (Reflection Inputs):** Unified direct sun lighting scaling via `DIRECT_LUX_TO_RADIANCE` ($0.000035$) and ambient specular evaluation using `FresnelSchlickRoughness`.

## U3-18 — sRGB/linear texture and packed ORM conventions

**PASS (self-verification).** Verified in `Test_M15_SurfaceAndMaterialContractUnit.cpp`:
- Texture slot color spaces confirmed: `Albedo` (slot 0) and `Emissive` (slot 5) are sRGB; `Normal` (slot 1), `Roughness/ORM` (slot 2), `Metallic` (slot 3), `AO` (slot 4), `Clearcoat` (slot 6), `AnisotropyFlow` (slot 7), `DetailNormal` (slot 8), and `DetailMask` (slot 9) are Linear.
- `RHI::RHIMaterialDesc` with `TextureMode::PackedORM` correctly selects `RHIShadingModel::PackedORMTexturedPBR` and maps the ORM texture to slot 2 (register t2).

## U3-19.1 through U3-19.4 — Material round-trip persistence

**PASS (self-verification).** Fixed serialization gap in `Engine/Renderer/src/Material.cpp` and `Engine/RHI/src/RHIMaterial.cpp` where advanced material parameters (`SheenEnabled`, `SheenColor`, `SheenRoughness`, `SubsurfaceEnabled`, `SubsurfaceColor`, `SubsurfaceRadius`, `AbsorptionEnabled`, `AbsorptionColor`, `Dispersion`) were omitted from Save/Load.
- **19.1 (Asset <-> Component):** Full property synchronization verified via `SetAsset()` (`SyncFromAsset()`) and `SyncToAsset()`.
- **19.2 (Component -> Extraction):** `SyncToRHI()` accurately populates `RHIMaterialDesc`.
- **19.3 (Extraction -> GPU):** `BuildConstants()` formats GPU `MaterialConstants` matching shader cbuffer layout.
- **19.4 (Save / Reopen):** Verified round-trip disk serialization and reloading across both structured `.axleasset` and flat `.mat` file formats.

## Verification Evidence Matrix

1. **Architecture & Standards:**
   - `python scripts/check_architecture.py`: `AXLE_ARCH_CHECK PASS` (611 files, 25 CMake files).
   - `git diff --check`: Exit 0 (clean formatting, no trailing whitespace).
2. **CPU Contracts & Unit Tests:**
   - Debug: `build/Tests/Debug/AxleRenderUnitTests.exe --m15-surface-material-contract` (exit 0).
   - Debug Suite: `build/Tests/Debug/AxleRenderUnitTests.exe` (all CPU tests pass, exit 0).
   - Release: `build/Tests/Release/AxleRenderUnitTests.exe --m15-surface-material-contract` (exit 0).
   - Release Suite: `build/Tests/Release/AxleRenderUnitTests.exe` (all CPU tests pass, exit 0).
3. **GPU Runtime Rendering (D3D12 & Vulkan):**
   - Debug D3D12: `build/Tests/Debug/AxleEnvironmentRenderingTests.exe dx12` (exit 0).
   - Debug Vulkan: `build/Tests/Debug/AxleEnvironmentRenderingTests.exe vulkan` (exit 0).
   - Release D3D12: `build/Tests/Release/AxleEnvironmentRenderingTests.exe dx12` (exit 0).
   - Release Vulkan: `build/Tests/Release/AxleEnvironmentRenderingTests.exe vulkan` (exit 0).
