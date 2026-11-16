# M15 U3 batch 7 — independent review and correction

## Scope

Reviewed Gemini delivery `67e7f63b` covering M15-U3-24.1 through
M15-U3-26.4, including the emission evaluation-policy change and the new
temporal/diagnostics contract assertions.

## Finding and correction

- **Test correctness:** the diagnostics test retained a reference to
  `RenderDiagnostics::GetFrame().Passes`, then appended another pass. A
  `std::vector` reallocation could invalidate that reference and make the test
  undefined even though it passed on the current allocator. The test now reads
  the current frame vector after each append.
- **Evidence strength:** the CPU contract only proves descriptor policy. The
  native environment fixture now configures the target generation and checks
  the actual depth, auxiliary depth, screen-storage and exposure resources,
  including their SRV/UAV views.

## Verification

- `cmake --build build --config Debug --target AxleRenderUnitTests AxleEnvironmentRenderingTests -j 6` — PASS.
- `build/Tests/Debug/AxleRenderUnitTests.exe --m15-surface-material-contract` — PASS.
- `build/Tests/Debug/AxleEnvironmentRenderingTests.exe dx12 build/Tests/EnvironmentRendering/CodexU3Batch7-DX12` — PASS; SDR/HDR parity diffs `(0,0,0)`.
- `build/Tests/Debug/AxleEnvironmentRenderingTests.exe vulkan build/Tests/EnvironmentRendering/CodexU3Batch7-Vulkan` — PASS; active Vulkan 1.3, SDR/HDR parity diffs `(0,0,0)`.
- `python scripts/check_architecture.py` — PASS.

The native logs contain expected injected failure-path messages for lighting
and post-process allocation; the test exits zero and reports the final PASS
after exercising retry/fallback behavior.

## Acceptance status

M15-U3-24.1 through U3-26.4 are code and automated-test complete after the
correction. Native Editor visual gates remain separate user evidence:
M15-U3-10.4, M15-U3-20 and M15-U3-24.4.
