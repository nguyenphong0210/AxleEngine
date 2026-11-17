# M15 U5-B independent Codex review

Delivery `f49ff61bb69278213998c02273e1cbcc5b930186`, based on `966049eb`;
reply to `CG-20261117T081841500353Z-GEMINI-657341`. Scope: U5-08.1 through
U5-15. Gemini's original report is historical self-verification. The findings
and executed checks below are Codex's independent review and repairs.

## Verdict

**Automated U5-B accepted after corrections.** No known code blocker remains.
Interactive Editor captures remain **NOT RUN / user-deferred**; their PLAN
checkboxes stay open. This does not release U5-C or accept the whole U5.
One consolidated local Codex commit contains code, tests, documentation and
exchange. No push.

## Findings and corrections

### U5B-R1 - High - Nested rejection corrupted the active capture - FIXED

The recursion branch used the ordinary failure handler, which cleared the
outer `m_CaptureSession` and could redirect subsequent commands into the main
view. Checking only the active face also left resource allocation unguarded.
A scoped `m_CaptureInProgress` now covers the entire public operation, including
allocation, and returns a local rejected result without changing outer state.
Native callbacks attempt nested capture during both `CreateTexture` and
`BeginCommandBuffer`: rejection succeeds and the outer six-face transaction
still publishes valid radiance. Prior-main and inter-face completion failures
are checked before reuse of shared scratch/host-upload inputs.

### U5B-R2 - High - Automatic captures stayed stale or failed permanently - FIXED

The success cache considered only transform revision, resolution and position;
clip planes, capture shadow settings, lighting and material changes could reuse
old radiance. The failure cache suppressed every request at a failed revision,
even after correcting settings or clearing a transient GPU fault.

A shared input stamp now covers capture geometry/settings, relevant shadow
and sky controls, light values, material generations and custom properties,
native vertex/index generations, source identity, texture-file timestamps,
shader/dependency fingerprints and manual IBL handles. Captures can reuse an
unchanged request; corrected requests invalidate both success and failure
decisions. Only deterministic policy-validation failures are suppressed.
Transient native failures retry on the next request, retaining last-good until
a replacement succeeds. Overrides do not masquerade as default-policy captures.

Native automatic-frame tests verify unchanged-request reuse, material edits
with new green face radiance, near/far/shadow-policy edits at unchanged revision,
a far-plane edit that actually clips the wall, invalid-resolution last-good
retention, corrected-setting retry, and transient native-fault recovery without
a revision bump. The mirror fixture now retains its actual emissive walls and
excludes the mirror by probe entity identity; it no longer depends on a stale
capture from a different world.

In-place native texture-content edits must continue to advance the existing
material `ContentRevision` contract; mutating GPU bytes without a revision is
outside the publication contract.

### U5B-R3 - Medium - Valid clip ranges were silently widened - FIXED

Policy accepted `far > near`, but camera construction substituted `near+100`
for gaps at most 0.01. The camera now preserves every validated far plane.
A CPU assertion preserves the exact 0.1/0.105 range.

### U5B-R4 - Medium - Anonymous key conversion was unsafe and lossy - FIXED

Anonymous positions entered `llround(position*1000)` before finite validation,
exposing nonfinite/large inputs to invalid integer conversion and collapsing
distinct nearby positions. Keys now encode float bits without conversion.
CPU checks distinguish 0.0001 from 0.0002 and safely construct a key for an
infinite input, which capture subsequently rejects through finite validation.

### U5B-R5 - High - Manual inputs could feed published captures back into capture - FIXED

Clearing scene probes isolated automatic reflection selection but left manual
IBL and native/custom material inputs able to reference published cubemaps.
Capture now recognizes candidate/published resource generations and supplies
typed neutral inputs instead. Provenance uses immutable epoch/slot/generation
handles, including superseded captures whose original wrappers have expired.
It holds no owning leases and prunes dead registry handles when publishing.
Clearing publications preserves recognition of live external aliases.

The native regression retains a superseded green cubemap with
`RetainGeneration()`, releases the original publication wrappers and asserts
their expiration. Feeding that alias through manual IBL during capture matches
a nonzero neutral-IBL reference within 0.003, rather than capturing green feedback.
Custom/material feedback paths were also source-reviewed against this guard.

### U5B-R6 - Medium - Capture changed the exposed main environment publication - FIXED

