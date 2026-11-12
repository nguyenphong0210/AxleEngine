# Milestone 02 — Simulation Core & Viewport Loop Architecture

## 1. Target and Mandate

Milestone 02 (Phases 5 through 7) transforms AxleEngine into an interactive 3D simulation environment. Its architectural mandate establishes:
1. **Mathematical Core & Geometric Primitives** (Phase 5): Hardware-accelerated linear algebra via `GLM`, bounding volumes (AABB, Frustum, Ray), and spatial intersection algorithms.
2. **Editor Camera & Offscreen Viewport** (Phase 6): Decoupled editor camera controller and offscreen framebuffer presentation inside Dear ImGui.
3. **Simulation Tick & Input Architecture** (Phase 7): Deterministic fixed timestep accumulator loop and unified input polling.

```mermaid
flowchart TD
    subgraph AppLoop [Application Frame Loop]
        Clock[High-Resolution Frame Clock: std::chrono]
        TimeStep[Delta Time & Accumulator]
        InputPoll[Axle::Input Polling Subsystem]
    end

    subgraph SimulationTick [Fixed Timestep Loop]
        FixedAccum[Fixed Timestep Accumulator: dt = 1/60s]
        PhysicsTick[Physics Step Simulation]
        CameraTick[Editor Camera Controller: Orbit / Pan / Fly]
    end

    subgraph ViewportPresentation [Offscreen Viewport Pipeline]
        OffscreenFB[Offscreen Framebuffer: Color + Depth Buffers]
        RenderDraws[Scene Geometry Render Calls]
        ImGuiViewport[Dear ImGui Viewport Panel: ImGui::Image]
    end

    Clock --> TimeStep
    TimeStep --> FixedAccum
    FixedAccum --> PhysicsTick
    InputPoll --> CameraTick
    CameraTick --> RenderDraws
    RenderDraws --> OffscreenFB
    OffscreenFB --> ImGuiViewport


---

## 2. Fixed Timestep Simulation Loop

To guarantee physics determinism and prevent numerical instability caused by variable frame rate stuttering, AxleEngine implements a canonical accumulator loop:

```mermaid
flowchart TD
    Start[Frame Start] --> Measure[Measure Frame Delta Time]
    Measure --> Cap[Cap Delta Time to Max Threshold: e.g. 0.25s]
    Cap --> Add[Accumulator += DeltaTime]
    Add --> Check{Accumulator >= FixedDelta?}
    Check -- Yes --> Tick[Simulate Fixed Physics Step: dt = 1/60s]
    Tick --> Sub[Accumulator -= FixedDelta]
    Sub --> Check
    Check -- No --> Interpolate[Compute Alpha = Accumulator / FixedDelta]
    Interpolate --> VariableTick[Update Variable Logic & Render Frame]
    VariableTick --> End[Frame End]
```

---

## 3. Offscreen Viewport Architecture

### 3.1 Decoupled Viewport Rendering

- **Offscreen Framebuffer**: Instead of rendering directly to the OS window swapchain, the scene is rasterized into an offscreen framebuffer with distinct attachments:
  - Color Attachment (`GL_RGBA8` / `DXGI_FORMAT_R8G8B8A8_UNORM`).
  - Depth/Stencil Attachment (`GL_DEPTH24_STENCIL8` / `DXGI_FORMAT_D24_UNORM_S8_UINT`).
- **Dynamic Resize Handling**: When the user drags the Editor's viewport dock panel, the render target dimensions are adjusted dynamically to match panel extents, preserving the camera's aspect ratio without stretching.
- **Input Isolation**: The camera controller processes mouse and keyboard input only when the ImGui viewport panel is focused and hovered (`ImGui::IsWindowFocused()` && `ImGui::IsWindowHovered()`).
