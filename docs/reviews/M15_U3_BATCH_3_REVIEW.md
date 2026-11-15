# M15 U3 batch 3 — progressive pass extraction

The user released U3-09.1 through U3-10.2. The pass modules live under `Engine/Scene/src/Passes` because they initially consume scene-renderer resources; the existing `Axle::RenderGraph` and RHI remain the lower-level ownership boundary. U3-11 will replace the remaining callback orchestration.

## U3-09.1 — Pass contracts

**PASS (self-verification).** GTAO, Hi-Z, SSR, fog and post headers define typed graph resources. GTAO and Hi-Z also define borrowed recording inputs and operations; no callback or GPU resource may outlive its frame. The two extracting classes have explicit renderer friendship for their current resource access. All five headers are compiled through `RHISceneRendererPasses.cpp` in Debug and Release.

Commands (both exit 0): `cmake --build build --config Debug --target AxleScene --parallel 4`; same Release. Logs: `build/agent-runs/M15-U3-batch-3/09.1-debug-build.log` and `09.1-release-build.log`. This is a compile contract checkpoint; no GPU behavior changed, so no native readback is claimed here.
