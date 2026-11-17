# M15 U4 Batch 4 — Regression Fixtures (No-Sun, No-AO, No-Contact, Emissive, and Mirror/Unoccluded Sphere)

The user authorized implementation of Milestone 15 items `M15-U4-06.1` through `M15-U4-06.5` (M15-U4-D). Dedicated CPU unit contracts, production forward radiance fixtures, and native D3D12/Vulkan GPU verification evidence are recorded below. Independent review and acceptance remains pending for Codex and the user.

---

## 1. M15-U4-06.1 — Test No-Sun Regression

**PASS (self-verification).**

### Contract & Native Behavior
- **CPU Contract (`Axle::OcclusionContract::VerifyNoSunRegression`):**
  When no directional sun light is present in the scene (`HasSun = false`):
  $$L_o(\text{contact}=1.0) \equiv L_o(\text{contact}=0.0) = L_{\text{ambient}} \cdot ao + L_{\text{local}} + L_{\text{emissive}}$$
  Variations in contact shadow visibility have strictly zero effect on scene radiance.
  All computed radiance values are strictly finite (`std::isfinite`).
- **Native Production Radiance (`RunM15OcclusionForwardTests`):**
  - Verified on both built-in `ForwardPBR.hlsl` and canonical `MaterialSurface.hlsl`:
    - `no-sun-contact1` vs `no-sun-contact0` under isolated local lighting: `equal(modified, reference)` passes with max error $0.0$.
    - `no-sun-amb-contact1` vs `no-sun-amb-contact0` under isolated ambient lighting: `equal(modified, reference)` passes with max error $0.0$.
  - All readback pixels are verified finite.

---

## 2. M15-U4-06.2 — Test No-AO Regression

**PASS (self-verification).**

### Contract & Native Behavior
- **CPU Contract (`Axle::OcclusionContract::VerifyNoAORegression`):**
  When ambient occlusion evaluation is disabled (`EnableAO = false`):
  $$L_o(ao = 0.0) \equiv L_o(ao = 1.0) = L_{\text{ambient}} \cdot 1.0 + L_{\text{direct}} + L_{\text{emissive}}$$
  Ambient lighting receives full neutral 1.0 factor; direct sun, local lights, and emissive remain unaltered.
  All outputs are strictly finite.
- **Native Production Radiance (`RunM15OcclusionForwardTests`):**
  - Verified on built-in and canonical graph forward shaders:
    - `no-ao-reference` ($AO = 1.0$, `EnableAO = true`) vs `no-ao-disabled-zero` ($AO = 0.0$, `EnableAO = false`): `equal(modified, reference)` passes.
    - Disabled AO completely ignores synthetic zero occlusion buffer and preserves full ambient brightness.
  - All readback pixels are verified finite.

---

## 3. M15-U4-06.3 — Test No-Contact Regression

**PASS (self-verification).**

### Contract & Native Behavior
- **CPU Contract (`Axle::OcclusionContract::VerifyNoContactRegression`):**
  When contact shadows are disabled (`EnableContactShadows = false`):
  $$V_{\text{sun}} = V_{\text{csm}}$$
  Directional sun radiance follows standard cascaded shadow map visibility without contact shadow clipping or double-attenuation.
  Radiance at `contact = 0.0` strictly equals unattenuated reference at `contact = 1.0`.
  All outputs are strictly finite.
- **Native Production Radiance (`RunM15OcclusionForwardTests`):**
  - Verified on built-in and canonical graph forward shaders:
    - `no-contact-reference` (`contact = 1.0`, `EnableContactShadows = true`) vs `no-contact-disabled-zero` (`contact = 0.0`, `EnableContactShadows = false`): `equal(modified, reference)` passes.
  - All readback pixels are verified finite.

---

## 4. M15-U4-06.4 — Test Emissive Regression

**PASS (self-verification).**

### Contract & Native Behavior
- **CPU Contract (`Axle::OcclusionContract::VerifyEmissiveInvariance`):**
  Emissive surfaces are completely decoupled from both ambient occlusion and contact shadows:
  $$\frac{\partial L_{\text{emissive}}}{\partial ao} = 0, \quad \frac{\partial L_{\text{emissive}}}{\partial \text{contact}} = 0$$
  $$L_o(\text{emissive}, ao=0, \text{contact}=0) \equiv L_o(\text{emissive}, ao=1, \text{contact}=1) \equiv \text{comp.Emissive}$$
  Neither AO nor contact shadows darken emissive radiance under any combination of inputs.
  All outputs are strictly finite.
