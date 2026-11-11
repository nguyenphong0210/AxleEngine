# OPT-12 C12-06 OpenGL Material Graph Policy

Date: **2026-11-11** (Asia/Saigon). Base: `094fb8c`.
Status: **Complete.**

## Scope

C12-06 closes the remaining OPT-12 package by making the OpenGL boundary explicit.
The existing legacy OpenGL compatibility renderer has no graph GLSL lowering or
graph resource ABI. Material graph code therefore does not infer that generated
HLSL, DXIL or SPIR-V can run on OpenGL.

`MaterialGraphTarget::Backend` now records the requested RHI policy. `None` keeps
device-independent CPU compilation; Direct3D12 and Vulkan retain their existing
native paths. OpenGL fails before lowering or compiler work with the stable
`MG_OPENGL_UNSUPPORTED` diagnostic. Public `MaterialGraphProgram::Compile` accepts
the same target policy, so a caller cannot accidentally compile a graph and label
it as an OpenGL target. The failure leaves the output object unchanged, and a
runtime with an OpenGL target publishes no ready generation.

The pipeline identity includes the requested backend policy. This prevents a future
backend implementation from colliding with an existing CPU/DX12/Vulkan pipeline
cache entry. No GLSL compiler, OpenGL graph renderer, UI, or later OPT was activated.

## Verification

- `MaterialGraphOpenGLPolicyTests`: explicit compiler rollback, CPU success,
  OpenGL diagnostic and empty runtime generation.
- Enabled Debug focused Material Graph/MaterialUnit matrix: **16/16 PASS**, including
  C12-05 Slang selection/policy and physical D3D12/Vulkan tests.
- Enabled Debug full CTest: **106/106 PASS**, including Editor/Runtime and all
  existing native/architecture/plugin suites (`build/agent-runs/c12_06_debug_ctest_full.log`).
- Disabled Debug focused matrix: **13/13 PASS**, including the OpenGL policy and
  existing CPU/D3D12/Vulkan tests without the Slang SDK build.
- Enabled Release focused matrix: **16/16 PASS**, with Slang, native and OpenGL
  policy tests rebuilt in Release.
- Final separate-context review `/root/c12_06_review`: no blocking findings. The
  reviewer requested a pipeline-key schema bump; `MaterialGraphPipeline:2` is now
  used and was rebuilt/retested.
- `python scripts/check_architecture.py`: **560 sources / 25 CMake PASS**;
  `git diff --check`: PASS. No Asset-to-RHI dependency was introduced.

## Limits

OpenGL graph materials remain unsupported until a separately designed GLSL lowering,
reflection/layout contract, descriptor mapping and native image test are released.
The existing OpenGL compatibility renderer and unrelated legacy OpenGL tests are
not reinterpreted as graph support.
