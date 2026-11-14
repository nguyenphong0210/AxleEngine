# M14-U4 Follow-up Correction Recheck

Date: 2026-11-14 (Asia/Saigon)

**Verdict: PASS. No remaining M14-U4 blocker found.**

This separate-context recheck was requested after the first U4 acceptance exposed
two lifetime regressions in the Runtime presentation path: presentation source
views were published as opaque descriptor-set resources, and the temporal pass
allocated a new descriptor set every frame without resetting its arena. The
follow-up also typed frame-uniform constant-buffer views so all independent
descriptor aliases carry the same authority metadata.

The source correction now provides:

- `PublishTextureSRV` and `PublishBufferCBV` view aliases with an exact backing
  lease, native descriptor role and `ViewAccess` metadata;
- strict descriptor range/type/count checks that accept only managed descriptor
  sets or typed views with compatible backing resources;
- transactional `RHIDescriptorPool::Init` and a bounded `TemporalDescriptors`
  arena (`MaxDescriptorSets = 16`) re-created before each TAA allocation;
- presentation source publication through the typed SRV path and frame-uniform
  publication through the typed CBV path;
- graph command-scope guards and the existing exact generation/lifetime checks.

Source anchors: `Engine/RHI/src/RHIResource.cpp:383-410`,
`Engine/RHI/src/RHIResource.cpp:486-515`,
`Engine/RHI/src/RHIPresentationContext.cpp:61`,
`Engine/RHI/src/RHIFrameContext.cpp:94`,
`Engine/RHI/src/RHIDescriptorPool.cpp:50`,
`Engine/Scene/src/RHISceneRenderer.cpp:455`, and
`Engine/Scene/src/RHISceneRendererPasses.cpp:1733`.

Fresh reported verification evidence:

- Debug build of `AxleRenderUnitTests`, `AxleRenderGraphTests`,
  `AxleEnvironmentRenderingTests`, `AxleRenderServiceTests` and
  `AxleRuntimeRenderTests`: exit 0.
- Debug RenderGraph authority: D3D12 and Vulkan PASS.
- Debug environment: D3D12 and Vulkan PASS, including 270 TAA frames and the
  injected `CreateDescriptorPool` failure for `TemporalDescriptors`, followed
  by a successful retry.
- Debug RenderService canvas: D3D12 and Vulkan PASS.
- Debug Runtime product: loose and cooked D3D12 fixtures PASS, including startup
  scene load, bounded application loop, UI input, player frames and shutdown.
- Release build of all five targets: exit 0; Release unit, graph D3D12/Vulkan
  and RenderService canvas D3D12/Vulkan suites: exit 0.
- `python scripts/check_architecture.py`: PASS, 600 files and 25 CMake files.
- `python scripts/generate_m14_dependency_map.py --output-dir
  build/Tests/Work/M14-u4-followup/dependencies`: zero architecture and caller
  violations.

The reviewer `/root/u2_correction_review` inspected the current source and the
freshly reported logs in a separate context. It did not execute the GPU commands
itself; the commands above were executed by Codex sequentially on D3D12 and
Vulkan.

**Recommendation: accept M14-U4 and clear the Runtime product gate for U9.**
U5 service ownership/integration and consolidated U9 acceptance remain open.
M15 lighting/material fidelity remains outside this review.
