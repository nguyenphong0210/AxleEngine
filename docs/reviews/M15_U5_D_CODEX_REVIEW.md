# M15-U5-D independent Codex review

Reviewed 2026-11-17. Scope: S5.4, M15-U5-21.1 through M15-U5-26.4.
Reviewed Gemini delivery `fb699f21` against accepted U5-C baseline `f4f4e94a`,
replying to `CG-20261117T124813102206Z-GEMINI-0f71aa`.
Gemini's [self-verification report](M15_U5_D_REVIEW.md) remains historical reported
evidence. This report records Codex inspection, repairs and independent execution.

**Verdict: automated U5-D scope accepted after repairs.** Interactive Editor visuals for
U5-21.3, U5-21.4 and U5-26.1 remain user-deferred and NOT RUN. This review does
not activate U5-E or close the full U5 visual acceptance gate.

## Findings and repairs

| ID | Severity | Trigger, original behavior and correction |
| --- | --- | --- |
| U5D-R1 | P1 | A retained generation was excluded from its own replacement's admission calculation even while both resident and candidate cubemaps existed. Budget failures also suppressed unchanged inputs permanently, and cache diagnostics substituted the default 64 MiB for the actual scene budget. Admission now counts the retained generation and the candidate once each, pins the retained generation, retries capacity failures, and reports the most recently applied settings budget or explicit override. Only deterministic invalid-settings failures suppress identical retries. |
| U5D-R2 | P1 | Removing a probe, disabling it, or switching to authored data left pending jobs and cached generations alive. Pending slots stayed pinned indefinitely and could block later captures. The scheduler now treats the rendered world's dynamic-probe list as authoritative and retires absent jobs, residents, requests, invalidation/suppression records and diagnostics. Explicit invalidation also clears the canceled job's pending flag/progress. Capture-filtered worlds preserve scene instance and lifetime identity. |
| U5D-R3 | P2 | With more automatic probes than resident cache capacity, background capture could evict the reflection selected for the current main view, causing fallback and repeated recapture. Admission now protects the selected resident and retained generations of active jobs. Unselected, unpinned residents remain eligible for LRU eviction. A capacity-limited background request can fail without disturbing the selected reflection and remains retryable. |
| U5D-R4 | P2 | An explicit policy-override capture uses `InputStamp == 0`, but the new scheduler only checked resolution/position in that branch. An override with the same size and position but different clipping/content could be silently reused as the default capture. Zero-stamp receipts now invalidate default-policy reuse unconditionally. Manual probes still require an explicit request. |
| U5D-R5 | P1 | Public render entry points cleared `m_LastCompletion` before scheduling. Waiting on that reset receipt did not establish completion of the preceding main view before capture touched shared uploads/descriptors. Some capture waits were also ignored and discarded. Scheduled and explicit capture now consult the actual retained `m_ViewContext` completion before mutations. Failed waits defer capture, preserve the capture receipt and prevent scratch reuse. The scheduler's recursion guard covers synchronization/preparation as well as recording. |

Production changes are in `RHISceneRenderer.hpp/.cpp` and
`RenderContracts.cpp`. The native regressions in
`Test_M15_ReflectionCaptureRendering.cpp` exercise these production paths:

- Reject a cold probe under insufficient capacity, then automatically capture
  unchanged input after increasing the budget; verify the reported budget.
- Reject a partial rebuild when only the candidate fits; retain the previous
  generation. Permit it when both generations fit and verify both are counted.
- Remove a probe during a partial rebuild and switch a cold partial capture away
  from dynamic mode; require zero remaining jobs/bytes/diagnostics.
- Keep the selected probe stable across repeated background requests under a
  one-probe budget, with no repeated face rendering or fallback.
- Replace an explicit clip-policy override with a default automatic capture.
- Leave a main submission without a caller wait, inject a single `WaitFence`
  failure, require zero capture allocation/publication, then recover on the next
  render. Main rendering also recovers after the single failed wait.

The budget-recovery regression was first run against the delivered production
code and failed on Vulkan Debug at the automatic retry assertion, exit 1;
`build/Codex-U5D-baseline-build.log` and
`build/Codex-U5D-baseline-GPU.log` retain that reproduction.

Two existing fixtures were adapted to authoritative probe membership without
removing assertions: U5-B keeps the captured probe live in Manual mode while
checking main TAA progression; U5-C checks both prefilter rollback boundaries
before rendering the unrelated probe-free roughness sweep. The sweep still uses
the original retained cubemap and preserves all HDR/roughness assertions.

