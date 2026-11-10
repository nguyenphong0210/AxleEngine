# OPT-11 Execution Plan: Render Thread, Parallel Recording and Transient VRAM Aliasing

**Status:** Technical reference and original planning contract. This document
does not release implementation.
Implementation checkpoint (2026-11-10): the user subsequently released C11-01..07
in bounded assignments. The [C11-07 decision](../reviews/OPT_11_C11_07_ACCEPTANCE.md)
records qualified foundation/synchronous-product acceptance and deferred
asynchronous/native-parallel/scene-alias product integration. This reference does
not itself release later work or claim full OPT-11 deployment.

**Authority:** `docs/ROADMAP.md`, `docs/design/POST_OPT10_PROGRAM_PLAN.md`,
`docs/design/RHI_PRIMARY_RENDERER_ARCHITECTURE.md`, and
`docs/design/NRI_INTEGRATION_CONTRACT.md`.

**Planning baseline:** repository `5ff4583b1c40fbf92df8edbadf64700517ceafa4`.
That baseline predates the later explicit user releases. The live exchange owns
the current bounded assignment; the checkpoint above records the later acceptance
decision without rewriting the original target architecture as fully deployed.

## 1. Objective and boundary

OPT-11 establishes a measured ownership and memory foundation for rendering:

1. Simulation/extraction may run independently from GPU submission.
2. A bounded immutable snapshot queue provides explicit back-pressure and latency
   diagnostics.
3. A render worker can own native RHI operations without violating the current
   owner-thread checks.
4. Independent render work may be recorded in parallel only when the pinned NRI
   backend actually supports the required command-buffer and allocator contract.
5. RenderGraph may allocate transient resources and reuse compatible memory only
   when lifetime analysis and backend aliasing barriers are correct.
6. Plugin callbacks, resource leases, shutdown, resize, surface rebuild and
   device loss remain safe while frames are queued or transient allocations are
   live.

This OPT does not implement mesh shaders, GPU geometry, DDGI, lighting, shader
materials, animation authoring or VFX expansion. Those remain in OPT-12 onward.
It also does not promise a particular FPS or VRAM percentage; those are measured
against the frozen baseline gate.

## 2. Entry gate before C11-01

The following evidence must exist before source implementation begins:

- OPT-10 is accepted, or the user explicitly records a bounded deferral of its
  remaining gates.
- The current D3D12, Vulkan and OpenGL capability matrix is recorded. Unsupported
  rows are marked `NOT RUN` or `UNSUPPORTED`, never inferred as passing.
- Fixed benchmark scenes, camera paths, resolutions, quality presets, hardware
  tiers, warm-up length, sample count, frame-time percentiles and image-diff
  policy are frozen. Any numeric target is a target from this baseline, not a
  promise.
- RenderWorld snapshot ownership, RenderService lifetime, RenderGraph pass and
  resource contracts, resource-generation leases and completion-token semantics
  are frozen for this OPT.
- A synchronous reference path can render the benchmark scene and produce a
  golden image on each available modern backend and the OpenGL compatibility
  path where applicable.
- A clean Debug and Release build plus the existing rendering/RHI test set is
  recorded as the pre-change baseline.

The release record must name the exact base commit, build directories, hardware,
backend, driver and unavailable checks.

## 3. Planning-baseline constraints that shape the design

The current source already gives OPT-11 useful seams, but not the finished
feature:

- `RenderWorld` is an immutable scene snapshot, but it currently carries native
  mesh/material references and resource leases that must be audited before a
  second thread can consume them.
- `RenderService` is a process-level coordinator and records its owner thread;
  `RHIDevice::CanSubmit()` and resource creation require the resource owner
  thread.
- `RHIFrameContext` already protects frame arenas and completion tokens, and
  `RHISwapChain` already owns per-frame command allocators and present fences.
- `RenderGraph` is currently sequential and imports borrowed textures. It does
  not yet create graph-owned transient resources, calculate lifetimes or emit
  aliasing barriers.
- `RHICommandBuffer` exposes command allocator ownership and submission, but the
  current wrapper does not expose a secondary-command-buffer or execute-secondary
  merge operation. C11-04 must prove that contract from the pinned NRI headers
  and real backends before adding an engine API.
- NRI exposes placed resources and `features.resourceAliasing` in its native
  contract. The engine has a buffer placed-resource path and memory-group
  helpers, while texture placement and graph-level allocation still need a
  bounded design.
