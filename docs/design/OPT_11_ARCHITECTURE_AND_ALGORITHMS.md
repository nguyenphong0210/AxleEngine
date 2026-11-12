# OPT-11 Implementation Contract: Architecture and Algorithms

**Status:** implementation guidance only; it does not release OPT-11.

**Audience:** Gemini implementer and Codex reviewer.

**Related contracts:** [OPT-11 execution plan](OPT_11_EXECUTION_PLAN.md),
[RHI primary renderer architecture](RHI_PRIMARY_RENDERER_ARCHITECTURE.md),
[NRI integration contract](NRI_INTEGRATION_CONTRACT.md), and
[Phase 01 architecture](../phases/PHASE_01_RENDER_FOUNDATION/ARCHITECTURE.md).

The execution plan defines package scope and acceptance. This document defines
how an implementation should be structured and which invariants the code must
preserve. Names below are guidance; ownership, ordering and failure behavior are
mandatory.

## 1. Non-negotiable invariants

1. **One native owner.** The thread that owns `RHIDevice::Get().GetResources()`
   owns all native resource creation/destruction, command recording, queue submit,
   swapchain acquire/present, fence waits and retirement collection. The producer
   thread never calls those APIs after the render worker starts.
2. **Immutable snapshots.** A queued `RenderWorld` owns all CPU data and exact
   resource-generation leases it needs for its recording lifetime. It contains no
   `Scene*`, ECS registry, mutable component pointer, plugin object pointer or
   raw native object whose lifetime is not covered by a lease.
3. **Two completion domains.** An enkiTS task completion token proves CPU work is
   complete. An RHI completion token proves GPU work is complete. Neither token
   substitutes for the other.
4. **Publish after success.** Resource state, generation, completion token,
   graph allocation and retirement records become visible only after the
   relevant operation succeeds. Failed recording/submission leaves no successful
   completion advertised.
5. **No guessed capabilities.** A backend feature is enabled only from a checked
   NRI function table/device feature and a passing native probe. A missing or
   unverified feature reports `unsupported` and follows the serial/unaliased
   fallback.
6. **Deterministic order.** Graph topological order, queue sequence selection,
   pass merge order, diagnostics and image output are deterministic for the same
   input snapshot and configuration.
7. **Bounded shutdown.** Stop, device loss and plugin unload drain or cancel work
   with a finite policy. No loop may wait forever on a lost device or a worker
   that has already stopped.
8. **OpenGL is explicit fallback.** The OpenGL compatibility path remains
   synchronous unless a separate context-transfer proof is added. It never
   enters native NRI worker or aliasing code by accident.

## 2. Ownership architecture

### 2.1 Roles

| Role | Owns | May do | Must not do |
| --- | --- | --- | --- |
| Simulation/producer | Scene/ECS and simulation clock | Extract CPU snapshot; enqueue commands; read diagnostics | Native RHI calls, GPU waits, present, mutate queued snapshot |
| Render command bridge | Bounded queues and acknowledgements | Copy typed commands; cancel/drain on stop | Touch Scene/ECS or call backend directly |
| Render worker | RenderService, RHIDevice, resource service, frame slots, graph allocator, presentation | Resolve resources, record, submit, present, retire | Read mutable Scene/ECS or call plugin code without a declared thread contract |
| CPU recording jobs | Immutable recording plan and private worker arenas | Prepare draw packets or record native commands only if probe allows | Submit/present, reset another worker allocator, mutate global resource state |
| Plugin callback | Plugin-owned CPU data and declared render contribution | Add immutable commands/resources through the bridge | Retain raw worker/module pointers or execute native RHI calls from an unknown thread |

### 2.2 Thread-affinity guard

Add a small internal guard around owner-sensitive operations. It should record
owner thread ID and a reason code in diagnostics, then return failure in Debug and
Release when called by the wrong thread. Do not use assertions as the only guard.

```cpp
class RenderOwnerToken {
public:
    bool BindToCurrentThread();
    bool IsOwnerThread() const;
    bool RequireOwner(std::string_view operation) const;
    std::thread::id GetOwnerThread() const;
};
```

