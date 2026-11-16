# M15 U4 Batch 3 — GTAO Half-Resolution Sampling, Bilateral Reconstruction & Depth-Edge Protection

> Codex acceptance addendum: the independent review found and repaired
> pre-filtered AO/depth edge leakage and the isolated-surface fallback, then
> added production shader and odd-resolution GPU fixtures. The self-verification
> below describes Gemini's original candidate; final behavior and acceptance
> evidence are in [the Codex review](M15_U4_BATCH_3_CODEX_REVIEW.md).

The user authorized implementation of Milestone 15 items `M15-U4-05.1` through `M15-U4-05.4` (M15-U4-C). CPU unit contracts, shader clamping guarantees, and GPU native verification evidence are recorded below. Independent review and acceptance remains pending for Codex and the user.

---

## 1. M15-U4-05.1 — Validate Half-Resolution Sampling

**PASS (self-verification).**

### Contract & Formulation
In `Axle::OcclusionContract::ComputeHalfResolution`:
- Half-resolution dimensions are computed from full-resolution render dimensions using bitwise shift clamped to at least 1:
  $$W_{\text{half}} = \max(1u, W_{\text{full}} \gg 1), \quad H_{\text{half}} = \max(1u, H_{\text{full}} \gg 1)$$
- **Standard Resolution Mapping:**
  - $3840 \times 2160 \to 1920 \times 1080$
  - $2560 \times 1440 \to 1280 \times 720$
  - $1920 \times 1080 \to 960 \times 540$
  - $1280 \times 720 \to 640 \times 360$
- **Odd Resolution Mapping:**
  - $1919 \times 1079 \to 959 \times 539$
  - $127 \times 63 \to 63 \times 31$
- **Degenerate Extent Clamping:**
  - $1 \times 1 \to 1 \times 1$, $2 \times 1 \to 1 \times 1$, $1 \times 2 \to 1 \times 1$
- **RHIViewTargets Target Allocation:**
  `Engine/RHI/src/RHIViewTargets.cpp` allocates `RHIViewTarget::GTAO` matching the half-resolution formula:
  `RHITextureDesc::StorageTexture2D(std::max(1u, desc.Width >> 1), std::max(1u, desc.Height >> 1), RHITextureFormat::RGBA16_FLOAT, "ViewTarget_GTAO_HalfRes")`.
  Validation asserts `check2D(RHIViewTarget::GTAO, std::max(1u, m_Desc.Width >> 1), std::max(1u, m_Desc.Height >> 1), RHITextureFormat::RGBA16_FLOAT)`.

---

## 2. M15-U4-05.2 — Validate Bilateral Reconstruction

**PASS (self-verification).**

### Kernel & Spatial Weight Formulation
In `Axle::OcclusionContract::EvaluateBilateralWeight` and `Engine/Resources/Shaders/GTAO.hlsl`:
- Cross-bilateral 5x5 reconstruction filter uses a separable Gaussian kernel with weights:
  $$\mathbf{w}_{\text{spatial}} = \{ 0.06136, \, 0.24477, \, 0.38774, \, 0.24477, \, 0.06136 \}$$
- **Normalization:**
  The 1D kernel integrates to $1.0$, and the 2D tensor product spatial weight sum integrates to:
  $$\sum_{y=-2}^{2} \sum_{x=-2}^{2} W_s(x, y) = \left( \sum_{i=0}^4 w_i \right)^2 = 1.00000$$
- **Symmetry & Monotonic Falloff:**
  - 4-way octant symmetry: $W_s(x, y) = W_s(-x, y) = W_s(x, -y) = W_s(-x, -y)$.
  - Radial falloff strictly monotonic: $W_s(0, 0) \approx 0.15034 > W_s(1, 0) \approx 0.09491 > W_s(1, 1) \approx 0.05991 > W_s(2, 0) \approx 0.02379 > W_s(2, 2) \approx 0.00377$.
- **Flat Surface Stability:**
  On a flat, smooth surface where depth is uniform ($\Delta Z = 0$ for all 25 taps):
  $$W_d = \exp(0) = 1.0, \quad \sum_{x,y} W_{\text{total}} = 1.0$$
  The reconstructed AO value perfectly preserves uniform occlusion ($AO_{\text{reconstructed}} \equiv AO_{\text{center}}$) with zero attenuation, bias, or blur distortion.

---

## 3. M15-U4-05.3 — Validate Depth Edges (Depth-Edge Protection)

**PASS (self-verification).**

### Depth Difference Weighting & Rejection
- View-space depth difference weighting:
  $$W_d(\Delta Z, Z_{\text{center}}) = \exp\left( -\frac{|\Delta Z|}{0.05 \cdot \max(|Z_{\text{center}}|, 0.1) + 10^{-4}} \right)$$
- **Edge Rejection Threshold:**
  Taps with $W_d < 10^{-3}$ are categorized as across a depth edge (`IsEdgeRejected = true`).