- The unified enkiTS job system is suitable for immutable CPU preparation, but a
  completed job is not evidence that GPU work has completed. CPU task tokens and
  GPU completion tokens remain separate.
- OpenGL context ownership is not assumed to be transferable. The compatibility
  path remains synchronous unless a separate, verified context policy is added.

## 4. Target ownership model

The first accepted target is a single render owner, with optional CPU recording
workers behind it:

```text
Simulation / main thread
    -> ExtractRenderWorld (immutable CPU snapshot)
    -> bounded RenderSnapshotQueue
    -> RenderWorker owns RenderService, RHIDevice and presentation
       -> resolve resource generations and compile RenderGraph
       -> record primary/native command buffers
       -> submit, present and retire leases
    -> completion/diagnostics queue back to the producer
```

The producer never calls `CanSubmit`, creates/destroys a native resource, waits a
GPU fence, or presents a swapchain after the worker is running. It sends typed
commands (`Snapshot`, `Resize`, `VSync`, `Reconfigure`, `Stop`) and reads
published diagnostics. All queue operations have explicit ownership and
shutdown semantics.

The OpenGL path uses the existing owner-thread synchronous flow until a context
transfer probe passes on every supported product. The native RHI worker path is
feature-gated and can fall back to synchronous rendering at startup.

## 5. Package plan and deliverables

Each package is one reviewable unit (`C11-01` through `C11-07`). Production
changes and focused tests stay in the same commit; acceptance documentation may
follow as a small `-acceptance` commit. Do not start the next unit until the
previous unit's evidence and open gates are recorded.

### C11-01 / 11.1 — Ownership design and capability probes

**Purpose:** make thread and lifetime ownership explicit before moving work.

**Work:**

- Define the render-worker state machine: `Stopped`, `Starting`, `Running`,
  `SurfaceRebuildRequired`, `DeviceLost`, `Draining`, `Failed`.
- Bind `RenderService`, `RHIDevice`, `RHIResourceService`, `RHISwapChain`, frame
  slots, descriptor pools and retirement collection to one owner thread.
- Add an explicit render-thread configuration/capability report. It must state
  whether the selected backend supports worker ownership, parallel recording,
  placed resources and resource aliasing.
- Audit every current caller of `RenderService::Get`, `RHIDevice::Get`,
  `CanSubmit`, resource creation, `WaitIdle`, resize and presentation. Convert
  producer calls to commands or read-only diagnostics.
- Specify device-loss and shutdown ordering, including plugin unload and pending
  resource leases. No destructor may wait indefinitely on a lost device.
- Keep the synchronous renderer as the default fallback while the worker is
  disabled or unsupported.

**Likely source surface:** `Engine/Scene/RenderService.*`,
`Engine/RHI/RHIDevice.*`, `RHIResource.*`, `RHISwapChain.*`,
`RHIPresentationContext.*`, application/runtime/editor composition and a new
render-thread contract header.

**Tests/evidence:** owner-thread rejection tests; state-transition table tests;
startup fallback tests; injected surface/device-loss transitions; shutdown with
live leases; static audit showing no producer-side native submissions.

**Exit:** the worker path can be enabled only when all required capabilities are
reported, and the synchronous path remains behaviorally unchanged.

### C11-02 / 11.2 — Bounded RenderWorld snapshot queue

**Purpose:** decouple extraction from GPU work without unbounded memory growth or
ambiguous frame selection.

**Work:**

- Implement a bounded SPSC queue for the normal simulation-to-render path, with
  a documented MPSC adapter only if a real producer requires it.
- Give every snapshot a monotonically increasing sequence, simulation tick,
  scene revision, extraction timestamp and resource-generation epoch.
- Define queue-full policy: preserve the newest snapshot for interactive views,
  never drop a snapshot required by a readback/export request, and count every
  dropped/replaced frame.
- Define one-frame latency diagnostics and a maximum queue age. Do not silently
  block the simulation thread on a GPU fence.
- Ensure a snapshot retains the exact CPU data and resource-generation leases it
  records. A failed enqueue releases those leases immediately.
- Add a deterministic drain operation used by stop, restart, device loss and
  plugin unload.

**Likely source surface:** new `RenderSnapshotQueue` and `RenderFrameTicket`
headers/sources under `Engine/Scene` or a neutral renderer service; updates to
`RenderWorld`, `SceneExtraction` and producer integration.

**Tests/evidence:** multi-producer rejection or adapter tests; full/empty queue;
newest-frame replacement; protected export frame; sequence and revision
monotonicity; lease release on failed enqueue; bounded memory under overload;
stop while producers are active; ThreadSanitizer-style stress where available.

