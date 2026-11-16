# M15 U4 Batch 2 — Directional Contact Shadow Application & CSM/Local Non-Double Attenuation

The user authorized implementation of Milestone 15 items `M15-U4-04.1` and `M15-U4-04.2` (M15-U4-B). CPU unit contracts and GPU native verification evidence are recorded below. Independent review and acceptance remains pending for Codex and the user.

---

## 1. M15-U4-04.1 — Apply Contact Visibility Strictly to Directional Sun Contribution

**PASS (self-verification).**

### Contract & Formulation
In `Axle::OcclusionContract`:
- Directional contact shadow visibility $V_{\text{contact}} \in [0.0, 1.0]$ modulates strictly the matching directional sun radiance:
  $$L_{\text{sun}} = (\text{SunDiffuse} + \text{SunSpecular}) \cdot V_{\text{sun}}$$
- **Local Light Isolation:**
  Local lights (point lights, spot lights) are independent of directional contact shadows:
  $$L_{\text{local}} = \text{LocalDiffuse} + \text{LocalSpecular}$$
  $$\frac{\partial L_{\text{local}}}{\partial V_{\text{contact}}} = 0$$
- **Ambient and Emissive Invariance:**
  $$\frac{\partial L_{\text{ambient}}}{\partial V_{\text{contact}}} = 0, \quad \frac{\partial L_{\text{emissive}}}{\partial V_{\text{contact}}} = 0$$
- **Sun Present vs. Sun Absent:**
  When no directional sun light exists (`HasSun = false`), $L_{\text{sun}} = 0$. Contact shadows have zero effect on total scene radiance:
  $$L_o(\text{contact}=1.0) = L_o(\text{contact}=0.0) = L_{\text{ambient}} \cdot ao + L_{\text{local}} + L_{\text{emissive}}$$
- **Contact Shadows Disabled:**
  When contact shadows are disabled (`EnableContactShadows = false`), directional sun radiance preserves normal direct-light behavior:
  $$V_{\text{sun}} = V_{\text{csm}}$$
- **Shader & Pass Guards:**
  - `GTAOPass.cpp`: `constants.SunDirView` and `constants.ContactParams` are set to `0.0` when directional light is null or `EnableContactShadows` is false.
  - `GTAO.hlsl`: The screen-space contact raymarch is gated by `if (u_SunDirView.w > 0.5 && u_ContactParams.z > 0.001)`. When gated off, `contactShadow` outputs neutral `1.0`. In the apply pass, `contactWeight = 0.20 * saturate(u_ContactParams.z) == 0.0`, rendering `contactFactor = 1.0`.

---

## 2. M15-U4-04.2 — Avoid CSM and Local Double Attenuation

**PASS (self-verification).**

### Directional Visibility Combination Rule
To prevent double attenuation between cascaded shadow maps (macro-occlusion) and screen-space contact shadows (micro-occlusion along the same sun vector):
$$V_{\text{sun}} = \text{CombineDirectionalVisibility}(V_{\text{csm}}, V_{\text{contact}}) = \min(V_{\text{csm}}, V_{\text{contact}})$$

### Non-Double Attenuation Guarantees
1. **Fully Shadowed CSM Regions:**
   When $V_{\text{csm}} = 0.0$ (surface point fully occluded by distant caster in CSM), $V_{\text{sun}} = \min(0.0, V_{\text{contact}}) = 0.0$. Contact shadows cannot double-darken or apply redundant attenuation.
2. **Penumbra Non-Compounding:**
   When $V_{\text{csm}} = 0.5$ (CSM filter penumbra) and $V_{\text{contact}} = 0.5$, $V_{\text{sun}} = \min(0.5, 0.5) = 0.5$. Compounded multiplicative attenuation ($0.5 \times 0.5 = 0.25$) is strictly rejected.
3. **CSM Disabled:**
   When CSM is disabled (`EnableCSM = false`), $V_{\text{sun}} = V_{\text{contact}}$, allowing unclipped micro-shadowing.
4. **Contact Disabled:**
   When contact shadows are disabled (`EnableContactShadows = false`), $V_{\text{sun}} = V_{\text{csm}}$, preserving normal cascade shadow behavior.
5. **Local Shadows Independence:**
   Point light shadow cube maps and spot light shadow maps maintain their own attenuation factors and are never multiplied by directional contact shadows.

---

## 3. Verification Evidence Matrix

### 1. Architecture Layering & Code Standards
| Check | Command | Result |
| :--- | :--- | :--- |
| Layering rules | `python scripts/check_architecture.py` | `AXLE_ARCH_CHECK PASS` (611 files, 25 CMake files) |
| Git whitespace | `git diff --check` | Exit 0 (no trailing whitespace or format issues) |

### 2. CPU Contracts & Unit Tests
| Configuration | Command | Exit Code | Result |
| :--- | :--- | :---: | :--- |
| Debug Focused | `build\Tests\Debug\AxleRenderUnitTests.exe --m15-occlusion-contract` | 0 | PASS (U4-01 through U4-04.2) |
| Debug Full | `build\Tests\Debug\AxleRenderUnitTests.exe` | 0 | PASS (All unit suites pass) |
| Release Focused | `build\Tests\Release\AxleRenderUnitTests.exe --m15-occlusion-contract` | 0 | PASS (U4-01 through U4-04.2) |
| Release Full | `build\Tests\Release\AxleRenderUnitTests.exe` | 0 | PASS (All unit suites pass) |

### 3. Native GPU Parity & Regression (Release)
| Backend | Command | Exit Code | Result |
| :--- | :--- | :---: | :--- |
| Direct3D 12 | `build\Tests\Release\AxleEnvironmentRenderingTests.exe dx12 build/Tests/EnvironmentRendering/U4-DX12` | 0 | PASS (Parity `diff=(0,0,0)`, HDR, CSM, Pass list preserved) |
| Vulkan 1.3 | `build\Tests\Release\AxleEnvironmentRenderingTests.exe vulkan build/Tests/EnvironmentRendering/U4-Vulkan` | 0 | PASS (Parity `diff=(0,0,0)`, HDR, CSM, Pass list preserved) |