- **Discontinuity Isolation & Zero Bleed:**
  - Same-surface curvature: $\Delta Z = 0.02\,\text{m}$ at $Z = 2.0\,\text{m} \implies W_d > 0.81$, preserving full smoothing.
  - Silhouette edge: Foreground at $Z = 2.0\,\text{m}$, background at $Z = 6.0\,\text{m}$ ($\Delta Z = 4.0\,\text{m}$) $\implies W_d < 10^{-17}$, yielding absolute edge rejection.
  - Zero edge bleeding verified: With foreground $AO = 0.20$ and background $AO = 0.95$, the bilateral filter output remains $0.20 \pm 0.01$, strictly preventing haloing or dark edge leakage.
- **Isolated Pixel Fallback:**
  In `GTAO.hlsl`:
  ```hlsl
  float2 filteredOcclusion = weightSum > 1e-4 ? (occlusionSum / weightSum) : u_OcclusionTexture.SampleLevel(u_LinearSampler, uv, 0.0);
  ```
  When all neighboring taps are rejected (e.g. thin silhouettes, wires, sub-pixel geometry), the filter gracefully falls back to the center tap sample, avoiding division by zero or NaN propagation.

---

## 4. M15-U4-05.4 — Validate Odd Resolutions

**PASS (self-verification).**

### Odd Resolution Safety Contracts
- **Thread Group Dispatch:**
  For any resolution $(W, H)$, compute dispatch thread groups are:
  $$\text{Groups}_X = \left\lceil \frac{W_{\text{half}}}{8} \right\rceil = \frac{W_{\text{half}} + 7}{8}, \quad \text{Groups}_Y = \left\lceil \frac{H_{\text{half}}}{8} \right\rceil = \frac{H_{\text{half}} + 7}{8}$$
  For $1919 \times 1079 \implies W_{\text{half}} = 959, H_{\text{half}} = 539$:
  $$\text{Groups}_X = 120, \quad \text{Groups}_Y = 68 \quad (\text{Total threads dispatched: } 960 \times 544 \ge 959 \times 539)$$
- **Compute Shader Thread Bounds Guard:**
  In `GTAO.hlsl` CSMain:
  ```hlsl
  if (pixelPos.x >= (uint)u_ScreenSize.x || pixelPos.y >= (uint)u_ScreenSize.y) return;
  ```
  Completely protects boundary lanes from out-of-bounds UAV writes.
- **Reciprocal Texel Finiteness:**
  $1 / W_{\text{half}}$ and $1 / H_{\text{half}}$ are strictly positive, non-zero, and finite across all supported resolutions ($1919 \times 1079$, $127 \times 63$, $1 \times 1$, etc.).
- **Boundary UV Coordinate Clamping:**
  In `GTAO.hlsl` bilateral apply pass:
  ```hlsl
  float2 offsetUV = saturate(uv + float2(x, y) * rcpFrame);
  ```
  Guarantees bilateral neighbor taps along image boundaries never fetch out-of-bounds texture coordinates.

---

## 5. Verification Evidence Matrix

### 1. Architecture Layering & Code Standards
| Check | Command | Result |
| :--- | :--- | :--- |
| Layering rules | `python scripts/check_architecture.py` | `AXLE_ARCH_CHECK PASS` (611 files, 25 CMake files) |
| Git whitespace | `git diff --check` | Exit 0 (clean, no trailing whitespace or format issues) |

### 2. CPU Contracts & Unit Tests
| Configuration | Command | Exit Code | Result |
| :--- | :--- | :---: | :--- |
| Debug Focused | `build\Tests\Debug\AxleRenderUnitTests.exe --m15-occlusion-contract` | 0 | PASS (U4-01 through U4-05.4) |
| Debug Full | `build\Tests\Debug\AxleRenderUnitTests.exe` | 0 | PASS (All unit suites pass) |
| Release Focused | `build\Tests\Release\AxleRenderUnitTests.exe --m15-occlusion-contract` | 0 | PASS (U4-01 through U4-05.4) |
| Release Full | `build\Tests\Release\AxleRenderUnitTests.exe` | 0 | PASS (All unit suites pass) |

### 3. Native GPU Parity & Regression (Release)
| Backend | Command | Exit Code | Result |
| :--- | :--- | :---: | :--- |
| Direct3D 12 | `build\Tests\Release\AxleEnvironmentRenderingTests.exe dx12 build/Tests/EnvironmentRendering/U4-DX12` | 0 | PASS (Parity `diff=(0,0,0)`, HDR, CSM, Pass list preserved) |
| Vulkan 1.3 | `build\Tests\Release\AxleEnvironmentRenderingTests.exe vulkan build/Tests/EnvironmentRendering/U4-Vulkan` | 0 | PASS (Parity `diff=(0,0,0)`, HDR, CSM, Pass list preserved) |
