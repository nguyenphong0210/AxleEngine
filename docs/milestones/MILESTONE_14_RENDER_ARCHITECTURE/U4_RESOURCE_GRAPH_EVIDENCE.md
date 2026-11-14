# M14-U4 - Resource Lifetime and RenderGraph Authority Evidence

**Status:** `ACCEPTED`  
**Unit:** M14-U4  
**Scope:** complete view-target generations, strict graph physical-resource validation, generation/lifetime retention and descriptor immutability after binding.

## 1. Target generation contract

`RHIViewTargets::Configure` already built candidates transactionally; U4 completes the contract by adding:

- a monotonic published generation ID (`GetGeneration()`);
- `Validate()` checks for required primary/auxiliary slots, dimensions, formats and sample counts;
- UAV validation for storage targets;
- Hi-Z, bloom, GTAO, history, exposure and DDGI extent validation;
- validation before candidate publication, preserving the old generation on any failure.

The existing `RHITexture::RetainGeneration()` and `RHIResourceLease` keep an exact native generation alive after wrapper replacement. The generation ID is view-level diagnostic identity; native handles/leases remain the lifetime authority.

## 2. RenderGraph authority

`RenderGraph::Compile(bool strictPhysicalResources)` now supports a strict renderer mode. `RHISceneRenderer` calls `Compile(true)`, so every resource used by a pass must have a live native texture and lease before recording. The default non-strict mode remains available for device-free graph planner tests that validate dependency and load/store rules without a GPU.

Strict mode rejects missing native textures, dead generations, undefined/mismatched layout/access declarations, reads before writes, duplicate resource use and pass recording out of compiled order. `BeginPass` pins each exact imported lease through `RHICommandBuffer::KeepAlive` before recording transitions.

## 3. Descriptor and upload lifetime

`RHIDescriptorPool::UpdateTextureRange` checks `RHIResourceService::IsInUse` for the descriptor-set generation. Once a descriptor set is recorded by `BindDescriptorSet`, updates are rejected until the submission retires and the pool/set generation is reset.

`RHICommandBuffer::KeepAlive` remains the command-recording boundary for textures, buffers, descriptor sets, pipeline layouts and transitive dependencies. `RHIFrameContext` waits for completion before arena/pool reuse.

## 4. Verification

### CPU graph contracts

```text
build/Tests/Debug/AxleRenderUnitTests.exe
```

Result: **exit code 0**. The graph suite includes a strict physical-resource negative case; dependency, layout, load/store, duplicate-use and ordering cases pass.

### Native target generation and descriptor lifetime

```text
build/Tests/Debug/AxleRHIResourceViewTests.exe dx12
```

Result: **exit code 0**. The fixture verified published generation validation, generation ID changes after replacement, atomic failed replacement, external wrapper retention, old-generation release after completion and descriptor mutation rejection after binding, with no recorded native validation errors.

### Renderer strict graph integration

```text
build/Tests/Debug/AxleEnvironmentRenderingTests.exe dx12 build/Tests/Work/M14-U4-baseline
```

Result: **exit code 0**. Native ForwardPBR/environment rendering passed after strict graph compilation was enabled in `RHISceneRenderer`.

### Build and architecture checks

```text
cmake --build build --config Debug --target AxleRenderUnitTests AxleRHIResourceViewTests AxleEnvironmentRenderingTests --parallel 4  => exit 0
python scripts/check_architecture.py                                                                                  => PASS
git diff --check                                                                                                       => PASS
```

## 5. U4 decision

**U4 ACCEPTED.** Target generations, strict graph resource authority, exact-generation retention and immutable-after-bind descriptor behavior are implemented and verified. The next released Unit is **M14-U5 - Lighting, reflection and GI service boundaries**.
