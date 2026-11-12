# Phase 01 — Render and Material Foundation Architecture

## 1. Target and Scope

Phase 01 establishes modern rendering execution and material pipeline architectures for AxleEngine. The primary targets are:
1. **Decoupled Concurrency:** Separating the Main/Simulation Thread from GPU command recording and presentation, eliminating frame hitches caused by game logic or physics.
2. **Transient Memory Aliasing:** Reclaiming 40%–60% of transient Render Target VRAM by analyzing pass lifetimes and suballocating from shared GPU heaps.
3. **Transactional Material Pipeline:** Providing a versioned node-based material representation (.axleasset), compiling to native DXIL and SPIR-V via DXC with strict reflection, layout caching, and last-known-good fault tolerance.

---

## 2. Concurrency & Memory Architecture

`mermaid
flowchart TD
    subgraph MainThread [Main / Simulation Thread]
        Sim[Simulation & ECS Advance Frame N+1]
        Extract[ExtractRenderWorld: Sub-ms Immutable POD Snapshot]
    end

    subgraph ThreadQueue [Thread-Safe Queue]
        Queue[Bounded Lock-Free RenderWorld Queue]
    end

    subgraph RenderWorkerThread [Dedicated Render Thread]
        Consume[Consume Frame N Snapshot]
        GraphBuild[RenderGraph Execution & Lifetime Analysis]
        RecordWorkers[Worker Jobs: Parallel Command Recording]
        CmdSubmit[Primary Command List Submission]
        Present[Swapchain Acquire & Present]
    end

    subgraph MemoryHeap [VRAM Memory Aliasing Heap]
        Heap[Placed Resource Shared Heap ID3D12Heap / VkDeviceMemory]
        TargetA[Pass 1: Refraction Buffer Lifetime: T0..T2]
        TargetB[Pass 4: ToneMap Buffer Lifetime: T3..T5]
    end

    Sim --> Extract
    Extract --> Queue
    Queue --> Consume
    Consume --> GraphBuild
    GraphBuild --> RecordWorkers
    RecordWorkers --> CmdSubmit
    CmdSubmit --> Present

    TargetA -. Overlapping Offset .-> Heap
    TargetB -. Overlapping Offset .-> Heap
`

### 2.1. Render Thread Decoupling
- **1-Frame Latency Pipeline:** While the Render Thread records commands for Frame $, the Main Thread concurrently advances Simulation, ECS, and Physics for Frame +1$.
- **OS Message Loop Separation:** Win32 message pumping remains on the Main Thread, while presentation fence waits and swapchain presentation execute on the Render Thread.
- **Parallel Pass Recording:** Leveraging NRI command allocators to record independent scene passes concurrently on worker threads via enkiTS, stitching them into the primary command queue before submission.

### 2.2. VRAM Memory Aliasing (RenderGraph Suballocation)
- **Topological Lifetime Analysis:** The RenderGraph compiler calculates the disjoint lifespan intervals $[T_{	ext{first}}, T_{	ext{last}}]$ of transient render targets.
- **Placed Resources:** Targets with non-overlapping intervals (e.g., refraction copy target vs. post-processing bloom targets) share the same physical VRAM memory offset.
- **Aliasing Barriers:** Automatic insertion of D3D12_RESOURCE_BARRIER_TYPE_ALIASING and Vulkan image barriers guarantees clean GPU cache invalidation.

---

## 3. Material Graph & Compiler Architecture

`mermaid
flowchart LR
    AssetFile[Material Asset .axleasset] --> Parser[Document Parser & AST Validator]
    Parser --> AST[Abstract Syntax Tree IR]
    AST --> Lowering[Registry-Driven HLSL Lowering]
    Lowering --> DXC[DXC Shader Compiler]
    DXC --> DXIL[DirectX 12 DXIL]
    DXC --> SPIRV[Vulkan SPIR-V]
    DXIL --> Cache[Pipeline Layout & Code Cache]
    SPIRV --> Cache
    Cache --> RHI[Native RHI Binding]

    AST -. Target == OpenGL .-> Reject[MG_OPENGL_UNSUPPORTED Diagnostic]
`

### 3.1. Document Model & AST Representation
- .axleasset files store graph nodes, explicit pin connections, and named parameter values.
- Authoring data remains clean and decoupled from backend compiler IR.

### 3.2. DXC Cross-Compilation
- Authoring uses HLSL 2021 syntax.
- DXC lowers the generated surface shader directly to DXIL for D3D12 and SPIR-V for Vulkan.
- Strict reflection extracts constant buffer layouts, texture registers, and sampler bindings automatically.

### 3.3. Last-Known-Good Reload & Fault Tolerance
- If an artist modifies a graph introducing a syntax or type error, the compiler safely traps the error, reports the exact file/node diagnostics to the editor console, and retains the **last-known-good compiled pipeline**. The viewport continues rendering without crashing.

---

## 4. Technology and Boundaries

1. **RHI Independence:** Direct3D 12 and Vulkan serve as primary backends via NRI.
2. **OpenGL Boundary:** OpenGL targets report MG_OPENGL_UNSUPPORTED transactionally, preserving stability without fabricating partial shader support.
3. **Compiler Contract:** DXC is the primary compiler; Slang remains an optional evaluated prototype.
