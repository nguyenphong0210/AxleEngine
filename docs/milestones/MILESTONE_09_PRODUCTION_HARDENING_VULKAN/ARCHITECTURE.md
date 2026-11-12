# Milestone 09 — Production Hardening, Vulkan & Diagnostics Architecture

## 1. Target and Mandate

Milestone 09 (Phases 35 through 37) elevates AxleEngine to commercial-grade runtime stability. Its architectural mandate establishes:
1. **Explicit Vulkan Architecture** (Phase 35): Initializing modern Vulkan rendering via explicit device selection, memory validation layers, and Vulkan command queues.
2. **Telemetry & Profiler Architecture** (Phase 36): Multi-threaded CPU/GPU instrumentation, Tracy profiler zone tracking, and memory budget telemetry.
3. **Fault Diagnostics & Crash Breadcrumbs** (Phase 37): Device-loss isolation, GPU breadcrumb ring buffers, and automated crash dump telemetry.

```mermaid
flowchart TD
    subgraph DiagnosticsCore [Diagnostics & Profiling Subsystem]
        Tracy[Tracy Profiler Instrumentation]
        BreadcrumbMgr[GPU Breadcrumb Ring Buffer]
        CrashReporter[Crash Dump & Minidump Handler]
    end

    subgraph VulkanHAL [Vulkan Backend Core]
        VkInstance[VkInstance & Physical Device Selection]
        VkDevice[VkDevice Logical Context]
        VkQueues[Graphics, Compute & Transfer Queues]
        Validation[Khronos Validation Layers]
    end

    subgraph HostEngine [Engine Application Runtime]
        AppLoop[Application Frame Loop]
        RenderCmd[Render Command Recording]
    end

    AppLoop --> Tracy
    RenderCmd --> BreadcrumbMgr
    VkInstance --> Validation
    VkInstance --> VkDevice
    VkDevice --> VkQueues
    BreadcrumbMgr -.->|On Device Loss| CrashReporter


---

## 2. Vulkan Integration Architecture

### 2.1 Instance & Device Life-Cycle

- **Physical Device Scoring**: Evaluates discrete vs. integrated GPUs, dedicated VRAM capacity, feature bits (geometry shaders, sampler anisotropy, timeline semaphores).
- **Queue Family Allocation**:
  - `Graphics Queue`: Primary rendering, rasterization, and presentation.
  - `Compute Queue`: Asynchronous particle/simulation workloads.
  - `Transfer Queue`: Asynchronous asset uploading to dedicated VRAM heaps.

### 2.2 Validation & Debug Markers

- Integration of `VK_LAYER_KHRONOS_validation` during Debug and Profile configurations.
- API object naming (`VK_EXT_debug_utils`) to inject user-readable identifiers into RenderDoc and GPU capture tools.

---

## 3. Profiling & Diagnostics Architecture

### 3.1 Hierarchical CPU/GPU Profiling

```mermaid
sequenceDiagram
    autonumber
    participant Frame as Frame Loop
    participant CPU as Tracy CPU Zone
    participant Cmd as Command List
    participant GPU as GPU Timestamp Query

    Frame->>CPU: AXLE_PROFILE_ZONE("RenderFrame")
    CPU->>Cmd: Begin Recording
    Cmd->>GPU: WriteTimestamp(Begin)
    Cmd->>Cmd: Execute Passes & Draws
    Cmd->>GPU: WriteTimestamp(End)
    Cmd->>Frame: Submit Command Buffer
    Frame->>CPU: End Zone
    GPU-->>CPU: Resolve Query Pool (N-2 Latency)
```

### 3.2 GPU Crash Breadcrumb Subsystem

To pinpoint GPU TDRs (Timeout Detection and Recovery) and device-lost errors:
- **Write-After-Pass Marker**: Writes an incrementing 32-bit execution token into host-visible, device-coherent memory immediately before and after every draw pass.
- **TDR Interception**: Upon `VK_ERROR_DEVICE_LOST` or `DXGI_ERROR_DEVICE_REMOVED`, the host reads the last written breadcrumb value to immediately determine the exact shader or draw command that hung the GPU.
