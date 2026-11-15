# M15 U3 batch 4 — pass recording and orchestration

The user released U3-10.3 through U3-11.2. Native verification and Editor visual evidence are recorded separately. Each completed source checkpoint receives its own local commit.

## U3-10.3 — Screen-space reflection recording

**PASS.** `ScreenSpaceReflectionPass::Record` owns the scene copy, eight-level Hi-Z generation, SSR compute and fullscreen apply. The existing descriptor and probe fallback policy is retained, including a neutral prefiltered cubemap when no authored probe is active. Graph declaration and recording use the same pass order; recording failure returns through the renderer's diagnostic path.

Builds: `cmake --build build --config {Debug,Release} --target AxleEnvironmentRenderingTests --parallel 4`, both exit 0. Native runs: `build/Tests/{Debug,Release}/AxleEnvironmentRenderingTests.exe {dx12,vulkan} build/agent-runs/M15-U3-batch-4/10.3-{config}-{backend}-artifacts`, 4/4 exit 0. Logs and native artifacts: `build/agent-runs/M15-U3-batch-4/10.3-*`. The strict fixture checks `SSRCopy`, `HiZ0..7`, `SSRCompute`, `SSRApply`, nontrivial level readback, an authored probe response and fallback behavior. User-supplied Editor evidence uses the same camera/material with SSR off/on; the enabled image shows a bright reflected streak on the plane and the disabled image does not, with no black edge.

## U3-10.4 — Fog recording

**Automated PASS; Editor visual PENDING.** `FroxelFogPass::Record` owns the depth/color copy, compute constants/descriptors/dispatch and fullscreen apply. It retains authored volumetric fog parameters when enabled and settings fallback otherwise, including the current directional-light default. Graph pass order and failure propagation are unchanged.

Debug/Release `AxleEnvironmentRenderingTests` builds exit 0. Four native runs on D3D12/Vulkan exit 0, with the same command pattern and `10.4-` prefix. Logs and artifacts: `build/agent-runs/M15-U3-batch-4/10.4-*`. The fixture exercises fog enabled/disabled and authored fog data. A visual confirmation of blocker shafts and the zero-density/no-light result is still required by PLAN.

## U3-10.5 — Post and temporal recording

**PASS (self-verification).** `PostProcessPass::Record` now owns the bloom down/up pyramid and apply, auto-exposure compute, TAA resolve/copy, tone mapping and FXAA recording. Material-generation observation remains in the invoking adapter until the view-state forwarding checkpoint. The graph declarations remain in `PostProcessPass::Declare` and use the original order; failures return through the renderer's diagnostic path.

Debug/Release `AxleEnvironmentRenderingTests` builds exit 0. Native D3D12/Vulkan runs exit 0 in all four combinations with `10.5-` log/artifact prefixes under `build/agent-runs/M15-U3-batch-4/`. The fixture exercises bloom, exposure and TAA ordering and output, including first/second history frames. No Editor visual gate is specified for this leaf.

## U3-11.1 — Scheduler orchestration

**PASS (self-verification).** The previous combined reflection/GI callback is split into ordered `DDGIUpdate`, `GTAOPass`, `ScreenSpaceReflectionPass` and `FroxelFogPass` scheduler nodes. The post node is named `PostProcessPass`. Each node supplies its own graph declaration, preparation and recording callback; a failed callback stops execution and propagates the scheduler diagnostic. The current `CallbackRenderPass` remains the `IRenderPass` adapter for each node. This checkpoint does not claim the later migration of every pass class into Engine/Renderer.

Debug/Release `AxleEnvironmentRenderingTests` builds exit 0. Native D3D12/Vulkan runs exit 0 in all four combinations with `11.1-` log/artifact prefixes under `build/agent-runs/M15-U3-batch-4/`. The fixture checks graph pass order for enabled DDGI/GTAO/SSR/fog/post paths and rendered outputs. The logs also contain intentional negative-path errors from fixture cases; the test process exits 0 and ends with its `PASS` summary.

## U3-11.2 — ViewContext state forwarding

**PASS (self-verification).** `FramePassInputs` no longer duplicates per-view camera, settings, target wrapper, dimensions or jitter. `RecordFramePasses` reads those values from the active `ViewContext` snapshot after final target adoption and rejects calls without an active frame or valid targets with explicit diagnostics. `ViewContext` therefore remains the owner of target generation, reset identity, temporal history selection and post-process policy while the renderer supplies only world/service data and native output handles.

Debug/Release builds of `AxleEnvironmentRenderingTests`, `AxleRenderUnitTests` and `AxleEditor` exit 0. The native D3D12/Vulkan matrix for 11.2 exits 0 in all four combinations; logs and artifacts are under `build/agent-runs/M15-U3-batch-4/11.2-*`. `Test_ViewContextUnit` covers target replacement, material generation, history invalidation and reset reasons. `python scripts/check_architecture.py` reports `AXLE_ARCH_CHECK PASS` (611 files, 25 CMake files).
