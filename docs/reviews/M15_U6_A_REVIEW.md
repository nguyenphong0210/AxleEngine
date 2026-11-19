# M15 U6-A independent review

The review covers the U6-A delivery at `eb11bca8` against baseline `cea412b8`, with repairs in `ecf6303c30616e43a653d54572bb4eeb85109b2b`. The 2026-11-19 user request names M16 U6-A. The active task card, plan and implementation identify this delivery as M15 U6-A.

## Findings and repairs

| ID | Severity | Finding | Repair |
| --- | --- | --- | --- |
| U6A-R1 | High | An invalid authored quality selected Ultra. Froxel allocations also proceeded without depth or with an invalid depth range. Service-owned and viewport-owned targets could be built from authored settings while the scene renderer compared against backend-resolved settings. | Resolve all three target paths from backend settings. Invalid quality selects High. Froxel volumes require a valid depth interval and a single-sample depth target. Scene diagnostics identify the fallback and the low-level target builder rejects incompatible resource requests. |
| U6A-R2 | Medium | A storage texture described as 3D with one depth slice became a native 2D texture. Tier allocation and public tier metadata were also maintained in separate tables. | Carry 3D identity through native views, imports, transient compatibility and 2D material binding validation. Use one RHI tier table for allocation, capacity and scorecard labels. Vulkan barriers cover the relevant volume mip and all of its depth slices. |
| U6A-R3 | Medium | Consecutive representable near and far values passed validation even though multiple float slice boundaries collapsed to the same depth. The former native fixture did not establish a successful retry after late OOM or a froxel-specific device epoch restart. | Require enough float precision for distinct slice centers and boundaries at Ultra. Verify late-allocation failure, a successful resized retry, retained history, and validity across device restart. |

A separate-context source reviewer checked the corrections and affected callers. The follow-up added effective texture dimensionality to transient import validation, alias compatibility, and 2D material and view target checks. The reviewer also confirmed the depth interval precision bound after the repair.

The legacy fog component still supplies medium parameters through scene extraction and its runtime codec. U6-A adds view-owned allocations and the slice contract. Media injection and shader consumption of the 3D volumes belong to U6-B.

## Requirements and independent evidence

| Requirement | Implementation and callers | Independently verified evidence | Status |
| --- | --- | --- | --- |
| M15-U6-01.1 allocation and depth contract | `RHITexture`, `RHIViewTargets`, `FroxelFogContract`, and `RHISceneRenderer` | The CPU fixture checks exponential mapping, inverse mapping, near/far validation and float precision. Native tests allocate all four RGBA16F SRV/UAV volumes. The compute fixture dispatches a 3D UAV writer, checks every depth slice by readback and checks interpolation by sampling the Texture3D SRV. | PASS |
| M15-U6-01.1 lifetime and failure policy | `RHIViewTargets::Configure`, `RHIResourceService`, and native resource view tests | Illegal options and an injected OOM on the last history allocation preserve the previous generation. Retrying publishes the resized generation. An independent device restart invalidates old history and creates current-epoch volumes. | PASS |
| M15-U6-01.1 production view paths | `RHISceneRenderer::RenderWorldView`, `RenderService::RenderView` for Runtime-style main views, and `EditorLayer` via `RHIViewportDescriptor` | The native scene fixture submits with external view targets. The service fixture submits with owned froxels, reuses the generation, suppresses allocation for an invalid depth range, then restores allocation. The Vulkan viewport fixture switches tiers, retires froxels for MSAA and restores them when returning to a single sample. | PASS |
| M15-U6-01.2 scale quality tiers | `RHIViewTargets::ComputeFroxelGridExtent`, `FroxelFogContract`, and the Editor RenderFX selector | CPU dimension and monotonicity checks span tiny, odd-sized, reference and 4K views. Native allocation exercises all four tiers. High is exactly 160 × 90 × 64 at 720p, 1080p and 1440p. | PASS |
| M15-U6-07.5 initial memory | `FroxelFogContract::EstimateMemoryBudget` and `RHITexture::QueryMemoryRequirements` | CPU checks exact logical bytes. Native scorecards record four-volume device requirements and allocation alignment for every tier at 720p, 1080p and 1440p. | PASS for U6-A |
| D3D12 windowed viewport and Editor startup | `RHIDevice` and NRI swap-chain capability probe | NRI reports that the required `SwapChainInterface` is unavailable before either windowed fixture reaches froxel code. | NOT RUN on this host |