`RHIDevice::CanSubmit()` may continue to be the final RHI check, but the render
worker must reject a command before entering the native interface and must publish
a structured `WrongOwnerThread` diagnostic. Read-only capability queries are
allowed from the producer only if they do not touch mutable native state.

### 2.3 Worker state machine

```text
Stopped
  -> Starting             (start requested)
Starting
  -> Running              (device/surface/frame slots ready)
Starting
  -> Failed               (startup transaction failed)
Running
  -> SurfaceRebuildRequired (resize/out-of-date/suboptimal)
Running
  -> DeviceLost           (device loss or ambiguous queue failure)
Running
  -> Draining             (stop requested)
SurfaceRebuildRequired
  -> Running              (replacement targets committed)
SurfaceRebuildRequired
  -> DeviceLost           (rebuild reports loss)
DeviceLost
  -> Draining             (cancel policy selected)
DeviceLost
  -> Starting             (explicit restart with a new device epoch)
Draining
  -> Stopped              (queues and leases drained/cancelled)
Failed
  -> Stopped              (resources rolled back)
```

State transitions are serialized by the worker. Producers see an atomic snapshot
of the state plus a monotonically increasing `stateEpoch`; they cannot mutate the
state directly.

## 3. Data contracts

### 3.1 Snapshot ticket

```cpp
struct RenderSnapshotTicket {
    RenderWorld World;                 // immutable value, never a Scene reference
    uint64_t Sequence = 0;             // producer-assigned, strictly increasing
    uint64_t SimulationTick = 0;
    uint64_t SceneRevision = 0;
    uint64_t ResourceEpoch = 0;
    uint64_t ExtractedAtNs = 0;
    bool ProtectedRequest = false;     // export/readback cannot be dropped
    std::shared_ptr<const RenderLeaseSet> Leases;
};
```

`RenderLeaseSet` must retain exact resource generations used by the snapshot. It
must not retain a plugin module, UI panel or ECS registry. A failed enqueue drops
the ticket and releases `Leases` immediately.

### 3.2 Commands and acknowledgements

```cpp
enum class RenderCommandKind { Snapshot, Resize, SetVSync, Reconfigure, Stop };

struct RenderCommand {
    RenderCommandKind Kind;
    uint64_t RequestId;
    std::variant<RenderSnapshotTicket, ResizeRequest, VSyncRequest,
                 RenderConfig, StopRequest> Payload;
};

struct RenderAck {
    uint64_t RequestId;
    RenderState State;
    bool Accepted;
    RenderError Error;
    uint64_t StateEpoch;
};
```

Commands are moved into the worker queue. A producer may wait on an acknowledgement
for configuration or export, but never waits on a GPU fence directly.

### 3.3 Published diagnostics

```cpp
struct RenderFrameDiagnostics {
    uint64_t Sequence;
    uint64_t StateEpoch;
    RenderState State;
    uint64_t QueueDepth;
    uint64_t QueueAgeNs;
    uint64_t ReplacedFrames;
    uint64_t DroppedFrames;
    uint64_t CpuExtractNs;
    uint64_t CpuRecordNs;
    uint64_t SubmitNs;
    uint64_t GpuCompletionValue;
    uint64_t TransientRequestedBytes;
    uint64_t TransientCommittedBytes;
    uint64_t TransientAliasedBytes;
    std::string Backend;
    std::string FeatureFallback;
    std::optional<RenderError> FirstError;
};
```

Diagnostics are values copied out of the worker. Do not expose references to a
worker-owned renderer or resource manager.

## 4. Bounded snapshot queue algorithm

Use SPSC first: one extraction producer and one render consumer. If a real product
has more producers, wrap the SPSC queue with a mutex-protected MPSC adapter rather
than pretending a second producer is safe.

### 4.1 Ring layout

```cpp
struct QueueSlot {
    std::atomic<bool> Ready{false};
    std::optional<RenderSnapshotTicket> Ticket;
};

struct RenderSnapshotQueue {
    std::vector<QueueSlot> Slots;       // fixed capacity, never grows at runtime
    std::atomic<uint64_t> Write{0};
    std::atomic<uint64_t> Read{0};
    std::atomic<uint64_t> Replaced{0};
    std::atomic<uint64_t> Dropped{0};
};
```

