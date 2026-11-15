# M15 U3 batch 3 — progressive pass extraction

The user released U3-09.1 through U3-10.2. The pass modules live under `Engine/Scene/src/Passes` because they initially consume scene-renderer resources; the existing `Axle::RenderGraph` and RHI remain the lower-level ownership boundary. U3-11 will replace the remaining callback orchestration.

## U3-09.1 — Pass contracts

**PASS (self-verification).** GTAO, Hi-Z, SSR, fog and post headers define typed graph resources. GTAO and Hi-Z also define borrowed recording inputs and operations; no callback or GPU resource may outlive its frame. The two extracting classes have explicit renderer friendship for their current resource access. All five headers are compiled through `RHISceneRendererPasses.cpp` in Debug and Release.

Commands (both exit 0): `cmake --build build --config Debug --target AxleScene --parallel 4`; same Release. Logs: `build/agent-runs/M15-U3-batch-3/09.1-debug-build.log` and `09.1-release-build.log`. This is a compile contract checkpoint; no GPU behavior changed, so no native readback is claimed here.

## U3-09.2 — Pass graph implementations

**PASS (self-verification).** Five `.cpp` modules now declare the exact GTAO, Hi-Z, SSR, fog and post graph reads/writes; `RHISceneRendererPasses.cpp` calls those modules in the original order. `ScreenSpaceReflectionPass` composes the Hi-Z declaration so its eight levels stay inside the SSR sequence. The renderer still owns recording at this checkpoint; U3-10 extracts that work incrementally.

Build commands (both exit 0): `cmake --build build --config Debug --target AxleEnvironmentRenderingTests --parallel 4`; same Release. Native commands (all exit 0): `build/Tests/{Debug,Release}/AxleEnvironmentRenderingTests.exe {dx12,vulkan} build/agent-runs/M15-U3-batch-3/09.2-{config}-{backend}-artifacts`. Four logs and native artifacts are under `build/agent-runs/M15-U3-batch-3/09.2-*`; each log ends `PASS HDR native subresources, sky/specular/diffuse, cache, exposure, reload, fallback and upload retry`. The strict fixture includes GTAO, SSR/Hi-Z, fog and post feature paths.