Capture used the main environment preparation cache, so a direct capture with
a different/disabled sky could change `GetEnvironment().Ready()` afterward.
The operation now preserves/restores the environment state and prepared manual
IBL flag, and invalidates the bound-IBL identity so the next view refreshes its
descriptors. A native sentinel asserts the main environment generation remains
the same after capture.

Main ViewContext/TAA, frame index, last completion and exposed frame/target state
remain isolated. GPU draw, light and shadow scratch is shared and is prepared
again by the next main frame; this is not a promise to preserve internal scratch
buffer contents. Native main-frame continuation and TAA progression still pass.

## Independently executed verification

From `D:/WorkSpace/AxleEngine`; `<Config>` means Debug and Release,
`<backend>` means dx12 and vulkan. All following final commands exit **0**:

```text
cmake --build build --config <Config> --target AxleRenderUnitTests AxleEnvironmentRenderingTests AxleEnvironmentBakeTests AxleRHIResourceViewTests AxleEditor --parallel 8
build/Tests/<Config>/AxleRenderUnitTests.exe --m15-reflection-capture-contract
build/Tests/<Config>/AxleRenderUnitTests.exe
ctest --test-dir build -C <Config> -R ^(M15ReflectionCaptureContract|EnvironmentBakeTests)$ --output-on-failure
build/Tests/<Config>/AxleEnvironmentRenderingTests.exe <backend> build/Tests/EnvironmentRendering/Codex-U5B-<Config>-<backend>
build/Tests/<Config>/AxleRHIResourceViewTests.exe <backend>
build/Editor/<Config>/AxleEditor.exe --verify-empty-startup --rhi=<backend>
python scripts/check_architecture.py
python scripts/agent_exchange.py validate
git diff --check
```

GPU fixtures run serially on hardware RTX 3070, after rebuilding the final
sources. Four native configuration/backend runs pass all U5-B and prior
U4/U5-A regressions with **zero native validation errors**. Both focused/full
unit configurations and both two-test CTest runs pass. The two environment-bake
suites are executed by CTest. Resource-view checks pass on both backends in
both configurations. Architecture: **613 files / 25 CMake files**.
Existing LNK4098 runtime-library warnings remain unrelated.

Evidence paths:

| Check | Path |
| --- | --- |
| Builds | `build/Codex-U5B-build-<Config>.log` |
| Focused/full CPU | `build/Codex-U5B-<Config>-{focused,full}.log` |
| Capture/bake CTest | `build/Codex-U5B-<Config>-ctest.log` |
| Native GPU | `build/Codex-U5B-<Config>-<backend>.log` |
| Resource-view | `build/Codex-U5B-<Config>-resource-vulkan.log` and `build/Codex-U5B-<Config>-resource-dx12-final.log` |
| Editor startup | `build/Codex-U5B-Editor-<Config>-<backend>.log` |
| Native face strip | `build/Tests/EnvironmentRendering/Codex-U5B-<Config>-<backend>/M15_U5B_faces_strip.ppm` |

Native controls retain the six distinct face colors, off-axis orientation and
HDR checks: +X marker region TR=(0.84,1.96,1.96), TL/BR=(1.92,0,0); SSR probe
reflection with/without capture=(43,0,0)/(0,0,0); directional-shadow receiver
unshadowed=(1.94751,1.94751,1.94751), shadowed=(0,0,0). The native strip was
visually inspected separately; it is not an interactive Editor capture.

**Recovered initialization failure:** the initial default DX12 resource-view
invocation could not obtain NRI `SwapChainInterface`
(`build/Codex-U5B-Debug-resource.log`, exit 1). After final builds and serial
GPU/Editor checks, explicit DX12 resource-view reruns pass in both configurations
(`resource-dx12-final.log`, exit 0). The initial failed attempt is retained as
historical execution evidence; it does not replace or invalidate the final passes.
All four Editor startup logs identify the requested actual backend.

Interactive captures for U5-08.1/08.2/10.1-10.4/11.2/13.1/13.2/14 remain
user-deferred, along with earlier open visual gates. Empty-startup checks do
not replace those captures. No visual acceptance is invented.

The protocol-required separate-context source reviewer rechecked recursion,
cache invalidation, camera/key safety, capture provenance/lifetimes, environment
restoration and affected tests. No blocking finding remains. Its review was
source-only; runtime results above were executed by Codex in this session.
