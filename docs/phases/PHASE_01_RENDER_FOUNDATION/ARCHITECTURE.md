# Phase 01 — Render and material foundation

## Target

One RenderService coordinates immutable snapshots, backend selection, resource
generations, graph passes and frame retirement. Material documents remain
separate from compiler IR and backend pipelines.

## Implementation and algorithms

Use bounded queues, transactional last-known-good generations, reflection-based
layout identities, explicit sampler/color/fallback semantics and graph resource
lifetime tracking. Native D3D12/Vulkan paths use DXC/SPIR-V contracts; OpenGL
graph compilation reports an explicit unsupported policy.

## Technology and limits

NRI, DXC, optional bounded Slang evaluation, EnTT/Scene extraction and existing
RenderGraph are retained. Full asynchronous product rollout remains opt-in or
deferred until measured.
