# OPT-11 C11-06 Lifetime Stress Delivery

Baseline: `7b9db0c`. User authorized Codex to implement the complete C11-06
package, sequentially, with local commits. C11-07 rollout, full hybrid, vendor
upgrades and scene migration remain outside this release.

## Architecture and implemented corrections

Snapshot replacement, PopLatest discard and explicit drain detach retired values
while accounting is locked, then destroy payloads outside the queue mutex.
Queue destruction stops admission/clears visible counts and clears its existing
storage outside the lock without constructing retirement scratch. Reentrant
payload queries remain valid until all payloads are destroyed.

The worker retains a CPU `LeaseAnchor`, separately from native submission leases
such as `RenderDebugLine::OwnerLease`. Accepted frames own prebuilt normal and
cancellation result objects. Start reserves the bounded pending/result pointer
arrays; admission allocates payload/result boxes before acceptance. Stop and
exception cancellation detach the existing queue by pointer and publish terminal
result pointers into reserved capacity. No new scratch or result strings are
constructed during CPU cancellation. This also avoids MSVC Debug iterator-proxy
allocation in otherwise noexcept vector/string construction and moves.

Queued payload cleanup, active CPU payload cleanup, synchronous service teardown
and GPU drain all execute outside the worker mutex. Every accepted request has
one terminal result; unpolled results prevent restart. Start/Stop/synchronous
fallback are serialized lifecycle operations; producer Submit, Poll and diagnostic
queries may overlap Stop. A recoverable exception in the native thread becomes a
failure result/state, cancels pending work and performs owner-thread teardown.
Both lazy singleton acquisitions are inside that exception boundary. First-error
diagnostics survive later failures. Unresolved native shutdown retains ownership
and reports Failed rather than advertising a clean restart.

This does not promise recovery from sustained whole-process OOM, arbitrary
throwing destructors or OS/driver stalls. Native waits retain the existing bounded
RHI policy; no per-frame production WaitIdle was introduced.

## Stress matrix

| Fixture | Forced overlap and checked outcome |
| --- | --- |
| CPU snapshot | 16 rounds, 4 producers x 1200 tickets: 76800 tickets, protected pressure, Pop/PopLatest, Stop race, exact admission/drop/cancel/release accounting |
| Reentrant cleanup | Destructors query the production queue/worker APIs during replacement, drain, cancellation and shutdown |
| Teardown allocation guard | Test allocator armed across populated queue and stopped-worker destruction; no retirement allocation occurs |
| Worker stop/restart | Six gated cycles per backend, four accepted requests, producer pressure during Stop, exactly one result per ID, native restart only after result drain |
| Pending cancellation allocation guard | Stop caller's allocator remains armed across cancellation/CPU payload release/join; results publish without allocation |
| Worker exception | Two gated cycles per backend; fail the throwing allocation for a 16-mesh snapshot copy on the actual render owner; active request ID 2 gets one Failed result and prior error remains first |
| Synchronous fallback | Three cycles per backend; real GPU owner lease queries worker during Stop drain; no lock inversion and full owner teardown |
| Real DLL CPU work | 32 load/stop/unload cycles; a DLL callback actually enqueues a protected snapshot while a CPU task is gated; stop revokes admission and final unload waits for both task and ticket |
| Real DLL GPU retention | 32 cycles per backend; CPU task/ticket completion cannot unload GPU-pinned code, delayed fence observation blocks mapped reuse, exact readback and DLL cleanup execute once before owner-thread FreeLibrary |
| Native transient normal | Three epochs x 1024 frames = 3072 frames per backend/configuration; maximum three outstanding readback batches; committed, placed and proven alias modes |
| Native terminal faults | 192 frames/three epochs per backend for submit failure and simulated loss, then poison/quarantine, bounded shutdown and clean epoch restart |
| Native mixed failures | Size changes, actual RTV creation failures, callback throws/abandon, alias-barrier faults, import reload after recording starts, exact all-pixel/all-word comparisons and stale handle/token rejection |
| Window surface lifecycle | Six real presenter/device/window cycles per backend, resize, iconify/restore, VSync changes, out-of-date acquire/present, abandoned callback and failed submit |

Native transient allocation/command wrappers are destroyed before the delayed
completion check. Every submitted marker and retired resource handle is checked;
markers must release exactly once and stale generations must remain inaccessible.
The fixed PRNG starts at `0xC110600`. Native alias proof is repeated after each
new device epoch, rather than carrying a capability flag across restart.

Device loss is simulated at QueueSubmit before the native call, after all prior
work has been checked/drained. This is state/recovery evidence, not a physical
GPU reset. The real window test uses GLFW/NRI windows and presentation, not a
mock surface. These tests do not establish scene FPS or whole-device VRAM gains.

## Consolidated review and repairs

Separate-context reviewer: `c11_06_final_review`. Full released diff inspected,
followed by focused correction rechecks. Findings:

- **C11-06-R1, P2:** synchronous fallback Stop drained GPU owners while holding
  the worker mutex. Fixed by unlocked native teardown; real owner-lease regression.
- **C11-06-R2, P2:** retirement scratch allocated in destructors/cancellation;
  partial cancellation could publish a duplicate result after allocation failure.
  Fixed by allocation-free queue destruction, detached pointer-owned worker
  storage and prebuilt boxed results. One-shot tests additionally exposed MSVC
  Debug vector/string proxy allocation; final tests cover that actual behavior.
- **C11-06-R3, P2:** stack DLL stop observer could dangle after an early fixture
  failure while GPU code stayed pinned. Fixed by heap observer retained through
  the module's `LoadRequest::ServiceLifetime`.
