# M15 U4 Batch 5 — Normal-Mapped Materials and Animated/Deformed Mesh (U4-07.1 & U4-07.2)

The user authorized implementation of Milestone 15 items `M15-U4-07.1` through `M15-U4-07.2` (M15-U4-E). Dedicated CPU unit contracts, production forward radiance fixtures, and native D3D12/Vulkan GPU verification evidence are recorded below. Independent review and acceptance remains pending for Codex and the user.

---

## 1. M15-U4-07.1 — Test Normal-Map Behavior

**PASS (self-verification).**

### Contract & Native Behavior
- **CPU Contracts (`Axle::OcclusionContract::EvaluateTangentSpaceNormal`, `VerifyNormalMapOcclusionStability`):**
  - **Tangent-space normal decoding:** Raw texture samples $(rg \in [0, 1])$ are decoded to $[-1, 1]$ with reconstructed $z = \sqrt{\max(0, 1 - x^2 - y^2)}$ and scaled by authored normal strength.
  - **Orthonormal TBN construction:** Reuses existing U3 normal transformation rules (`TransformNormalCofactors`, `TransformTangent`). Gram-Schmidt orthonormalization guarantees $N \cdot T = 0$, and bitangent handedness ($B = (N \times T) \cdot \text{sign}(T.w)$) is preserved under mirrored transforms.
  - **ScreenNormal buffer generation:** Declared as valid `RGBA16_FLOAT` render target with $w = 1.0$ surface coverage marker (background cleared to 0.0). Both built-in `SurfaceNormal.hlsl` and custom graph `MaterialSurface.hlsl` (`PSNormalMain`) evaluate identical normal perturbation.
  - **GTAO horizon search & stability:**
    - Reconstructed view-space normal $N_{\text{view}} = \text{normalize}((M_{\text{view}})_{3\times 3} N_{\text{world}})$.
    - Tangent-plane rejection ($\vec{\delta} \cdot \vec{N}_{\text{view}} > \text{bias}$) prevents self-occlusion artifacts and false acne on high-frequency perturbed surfaces.
    - Grazing/back-facing perturbation protection: when normal map causes normal to point away from the eye ($N_{\text{view}} \cdot \text{viewDir} \le 0$), GTAO cleanly falls back to neutral visibility ($AO = 1.0$), eliminating black faceting, halos, and NaNs.
    - Directional contact shadow stability: $N_{\text{view}} \cdot L > 0$ determines ray launch; ray origin bias offset $p_{\text{ray}} = p_{\text{view}} + N_{\text{view}} \cdot (2 \cdot \text{bias})$ avoids self-intersection / acne.
  - **Shading composition parity:**
    - Direct diffuse and specular glints evaluate with perturbed normal.
    - Specular highlights and directional sun contributions are 100% invariant under AO variations ($AO = 0$ vs $AO = 1$).
    - AO modulates only indirect/ambient lighting linearly.
    - Emissive radiance is strictly invariant.
    - All outputs are strictly finite (`std::isfinite`).

- **Native Production Radiance (`RunM15OcclusionForwardTests`):**
  - Verified across both built-in `ForwardPBR.hlsl` and canonical `MaterialSurface.hlsl`:
    - `normal-map-specular-ref` vs `normal-map-specular-ao0`: `equal(modified, reference)` passes (specular glints invariant under AO).
    - `normal-map-sun-ref` vs `normal-map-sun-ao0`: `equal(modified, reference)` passes (directional sun invariant under AO).
    - `normal-map-ambient-ref` vs `normal-map-ambient-half`: `equal(modified, reference, 0.5f)` passes (AO scales ambient linearly).
    - `normal-map-emissive-ref` vs `normal-map-emissive-darkened`: `equal(modified, reference)` passes (emissive strictly invariant).
  - All readback pixels are verified finite across all channels.

---

## 2. M15-U4-07.2 — Test Animated / Deformed Mesh

**PASS (self-verification).**

### Contract & Native Behavior
- **CPU Contracts (`Axle::OcclusionContract::EvaluateDeformedVertex`, `VerifyDeformedMeshOcclusionStability`):**
  - **Vertex deformation:** Linear blend skinning / dynamic mesh deformation evaluates:
    $$P' = M_{\text{skin}} P, \quad N' = \text{normalize}((M_{\text{skin}})_{\text{invT}} N), \quad T' = \text{normalize}(M_{\text{skin}} T)$$
    Normals and tangents preserve strict orthogonality under non-uniform scaling and skeletal rotation ($|N' \cdot T'| < 10^{-3}$).
  - **Depth reconstruction:** View-space depth $Z'$ accurately tracks the deformed mesh geometry.
  - **Bilateral edge protection & disocclusion isolation:**
    - Disocclusion / silhouette step: when moving deformed mesh is in front of background surface ($\Delta Z = |Z_{\text{bg}} - Z| > 0.5$), cross-bilateral filter evaluates $W_{\text{depth}} \to 0$ (tap rejected), completely preventing occlusion halos, ghosting, or disocclusion smearing.
    - Continuous deformed surface ($\Delta Z \le 0.05$): bilateral filter maintains full spatial weight, reconstructing smooth continuous occlusion without triangle bands or faceting.
  - **Parameter sweep:** Tested over skeletal joint bend angles ($0^\circ, 15^\circ, 30^\circ, 60^\circ, 90^\circ, 120^\circ$) with non-uniform scale and rotation; all outputs remain bounded and finite.