**Exit:** queue depth, age, replacement and drop counts are observable, bounded
and deterministic, and no snapshot contains a mutable ECS or Scene reference.

### C11-03 / 11.3 — Render worker, offscreen path and presentation handoff

**Purpose:** move native rendering to the owner worker while keeping resize,
acquire, present and surface rebuild transactional.

**Work:**

- Start the render worker after backend selection and transfer/bind all native
  ownership on that thread. The worker consumes snapshots and publishes
  `RenderResult` plus diagnostics.
- Keep offscreen rendering first. Add the windowed presentation handoff only
  after offscreen worker rendering passes on D3D12 and Vulkan.
- Route resize, minimize/restore, VSync and surface-rebuild requests through the
  worker command queue. Allocate replacement target sets transactionally and
  retire old targets only after their recording/submission leases complete.
- Ensure `Acquire -> render -> submit -> present` is one owner-controlled chain.
  Empty/minimized frames still complete the acquire/present policy explicitly.
- Handle `NOT_READY`, out-of-date/suboptimal surfaces, timeout, submit failure
  and device loss as structured states. Never spin on a lost device.
- Preserve synchronous fallback for OpenGL and for any failed native worker
  startup probe.

**Likely source surface:** `RenderService`, `RHIPresentationContext`,
`RHISwapChain`, `RHIFrameContext`, Editor/Runtime composition and new worker
lifecycle/command files.

**Tests/evidence:** headless worker render; D3D12/Vulkan offscreen image match;
windowed acquire/present; resize and minimize/restore; VSync request; worker
restart; failed submit; device-loss shutdown; plugin stop with queued frames.
Native validation output must be captured for each available backend.

**Exit:** no cross-thread `CanSubmit` or resource-owner violation is observed;
worker and synchronous paths produce equivalent output and bounded shutdown.

### C11-04 / 11.4 — Parallel command recording probe and implementation

**Purpose:** reduce CPU recording time only where the actual NRI contract permits
it.

**Probe first:**

- Inspect the pinned NRI interface and validation layer for secondary command
  buffers, execute/merge support, allocator reset rules and thread-safety.
- Run a minimal native probe on D3D12 and Vulkan using one allocator per worker,
  one command buffer per worker and independent render work. Capture validation
  output and backend-specific failures.
- If NRI exposes no portable merge operation, do not invent one. Keep GPU command
  recording on the render owner and evaluate parallel CPU packet preparation as
  a separate, measured fallback.

**If supported:**

- Add a `RenderRecordingPlan` that partitions independent graph passes/draw
  ranges, declares resource reads/writes and records no shared mutable state.
- Give each worker its own allocator, command buffer, descriptor arena and
  recording lease. Workers may not submit, present, reset another worker's
  allocator or mutate global resource state.
- Merge/execute recorded work on the owner command buffer in graph order, insert
  barriers on the owner path and submit once.
- Disable parallel recording for unsupported passes, OpenGL, readback/export,
  plugin callbacks with unknown thread contracts and any validation failure.

**Likely source surface:** `RHICommandBuffer.*`, `RHIFrameContext.*`,
`RenderGraph.*`, new recording-plan/worker files and `Engine/Core/JobSystem`
adapters.

**Tests/evidence:** allocator isolation; deterministic pass order; race and
lifetime stress; D3D12/Vulkan validation; image equivalence against serial
recording; CPU recording time and worker utilization; forced fallback test.

**Exit:** parallel mode is enabled only from positive backend capability evidence.
A serial or CPU-preparation fallback is a valid C11-04 result when the pinned NRI
cannot merge native command buffers.

### C11-05 / 11.5 — RenderGraph transient allocation and VRAM aliasing

**Purpose:** add graph-owned transient resources with proof that reuse is safe.

**Graph contract:**

- Distinguish `Imported` resources from `Transient` resources. Imported resource
  generations and external/UI leases remain unchanged.
- Compile explicit dependencies plus hazard edges into a DAG and produce a stable
  topological pass order. Reject cycles, undefined resources and unsupported
  queue transfers.
- For each transient resource calculate first use, last use, format/size/sample
  compatibility, alignment, usage flags and memory location.
- Assign non-overlapping lifetime intervals to reusable blocks (linear scan or
  interval coloring), with conservative compatibility rules. Never alias a
  resource whose lifetime overlaps, whose layout/usage is incompatible, or whose
  queue ownership is unresolved.