- **C11-06-R4:** lazy service/device singleton acquisition preceded the thread
  exception boundary. Both acquisitions and guarded cleanup are now protected.

The final source recheck reports no remaining P1/P2 findings. Reviewer independently
ran CPU/Plugin Debug and Release checks, and reran Debug CPU/Plugin after the final
boxed-result refinement (2/2 PASS per executed selection), plus whitespace PASS.
It did not independently rebuild/run the native/window matrix. Native results
below are implementer-executed evidence; no Gemini review response is claimed.

The initial post-review Debug matrix failed the new allocation guards/timeouts
and missed an unbuilt static-plugin test target. Those results are not accepted
evidence. The corresponding defects/build prerequisite were corrected; the final
matrix is rebuilt and rerun. Historical failure log:
`build/c11_06_before_repairs_Debug_ctest.log`.

## Reproduction and final evidence

Configure native registration (off by default on other machines):

```powershell
cmake -S . -B build -DAXLE_ENABLE_OPT11_LIFETIME_NATIVE_TESTS=ON
```

For Debug and Release, build:

```powershell
cmake --build build --config CONFIG --target AxleRenderLifetimeStressTests AxleRenderUnitTests AxleTransientGraphTests AxleRHIResourceTests AxleRHIStateTests AxleRHIRetirementTests AxleRHIFrameTests AxleRHIPresentationTests AxlePluginRuntimeTests AxleStaticPluginLifetimeTests --parallel 6
ctest --test-dir build -C CONFIG -R '^(RenderLifetime.*|RenderUnitTests|RenderHarnessFailure|PluginRuntimeTests|StaticPluginLifetimeTests|TransientGraphPolicy|RHIResourcePolicy|RHIStatePolicy|RHIRetirementPolicy)$' --output-on-failure
```

Native cases use RUN_SERIAL and 180-second process timeouts; CPU queue and DLL
cases have separate finite timeouts. All are fail-closed when GPU, validation,
DLL or window prerequisites are unavailable. Unknown stress mode is an error.

Final production/test source: `dc9b90c`. All final commands exit 0.

| Final gate | Debug | Release |
| --- | --- | --- |
| Ten-target build above | PASS | PASS |
| Selected CTest, including 16 native/window stress cases | 26/26 PASS | 26/26 PASS |
| Existing native transient alias normal/submit-failure regression, both backends | 4/4 PASS | 4/4 PASS |
| Existing native resource/state/retirement/frame regression, both backends | 8/8 PASS | 8/8 PASS |
| NRI/API validation errors through fixture teardown | 0 | 0 |

The final CTest runs take 130.25s Debug and 117.63s Release. All 3072-frame normal
transient runs and 192-frame terminal-fault runs complete; retirement markers
reconcile exactly. Across both configurations/backends/terminal modes this is
13824 successful native transient frames, excluding probe/reference/regression
work. The three-slot batching bound remains fixed throughout.

Regression commands: from `build/Tests/CONFIG/`, run `AxleTransientGraphTests.exe`
with `dx12_alias`, `vulkan_alias`, `dx12_alias_fail_submit`,
`vulkan_alias_fail_submit`; then each of `AxleRHIResourceTests.exe`,
`AxleRHIStateTests.exe`, `AxleRHIRetirementTests.exe`, `AxleRHIFrameTests.exe`
with `dx12` and `vulkan`.

Hardware: NVIDIA GeForce RTX 3070, NVIDIA driver 610.88, Windows. Pinned NRI:
`6998e3fbbaed1b132f386a9d49743a2481003487`. Both backends run with NRI/native API
validation enabled. Expected injected operation failures are distinct from
validation errors. Existing D3D12 options22 feature-query and MSVC LNK4098 library
warnings are disclosed; builds succeed and fixtures check zero validation errors.

Logs: `build/c11_06_final_{debug,release}_build.log`,
`build/c11_06_final_{Debug,Release}_ctest.log`,
`build/c11_06_final_CONFIG_AxleTransientGraphTests_MODE.log`,
`build/c11_06_final_CONFIG_AxleRHI{Resource,State,Retirement,Frame}Tests_BACKEND.log`.
CTest additionally records per-case native output in
`build/Testing/Temporary/LastTest.log` (most recent configuration only).

`python scripts/check_architecture.py`: PASS, 534 source files/25 CMake files.
`git diff --check` and exchange schema/reference validation: PASS.

Sequential local commits:

- `ba6c97e`: release and detailed stress execution plan.
- `4adbdc4`: C11-06.1 queue cleanup and concurrent snapshot stress.
- `57663fd`: C11-06.2 worker/real DLL lifecycle stress and initial repairs.
- `dc9b90c`: C11-06.3 native/window/long-run stress and final R1-R4 corrections.
- C11-06.4: this final evidence/index/roadmap/exchange handoff; no source changes.

Package status: implemented, final matrix verified, consolidated source review
and correction rechecks complete. Stop at C11-06; C11-07 is not released here.

## Boundaries

The new executor remains opt-in and current scene targets remain imported/
persistent. Supported alias classes remain the conservative C11-05 classes.
Native calls still have one declared owner; full hybrid/secondary/bundle execution
is neither enabled nor claimed by these worker tests.

Multi-vendor/other-driver runs, physical GPU reset, sanitizer/ThreadSanitizer and
unbounded-duration soak are NOT RUN. The deterministic finite stress matrix
does not prove that every possible scheduling or driver failure is absent.
Editor user-driven project switching and whole-product rollout remain C11-07
acceptance work; these are real production API/DLL/presentation fixtures.
