# OPT-11 C11-07 Consolidated Acceptance and Rollout Decision

## Decision

**C11-07 is delivered as a qualified acceptance/rollout decision. OPT-11 is not
claimed fully deployed in products.** The synchronous product path and tested
foundation APIs are accepted within the evidence below. Product asynchronous
ownership, native recording on job workers and scene transient aliasing remain
**DEFERRED, default OFF**. Their implementation/readiness is separate from a
hardware capability or a successful offscreen probe.

Baseline `ebc3a79`; tested production source `119123cd813f3c0eec9fad78615544e0ac4044c2`.
Supplemental native Editor restart verification is in `2853e5d`, with diagnostic
preservation in `a389b26`; those commits change only the verification harness.
User released C11-07 to Codex. Full hybrid/NRI fork, vendor upgrades and OPT-12
were not released and were not performed. No push.

## Delivered source and real callers

- `d9f28e3`: execution/measurement contract and task release.
- `002cfed`: one product feature parser/readiness policy, used by both actual
  Editor and Runtime entry points. Explicit fallback reasons, strict early
  rejection and Editor settings diagnostics. Defaults preserve synchronous
  rendering and persistent/imported targets.
- `0869815`: bounded explicit RGBA8 CPU export, admission/dequeue/render/export/
  publication timestamps, CPU output surviving native owner shutdown, frozen
  serial/worker image and latency fixture.
- `119123c`: Runtime/preview consumer assertions, real-product runner, user guide
  and review refinements. No per-view policy-string copying; symmetric export
  warmup; image/JSON artifact write checks.
- `2853e5d`, `a389b26`: execute real native Editor restart-candidate modes
  with API validation, and preserve existing crash reports during deliberate faults.

Editor/Runtime do not secretly start RenderWorker or invoke the transient graph
allocator. Their requests for those deferred integrations fall back explicitly
or reject in strict-feature mode before GPU initialization. Renderer ownership
checks, native feature capabilities and product readiness are distinct.
The [selection guide](../guides/OPT_11_RENDER_ROLLOUT.md) documents exact flags,
limits and the integration work still required.

## Feature/backend matrix

| Feature | D3D12 | Vulkan | OpenGL | Product decision |
| --- | --- | --- | --- | --- |
| Synchronous owner, scene/UI/presentation | PASS | PASS | PASS compatibility | ON, accepted default |
| Bounded snapshot queue / CPU payload lifetime | CPU/stress PASS | CPU/stress PASS | CPU container only; no GL context transfer | Foundation accepted |
| Native offscreen owner / CPU-source snapshot / RGBA8 export | PASS | PASS | UNSUPPORTED | API scope accepted; product asynchronous ownership DEFERRED |
| Independent owner-recorded primary batches | Native state suite PASS | Native state suite PASS | UNSUPPORTED | RHI foundation accepted; not parallel native recording |
| Recording native commands on job workers | DEFERRED | DEFERRED | UNSUPPORTED | OFF; authority/state isolation and product benefit missing |
| Transient graph and conservative placed/alias classes | Native parity/retirement PASS | Native parity/retirement PASS | Native API unsupported; existing GL targets retained | API scope accepted; scene migration/aliasing OFF |
| Runtime/preview resize, output and ownership | Actual consumer cases PASS | Actual consumer cases PASS | Compatibility cases PASS | Accepted within finite consumer matrix |
| Native Editor restart candidate via test launcher | Normal/frame-fault/changed-plan PASS | Normal/frame-fault/changed-plan PASS | NOT RUN | Candidate startup/acknowledgement/teardown, not a live parent Editor UI restart |
| Foundation plugin/resource/window lifecycle stress | PASS | PASS | Supported compatibility cases PASS | Foundation evidence; manual Editor project/UI switching remains NOT RUN |

The offscreen worker evidence uses immutable builtin CPU-source snapshots, not
an assertion that arbitrary GL/native wrappers can be transferred between product
threads. Existing UI/history/final-output generations stay persistent/imported.
The missing product ownership/resource bridges remain required before enabling
their flags. Full hybrid remains a separately deferred design, not a prerequisite
for the safe synchronous default.

## Final executed verification

Hardware: NVIDIA GeForce RTX 3070, driver 610.88, Windows. OpenGL logs identify
NVIDIA Corporation. NRI remains pinned at
`6998e3fbbaed1b132f386a9d49743a2481003487`.