- Emit an aliasing barrier between the previous occupant's last use and the next
  occupant's first use. Keep pending graph state separate from submitted state;
  publish allocation and leases only after successful recording/submission.
- Retire transient memory with the frame completion token. Abandoned recordings
  release it without waiting for submission.

**Backend strategy:**

- Query `features.resourceAliasing`, memory requirements and alignment from NRI.
- Use NRI `AllocateMemory`/`Bind*Memory` or `CreatePlaced*` only behind the RHI
  allocator; reuse the existing buffer placed path and add a transactional
  texture placed path where required.
- D3D12 and Vulkan need independent validation fixtures for placed resources and
  aliasing barriers. OpenGL uses committed allocations and the graph's serial
  lifetime checks; it does not claim VRAM aliasing.
- If a backend lacks a required function, capability or validation proof, keep
  transient allocation unaliased and report `unsupported` with measured peak
  bytes.

**Likely source surface:** `Engine/Renderer/RenderGraph.*`, new graph allocator
and lifetime-analysis files, `Engine/RHI/RHITexture.*`, `RHIBuffer.*`,
`RHIResource.*`, `RHICommon` capability reporting and frame retirement.

**Tests/evidence:** pure DAG/lifetime/packing tests; compatible and incompatible
resource pairs; alias barrier placement; cycle/invalid-use rejection; allocation
failure rollback; stale-generation rejection; D3D12/Vulkan validation with
actual transient textures/buffers; readback image equivalence; peak committed
and requested bytes before/after aliasing.

**Exit:** aliasing is opt-in per backend and graph. It is accepted only when
validation is clean, image output matches the serial committed path, and the
measured scene shows a real reduction in peak transient memory. Otherwise the
unaliased transient allocator remains the supported path.

### C11-06 / 11.6 — Plugin, resource-lease and lifetime stress

**Purpose:** prove that the new concurrency model does not extend plugin or GPU
object lifetimes past their owners.

**Work:**

- Exercise plugin callbacks that add render work, then stop/unload while frames
  are queued, recording workers are active or transient resources are leased.
- Drain or cancel queued snapshots before unloading a plugin. No callback may
  enter a destroyed module or retain a raw native pointer.
- Stress resize, restart, device-loss state, failed submit, abandoned frame,
  aliasing heap reuse and resource reload in mixed order.
- Verify CPU task completion and GPU completion are both observed before final
  destruction; never substitute one token domain for the other.
- Capture first-error diagnostics and ensure recovery/restart starts from a clean
  epoch with stale handles rejected.

**Likely source surface:** plugin session/lifetime bridges, render worker,
resource leases, frame retirement and test fixtures.

**Tests/evidence:** repeated start/stop/restart; plugin unload with queued work;
resource reload while recording; failed allocation/submit; surface rebuild;
device-loss simulation; long-run leak and handle-generation checks; Debug and
Release runs.

**Exit:** no use-after-unload, stale generation, double retirement, deadlock or
unbounded shutdown wait is observed in the required stress matrix.

### C11-07 / 11.7 — Consolidated acceptance and rollout decision

**Purpose:** decide which parts of OPT-11 are production-ready.

**Acceptance matrix:**

| Gate | Required evidence | Result policy |
| --- | --- | --- |
| Ownership | Worker owns every native call; producer has no direct submit/present/resource mutation | PASS or block C11-03 |
| Queue | Bounded memory, deterministic replacement/drop policy, sequence/age diagnostics | PASS or keep synchronous extraction |
| Output | Golden image and readback parity for serial vs worker and serial vs aliasing path | Any diff above frozen tolerance blocks that path |
| Latency | Frame age, queue depth, CPU/GPU completion and dropped-frame percentiles | Compare with frozen baseline; do not invent a universal FPS target |
| Parallel recording | Positive NRI/validation proof, allocator isolation and measured CPU benefit | Otherwise record UNSUPPORTED and retain serial/CPU-prep fallback |
| Aliasing | Correct lifetimes/barriers, validation clean, peak transient reduction | Otherwise retain unaliased transient allocation |
| Reliability | Resize, minimize, restart, failed submit, device loss, plugin stop/unload | Any unresolved P1/P2 blocks acceptance |
| Shutdown | Bounded drain, no leaks, no stale handles, no callbacks into unloaded code | Required on every supported backend/configuration |
| Rollout | Feature flags, diagnostics and documented backend matrix | Enable per backend only after its row passes |

