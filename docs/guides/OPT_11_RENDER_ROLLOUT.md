# OPT-11 Rendering Selection and Readiness

Editor, standalone Runtime, private previews and thumbnails currently use the
synchronous render owner and persistent/imported scene targets. This is the
accepted product default. Offscreen worker and transient graph API verification
does not automatically enable those paths in a product.

## Startup flags

Both Editor and Runtime parse the same flags:

| Flag | Default | Current product result |
| --- | --- | --- |
| `--render-thread=sync` / `worker` | `sync` | Worker request reports deferred integration and selects synchronous fallback |
| `--render-parallel-recording=off` / `on` | `off` | Native recording request reports deferred integration and selects serial fallback |
| `--render-transient-aliasing=off` / `on` | `off` | Scene aliasing request reports deferred integration and retains persistent/imported targets |
| `--strict-render-features` | absent | Any requested deferred feature rejects startup before GPU initialization |

An invalid value or unknown `--render-*` flag rejects startup. These switches
are startup-only and do not edit authored scene/project data. They are independent
of `--rhi=dx12|vulkan|gl` and `--strict-rhi`, which select the backend/fallback
policy. For example:

```text
AxleEditor --empty-project --rhi=vulkan --strict-rhi --render-thread=sync
AxleRuntime --project=Project.axleproj --rhi=dx12 --render-thread=worker
AxleRuntime --project=Project.axleproj --render-thread=worker --strict-render-features
```

The second command explicitly reports synchronous fallback; the third exits with
the deferred-feature reason. The log prints `RENDER_PRODUCT_POLICY` with the
selected mode and fallback status. Editor hardware rendering settings also show
the actual product owner/recording/allocation modes. Hardware/API capability bits
are separate from these product choices.

## API scope

The native offscreen RenderWorker API has independent owner/device epochs,
bounded request/result storage, timestamps and an optional `CaptureRgba8` request.
Capture supports worker-owned main views with at most 16 MiB of RGBA8 CPU output
and at most 64 MiB of native readback storage. Returned image bytes survive worker
shutdown. Only explicit capture adds export/readback waits; existing frame-slot
retirement waits remain. Timings are CPU steady-clock wall times, not GPU timings.
GL context/resource transfer to this worker is unsupported.

The transient graph API supports the proven C11-05 single-queue alias classes
after an explicit per-epoch native probe. Imported UI/history/output generations
remain external. A readback fixture's backing-request reduction is not a product
VRAM measurement. Independent primary batch submission is available as an RHI
foundation; native recording on job workers and full hybrid are deferred.

## Remaining product integration

Before a future change enables a deferred flag:

1. Route the real product snapshot/resize/VSync/reconfigure/stop commands through
   one bounded owner protocol. Move UI, presentation, resource creation and
   teardown consistently; the offscreen worker cannot attach to an already
   initialized UI-owned device. Audit every CPU/native/GL snapshot reference.
2. For native parallel recording, implement isolated recording authority and
   resource-state journals per worker, then owner-controlled merge/submit with
   actual measured benefit. CPU preparation or multiple owner-recorded primaries
   alone does not prove parallel native recording.
3. For scene aliasing, migrate real graph-owned scratch lifetimes while preserving
   external UI/history/output generations. Broader formats/use classes need their
   own alias barrier, parity, validation and memory proof.
4. Repeat product/backend/quality/lifecycle/latency gates and update the shared
   readiness decision before changing defaults. Native GPU completion and CPU
   task completion remain separate retirement domains.

The [C11-07 plan](../design/OPT_11_C11_07_ACCEPTANCE_PLAN.md) owns the current
acceptance scope; the final report records measured outcomes and deferred rows.
No later feature implementation is released by this guide.