| Gate | Debug | Release |
| --- | --- | --- |
| `AxleAllTests`, `AxleEditor`, `AxleRuntime` full build | PASS | PASS |
| Full CTest, scene/plugin/asset/physics/render/RHI/architecture/stress | 91/91 PASS | 91/91 PASS |
| Serialized real-product runner | 47/47 PASS | 47/47 PASS |
| Additional native Editor restart candidates | 6/6 PASS | 6/6 PASS |
| Final serial/worker parity/measurement, D3D12 + Vulkan | 2/2 PASS | 2/2 PASS |
| Additional native alias and resource/state/retirement/frame regressions | 12/12 PASS | 12/12 PASS |

Full CTest takes 237.61s Debug and 198.33s Release. The final measurement executable
was additionally rebuilt after the JSON flush refinement before these final runs.
Native fixtures enable NRI and API validation and check zero validation errors
through teardown. The product runner requires expected exits/markers and rejects
observed NRI/GL API errors. Strict/malformed CLI cases intentionally require exit
1; positive cases and each overall runner command exit 0.

The 47 product cases per configuration include:

- 8 fail-closed CLI cases across the real Editor/Runtime executables;
- real Editor empty startup (five UI frames) and simulation at 30/60/144 rates
  for each backend;
- actual service, canvas, thumbnail and private Asset/Assembly/VFX/physics/surface
  preview consumers with readback assertions;
- Runtime loose/cooked Application/UI/presentation fixtures and the actual
  standalone executable consuming each generated project/package for eight frames;
- explicit startup-backend fallback with native creation fault injection.

After report review exposed a restart-coverage wording gap, a separate run used
the real SDK plugin/project generated by each configuration's passing
`PluginSessionTests`. D3D12 and Vulkan each run normal native candidate startup
(five completed frames and acknowledgement), pre-acknowledgement presentation failure and
changed-plan rejection: 6/6 per configuration. The test launcher retains control
when a candidate exits 1. This does not prove an already running parent Editor
continues rendering or a user-driven project switch. Native validation is enabled;
the harness archives expected frame-failure diagnostics and restores existing
reports. Default self-child `PluginRestartTests` was also rebuilt/rerun (1/1 each).
The presentation fault is injected after four completed frames, before the
candidate can acknowledge readiness. The final native rerun also retains an
existing sentinel crash report's bytes/mtime across deliberate faults and archives
their diagnostics, exercising the added preservation helper.

Existing build/export/plugin SDK workflows also pass in the full CTest run.
This is actual consumer/executable evidence, not a source-only caller claim.
Existing D3D12 options22 feature-query and MSVC LNK4098 warnings remain disclosed;
they are not silently relabeled as validation errors or skipped tests.

## Frozen measurement and image evidence

Workload: one immutable builtin cube, red emissive material, fixed camera,
64x64 RGBA8, no sky/shadows/temporal effects, two job threads. Eight warmups,
64 samples, nearest-rank p50/p95/p99. Ordinary worker measurement saturates a
bounded capacity-three queue. Image exports run separately with one outstanding
request; serial and worker export distributions both exclude eight warmups.

All 72 worker captures in each backend/configuration match their serial reference
in **every RGBA byte**, including after the authored scene changes to green and
after worker/device shutdown. Geometry/color checks reject matching empty output.
Each measurement process reconciles 144 accepted results and two intentional
invalid-export rejections. Ordinary results carry no implicit image export.

Final p95 CPU wall measurements, milliseconds:

| Configuration/backend | Serial render | Worker render | Worker queue age | Worker metadata roundtrip |
| --- | ---: | ---: | ---: | ---: |
| Debug D3D12 | 19.307 | 6.353 | 17.694 | 24.020 |
| Debug Vulkan | 3.532 | 2.666 | 5.134 | 7.648 |
| Release D3D12 | 17.869 | 17.044 | 32.188 | 47.688 |
| Release Vulkan | 3.174 | 2.893 | 4.687 | 6.735 |

These are same-source serial/worker API observations, **not before/after OPT-11
product FPS measurements**. They include existing frame-slot/queue retirement,
validation and scheduling costs. Metadata roundtrip ends at producer polling,
not GPU completion, presentation or input-to-photon. Export timings include the
explicit readback waits. No GPU timestamp duration or universal backend ranking
is inferred. The small fixture and saturated queue do not justify a production
worker default; representative scene/end-to-end latency gates remain deferred.