The producer writes the ticket before publishing `Ready=true` with release order.
The consumer reads `Ready` with acquire order, moves the ticket, clears `Ready` with
release order, then advances `Read`. Queue capacity is fixed by configuration and
reported in diagnostics.

### 4.2 Push policy

```text
Push(ticket):
  if stopping: release ticket; return RejectedStopping
  if free slot exists:
      write ticket; publish Ready; return Accepted
  if ticket.ProtectedRequest:
      return RejectedProtectedFull (caller keeps ownership for retry/error)
  replace the newest not-yet-consumed ticket:
      release replaced ticket
      write new ticket; increment Replaced
      return AcceptedReplacing
```

Never replace a protected ticket. If every slot is protected, reject the new
non-protected ticket and increment `Dropped`. Do not block the simulation thread on
GPU completion. The producer may choose to block only on a full queue when the
caller explicitly requested a protected export and the timeout is bounded.

### 4.3 Pop policy

```text
PopForRender():
  if empty: return None
  consume the newest available non-protected ticket
  release older unprotected tickets without rendering them
  preserve the oldest protected ticket that is already admitted
  return selected ticket
```

The implementation may use a simpler FIFO for deterministic tests, but interactive
mode must document whether it renders newest, oldest, or FIFO. The policy must be
visible in `RenderSnapshotQueueStats`.

### 4.4 Queue invariants to test

- Sequence numbers never decrease at the consumer.
- Queue memory does not grow under overload.
- Replaced/dropped counters equal the number of released tickets.
- Protected tickets are never silently discarded.
- Stop drains or cancels every ticket exactly once.
- A ticket cannot be read while its producer is still writing it.

## 5. Render-worker loop

The worker owns the only mutable renderer state. The loop has one place where a
frame can be submitted and one place where completion/retirement is collected.

```text
WorkerMain:
  bind owner token to current thread
  transition Starting
  if !InitDeviceSurfaceFrames(): publish Failed; return
  transition Running

  while true:
    DrainCommandsWithoutBlocking()
    if state == Draining: break
    if state == DeviceLost:
        CancelUnsubmittedWork()
        if restart requested: RecreateFromNewEpoch()
        else: break

    ApplyAtMostOneResizeOrReconfigure()
    ticket = queue.PopForRender()
    if no ticket:
        PumpSurfaceEventsAndRetirement()
        WaitForCommandOrSnapshot()
        continue

    if state != Running:
        Cancel(ticket)
        continue

    if !BeginFrameSlot():
        publish busy-slot diagnostic; CancelOrRetry(ticket); continue
    if !ResolveSnapshotResources(ticket):
        AbandonFrame(); publish error; continue

    plan = BuildRenderPlan(ticket.World)
    if !plan.Compile():
        AbandonFrame(); publish graph error; continue

    result = RecordAndSubmit(plan, ticket)
    if result == DeviceLost: transition DeviceLost
    else if result == SurfaceRebuildRequired: transition SurfaceRebuildRequired
    else if result == Success: publish completion and diagnostics
    else: publish failure; abandon or retire according to frame state

    CollectCompletedRetirements()

  CancelQueuedTickets()
  WaitOnlyForKnownLiveCompletionsWithBoundedTimeout()
  DestroyWorkerOwnedObjectsInOwnerOrder()
  transition Stopped
```

`BeginFrameSlot` may wait only for that slot's completion token. It must not call
`WaitIdle` on every frame. `AbandonFrame` must release recording leases and leave
published resource state unchanged.

## 6. Resize, surface rebuild and device loss

### 6.1 Transactional resize

```text
Resize(request):
  if width == 0 or height == 0:
      mark minimized; stop allocating new targets; keep old generation retired later
      acknowledge success-without-present
  else:
      wait for only the target generations that must be replaced
      create candidate color/depth/view targets
      create candidate UI registrations if required
      if every candidate succeeds:
          publish new target generation
          retire old targets with their recording/submission leases
      else:
          destroy candidates
          preserve previous valid target generation
          acknowledge resize failure
```

Do not unregister a live UI texture before the replacement has been published and
old frame leases are safe to retire.

