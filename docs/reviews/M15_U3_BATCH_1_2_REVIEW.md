# M15 U3 batches 1–2 — RHI ownership and graph

The user released U3-03.3 through U3-08 in sequence. Each completed checkpoint is committed independently. Evidence here is self-verification; Editor visual gates are recorded separately.

## U3-03.3 — Constant update helpers

**PASS (self-review).** All `RHIBuffer::UpdateData` call sites in the scene renderer check the returned bool before binding or drawing. The native RenderGraph fixture now initializes a host-upload constant buffer, injects a `MapBuffer` failure, checks that the update returns false while the original generation and bytes remain, then retries and checks the complete replacement bytes.

Build commands (both exit 0):

```text
cmake --build build --config Debug --target AxleRenderGraphTests --parallel 4
cmake --build build --config Release --target AxleRenderGraphTests --parallel 4
```

Run commands (each exit 0):

```text
build/Tests/Debug/AxleRenderGraphTests.exe dx12
build/Tests/Debug/AxleRenderGraphTests.exe vulkan
build/Tests/Release/AxleRenderGraphTests.exe dx12
build/Tests/Release/AxleRenderGraphTests.exe vulkan
```

Logs: `build/agent-runs/M15-U3-batches-1-2/03.3-build-*.log` and `03.3-*-*.log`. All four native logs end `RenderGraph authority PASS`. The failure is injected before native map; a GPU/driver failure after a void native call cannot be observed by this helper. No Editor image is required for this checkpoint.

## U3-04 — Bound descriptor immutability

**PASS (self-review).** `RHIDescriptorPool::UpdateTextureRange` already rejects a pinned descriptor set, and `RHIResourceService::ReplaceDescriptorRange` repeats the guard. A new native fixture binds a real set, tries to replace its texture while the command is recording, executable and submitted, checks the original backing dependency remains published, then waits for completion and verifies that replacement and restoration work. No native update is attempted on rejected paths.

The same Debug/Release build commands above were run after this fixture was added; both exit 0. The same four native fixture commands above were rerun with exit 0. Logs: `build/agent-runs/M15-U3-batches-1-2/04-build-*.log` and `04-*-*.log`; each native log ends `RenderGraph authority PASS`. No Editor image is required.

## U3-05.1 — Hi-Z graph reads and writes

**PASS (self-review).** The production graph declares each `HiZ0..7` pass as an SRV read of the copied depth or previous level and a storage write to its own level. `SSRCompute` declares all levels as SRV reads. The strict native environment fixture now asserts the complete pass sequence, and its existing readback asserts each level has data distinct from its predecessor.

Builds: `cmake --build build --config Debug --target AxleEnvironmentRenderingTests --parallel 4` and the same Release command, both exit 0. Runs: `build/Tests/{Debug,Release}/AxleEnvironmentRenderingTests.exe {dx12,vulkan} build/agent-runs/M15-U3-batches-1-2/05.1-{config}-{backend}-artifacts`, all four exit 0. Logs: `build/agent-runs/M15-U3-batches-1-2/05.1-build-*.log` and `05.1-*.log`; generated native images and scorecards are in the corresponding `05.1-*-*-artifacts` directories. No Editor image is required for this checkpoint.

## U3-05.2 — Normal and probe graph reads

**PASS (self-review).** GTAO and SSR graph declarations read the produced `ScreenNormal`; SSR declares the selected prefitered probe SRV before graph compile. The native environment fixture checks ScreenNormal GPU readback, the full SSR pass sequence with an authored probe, live probe generation, and a changed image when the probe is added. Strict graph validation rejects an undeclared physical read.

The same Debug/Release environment build and four backend run commands as U3-05.1 were executed with the `05.2-` artifact prefix; all exit 0. Logs and native images: `build/agent-runs/M15-U3-batches-1-2/05.2-*`. No Editor image is required for this checkpoint.

## U3-05.3 — History and exposure graph reads

**Automated PASS; Editor visual PENDING.** Production `TAAResolve` declares the current color, depth, history read slot and history write slot; `AutoExposure` writes exposure, which `ToneMapping` reads. The native environment fixture checks pass order (`AutoExposure` before `ToneMapping`; `TAAResolve` before `TAACopy` before `ToneMapping`) and successful first/second TAA frames. The first frame uses an invalid-history flag to bypass stale history. Debug/Release Editor and environment builds exit 0; four environment runs on D3D12/Vulkan exit 0.

Commands: `cmake --build build --config {Debug,Release} --target AxleEnvironmentRenderingTests AxleEditor --parallel 4`; `build/Tests/{Debug,Release}/AxleEnvironmentRenderingTests.exe {dx12,vulkan} build/agent-runs/M15-U3-batches-1-2/05.3-{config}-{backend}-artifacts`. Logs and native images are in `build/agent-runs/M15-U3-batches-1-2/05.3-*`. The PLAN checkbox remains open until the required Editor capture confirms no invalid image output with Auto-Exposure and TAA toggled.

## U3-06 — Per-view target generation, completion and reset

**PASS (self-review).** `ViewContext` owns recording, target identity, reset reasons and completion. The renderer adopts final targets after frame acquisition and before temporal slot selection; submission publishes a completion token. The native environment fixture now checks the target generation in diagnostics, a waitable completion token with current device epoch, a later token after same-wrapper target replacement, and frame-index reset to zero. Its existing fault injection checks failed target reconfiguration retains the old generation and successful retry publishes a new one.

Builds: `cmake --build build --config {Debug,Release} --target AxleEnvironmentRenderingTests --parallel 4`, both exit 0. Runs: `build/Tests/{Debug,Release}/AxleEnvironmentRenderingTests.exe {dx12,vulkan} build/agent-runs/M15-U3-batches-1-2/06-{config}-{backend}-artifacts`, four exits 0. Logs and native artifacts: `build/agent-runs/M15-U3-batches-1-2/06-*`. No Editor image is required for this checkpoint.

## U3-07.1 — Resize

**PASS (self-review).** The native two-view fixture resizes an explicitly owned HDR/MSAA view from 8×8 to 12×10 and back, checks a new generation and complete validation at each size, and checks the independent second view's pixels remain unchanged. Invalid zero extent and partial allocation failure are already rejected by the same fixture.

Builds: `cmake --build build --config {Debug,Release} --target AxleRHIResourceViewTests --parallel 4`, both exit 0. Runs: `build/Tests/{Debug,Release}/AxleRHIResourceViewTests.exe {dx12,vulkan} targets`, four exits 0. Logs: `build/agent-runs/M15-U3-batches-1-2/07.1-*`. No Editor image is required.

## U3-07.2 — Feature toggle

**PASS (self-review).** The native environment fixture enables GTAO/contact, disables both, then enables SSR. It checks that target generation changes, ScreenNormal exists only when required and Hi-Z exists only for SSR. All render outputs remain readable.

Builds: `cmake --build build --config {Debug,Release} --target AxleEnvironmentRenderingTests --parallel 4`, both exit 0. Runs: `build/Tests/{Debug,Release}/AxleEnvironmentRenderingTests.exe {dx12,vulkan} build/agent-runs/M15-U3-batches-1-2/07.2-{config}-{backend}-artifacts`, four exits 0. Logs and native images: `build/agent-runs/M15-U3-batches-1-2/07.2-*`.
