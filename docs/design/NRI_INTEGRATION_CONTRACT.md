# NRI Integration and Normalization Contract

> **Execution:** follow [OPT-00..11](../ROADMAP.md). This document is a
> technical contract, not a separate phase queue. Old Phase/G labels and
> source-audit baselines are retained for traceability. Early Vehicle retirement
> is delivered; acceptance and new plugin implementation remain open.

Date: **2026-11-04**. Engine planning baseline:
**cb200d373550b6dd4f7e12d483a161012d863a27**.
Status: **design only; no native integration change or vendor upgrade implemented**.

This specializes the existing [rendering architecture](RHI_PRIMARY_RENDERER_ARCHITECTURE.md)
and [P41 packages](RHI_PRIMARY_RENDERER_IMPLEMENTATION_PLAN.md) for the
[architecture-first master plan](ARCHITECTURE_FOUNDATION_MASTER_PLAN.md).
NR-01..12 below are contract rules mapped to P41 work, **not twelve additional
implementation packages**. Reuse [EJ ownership](ENTT_ENKITS_REFACTOR_PLAN.md) and
[plugin lifecycle](PLUGIN_SYSTEM_ARCHITECTURE.md) rather than new parallel services.

## 1. Pin and scope

The inspected local NRI checkout is
`6998e3fbbaed1b132f386a9d49743a2481003487`.
[Its public header](../../ThirdParty/NRI/Include/NRI.h) declares
`NRI_VERSION 190` and `NRI_VERSION_DATE "9 October 2026"`. These describe the local
baseline, not the latest upstream release. Record any local vendor modifications
separately; an upstream hash alone does not identify a dirty working tree.

[Root build settings](../../CMakeLists.txt) currently enable NRI D3D12, Vulkan,
None/dummy, validation and Agility SDK; D3D11/WebGPU/Metal and NVAPI/AMDAGS paths
are disabled. The first accepted product matrix is Windows x64 D3D12/Vulkan plus
the Engine's separate OpenGL compatibility path. Enabled vendor support does not
automatically establish Engine feature support or tested platform support.

Do not fetch/update NRI as part of this normalization. Bind SDK/build/artifact
fingerprints to the actual source revision/content, public API version, options,
toolchain, CRT/configuration and shader toolchain. A separate vendor update needs
its own rationale, migration diff and current regression/native evidence.