### 6.2 Device-loss policy

On the first ambiguous submit/present result or `DEVICE_LOST`:

1. Capture backend, operation, result, frame sequence and first-error diagnostics.
2. Stop new submissions immediately.
3. Cancel unsubmitted graph work and release CPU tickets.
4. Do not repeatedly call `WaitIdle` or create recovery fences on the lost device.
5. Destroy resources in owner order after bounded cleanup.
6. Increment device epoch; all old resource handles fail generation/epoch checks.
7. Restart only after an explicit reconfigure/start request; otherwise remain
   `Draining -> Stopped`.

## 7. RenderGraph compilation and lifetime analysis

### 7.1 Resource model

Extend the graph model without changing imported-resource semantics:

```cpp
enum class GraphResourceKind { Imported, TransientTexture, TransientBuffer };

struct GraphResourceDesc {
    GraphResourceKind Kind;
    std::string Name;
    TextureDesc Texture;
    BufferDesc Buffer;
    bool ContentsDefined = false;
    bool AllowAlias = false;
};

struct GraphLifetime {
    uint32_t FirstPass = UINT32_MAX;
    uint32_t LastPass = 0;
    uint64_t Size = 0;
    uint64_t Alignment = 1;
    CompatibilityKey Key;
};

struct GraphAllocation {
    uint32_t Resource;
    uint32_t Block;
    uint64_t Offset;
    uint64_t Size;
    uint32_t PreviousOccupant = UINT32_MAX;
};
```

Imported resources never enter the alias allocator. They retain their external
lease and expected initial/final state.

### 7.2 Dependency graph

Build edges from both explicit dependencies and resource hazards:

```text
for pass in declaration order:
  for use in pass.Uses:
    validate resource ID and access/layout
    if prior writer exists: add edge(prior writer, pass)
    if pass writes and prior readers exist: add edges(prior reader, pass)
    if pass writes and prior writer exists: edge already present
    update last writer/readers for this resource
  add explicitly named dependency edges
```

Deduplicate edges, then run Kahn's algorithm with a min-heap keyed by original
pass index. This gives deterministic topological order. If the output count is
smaller than the pass count, reject a cycle and do not allocate resources.

### 7.3 Lifetime computation

After topological order is fixed:

```text
for each transient resource r:
  first = +infinity
  last = -infinity
  for each ordered pass index i using r:
      first = min(first, i)
      last  = max(last, i)
  if no use: reject or remove before allocation
  r.Lifetime = {first, last, memory requirements, compatibility key}
```

A resource is live over the closed interval `[first, last]`. Two resources may
share memory only when `a.last < b.first` or `b.last < a.first`, and their
compatibility keys permit the same memory block. Be conservative with format,
samples, usage flags, memory location, alignment and queue ownership.

### 7.4 Interval-coloring allocator

Use deterministic first-fit interval coloring for the first implementation:

```text
sort transient resources by (first, last, resource ID)
active = empty list of {last, block, offset, size, key}
freeBlocks = empty deterministic map

for resource r:
  retire every active item with item.last < r.first
  candidate = first free block compatible with r.Key and large enough
  if candidate exists:
      assign candidate offset
      previous = candidate.lastOccupant
  else:
      create a new block with r.Alignment and r.Size
  add r to active
```

The allocator must report both the unaliased sum and the packed peak. It must
never pack resources with overlapping intervals. A failed native allocation
rolls back every candidate created during that graph compile and leaves the last
published graph generation usable.

### 7.5 Aliasing barriers

For every reused block where `previous.last < next.first`, emit an alias event
between those ordered passes:

```text
AliasEvent {
    Block;
    PreviousResource;
    NextResource;
    BeforePass = Previous.LastPass;
    AfterPass = Next.FirstPass;
}
```

The RHI layer translates the event to the backend barrier contract. If the pinned
NRI/backend has no verified aliasing barrier operation, compilation must select
`UnaliasedTransient` mode rather than silently reusing memory.

Barriers are recorded on the render owner in graph order. Worker recording cannot
publish or mutate graph state before those barriers are committed.

### 7.6 Graph state commit