The [raw measurements and image hashes](OPT_11_C11_07_MEASUREMENTS.json) preserve
p50/p95/p99 and export timing rows. Eight linked PNGs under
`OPT_11_C11_07_ARTIFACTS/` are lossless format conversions of actual GPU readback
PPMs; RGBA equality is checked before RGB visualization export. Example:
[Release D3D12 serial](OPT_11_C11_07_ARTIFACTS/Release_dx12_serial.png),
[matching worker](OPT_11_C11_07_ARTIFACTS/Release_dx12_worker.png).

Final alias regression backing requests are 262144 -> 131072 bytes on D3D12 and
1536 -> 768 on Vulkan (50% within the tiny proven fixture). These exclude imported
targets, driver overhead and whole-device VRAM. No product scene VRAM savings are
claimed or used to enable the scene-alias flag.

## Consolidated review

Separate-context reviewer `c11_07_final_review` inspected the complete source,
actual callers, capture bounds/ownership/failure handling, cancellation safety,
measurement method and product runner. No P1/P2 findings. P3 refinements for
per-view report allocation, symmetric export warmup and artifact stream checks
were fixed and rechecked. Final source/guide recheck on `119123c`: no findings;
whitespace PASS. The reviewer ran no executables/native/window checks. Native,
product and CTest evidence above is implementer-executed, not an independent
native rerun or Gemini approval.

## Reproduction and limits

```powershell
cmake -S . -B build -DAXLE_ENABLE_OPT11_LIFETIME_NATIVE_TESTS=ON
cmake --build build --config CONFIG --target AxleAllTests AxleEditor AxleRuntime --parallel 6
ctest --test-dir build -C CONFIG --output-on-failure
python scripts/run_opt11_product_acceptance.py --config CONFIG
python scripts/run_opt11_product_acceptance.py --config CONFIG --restart-only --output build/c11_07_restart_products/CONFIG
build/Tests/CONFIG/AxleRenderAcceptanceTests.exe dx12 build/c11_07_measurements/CONFIG/dx12
build/Tests/CONFIG/AxleRenderAcceptanceTests.exe vulkan build/c11_07_measurements/CONFIG/vulkan
```

Use Debug and Release separately. Additional regressions invoke
`AxleTransientGraphTests.exe` with each backend's `_alias` and
`_alias_fail_submit`, then `AxleRHIResourceTests`, `AxleRHIStateTests`,
`AxleRHIRetirementTests`, `AxleRHIFrameTests` with `dx12` and `vulkan`.

Logs/manifests: `build/c11_07_final_{debug,release}_build.log`,
`build/c11_07_final_{Debug,Release}_ctest.log`, configuration-specific
`c11_07_final_CONFIG_LastTest.log`, `build/c11_07_products/CONFIG/results.json`,
`build/c11_07_products_CONFIG.log`, `build/c11_07_measurements/CONFIG/BACKEND/`,
and `build/c11_07_final_CONFIG_*` native regression logs.
Supplemental restart results/builds: `build/c11_07_restart_products/CONFIG/results.json`,
`build/c11_07_restart_CONFIG_build.log`, `build/c11_07_restart_CONFIG_selftest.log`.
The restart-only runner consumes the saved full CTest log to locate its SDK
fixture. If reproducing outside this wrapper, save `LastTest.log` to
`build/c11_07_final_CONFIG_LastTest.log` after each configuration or pass
`--session-log` explicitly before the next configuration overwrites it.

Architecture: PASS (538 source files, 25 CMake files). Exchange schema/references,
whitespace and document links are validated at handoff.

NOT RUN / not accepted as product rollout: other GPUs/vendors/drivers, physical
GPU reset, sanitizer/ThreadSanitizer, unbounded soak, representative game-scene
performance, GPU timestamps, end-to-end display latency, arbitrary GL resource
transfer to a native worker and manual user-driven project/UI acceptance. Loss
is simulated at controlled native boundaries. The finite passing matrix is not
a proof covering every possible schedule or driver failure.

The [rollout guide](../guides/OPT_11_RENDER_ROLLOUT.md#remaining-product-integration)
lists the remaining ownership/recording/scratch-lifetime integration. Those rows
remain DEFERRED and disabled; this decision does not silently waive them or mark
full OPT-11 production deployment accepted.