Produce one delivery report with base/head commits, exact commands and exit
codes, hardware/driver identity, log paths, image-diff summaries, memory reports,
open `NOT RUN` gates and a separate-context correction review. Keep unsupported
parallel-recording or aliasing backends deferred instead of marking the whole OPT
as passed by inference.

## 6. Proposed API/data contracts

Names are proposals to guide implementation; ownership and behavior are the
contract.

- `RenderThreadConfig`: backend policy, queue capacity, frame-slot count,
  worker-recording request, transient-aliasing request and fallback policy.
- `RenderThreadCapabilities`: selected backend, native readiness, worker
  ownership, parallel-recording support, placed-resource support, aliasing
  support and reason strings for every disabled feature.
- `RenderSnapshotTicket`: immutable `RenderWorld`, sequence/tick/revision,
  extraction timestamp, resource epoch, protected-request flag and release hook.
- `RenderSnapshotQueueStats`: depth, high-water mark, age, replacements, drops,
  blocked producers, protected-frame failures and last-consumed sequence.
- `RenderCommand`: snapshot/resize/vsync/reconfigure/stop, with cancellation and
  acknowledgement semantics.
- `RenderFrameDiagnostics`: sequence, backend, state, CPU recording time, GPU
  completion token, queue age, dropped/replaced count, transient requested and
  committed bytes, alias bytes saved, validation/error fields.
- `TransientResourceDesc` and `TransientAllocation`: graph resource identity,
  native description, lifetime interval, compatibility key, memory block/offset,
  generation and retirement token.
- `RenderRecordingPlan`: graph pass IDs, dependency order, resource leases,
  worker allocator IDs, fallback reason and merge order.

All public contracts must avoid exposing mutable ECS registries, raw module
pointers or untracked native resources to producers and plugins.

## 7. Verification matrix

Run the applicable rows for every package and repeat the final matrix after the
last correction:

- **Always active CPU:** queue, state machine, DAG, interval packing, capability
  reporting, failure rollback, generation/lease and shutdown unit tests.
- **Debug/Release:** changed targets plus the existing rendering, RHI, scene,
  plugin and architecture suites. Build the test executable before using its
  result.
- **D3D12 native:** headless worker/offscreen, windowed presentation, resize,
  serial/parallel probe, transient placed resources and alias barriers with API
  validation.
- **Vulkan native:** the same rows, including validation-layer output and
  surface-rebuild behavior.
- **OpenGL compatibility:** synchronous fallback, unchanged image behavior,
  explicit `parallel_recording=unsupported` and `aliasing=unsupported` unless a
  separate context/memory proof is added.
- **Product:** Editor, standalone Runtime, preview/thumbnail and selected
  export paths use the same worker policy and report their own unsupported rows.
- **Stress:** repeated restart, queue overload, resize/minimize, failed submit,
  device-loss injection, plugin unload and resource reload.

No CPU-only run, null NRI backend, compile success or old delivery report can
substitute for a required native/product row.

## 8. Rollout and rollback

Ship each capability behind an explicit configuration and diagnostic flag:

1. `render_thread=off` keeps the current synchronous path.
2. `render_thread=on` is allowed only after C11-01/02 and native offscreen C11-03
   pass for that backend.
3. `parallel_recording=on` requires the C11-04 positive probe; otherwise startup
   reports `unsupported` and uses serial or CPU-preparation fallback.
4. `transient_aliasing=on` requires C11-05 graph and backend evidence; otherwise
   transient resources use committed/unaliased allocation.
5. Any validation error, device loss, image mismatch, lease violation or
   unbounded shutdown returns to the last accepted flag combination without
   changing authored scene or asset data.

The final rollout removes only adapters that are proven obsolete and updates the
capability matrix, user-facing diagnostics and architecture references in the
same acceptance update.

## 9. Recommended release sequence

1. Close or explicitly defer the remaining OPT-10 gates.
2. Release C11-01 only and record the ownership/capability decision.
3. Release C11-02 and prove bounded snapshots while rendering stays synchronous.
4. Release C11-03 offscreen worker first; add windowed presentation after proof.
5. Release C11-04 as a probe/experiment. Accept serial fallback if NRI cannot
   merge native command buffers.
6. Release C11-05 with unaliased transient graph allocation first, then enable
   aliasing per backend after barrier validation.
7. Release C11-06 stress and correction rechecks.
8. Release C11-07 consolidated acceptance. Stop at OPT-11; OPT-12 onward needs a
   separate user release.

The immediate next action is therefore documentation and baseline preparation,
not source implementation. A later user instruction must activate the concrete
C11 package range and name the implementer/reviewer boundary.