```text
Compile -> validate -> topological order -> lifetime plan -> native candidates
Record  -> pending state/leases only
Submit  -> publish graph generation and completion token
Failure -> destroy candidates; restore previous published state
```

`RenderGraph::Compile()` remains side-effect free from the caller's point of view:
its candidate allocations are owned by a temporary compile result until the frame
successfully adopts them.

## 8. Native resource allocation strategy

1. Query the backend's memory requirements and alignment for every transient
   resource description before choosing a block.
2. Check `features.resourceAliasing` and the required `CoreInterface` functions.
3. Allocate one memory object per compatible block using the RHI allocator. Use
   `CreatePlacedBuffer`/`CreatePlacedTexture` or `Bind*Memory` through a small RHI
   abstraction; do not expose NRI memory objects to Renderer or Scene APIs.
4. Create all views/descriptors as candidates. Publish only after the full graph
   resource set is valid.
5. Attach a resource-generation lease to the frame ticket and completion token.
6. Retire the block and all views together after GPU completion.

The existing `RHIBuffer::InitPlaced` and `RHIMemoryManager` are starting points;
texture placement and transactional graph ownership must be added or corrected
without weakening ordinary committed allocation.

## 9. Parallel command recording

### 9.1 Capability probe

The probe must verify, from pinned headers and a real backend:

- command allocator creation is safe from the intended worker context;
- each worker can own a distinct allocator and command buffer;
- descriptor pool/arena usage is isolated per worker;
- the API provides a portable way to execute/merge recorded command buffers, or
  the backend-specific path is explicitly encapsulated and tested;
- validation accepts the ordering, barriers and resource leases;
- allocator reset is performed only after its GPU completion token.

The current `RHICommandBuffer` wrapper exposes creation and submission but not a
portable secondary execute/merge operation. C11-04 must not add a fake merge API
just to make the design compile.

### 9.2 Pass partitioning

If the probe passes, partition only independent graph passes. Compute DAG levels:

```text
level[pass] = 0 if no predecessors
level[pass] = 1 + max(level[pred]) otherwise
```

Within a level, sort by pass ID and assign contiguous ranges to workers. A pass
may be recorded on a worker only if:

- all resource reads/writes and descriptor ranges are declared;
- it owns a private command allocator, command buffer and descriptor arena;
- it does not call `RHIDevice::Submit`, `Present`, `WaitIdle` or global mutable
  pipeline/resource caches;
- its barriers are represented in the plan and applied in owner merge order;
- no plugin callback with an unknown thread contract is reachable.

### 9.3 Deterministic merge

Workers return `{PassID, CommandBuffer, RecordingLease, Status}`. The owner waits
for the CPU recording task tokens, sorts results by graph order, applies graph
barriers and merges/submits in that order. A missing or failed result cancels the
whole frame; it never submits a partial graph unless the pass contract explicitly
allows an empty frame.

If native merge is unsupported, use this fallback:

```text
workers prepare immutable DrawPacket arrays and resource references
owner records all native commands serially from packets
owner submits once
```

Measure this separately. It is a CPU-preparation optimization, not proof of
parallel native command recording.

## 10. Plugin and callback boundary

Plugins contribute values and declared work through a render-contribution object:

```cpp
struct RenderContribution {
    UUID PluginInstance;
    uint64_t SessionEpoch;
    std::vector<RenderPacket> Packets;
    std::vector<RenderResourceRequest> Resources;
};
```

Rules:

- The contribution owns copies or leases, never plugin object pointers.
- The worker checks `PluginInstance` and `SessionEpoch` before consuming it.
- Stop/unload first prevents new contributions, then drains/cancels queued
  contributions, then releases module-owned data.
- A worker job must not call into a DLL after unload has begun.
- A plugin may request a render result/readback only through a protected ticket;
  it cannot force an unbounded queue or a device wait.

## 11. Failure matrix and recovery behavior