- **Native Production Radiance (`RunM15OcclusionForwardTests`):**
  - Verified on built-in and canonical graph forward shaders:
    - `emissive-bright-ref` ($AO = 1.0, \text{contact} = 1.0$) vs `emissive-darkened-test` ($AO = 0.0, \text{contact} = 0.0$): `equal(modified, reference)` passes.
    - Reference center pixel radiance is strictly non-zero ($> 0.05$).
  - All readback pixels are verified finite.

---

## 5. M15-U4-06.5 — Test Mirror / Unoccluded Sphere

**PASS (self-verification); Editor visual gate remains user-provided.**

### Contract & Native Behavior
- **CPU Contract (`Axle::OcclusionContract::VerifyUnoccludedSpecularInvariance`):**
  Unoccluded specular highlights (metallic = 1.0, roughness = 0.0 - 0.2):
  Direct specular glints from directional sun or local lights are 100% invariant under AO variation:
  $$L_{\text{specular}}(ao = 0.0) \equiv L_{\text{specular}}(ao = 1.0)$$
  AO attenuation strictly modulates indirect ambient, leaving specular glints unattenuated.
  All outputs are strictly finite.
- **Native Production Radiance (`RunM15OcclusionForwardTests`):**
  - `mirror-specular-ref` ($AO = 1.0, \text{contact} = 1.0$) vs `mirror-specular-ao0` ($AO = 0.0, \text{contact} = 1.0$): `equal(modified, reference)` passes on both built-in and canonical graph shaders.
- **Native Environment Sphere Test (`Test_EnvironmentRendering.cpp`):**
  - A high-fidelity metallic sphere (`RHIMesh::CreateSphere(1)`, metallic = 1.0, roughness = 0.2) is rendered in an HDR environment under Direct3D 12 and Vulkan.
  - Verified that specular highlight at sphere center remains uncrushed under GTAO (`gtao[center] > 0`).
  - Verified that surface shading across adjacent pixels is smooth and continuous without triangle bands or faceting (`diffR <= 25 && diffD <= 25`).
  - Native image artifacts saved to `build/Tests/EnvironmentRendering/U4D-DX12/M15_U4D_sphere.ppm` and `build/Tests/EnvironmentRendering/U4D-Vulkan/M15_U4D_sphere.ppm`.
- **Editor Visual Gate:**
  - `AxleEditor.exe` builds cleanly in both Debug and Release configurations (exit code 0).
  - Interactive Editor visual capture remains user-provided.

---

## 6. Verification Evidence Matrix

### 1. Architecture Layering & Code Standards
| Check | Command | Result |
| :--- | :--- | :---: |
| Layering rules | `python scripts/check_architecture.py` | `AXLE_ARCH_CHECK PASS` (611 files, 25 CMake files) |
| Git whitespace | `git diff --check` | Exit 0 (clean, no trailing whitespace or format issues) |

### 2. CPU Contracts & Unit Tests
| Configuration | Command | Exit Code | Result |
| :--- | :--- | :---: | :--- |
| Debug Focused | `build\Tests\Debug\AxleRenderUnitTests.exe --m15-occlusion-contract` | 0 | PASS (U4-01 through U4-06.5) |
| Debug Full | `build\Tests\Debug\AxleRenderUnitTests.exe` | 0 | PASS (All unit suites pass, 100%) |
| Release Focused | `build\Tests\Release\AxleRenderUnitTests.exe --m15-occlusion-contract` | 0 | PASS (U4-01 through U4-06.5) |
| Release Full | `build\Tests\Release\AxleRenderUnitTests.exe` | 0 | PASS (All unit suites pass, 100%) |

### 3. Native GPU Parity & Regression (Release)
| Backend | Command | Exit Code | Result |
| :--- | :--- | :---: | :--- |
| Direct3D 12 | `build\Tests\Release\AxleEnvironmentRenderingTests.exe dx12 build/Tests/EnvironmentRendering/U4D-DX12` | 0 | PASS (Parity `diff=(0,0,0)`, HDR, CSM, GTAO, U4-D regressions, `M15_U4D_sphere.ppm` generated) |
| Vulkan 1.3 | `build\Tests\Release\AxleEnvironmentRenderingTests.exe vulkan build/Tests/EnvironmentRendering/U4D-Vulkan` | 0 | PASS (Parity `diff=(0,0,0)`, HDR, CSM, GTAO, U4-D regressions, `M15_U4D_sphere.ppm` generated) |

### 4. Editor Compilation
| Configuration | Command | Exit Code | Result |
| :--- | :--- | :---: | :--- |
| Debug Editor | `cmake --build build --config Debug --target AxleEditor -j 6` | 0 | PASS |
| Release Editor | `cmake --build build --config Release --target AxleEditor -j 6` | 0 | PASS |