## Initial 1080p memory

Each row measures one four-volume generation on the RTX 3070. The native figures are device allocation requirements.

| Tier | Grid | Logical bytes | D3D12 bytes | Vulkan bytes |
| --- | --- | ---: | ---: | ---: |
| Low | 80 × 45 × 32 | 3,686,400 | 3,932,160 | 3,932,160 |
| Medium | 128 × 72 × 48 | 14,155,776 | 18,874,368 | 14,155,776 |
| High | 160 × 90 × 64 | 29,491,200 | 31,457,280 | 31,457,280 |
| Ultra | 240 × 135 × 96 | 99,532,800 | 117,964,800 | 100,270,080 |

Full scorecards are in `build/agent-runs/M15-U6-A/codex/<Config>/<backend>/M15_U6A_froxel_memory.txt`. Final integrated memory reconciliation belongs to U6-E.

## Build and execution record

The review rebuilt the repair sources in Debug and Release with this CMake command.

```powershell
cmake --build build --config <Config> --target AxleRenderUnitTests AxleRHIResourceViewTests AxleRHIStateTests AxleRenderServiceTests AxleEnvironmentRenderingTests AxleEditor AxleRuntime -- /m:<N> /v:minimal
```

Both builds exited 0 with `N=5` for Debug and `N=4` for Release. A final incremental rerun for `AxleRenderUnitTests` also exited 0 in each configuration. Logs are `build/agent-runs/M15-U6-A/codex-build-{debug,release}.log` and `codex-build-{debug,release}-unit-final.log` in the same directory. The Runtime link reported warning LNK4098 in both configurations.

The commands below ran from the repository root in both configurations. Each test executable is under `build/Tests/<Config>`, and `AxleEditor.exe` is under `build/Editor/<Config>`. Each exact command, working directory and exit code is in `build/agent-runs/M15-U6-A/codex/<Config>/*.log`. Test processes using native RHI validation reported zero validation errors on successful executions.

| Command | Debug | Release |
| --- | --- | --- |
| `AxleRenderUnitTests.exe` | 0 | 0 |
| `ctest --test-dir build -C <Config> --output-on-failure -R "^(M15FroxelFogContract\|M15HiZContract\|M15OcclusionContract\|M15ReflectionCaptureContract)$"` | 0, 4/4 | 0, 4/4 |
| `AxleRHIStateTests.exe policy` | 0 | 0 |
| `AxleRHIStateTests.exe <dx12 or vulkan>` | 0, both | 0, both |
| `AxleRHIResourceViewTests.exe <dx12 or vulkan> storage` | 0, both | 0, both |
| `AxleRenderServiceTests.exe <dx12 or vulkan>` | 0, both | 0, both |
| `AxleEnvironmentRenderingTests.exe <dx12 or vulkan> build/agent-runs/M15-U6-A/codex/<Config>/<backend>` | 0, both | 0, both |
| `AxleRHIResourceViewTests.exe vulkan targets` | 0 | 0 |
| `AxleEditor.exe --verify-empty-startup --rhi=vulkan --no-fallback --rhi-validation` | 0, five UI frames | 0, five UI frames |
| `AxleRHIResourceViewTests.exe dx12 targets` | 1, blocked at swap-chain capability | 1, blocked at swap-chain capability |
| `AxleEditor.exe --verify-empty-startup --rhi=dx12 --no-fallback --rhi-validation` | 1, blocked at swap-chain capability | 1, blocked at swap-chain capability |

The Debug and Release memory scorecards match byte for byte within each backend. `python scripts/check_architecture.py --root .`, `python scripts/agent_exchange.py validate`, and `git diff --check` exited 0.

The automated U6-A allocation, tier and initial memory requirements pass. D3D12 windowed verification remains open because the required NRI swap-chain interface was unavailable at startup.