- **Native Production Radiance (`RunM15OcclusionForwardTests`):**
  - Verified across built-in and canonical graph forward pipelines with dynamically deformed vertex buffers:
    - `deformed-mesh-sun-ref` vs `deformed-mesh-sun-ao0`: `equal(modified, reference)` passes (directional sun invariant under AO).
    - `deformed-mesh-local-ref` vs `deformed-mesh-local-contact0`: `equal(modified, reference)` passes (local lights invariant under contact shadow variations).
    - `deformed-mesh-emissive-ref` vs `deformed-mesh-emissive-darkened`: `equal(modified, reference)` passes (emissive radiance preserved).
  - All readback pixels are verified finite across all channels.

---

## 3. Verification Commands & Evidence Matrix

| Gate / Configuration | Command | Exit Code | Result Summary |
| :--- | :--- | :--- | :--- |
| **Debug Occlusion Unit** | `build\Tests\Debug\AxleRenderUnitTests.exe --m15-occlusion-contract` | `0` | **PASS**: 11/11 contract sections pass (U4-01 through U4-07.2). |
| **Debug Full Unit Suite** | `build\Tests\Debug\AxleRenderUnitTests.exe` | `0` | **PASS**: 100% of all unit test suites pass. |
| **Debug Focused CTest** | `ctest --test-dir build -C Debug -R ^M15OcclusionContract$ --output-on-failure` | `0` | **PASS**: 1/1 CTest passed (0.03s). |
| **Debug D3D12 Native** | `build\Tests\Debug\AxleEnvironmentRenderingTests.exe dx12 build/Tests/EnvironmentRendering/U4E-Debug-DX12` | `0` | **PASS**: 135 PSOs, zero validation errors, parity `diff=(0,0,0)`. |
| **Debug Vulkan Native** | `build\Tests\Debug\AxleEnvironmentRenderingTests.exe vulkan build/Tests/EnvironmentRendering/U4E-Debug-Vulkan` | `0` | **PASS**: 135 PSOs, zero validation errors, parity `diff=(0,0,0)`. |
| **Release Occlusion Unit** | `build\Tests\Release\AxleRenderUnitTests.exe --m15-occlusion-contract` | `0` | **PASS**: 11/11 contract sections pass (U4-01 through U4-07.2). |
| **Release Full Unit Suite** | `build\Tests\Release\AxleRenderUnitTests.exe` | `0` | **PASS**: 100% of all unit test suites pass. |
| **Release Focused CTest** | `ctest --test-dir build -C Release -R ^M15OcclusionContract$ --output-on-failure` | `0` | **PASS**: 1/1 CTest passed (0.01s). |
| **Release D3D12 Native** | `build\Tests\Release\AxleEnvironmentRenderingTests.exe dx12 build/Tests/EnvironmentRendering/U4E-Release-DX12` | `0` | **PASS**: 135 PSOs, zero validation errors, parity `diff=(0,0,0)`. |
| **Release Vulkan Native** | `build\Tests\Release\AxleEnvironmentRenderingTests.exe vulkan build/Tests/EnvironmentRendering/U4E-Release-Vulkan` | `0` | **PASS**: 135 PSOs, zero validation errors, parity `diff=(0,0,0)`. |
| **Release Editor Startup** | `build/Editor/Release/AxleEditor.exe --project=D:/WorkSpace/AxleEngine/Tests/Fixtures/M15/U4D/U4D.axleproj --rhi=vulkan --verify-startup` | `0` | **PASS**: loads 2 entities, renders 5 UI frames, clean teardown. |
| **Debug Editor Startup** | `build/Editor/Debug/AxleEditor.exe --project=D:/WorkSpace/AxleEngine/Tests/Fixtures/M15/U4D/U4D.axleproj --rhi=vulkan --verify-startup` | `0` | **PASS**: loads 2 entities, renders 5 UI frames, clean teardown. |
| **Architecture Gate** | `python scripts/check_architecture.py` | `0` | **PASS**: 611 files, 25 CMake files. |
| **Diff / Style Gate** | `git diff --check` | `0` | **PASS**: no whitespace / formatting anomalies. |

---

## 4. Open Gates & Next Actions

- **Interactive Editor Visual Inspection:** Both Debug and Release `AxleEditor` compile, link, and successfully load the dedicated test scene (`Tests/Fixtures/M15/U4D/U4D.axleproj`) for 5 UI frames. Interactive screen capture remains open per project protocol.
- **Stop Condition:** Stopping at the `M15-U4-E` boundary (`M15-U4-07.1` through `U4-07.2`). Handoff to Codex for independent review.
