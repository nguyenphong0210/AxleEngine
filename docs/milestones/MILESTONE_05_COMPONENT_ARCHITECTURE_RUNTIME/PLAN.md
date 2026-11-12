# Milestone 05 — Component-Driven Architecture & Standalone Runtime Plan

**Status:** Completed Historical Milestone.
**Parent Document:** [`ROADMAP.md`](../../ROADMAP.md).

---

## Work Breakdown & Historical Checklist

- [x] **Phase 20: Component-Driven Vehicle Creation from Scratch (`Engine/Vehicle`, `Engine/Scene` & `Engine/UI`)**
  - [x] **Phase 20.1: Decoupled Tags & Component-per-Wheel Architecture (N-Wheeled)**: Support arbitrary wheel counts (2-wheel bike, 4-wheel car, 6x6 truck, 8x8 APC) via `WheelComponent` (`VehicleWheel3D`) with differential torque splitting.
  - [x] **Phase 20.2: Vehicle Inspector Tuning & Modular Workflow**: Live telemetry cards, chassis mass, peak engine torque, and RPM limits.
  - [x] **Phase 20.3: Modern Unity ECS (`com.unity.vehicles`) Architecture & Full Scripting Decoupling**: Pure data container components, dynamic N-wheel discovery, and 100% neutral `EditorLayer`.
  - [x] **Phase 20.4: Editor Decoupling & Camera / Input Standardization**: Decoupled Game View & Eject Mode (`F2` key), `CarController.hpp` template, and `VehicleChaseCamera.hpp` dynamic script.
- [x] **Phase 21: Decoupled Scene Renderer Architecture (`Engine/Scene`)**
  - [x] Encapsulate full OpenGL rendering pipeline into `Axle::SceneRenderer`.
  - [x] Multi-pass pipeline: Shadow depth pass, PBR lighting pass, atmospheric skybox pass, and tonemapping/gamma post-processing.
  - [x] Clean separation of concerns between `EditorLayer` and rendering backend.
- [x] **Phase 22: Standalone Game Runtime Player (`AxleRuntime.exe`)**
  - [x] Standalone Executable Target (`Runtime/`): completely detached from `Axle::UI` (zero ImGui overhead for maximum FPS).
  - [x] Clean `RuntimeLayer` bootstrapping directly into `.axleproj` startup scenes.
  - [x] Game Controls & Windowing Ergonomics: Fullscreen toggle (`Alt+Enter` / `F11`), pause menu (`Escape`), and smooth exit handling.
- [x] **Phase 23: Asset Packaging & Cooking Pipeline (`.axlepak` & VFS)**
  - [x] Binary Package Specification (`.axlepak`) with `AXPK` magic header, TOC, and CRC32 checksums.
  - [x] In-Editor Asset Cooker & Project Exporter (`File -> Export Standalone Game...` and headless CLI `--export`).
  - [x] Virtual File System (VFS) in `Axle::FileSystem`: Mount and stream directly from `Data.axlepak` in runtime distribution mode.

---
