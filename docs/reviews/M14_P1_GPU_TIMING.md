# M14 P1 — GPU timing and submission-path correction

**Status:** Implemented and verified for the timing checkpoint; broader P1 resource/lifetime work and M14 acceptance remain open.
**Base:** `05ff746` (P0 baseline).
**Commit:** recorded with this checkpoint.

## Corrections

- `RHISceneRenderer` now consumes the previous completion/query readback from every public scene/view rendering path before resetting the completion token. Direct `RenderWorldView` calls no longer leave the timestamp getter at its default zero.
- The consumption helper clears the measurement only when a previous completion exists, so nested overloads do not erase a value already read by an outer wrapper.
- The timestamp probe no longer requests an invalid 4096-byte structured stride. The RHI limit is 2048 bytes; the probe uses a valid 16-byte stride while retaining a nontrivial GPU zeroing workload.
- The environment fixture now requires the probe submission and a positive elapsed tick count, and requires the renderer-integrated scope to be positive after a submitted view. A frequency-only PASS is no longer sufficient.

## Verification

Command:

```text
cmake --build build --config Debug --target AxleEnvironmentRenderingTests --parallel 6
build/Tests/Debug/AxleEnvironmentRenderingTests.exe dx12 build/m14_p1_env_dx12
build/Tests/Debug/AxleEnvironmentRenderingTests.exe vulkan build/m14_p1_env_vulkan
```

Results from this session:

| Backend | Exit | Probe submission | Probe GPU ms | Renderer scope ms |
|---|---:|---:|---:|---:|
| D3D12 — NVIDIA GeForce RTX 3070 | 0 | 1 | 0.098304 | 0.017408 |
| Vulkan 1.3 — NVIDIA GeForce RTX 3070 | 0 | 1 | 0.012512 | 0.111616 |

Release rebuild and rerun also passed:

| Backend | Exit | Probe GPU ms | Renderer scope ms |
|---|---:|---:|---:|
| D3D12 — NVIDIA GeForce RTX 3070 | 0 | 0.098304 | 0.016384 |
| Vulkan 1.3 — NVIDIA GeForce RTX 3070 | 0 | 0.012288 | 0.108544 |

The Release link emitted MSVC warning `LNK4098` about conflicting CRT default libraries; it did not change the exit code or the native fixture result and remains a separate build-hygiene issue.

Both runs completed the existing native environment fixture and emitted the existing final PASS line. The recorded logs include no failure from the new timestamp assertions.

## Boundary and remaining work

This closes the invalid-probe and missing-direct-readback findings. It does not close Hi-Z binding, normal production, SSR quality, exposure, bloom, DDGI, or the complete Milestone 18 benchmark matrix. The timestamp values above are valid scope measurements for this fixture; they are not yet the final 1080p/1440p lighting-budget scorecard.
