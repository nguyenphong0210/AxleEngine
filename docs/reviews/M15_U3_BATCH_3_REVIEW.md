# M15 U3 batch 3 — progressive pass extraction

The user released U3-09.1 through U3-10.2. The pass modules live under `Engine/Scene/src/Passes` because they initially consume scene-renderer resources; the existing `Axle::RenderGraph` and RHI remain the lower-level ownership boundary. U3-11 will replace the remaining callback orchestration.

## U3-09.1 — Pass contracts

**PASS (self-verification).** GTAO, Hi-Z, SSR, fog and post headers define typed graph resources. GTAO and Hi-Z also define borrowed recording inputs and operations; no callback or GPU resource may outlive its frame. The two extracting classes have explicit renderer friendship for their current resource access. All five headers are compiled through `RHISceneRendererPasses.cpp` in Debug and Release.

Commands (both exit 0): `cmake --build build --config Debug --target AxleScene --parallel 4`; same Release. Logs: `build/agent-runs/M15-U3-batch-3/09.1-debug-build.log` and `09.1-release-build.log`. This is a compile contract checkpoint; no GPU behavior changed, so no native readback is claimed here.

## U3-09.2 — Pass graph implementations

**PASS (self-verification).** Five `.cpp` modules now declare the exact GTAO, Hi-Z, SSR, fog and post graph reads/writes; `RHISceneRendererPasses.cpp` calls those modules in the original order. `ScreenSpaceReflectionPass` composes the Hi-Z declaration so its eight levels stay inside the SSR sequence. The renderer still owns recording at this checkpoint; U3-10 extracts that work incrementally.

Build commands (both exit 0): `cmake --build build --config Debug --target AxleEnvironmentRenderingTests --parallel 4`; same Release. Native commands (all exit 0): `build/Tests/{Debug,Release}/AxleEnvironmentRenderingTests.exe {dx12,vulkan} build/agent-runs/M15-U3-batch-3/09.2-{config}-{backend}-artifacts`. Four logs and native artifacts are under `build/agent-runs/M15-U3-batch-3/09.2-*`; each log ends `PASS HDR native subresources, sky/specular/diffuse, cache, exposure, reload, fallback and upload retry`. The strict fixture includes GTAO, SSR/Hi-Z, fog and post feature paths.

## U3-10.1 — GTAO recording extraction

**PASS (self-verification).** `GTAOPass::Record` now owns the GTAO color/depth copy, compute constants/descriptors/dispatch and fullscreen apply. The renderer supplies borrowed graph pass, uniform, descriptor and rendering operations; failures return to its existing diagnostic path. The feature still records `GTAOCopy`, `GTAOCompute`, `GTAOApply` in the same graph order and keeps the same half-resolution target and fallback normal SRV.

Builds: Debug and Release `AxleEnvironmentRenderingTests`, exit 0. Native runs: Debug/Release × D3D12/Vulkan 4/4 exit 0 with the same command pattern as U3-09.2 and prefix `10.1-`. The strict fixture asserts the GTAO pass sequence and a changed output for GTAO/contact variants. Logs and native artifacts: `build/agent-runs/M15-U3-batch-3/10.1-*`. The initial compile failures from missing `RHIMaterial` qualification were corrected before these final builds; no failed run is counted as a pass.

## U3-10.2 — Hi-Z recording extraction

**PASS (self-verification).** `HiZGenerationPass::Record` now owns all eight min-depth levels, with level 0 reading the copied scene depth and each later level reading its predecessor. It updates each level's source/destination dimensions, descriptor sets and dispatch size, then returns to the SSR recorder. The pass declaration is composed from `ScreenSpaceReflectionPass::Declare`, preserving `SSRCopy` before Hi-Z and `SSRCompute` after it.

Builds: Debug and Release `AxleEnvironmentRenderingTests`, exit 0. Native runs: Debug/Release × D3D12/Vulkan 4/4 exit 0 with the same command pattern and prefix `10.2-`. The strict fixture asserts `SSRCopy`, `HiZ0..7`, `SSRCompute`, `SSRApply`, reads back nontrivial per-level Hi-Z data and checks distinct levels. Logs and native artifacts: `build/agent-runs/M15-U3-batch-3/10.2-*`. No Editor visual gate is specified for U3-09.1–10.2; the SSR visual gate begins at U3-10.3.

## Batch closure and boundary

Checkpoint commits: `80a5a45c` (09.1), `4ea79584` (09.2), `5f0e8f2d` (10.1) and `ba16349c` (10.2). On the final source, `python scripts/check_architecture.py` exits 0 (`AXLE_ARCH_CHECK PASS`, 611 source files and 25 CMake files). Debug and Release `AxleEditor` builds both exit 0; logs: `build/agent-runs/M15-U3-batch-3/final-editor-*-build.log`. These are compile checks, not Editor visual captures.

The modules currently live in `Engine/Scene/src/Passes` and expose typed graph declarations. GTAO and Hi-Z own GPU recording, while the existing `ScreenSpace` scheduler adapter still calls them synchronously. SSR, fog and post own only declarations until their U3-10.3–10.5 extraction. The target standalone `IRenderPass` registration and lightweight renderer facade remain U3-11 work; this batch does not claim that final modular architecture.
