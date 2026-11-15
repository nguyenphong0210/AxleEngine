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