| Failure | Detect at | Required action | Forbidden action |
| --- | --- | --- | --- |
| Queue full | enqueue | replace/drop by policy; count and diagnose | unbounded allocation |
| Protected queue full | enqueue | bounded retry or explicit rejection | silently discard export |
| Wrong owner thread | bridge/RHI guard | reject before native call; publish error | assert-only behavior |
| Graph cycle/invalid use | compile | reject frame; preserve previous published graph | partial allocation |
| Native allocation failure | candidate creation | destroy candidates; keep last valid generation | publish half-built resource |
| Command recording failure | worker result | abandon frame; release leases | submit partial graph |
| Submit/present failure | owner submit | capture first error; mark state per result | advertise completion |
| Device loss | RHI result | stop submits; bounded drain; epoch bump | repeated WaitIdle loop |
| Resize/minimize | command | transactional replacement or no-allocation minimized state | destroy live target first |
| Plugin unload | session bridge | stop admission, drain/cancel, unload after leases | callback into unloaded DLL |
| Worker exception/fatal error | worker boundary | publish Failed, cancel queue, join with bound | continue with corrupt state |

## 12. Implementation order for Gemini

Work in this order and stop at the released boundary:

1. **C11-01:** add owner token, state/capability report, wrong-thread tests and
   a synchronous fallback. Do not move rendering yet.
2. **C11-02:** add the bounded queue and ticket/diagnostics tests. Keep the
   synchronous renderer consuming a queue on the same thread for a safe vertical
   slice.
3. **C11-03:** add the worker with offscreen native RHI first. Integrate windowed
   acquire/present only after offscreen validation passes.
4. **C11-04:** run the NRI probe. Implement native parallel recording only if the
   probe proves the required merge contract; otherwise implement and measure the
   CPU-preparation fallback and record native recording as unsupported.
5. **C11-05:** add unaliased transient graph allocation first. Add interval
   packing and backend alias barriers behind a capability flag only after the
   unaliased path is stable.
6. **C11-06:** run plugin/lifetime stress and repair every finding before the
   consolidated acceptance package.
7. **C11-07:** produce the handoff with exact commits, commands, logs, backend
   identity, image diffs, memory measurements and NOT RUN/UNSUPPORTED rows.

For each package, Gemini should provide:

- base and head commit;
- exact files changed and API callers migrated;
- focused tests and their exit codes;
- native backend/device/driver information where applicable;
- feature flags and fallback behavior;
- known limitations and open gates;
- explicit statement that evidence is implemented/self-verified and pending Codex
  review.

## 13. Forbidden shortcuts

- Do not make `RenderWorld` contain a raw `Scene*` to avoid copying data.
- Do not call `WaitIdle` every frame to hide a lifetime race.
- Do not mark a device initialized because an OpenGL context exists when NRI is
  required.
- Do not use `shared_ptr` alone as proof that a native resource generation is
  safe; leases must be tied to recording/submission completion.
- Do not alias resources solely because their sizes match.
- Do not emit an aliasing barrier after reusing memory unless the backend contract
  has been validated.
- Do not call `CanSubmit`, `Present`, resource creation or destruction from a job
  worker unless the worker is the declared native owner for that operation.
- Do not claim parallel command recording from parallel CPU packet generation.
- Do not silently fall back from failed aliasing or recording while reporting the
  feature as enabled.
- Do not unload a plugin while queued snapshots, graph resources or worker jobs
  still hold module-owned callbacks.
- Do not stage or modify unrelated implementation files while preparing the
  documentation handoff.

## 14. Completion checklist

A package is ready for Codex review only when all applicable boxes have evidence:

- [ ] Package scope and base commit are recorded in the exchange task card.
- [ ] Owner-thread and lifetime invariants have focused regression tests.
- [ ] Failure paths leave no published partial resource or completion token.
- [ ] Debug and Release changed targets build and run.
- [ ] Native D3D12/Vulkan rows have actual logs or are marked NOT RUN.
- [ ] OpenGL fallback behavior is explicit and unchanged unless separately tested.
- [ ] Queue, graph, allocator and diagnostics counters are observable.
- [ ] Image output matches the serial reference within the frozen tolerance.
- [ ] Device-loss, resize, restart, plugin unload and shutdown are bounded.
- [ ] Performance and VRAM claims include scene, hardware, backend, resolution,
      preset and percentile context.
- [ ] Gemini's delivery is labeled implemented/self-verified; Codex review remains
      separate and is not inferred from the implementer's report.
