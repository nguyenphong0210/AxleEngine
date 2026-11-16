# M15 U4 Batch 1 — Pre-Lighting Occlusion Contract & Indirect Target Decision

The user authorized implementation of Milestone 15 items `M15-U4-03.1` and `M15-U4-03.2` (M15-U4-A). CPU unit contracts and GPU native verification evidence are recorded below. Independent review and acceptance remains pending for Codex and the user.

---

## 1. M15-U4-03.1 — Move AO Before Lighting (Ambient-Only Modulation)

**PASS (self-verification).**

### Contract & Mathematical Formulation
Under the pre-lighting occlusion contract (`Axle::OcclusionContract::DataflowMode::PreLightingSeparated`):
- Total surface radiance is evaluated with strict term separation:
  $$L_o = L_{\text{ambient}} \cdot ao + (L_d + L_s) \cdot cs + L_e$$
  where:
  - $L_{\text{ambient}}$: indirect/ambient radiance (diffuse irradiance + split-sum specular IBL)
  - $ao \in [0.0, 1.0]$: ambient occlusion factor computed from depth/normal fields
  - $L_d$: direct diffuse radiance (directional, point, spot lights)
  - $L_s$: direct specular radiance (GGX highlights)
  - $cs \in [0.0, 1.0]$: directional contact shadow attenuation
  - $L_e$: emissive radiance
- **Invariance Guarantees:**
  $$\frac{\partial L_d}{\partial ao} = 0, \quad \frac{\partial L_s}{\partial ao} = 0, \quad \frac{\partial L_e}{\partial ao} = 0$$
  Ambient occlusion modulates *strictly* indirect/ambient illumination. Direct specular glints, direct diffuse sun/point/spot lighting, and emissive surfaces remain perfectly invariant regardless of whether $ao = 0.0$ or $ao = 1.0$.
- **Rejection of Erroneous Whole-Scene Multiplication:**
  Whole-scene post-lighting compositing $L_o = (L_{\text{ambient}} + L_d + L_s + L_e) \cdot ao$ is explicitly rejected. Shaders (`ForwardPBR.hlsl`, `MaterialSurface.hlsl`) modulate ambient terms directly in registers, avoiding black-crushing of direct highlights and emissive fixtures.
- **Preservation of U4-01 / U4-02 Behavior:**
  - `ScreenNormal` target generation in `RHIViewTargets` is preserved (`RGBA16_FLOAT`).
  - GTAO compute soft minimum floor ($0.35$) is preserved to prevent geometric black crush.
  - Bounded composite weights ($0.25$ ambient weight, $0.20$ contact shadow weight) and post-lighting fallback composite passes (`GTAOCopy`, `GTAOCompute`, `GTAOApply`) are retained for backwards compatibility with existing native test suites.

*Note on Editor visual:* As per milestone planning, Editor visual verification remains a user-provided visual gate.

---

## 2. M15-U4-03.2 — Expose True Indirect Target Decision & Documentation

**PASS (self-verification).**

### Architectural Decision
**A separate physical indirect render target is NOT required for forward shading under the pre-lighting dataflow contract.**

### Decision Rationale
1. **In-Place Register Evaluation:**
   In forward rendering (`ForwardPBR.hlsl`, `MaterialSurface.hlsl`), all lighting terms (ambient, direct diffuse, direct specular, emissive) are computed within pixel shader registers. When ambient occlusion is generated before the forward lighting pass (or sampled via screen coordinates), the ambient radiance is scaled in-place prior to accumulation:
   $$\text{Color} = (\text{DiffuseAmbient} + \text{SpecularIBL}) \cdot ao + \text{DirectRadiance} \cdot cs + \text{Emissive}$$
2. **VRAM Savings:**
   Eliminating a separate full-resolution HDR indirect target saves substantial GPU memory:
   - At 1080p ($1920 \times 1080$, `RGBA16_FLOAT` = 8 bytes/pixel): $\mathbf{16.59\text{ MB}}$ per view.
   - At 1440p ($2560 \times 1440$, `RGBA16_FLOAT` = 8 bytes/pixel): $\mathbf{29.49\text{ MB}}$ per view.
   - At 4K ($3840 \times 2160$, `RGBA16_FLOAT` = 8 bytes/pixel): $\mathbf{66.36\text{ MB}}$ per view.
3. **Bandwidth & Render Pass Elimination:**
   Avoiding a separate indirect target eliminates Multiple Render Target (MRT) write bandwidth during forward rasterization and removes the subsequent fullscreen indirect composite pass.
4. **Decoupled / Auxiliary Target Support:**
   If decoupled indirect evaluation or debug visualization is requested in the future, `RHIViewTargetDesc::EnableScreenStorage` and `RHIViewTarget::ScreenStorage` provide the standardized mechanism to bind and allocate auxiliary screen storage without altering view target generation contracts.

The decision is codified in `Axle::OcclusionContract::IsSeparateIndirectTargetRequired()` and `Axle::OcclusionContract::GetIndirectTargetDecisionRationale()`.

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
| Debug Focused | `build\Tests\Debug\AxleRenderUnitTests.exe --m15-occlusion-contract` | 0 | PASS (U4-01, U4-02, U4-03.1, U4-03.2) |
| Debug Full | `build\Tests\Debug\AxleRenderUnitTests.exe` | 0 | PASS (All unit suites pass) |
| Release Focused | `build\Tests\Release\AxleRenderUnitTests.exe --m15-occlusion-contract` | 0 | PASS (U4-01, U4-02, U4-03.1, U4-03.2) |
| Release Full | `build\Tests\Release\AxleRenderUnitTests.exe` | 0 | PASS (All unit suites pass) |

### 3. Native GPU Parity & Regression (Release)
| Backend | Command | Exit Code | Result |
| :--- | :--- | :---: | :--- |
| Direct3D 12 | `build\Tests\Release\AxleEnvironmentRenderingTests.exe dx12 build/Tests/EnvironmentRendering/U4-DX12` | 0 | PASS (Parity `diff=(0,0,0)`, HDR, CSM, Pass list preserved) |
| Vulkan 1.3 | `build\Tests\Release\AxleEnvironmentRenderingTests.exe vulkan build/Tests/EnvironmentRendering/U4-Vulkan` | 0 | PASS (Parity `diff=(0,0,0)`, HDR, CSM, Pass list preserved) |
