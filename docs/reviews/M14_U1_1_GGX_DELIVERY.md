# M14-U1.1 — Multi-Bounce GGX Energy Compensation

## 1. Scope

This checkpoint adds Kulla-Conty/Filament-style multi-bounce GGX energy compensation to the built-in modern ForwardPBR shader. It preserves the existing DFG LUT, descriptor layout, IBL cache, CSM/local-shadow ownership, Material Graph path, and OpenGL compatibility renderer.

No dynamic sky, GTAO, SSR, TAA, DDGI, or new material schema is included.

## 2. Algorithm

The existing DFG lookup returns the split-sum terms `A` and `B`. Their sum is used as the directional single-scatter energy:

```text
r = max(A + B, 0.05)
energyCompensation = min(1 + F0 * (1 / r - 1), 8)
```

The compensation scales the single-scatter specular lobe for the sun, point, spot, IBL, and analytical ambient paths. The finite floor and upper bound keep malformed/grazing samples bounded; the existing HDR clamp remains the final safety boundary.

## 3. Changed Files

- `Engine/Resources/Shaders/ForwardPBR.hlsl`
- `Tests/src/Test_EnvironmentRendering.cpp`

## 4. Verification Roadmap

- [x] Baseline native D3D12 fixture captured before the shader change.
- [x] ForwardPBR DXIL compilation and native D3D12 execution.
- [x] ForwardPBR SPIR-V compilation and native Vulkan execution.
- [x] Rough-metal fixture shows increased retained energy.
- [x] OpenGL HDR/IBL policy regression remains passing; OpenGL uses its separate compatibility shader.
- [x] Rebuild the modified C++ test target and rerun the new explicit `rough >= 180` assertion.
- [x] Final Debug/Release native test target builds and focused environment fixture runs.

## 5. Evidence

| Run | Result |
|---|---|
| Baseline D3D12 `build/Tests/Debug/AxleEnvironmentRenderingTests.exe dx12 build/agent-runs/m14-u1-baseline/dx12` | PASS; sharp 255, rough 150 |
| Final D3D12 `build/Tests/Debug/AxleEnvironmentRenderingTests.exe dx12 build/agent-runs/m14-u1-ggx/dx12` | PASS; sharp 255, rough 197 |
| Final Vulkan `build/Tests/Debug/AxleEnvironmentRenderingTests.exe vulkan build/agent-runs/m14-u1-ggx/vulkan` | PASS; sharp 255, rough 197; no reported validation failure |
| OpenGL `build/Tests/Debug/AxleEnvironmentRenderingTests.exe gl build/agent-runs/m14-u1-ggx/gl` | PASS; background-only and explicit IBL gates |
| Debug MSBuild target `AxleEnvironmentRenderingTests.vcxproj` | PASS, exit 0; Visual Studio MSBuild with installed `cl.exe`/`link.exe` |
| Release MSBuild target `AxleEnvironmentRenderingTests.vcxproj` | PASS, exit 0; Visual Studio MSBuild with installed `cl.exe`/`link.exe` |
| Final Debug D3D12/Vulkan/OpenGL fixture | PASS; sharp 255, rough 197 and `rough >= 180` assertion |
| Final Release D3D12/Vulkan/OpenGL fixture | PASS; sharp 255, rough 197 and `rough >= 180` assertion |

The final executables were rebuilt after the shader and test-source changes. They load the modified production shader and execute the new C++ rough-metal assertion in both Debug and Release.

## 6. Acceptance Boundary

The shader behavior is verified on the physical RTX 3070 for D3D12 and Vulkan and does not change the OpenGL path. **Acceptance: PASS for M14-U1.1.** Broader M14 performance-budget acceptance remains owned by U9/Milestone 18.