NRI is a low-level interface. Its own [local README](../../ThirdParty/NRI/README.md)
explicitly leaves automatic barriers and general hidden management to higher
layers; AxleEngine must own resource state, frame lifetime and product policy.
The [upstream project](https://github.com/NVIDIA-RTX/NRI) provides the same general
boundary, but the pinned local headers are authoritative for implemented calls.

## 2. Present integration: facts versus target

| Source observation | Consequence for the refactor |
| --- | --- |
| [RHIDevice.hpp](../../Engine/RHI/include/Axle/RHI/RHIDevice.hpp) defines NRIInterfaces and exposes native device/queue/interface access | Keep native access in the reviewed adapter/host SDK boundary, not every consumer |
| [RHIDevice.cpp](../../Engine/RHI/src/RHIDevice.cpp) may log a missing graphics queue and still return success from TryInitNRI | Required queue/interface readiness must decide startup success |
| Helper/Streamer/SwapChain/WrapperVK queries have warning paths | A caller must validate its actual required interfaces, not assume every table works |
| OpenGL fallback has no native NRI device, while generic initialized state can be true | Separate selected backend, context and native readiness |
| [RHITexture.cpp](../../Engine/RHI/src/RHITexture.cpp) destroys prior state at Init and directly narrows sizes to native types | Transactional replacement and validated dimension/usage conversion |
| [RHICommandBuffer.cpp](../../Engine/RHI/src/RHICommandBuffer.cpp) reports submission success/failure | Callers must consume the result and publish completion/state only on success |
| [ImGuiLayer](../../Engine/UI/src/ImGuiLayer.cpp) and [RHISwapChain](../../Engine/RHI/src/RHISwapChain.cpp) have presentation responsibilities | Choose one owner; UI records into that frame rather than managing a second presentation loop |
| [RHI CMake](../../Engine/RHI/CMakeLists.txt) includes EnTT streaming in low-level RHI | Move ECS extraction/streaming adapter above native RHI |
| [Canvas2D::EndCanvas](../../Engine/UI/src/UI2D/Canvas2D.cpp) updates batch statistics without native submission | Require actual Runtime UI adapter output, not reported DrawCalls |

These are source observations; new device-loss, allocation or rendering failures
were not reproduced during this planning task. Historical P41/VFX evidence stays
historical until rebuilt and rerun for the implementation baseline.

## 3. NR-01 - Responsibility and public dependency boundary

Ownership: AF-02/03/04 and P41-11..13/26.

```text
SceneData + CPU asset references
    -> owning-thread render extraction
    -> immutable RenderWorld + ViewRequest
    -> RenderService / passes / resource and frame services
    -> Axle NRI adapter
    -> pinned NRI
    -> D3D12 or Vulkan
```

OpenGL plugs into RenderService through a separate compatibility adapter.
No NRI device/interface table is manufactured for it.

Native NRI types are permitted in the low-level implementation and approved
native interop boundary. Generic Scene/asset definitions, gameplay components,
project settings and plugin discovery must not include NRI or native API headers.
Do not create a one-to-one wrapper class for every vendor symbol merely for
cosmetic neutrality. Add Engine contracts for ownership, identity, features and
product behavior where NRI deliberately does not provide them.

Move RHIECSStreamer above low-level RHI. Move vehicle headlight/brake-light preset
policy to Vehicle/integration or a neutral lighting layer. Mesh extraction, PBR
material defaults and entity lookup are not device-interface responsibilities.

**Evidence:** target/include closures, headless scene/asset builds, no new reverse
edge and a working render fixture through the same service.

## 4. NR-02 - Readiness and required interface negotiation

Ownership: P41-01/03 with AF-02.

Return structured backend status: requested/selected backend, actual adapter,
window/context readiness, native device readiness, queue availability, validation
level, hardware capabilities and Engine-supported feature capabilities.
Successful selection is not proof that creation is complete.

| Capability | Required when | Failure policy |
| --- | --- | --- |
| NRI CoreInterface + native device | Every modern GPU operation | Candidate backend fails; no native calls with empty table |
| Graphics queue | Every current rendering product | Candidate backend fails, not a warning-only success |
| HelperInterface | A chosen resource/upload implementation calls it | Feature/path fails or uses an explicitly implemented/tested alternate path |
| StreamerInterface | The selected upload path uses NRI Streamer | Same explicit requirement/alternative policy; no null function call |
| SwapChainInterface | NRI is the chosen presentation implementation | Windowed product fails or selects the approved presentation path; headless tests do not need it |
| Native wrapper interface | The selected UI/native resource bridge requires it | Disable/reject that integration or fail product readiness |
| Optional advanced interfaces | An explicitly requested Engine feature uses them | Report feature unsupported; do not infer from GPU marketing tiers |

NRI interface query success, required function availability, queue acquisition and
workload readiness are separate checked steps. Build a candidate locally; publish
the active device only after the required set passes. Failure releases all partial
native objects. Headless Core tests and a windowed Editor have different requirements.

**Evidence:** missing queue/interface/validation prerequisites, explicit backend
startup and real physical D3D12/Vulkan creation. Record actual selected backend;
do not call automatic GL fallback a successful native backend test.

## 5. NR-03 - Backend selection and compatibility

Ownership: P41-01/14/19/20.

Resolve CLI/project/default policy once. Startup fallback is a complete backend
transaction including window/context, device, required renderer/UI and product
capabilities. Explicit GL and fallback GL use the same resource resolution and
primitive identities. A generic IsInitialized flag must never authorize an NRI call.

Initial policy supports fallback at startup or explicit restart; live backend
switch/device recreation of every object is deferred. If authored features require
an unavailable modern path, report it or select an explicitly allowed compatibility
configuration. Do not silently drop features and claim visual parity. Hardware
support is not equivalent to an implemented shadow/skinning/UI pass.

**Evidence:** explicit D3D12/Vulkan/GL, injected native startup failures, unavailable
feature requirements and actual fallback asset/UI rendering.

## 6. NR-04 - Transactional resources and CPU identity

Ownership: AF-01/04 and P41-02/04/05/11.

Keep authored asset UUID, entity UUID and GPU slot/generation/device epoch distinct.
Persistent data references CPU assets or canonical builtin mesh IDs. It never
serializes NRI pointers, descriptor indices, Jolt IDs or a current device epoch.

Validate finite extents, dimensions, mip/layer/sample counts, formats and usage
combinations before narrowing to native types. A texture wrapper is usable only
when allocation, memory binding/commit and required views all succeeded.
Construct a complete candidate; failed replacement preserves the last valid
generation unless the device is lost. Clean every partial wrapper/view/allocation.

CPU decode/import and resource description must not require a device. Upload/cache
keys include stable source/import interpretation, linear/sRGB treatment, backend
and device generation. Share immutable GPU assets; keep mutable per-view targets
and per-draw constants separate.

**Evidence:** injected failure at each creation step, invalid formats/sizes,
recoverable replacement and actual texture/mesh pixels after source reload.

## 7. NR-05 - Recording and GPU resource leases

Ownership: P41-05/06/10 with EJ/PLG lifetime integration.

A generation can be released only after CPU recordings/UI draw references no
longer need it and every submitted GPU use completed. The last submitted fence
does not cover a new unsubmitted UI image recorded earlier in the current frame.
Acquire the recording lease before publishing the GUI texture token.

Retire the old texture/views/descriptors/GUI registration as one owned generation.
Resize, selection switch, panel close, scene destruction and failed replacement
all follow this contract. Native resource destructors must retain a valid device
owner; a numeric epoch/fence token alone does not own the device.

Plugin-defined callbacks, resource captures and destructors retain their module
generation through final recording/submission retirement. Invalidating the Scene
owner does not permit unloading code still needed to destroy deferred resources.

**Evidence:** record then close/resize before submit, submit then switch selection,
multi-preview pressure, descriptor reuse checks and real readbacks on both native APIs.

## 8. NR-06 - State tracking and submission commit

Ownership: P41-07/13.

Passes declare reads/writes, subresource ranges, attachment load/store/clear and
stage/access/layout requirements. Track first use and imported-resource state.
Do not assume a new image previously had ShaderResource state. NRI does not
automatically infer all required barriers for AxleEngine.

Recording has tentative state. Commit submitted state and completion tokens only
after QueueSubmit succeeds. Abandoned/failed recording does not mutate committed
GPU state. If submit succeeds but present later fails, keep the submitted work's
leases/completion requirements; presentation failure does not undo GPU execution.
An ambiguous failed submission stops normal reuse and retains affected owners
until a checked native completion/destruction policy resolves it. A missing success
token is not proof that partially accepted GPU work cannot reference the resources.

The initial graph is sequential, with one graphics queue and no aliasing/async
compute requirement. Native UI/external commands declare their import/export
states within that graph. Do not add an unrelated state tracker in each panel.

**Evidence:** first-use transitions, multi-pass reuse, abandoned recording,
failed submission, present failure after successful submit and validation output.

## 9. NR-07 - Frame, queue and presentation owner

Ownership: P41-08/09/10/13.

One coordinator owns acquire, uploads, view recording, UI recording, submit,
completion, present and retirement. ImGui is a consumer/recording adapter.
Choose a single NRI/native presentation implementation per surface; do not keep
two swapchains or two independent fence/allocator rings for one window.

A frame slot owns its command allocators, transient uploads, per-draw descriptor/
constant storage and completion record. Reuse only after actual GPU completion.
A correct initial single-flight configuration is acceptable; additional frames in
flight require the same ownership tests before removing synchronization.
Per-view mutable draw constants cannot share storage that a later view overwrites.

Do not remove every QueueWaitIdle call as a performance patch. Replace each with
a proven slot/resource completion boundary first; diagnostic/shutdown waits are
separate. Minimize/zero-sized surfaces do not allocate zero-sized targets, and
out-of-date surfaces follow a checked resize path.

**Evidence:** more frames than the configured ring, several previews plus main
scene, rapid minimize/resize/close, VSync changes and stable readbacks/validation.

## 10. NR-08 - RenderWorld and consumer isolation

Ownership: AF-03, EJ-10..14 and P41-12..18/25.

Extract on the owning thread initially from a completed transform/simulation
revision. RenderWorld contains immutable resolved geometry/material references,
world transforms, lights, camera and supported skin/selection data. It retains no
mutable registry/component pointers or callbacks into a destroyed scene.

Each private view owns its camera/options/targets/history and isolated preview
simulation. Material/VFX/assembly/thumbnail consumers use RenderService rather
than duplicating resource resolution, queue submission or a renderer-specific
entity-name guess. Picking results carry enough entity/frame/generation identity
to reject reused handles; advanced modern picking remains P41-24 work.

Physics debug receives the active edit/Play context explicitly. Vehicle emits
generic leased debug/VFX data through its plugin registration; no SceneRenderer
GetRuntimeVehicle dependency is reintroduced.

**Evidence:** edit/Play/private scene separation, shared mesh identity between
render/pick, destroyed entity/readback invalidation and independent preview pixels.

## 11. NR-09 - Editor UI and shipping Canvas submission

Ownership: P41-09/15..19 with AF-04 and PLG-22/23.

The Editor bridge may interoperate with native API handles where required, but
shares the active device/context, frame and retirement owner. Evaluate the pinned
NRI/UI facilities or current backend adapters against that contract; using another
library is not itself evidence that synchronization is correct. No second ImGui
implementation/context is linked into each Editor plugin DLL.

Runtime uses its own in-game UI adapter, without Editor panels/ImGui linkage.
Canvas geometry must be uploaded and drawn using valid vertex/index storage,
font atlas/sprite resources, clip/scissor, blending, DPI and declared color handling.
CPU DrawCalls statistics are not proof of GPU submission.

This is a required P41-19 integration detail in the architecture-first scope.
Preserve existing supported widget/layout behavior; a new UI authoring system,
new text-shaping feature set or a generalized plugin renderer is not required.
Test UI interaction and pixels in an actual cooked player, not only in the
Editor's UICanvas ImGui renderer.

**Evidence:** real Runtime text/sprite/clip/blend output plus resize/input; no
AxleUI/ImGui/editor-plugin target or binary dependency in the shipping graph.

## 12. NR-10 - Shader, capability and artifact identity

Ownership: P41-11/20 with PLG-04/23.

Bind shader artifacts to source/includes, DXC revision/options, target API/entry/
profile, resource binding offsets/layout and relevant Engine/NRI ABI. Current
Vulkan binding shifts must be explicit and tested against reflection/layouts;
do not treat a DXIL and SPIR-V binary as interchangeable cache entries.

Report separate physical device features and renderer-implemented features.
Unsupported shadow/environment/skinning requests cannot be hidden by an adapter's
ray tracing/mesh-shader capability flags. Preserve supported material/VFX behavior
and last-good shader resources during reload.

DLL plugins import shared host render services and declare resource dependencies.
Static player/cooked metadata bind the same supported resource/shader set to a
matching receipt. A DLL/static linkage choice does not change asset identities
or permit duplicate device/cache ownership.

**Evidence:** shader layout/reflection checks, changed-source/options invalidation,
DX12/Vulkan pixels, malformed shader retention and exact exported artifact matching.

## 13. NR-11 - Failure and shutdown

Ownership: P41-03/06/09 plus EJ-07 and PLG-12.

Capture the initiating native error, selected backend/adapter/device epoch, operation
and validation output. Stop new submissions after device loss. Distinguish resource
creation failure, unsupported request, swapchain resize/out-of-date and lost device;
do not repeatedly create fences during a failed shutdown and obscure the first error.

Stop CPU producers/scene extraction and drain relevant tasks before releasing the
objects they touch. Retire valid submitted GPU work where possible while module/
device owners still exist. On device loss, follow a documented native destruction
path without indefinite waits for impossible completion or pretending every stale
resource became valid on a recreated device. Initial recovery is restart, not a
promise of live whole-engine GPU reconstruction.

**Evidence:** controlled result injection plus actual native teardown, compile/
panel/scene close during pending work, first-error logs and no success claim based
only on shutdown completing.

## 14. NR-12 - Physical acceptance and scope boundaries

Ownership: per-P41 evidence plus AF-05; full P41-28 remains separate.

| Check | Required proof |
| --- | --- |
| Readiness | Actual D3D12 and Vulkan device/queue plus injected required-interface failures |
| Resources | Native upload/draw/readback and transactional failed-create/replace behavior |
| Lifetime | Recorded/unsubmitted UI, GPU in-flight retirement and descriptor reuse |
| State/submission | First-use/multi-pass states, submission failure and present-after-submit failure |
| Frame/present | One owner, allocator reuse after completion, resize/minimize/multi-view stress |
| Consumers | Editor/main/private views/thumbnails/debug use the common service |
| Runtime UI | Cooked player shows and interacts with submitted Canvas output |
| Compatibility | Explicit and allowed fallback GL follow the documented resource path |
| Module graph | No EnTT policy in low-level RHI, no NRI in generic Scene, no Editor in Runtime |
| Plugin boundary | Shared render service/device identities and leased plugin debug/VFX resources |

CPU error injection proves error propagation only; physical tests prove the actual
API path. Null/dummy, software fallback or silent backend fallback is labeled and
does not satisfy a physical-backend requirement. Record machine/GPU/driver/API/
configuration, commands, exit codes, validation and pixel/log artifacts.

Architecture-first scope is P41-00..20/25/26. Advanced P41-21..24 and complete
P41-27..29 acceptance/rollout remain open. Benchmark foundation workloads honestly
without claiming all feature coverage or a guaranteed performance improvement.
Do not enable native defaults for unsupported workloads solely because a simple
triangle or one preview rendered successfully.

## 15. First implementation checkpoints and planning limits

Follow the [optimization roadmap](../ROADMAP.md): P41-00 captures the baseline
in OPT-01; P41-01 begins device readiness in OPT-04 after the safety/Core/data
phases, followed by P41-02/03 transactional creation/error behavior. Build one
real primitive/resource fixture before migrating every consumer. Preserve old
working paths until their common-service replacement and lifetime evidence exist.

The current task checks this document's links, source pin, rule mapping and
cross-plan consistency only. It performs no vendor update, NRI wrapper edit,
new native device/GPU/UI execution or performance measurement.