A separate-context, read-only reviewer inspected the corrections and affected
callers, then rechecked synchronization, override receipts and fixture changes.
No remaining P1/P2 source blocker was found. The runtime results below are from
the primary Codex reviewer, not delegated or copied from Gemini.

## Executed verification

Built the final source in Debug and Release with:

```text
cmake --build build --config C --target AxleRenderUnitTests AxleEnvironmentRenderingTests AxleEnvironmentBakeTests AxleRHIResourceViewTests AxleEditor --parallel 6
```

`C` is `Debug` or `Release`. Both full builds exited 0. After the fixture ordering
and line-ending corrections, the rendering target was rebuilt incrementally in
both configurations. Build logs are `build/Codex-U5D-{Debug,Release}-build*.log`
and `build/Codex-U5D-{Debug,Release}-rebuild*.log`.

| Final command/check | Debug | Release |
| --- | --- | --- |
| `AxleRenderUnitTests.exe --m15-reflection-capture-contract` | exit 0 | exit 0 |
| `AxleRenderUnitTests.exe --lighting-services` | exit 0 | exit 0 |
| `AxleRenderUnitTests.exe` (full) | exit 0 | exit 0 |
| `AxleEnvironmentBakeTests.exe` | exit 0 | exit 0 |
| `ctest --test-dir build -C C -R "M15ReflectionCaptureContract\|M15EnvironmentContract" --output-on-failure` | exit 0, 2/2 | exit 0, 2/2 |
| `AxleRHIResourceViewTests.exe dx12 storage` | exit 0 | exit 0 |
| `AxleRHIResourceViewTests.exe vulkan storage` | exit 0 | exit 0 |
| `AxleRHIResourceViewTests.exe dx12 targets` | exit 0 | exit 0 |
| `AxleRHIResourceViewTests.exe vulkan targets` | exit 0 | exit 0 |
| `AxleEnvironmentRenderingTests.exe dx12 <artifact-dir>` | exit 0 | exit 0 |
| `AxleEnvironmentRenderingTests.exe vulkan <artifact-dir>` | exit 0 | exit 0 |
| `AxleEditor.exe --verify-empty-startup --rhi=dx12 --no-fallback` | exit 0, 5 UI frames | exit 0, 5 UI frames |
| `AxleEditor.exe --verify-empty-startup --rhi=vulkan --no-fallback` | exit 0, 5 UI frames | exit 0, 5 UI frames |

All four native rendering runs cover U4/U5 regressions, U5D-R1 through R5,
prefilter rollback at both command boundaries, retained HDR texels, and real
device shutdown/reinitialization. They assert zero RHI validation errors. The
strict Editor runs report the requested actual backend and `fallback=none`.
Unlike the historical U5-C/Gemini U5-D attempts, DX12 presentation initialization
succeeds in this final matrix; no fallback result is counted as DX12 evidence.

`python scripts/check_architecture.py` exits 0 (613 source files, 25 CMake files),
`python scripts/agent_exchange.py validate` exits 0, and `git diff --check` exits 0.
Exact per-check arguments, exit codes and log paths are retained in
`build/Codex-U5D-results.json`. Native artifacts are under
`build/Tests/EnvironmentRendering/Codex-U5D-{Debug,Release}-{dx12,vulkan}`.

## Acceptance boundaries

- Partial jobs restart when tracked input changes. At two faces per render they
  require three compatible renders to publish. Continuous scene/animation edits
  can keep a cold probe pending or retain an increasingly stale last-good
  generation. S5.4 bounds scheduled work; it does not guarantee progress under
  continuous mutation or universally freeze mutable native material/texture
  inputs. No such progress/snapshot guarantee is claimed here.
- Cache bytes estimate the owned raw plus prefiltered cubemap texel payload,
  including staged candidates and retained residents. They exclude driver
  allocation overhead, shared scratch targets and generations held separately by
  external owners. Reducing a budget does not forcibly invalidate last-good
  generations; new admission can fail until enough unpinned capacity is available.
- Budget accounting requires old plus new capacity for a rebuild. Keeping the old
  reflection on a rejected rebuild is intentional and reported as failed/stale.
- CPU/native rendering tests and empty Editor startup are distinct from inspecting
  the actual fixtures in the Editor. U5-21.3, U5-21.4 and U5-26.1 remain unchecked
  until the consolidated interactive visual verification pass.
