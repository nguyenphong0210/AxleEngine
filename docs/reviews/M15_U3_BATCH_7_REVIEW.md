# M15 U3 batch 7 — Temporal post graph, reset matrix, and diagnostics contracts

The user requested completion of the remaining items of Milestone 15 SubUnit 3 (`M15-U3-24.1` through `M15-U3-26.4`), concluding the full engineering scope of M15-U3. CPU unit contracts, regression fixes, and native GPU verification evidence are recorded below. Independent review and acceptance remains pending for Codex and the user.

## Scope & Implementation Details

### 1. ShaderSurface emission regression fix
- In `Engine/Renderer/src/ShaderSurface.cpp`, resolved an emission regression introduced in commit `59ca9911`:
  - `Unsupported()` now accepts `SurfaceEvaluation evaluation`.
  - For `SurfaceEvaluation::MaterialGraph`, authored emission is supported directly by `MaterialSurface` and is not marked unsupported.
  - For `SurfaceEvaluation::CustomShader`, authored emission remains unrepresented by the canonical shader interface and is correctly marked unsupported (`SurfaceFeature::Emission`).
  - Restores full passing status for `Test_ResolvedSurfaceUnit.cpp` while maintaining `Test_M15_SurfaceAndMaterialContractUnit.cpp` assertions.

### 2. S3.4 — Temporal, diagnostics, and measurement integration

#### M15-U3-24.1..24.4 — Resource Declarations
- **24.1 (Motion resources):** Verified `RHIViewTargetDesc::EnableScreenStorage = true` declares screen storage for motion/properties; `TemporalService::Begin` / `Commit` explicitly enforces uninitialized motion bypass on frame 0 and valid motion tracking on subsequent frames; `TemporalService::InvalidateContents()` invalidates motion resources.
- **24.2 (Depth resources):** Verified `RHIViewTargetDesc::EnableDepth = true` with `DepthFormat = RHITextureFormat::D32_FLOAT` declares 32-bit floating point depth; `RenderAuxiliary = true` declares auxiliary depth resources (`OpaqueDepth`, `ParticleDepth`).
- **24.3 (History resources):** Verified `RHIViewTargetDesc::HistoryCount = 2` declares dual-slot ping-pong history resources; `TemporalService` tracks read and write slots (`ReadSlot=0, WriteSlot=1` initially with invalid history; flipping to `ReadSlot=1, WriteSlot=0` with valid history on commit).
- **24.4 (Exposure resources):** Verified `RHIViewTargetDesc::EnableAutoExposure = true` declares auto-exposure resource; `PostProcessService::BeginFrame` activates `PostProcessPolicy::AutoExposure`; `PostProcessService::UpdateExposure` clamps adapted exposure strictly to `[AutoExposureMin, AutoExposureMax]`.
  - *Note:* Editor visual verification remains a user-provided visual gate.

#### M15-U3-25.1..25.4 — Reset Matrix Contracts
- **25.1 (Target generation reset):** `ViewContext::ObserveTargets` detects target replacement, triggers `ViewResetReason::TargetGeneration`, resets temporal history validity, and restores initial history slot identities (`ReadSlot=0, WriteSlot=1`).
- **25.2 (Camera cut reset):** `ViewContextInput::CameraCut = true` triggers `ViewResetReason::CameraCut`; `TemporalService::Reset(ViewResetReason::CameraCut)` invalidates temporal history and disocclusion feedback, resetting committed frame count to zero.
- **25.3 (Feature policy reset):** `ViewContextInput::FeatureGeneration` increment triggers `ViewResetReason::FeaturePolicy`.
- **25.4 (Device epoch reset):** Device epoch increment (e.g., from 1 to 2) triggers `ViewResetReason::DeviceEpoch`, and `ViewContext` accurately records the updated device epoch.

#### M15-U3-26.1..26.4 — Diagnostics & Measurement Contracts
- **26.1 (Timing validity):** `RenderDiagnostics` records per-pass metrics; unqueried passes report `GPUValidity == DiagnosticValidity::Unknown`, while completed GPU queries report `DiagnosticValidity::Valid` with valid execution times.
- **26.2 (Fallback reasons):** Passes falling back record `DiagnosticStatus::Fallback` and explicit reason strings (e.g., `"ssr_depth_hierarchy_incomplete"`); frame diagnostics record structured diagnostics such as `"SURFACE_UNSUPPORTED_FEATURES: ... (mask=0x...)"`.
- **26.3 (Resource generations):** `RenderDiagnostics` records and exposes `TargetGeneration` and `MaterialGeneration` across frame queries.
- **26.4 (Unknown invalid measurements):** 0.0 ms unread GPU queries with `DiagnosticValidity::Unknown` report `IsGPUTimeValid() == false`, preventing false valid-zero timings.

---

## Verification Evidence Matrix

1. **Architecture & Standards:**
   - `python scripts/check_architecture.py`: `AXLE_ARCH_CHECK PASS` (611 files scanned, 25 CMake files).
   - `git diff --check`: Exit 0 (clean, no trailing whitespace or format violations).
2. **CPU Contracts & Unit Tests:**
   - Debug: `build/Tests/Debug/AxleRenderUnitTests.exe --m15-surface-material-contract` -> PASS (exit 0).
   - Debug Full Suite: `build/Tests/Debug/AxleRenderUnitTests.exe` -> PASS (exit 0, all tests passed).
   - Release: `build/Tests/Release/AxleRenderUnitTests.exe --m15-surface-material-contract` -> PASS (exit 0).
   - Release Full Suite: `build/Tests/Release/AxleRenderUnitTests.exe` -> PASS (exit 0, all tests passed).
3. **GPU Native Rendering (D3D12 & Vulkan Parity):**
   - Release D3D12: `build/Tests/Release/AxleEnvironmentRenderingTests.exe dx12 build/Tests/EnvironmentRendering/U3Final-DX12` -> PASS (exit 0; `diff=(0,0,0)` on both SDR and HDR).
   - Release Vulkan: `build/Tests/Release/AxleEnvironmentRenderingTests.exe vulkan build/Tests/EnvironmentRendering/U3Final-Vulkan` -> PASS (exit 0; `diff=(0,0,0)` on both SDR and HDR).

---

## Status & Remaining Gate

- Milestone 15 SubUnit 3 (`M15-U3-01` through `M15-U3-26.4`) implementation is complete and verified.
- The Editor visual verification gates (`M15-U3-10.4`, `M15-U3-20`, and `M15-U3-24.4`) remain user-provided visual captures.
