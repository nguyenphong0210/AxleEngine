# 📦 AxleEngine Subsystems & Module Reference

This document provides a comprehensive technical breakdown of every subsystem in AxleEngine, including its directory layout, responsibilities, primary classes, and integration patterns.

---

## 🗂️ Module Index

* [1. Engine/Core](#1-enginecore)
* [2. Engine/Platform](#2-engineplatform)
* [3. Engine/Renderer](#3-enginerenderer)
* [4. Engine/Audio](#4-engineaudio)
* [5. Engine/Physics](#5-enginephysics)
* [6. Engine/Scene](#6-enginescene)
* [7. Engine/Asset](#7-engineasset)
* [8. Engine/Project](#8-engineproject)
* [9. Engine/UI](#9-engineui)
* [10. Engine/Vehicle (Gameplay Toolkit)](#10-enginevehicle-gameplay-toolkit)
* [11. Editor Application](#11-editor-application)
* [12. Standalone Runtime Player](#12-standalone-runtime-player)

---

## 1. Engine/Core

The lowest layer of the engine, providing base primitives, logging, multi-threading, and application lifecycle.

* **Primary Classes & Headers**:
  * `Axle::Application` (`Application.hpp`): Central application singleton. Owns the platform window, maintains the `LayerStack`, processes events, and drives the main tick loop.
  * `Axle::Layer` & `Axle::LayerStack` (`Layer.hpp`, `LayerStack.hpp`): Modular slice of application logic (`OnAttach`, `OnDetach`, `OnUpdate`, `OnImGuiRender`, `OnEvent`).
  * `Axle::JobSystem` (`JobSystem.hpp`): High-performance task scheduler wrapping **enkiTS**. Provides persistent, lock-free worker threads scaled to CPU hardware concurrency (`JobSystem::GetScheduler()`).
  * `Axle::Log` (`Log.hpp`): Thread-safe logging subsystem powered by **spdlog**. Exposes standard logging macros (`AXLE_CORE_TRACE`, `AXLE_CORE_INFO`, `AXLE_CORE_WARN`, `AXLE_CORE_ERROR`, `AXLE_INFO`, etc.).
  * `Axle::Timestep` (`Timestep.hpp`): Frame timing wrapper providing delta-time in seconds and milliseconds.
  * `Axle::UUID` (`UUID.hpp`): Cryptographically randomized 64-bit globally unique identifier used across entities, assets, and prefabs.
  * `Axle::FileSystem` (`FileSystem.hpp`): Cross-platform file path resolution, loose file IO, and virtual package mounting (`MountPackage`, `ResolveAssetPath`).
  * `Axle::Types` (`Types.hpp`): Engine primitive aliases (`f32`, `f64`, `u8`, `u16`, `u32`, `u64`, `i8`, `i16`, `i32`, `i64`).

---

## 2. Engine/Platform

Isolates all OS-specific APIs, desktop windowing, hardware input polling, and dynamic library loading.

* **Primary Classes & Headers**:
  * `Axle::Window` (`Window.hpp`): Desktop window abstraction wrapping **GLFW**. Manages the OpenGL 4.5 Core Profile graphics context, V-Sync, and dispatches native OS events (`WindowCloseEvent`, `WindowResizeEvent`, `KeyEvent`, `MouseButtonEvent`, `MouseMovedEvent`, `MouseScrolledEvent`).
  * `Axle::Input` (`Input.hpp`): Global static input polling interface:
    * `Input::IsKeyPressed(KeyCode)`: Instantaneous keyboard query.
    * `Input::IsMouseButtonPressed(MouseCode)`: Mouse button query.
    * `Input::GetMousePosition()`: Cursor coordinates in screen pixels.
    * `Input::SetCursorMode(CursorMode)`: Toggles between `Normal` (editor mouse) and `Locked` (first-person FPS / game camera capture).
  * `Axle::DynamicLibrary` (`DynamicLibrary.hpp`): Cross-platform dynamic library loader (`LoadLibrary`, `GetProcAddress`, `FreeLibrary`) powering runtime gameplay script hot-reloading.

---

## 3. Engine/Renderer

Encapsulates the complete OpenGL 4.5 Core Profile graphics pipeline. Zero raw OpenGL commands or symbols leak outside this module.

* **Primary Classes & Headers**:
  * `Axle::RenderCommand` & `Axle::Renderer` (`RenderCommand.hpp`, `Renderer.hpp`): Low-level hardware draw calls (`DrawIndexed`, `DrawLines`, `Clear`, `SetViewport`, `SetDepthMask`).
  * `Axle::VertexBuffer`, `Axle::IndexBuffer`, `Axle::UniformBuffer` (`Buffer.hpp`): GPU buffer objects with declarative `BufferLayout` specifications.
  * `Axle::VertexArray` (`VertexArray.hpp`): Vertex array object (VAO) binding vertex buffers and index buffers.
  * `Axle::Shader` (`Shader.hpp`): GLSL shader compilation, program linking, and uniform caching (`SetInt`, `SetFloat`, `SetVec3`, `SetMat4`).
  * `Axle::Texture2D` & `Axle::Cubemap` (`Texture.hpp`): 2D textures and seamless 6-face cubemaps. Supports PNG/JPG/TGA/HDR via `stb_image`, auto mipmaps, and filtering modes.
  * `Axle::Framebuffer` (`Framebuffer.hpp`): Off-screen render targets supporting color attachments, depth/stencil buffers, and automatic resizing for editor viewports and post-processing.
  * `Axle::PerspectiveCamera` & `Axle::EditorCamera` (`Camera.hpp`, `EditorCamera.hpp`): Projection matrices, view matrices, and free-fly / orbit camera controllers.
  * `Axle::Material` (`Material.hpp`): Physically Based Rendering (PBR) material asset with Cook-Torrance BRDF properties (Albedo, Normal, Roughness, Metallic, Emission, UV scale) and sampler slot bindings.
  * `Axle::ShadowMap` (`ShadowMap.hpp`): Dedicated depth framebuffer with hardware bilinear percentage-closer filtering (PCF) and Vogel spiral soft shadow sampling.
  * `Axle::Skybox` (`Skybox.hpp`): Analytical atmospheric Rayleigh/Mie scattering skybox and HDR environment cubemap with Image-Based Lighting (IBL) convolution (Irradiance, Pre-filtered Specular, 2D BRDF LUT).
  * `Axle::AssetSerializer` (`AssetSerializer.hpp`): Human-readable `.axle` 3D mesh loader and high-speed binary `.axlebin` flat cache generator.

---

## 4. Engine/Audio

Spatial 3D audio engine built on top of [miniaudio](https://miniaud.io/), providing realistic audio environments with zero external dynamic dependencies.

* **Primary Classes & Headers**:
  * `Axle::AudioEngine` (`AudioEngine.hpp`): Master audio device coordinator and mixer bus.
  * `Axle::AudioClip` (`AudioClip.hpp`): Preloaded PCM waveform buffer supporting WAV and MP3 formats.
  * `Axle::AudioSource` (`AudioSource.hpp`): Individual audio emitter supporting volume, pitch, looping, 3D spatial attenuation (Inverse, Linear, Exponential), Doppler shift, and directional sound cones.
  * `Axle::AudioListener` (`AudioListener.hpp`): 3D ear anchor synchronized to the active camera's world position and orientation.

---

## 5. Engine/Physics

High-performance 3D rigid body dynamics and collision detection bridge built on [Jolt Physics](https://github.com/jrouwe/JoltPhysics).

* **Primary Classes & Headers**:
  * `Axle::PhysicsEngine` (`PhysicsEngine.hpp`): Jolt global initialization, memory allocators, and collision layer registration.
  * `Axle::PhysicsScene` (`PhysicsScene.hpp`): Physics world holding rigid bodies and constraints. Utilizes `JobSystemEnkiTS` to execute Jolt collision detection and solver tasks concurrently on enkiTS worker threads.
  * `Axle::PhysicsBody` (`PhysicsBody.hpp`): Body representation supporting `Static`, `Kinematic`, and `Dynamic` motion types, mass properties, linear/angular damping, friction, and restitution.
  * `Axle::CollisionShape`: Complete suite of collision geometries: Box, Sphere, Capsule, Cylinder, ConvexHull, and MeshShape.
  * **Queries & Callbacks**: Multi-layer raycasting, shape casting, and contact listener callbacks.

---

## 6. Engine/Scene

The central data-oriented coordinator integrating ECS, parent-child scene graphs, high-level render passes, and serialization.

* **Primary Classes & Headers**:
  * `Axle::Scene` (`Scene.hpp`): Encapsulates an `entt::registry`. Coordinates entity lifecycles, runtime start/stop, script ticks, and physics transform synchronization.
  * `Axle::Entity` (`Entity.hpp`): Lightweight handle wrapping an `entt::entity` ID and `Scene*`.
  * `Axle::Components` (`Components.hpp`): Declarative ECS component structures (see [ECS & Scene Graph Guide](ECS_AND_SCENE_GRAPH.md)).
  * `Axle::SceneRenderer` (`SceneRenderer.hpp`): Decoupled multi-pass rendering pipeline (Shadow Pass -> PBR Lighting Pass -> Skybox -> Post-Processing Tone Mapping).
  * `Axle::ParallelECS` (`ParallelECS.hpp`): Multithreaded batch component iteration via `JobSystemEnkiTS`.
  * `Axle::SceneSerializer` (`SceneSerializer.hpp`): YAML-based human-readable `.axlescene` serializer.
  * `Axle::AssemblySerializer` (`AssemblySerializer.hpp`): Reusable nested prefab `.axle` serializer with automatic UUID remapping.

---

## 7. Engine/Asset

Decoupled asset registry and management pipeline.

* **Primary Classes & Headers**:
  * `Axle::AssetManager` (`AssetManager.hpp`): Central asset broker with Flyweight caching (`TextureCache`, `MaterialCache`, `MeshCache`).
  * `Axle::AssetMetadata` (`AssetMetadata.hpp`): Companion `.meta` file schema storing persistent 64-bit UUIDs and asset types.
  * `Axle::AssetCooker` (`AssetCooker.hpp`): Production build tool that compiles and packages all referenced assets into monolithic binary packages (`.axlepak`).

---

## 8. Engine/Project

Project configuration and directory management.

* **Primary Classes & Headers**:
  * `Axle::Project` (`Project.hpp`): Active project model declaring asset directories, startup scenes, and cached build targets.
  * `Axle::ProjectSerializer` (`ProjectSerializer.hpp`): Serializes `.axleproj` configuration files.

---

## 9. Engine/UI

Dear ImGui abstraction and modular visual editor panels.

* **Primary Classes & Headers**:
  * `Axle::ImGuiLayer` (`ImGuiLayer.hpp`): ImGui context, docking integration, font atlas, and custom dark styling.
  * `Axle::SceneHierarchyPanel` (`SceneHierarchyPanel.hpp`): Visual entity hierarchy tree, parent-child drag-and-drop reparenting, and context creation menus.
  * `Axle::ContentBrowserPanel` (`ContentBrowserPanel.hpp`): File system explorer with folder navigation, asset badges, and drag-and-drop payloads.
  * `Axle::AssetPreviewPanel` & `AssetThumbnailManager` (`AssetPreviewPanel.hpp`, `AssetThumbnailManager.hpp`): 3D asset thumbnail renderer and turntable 3D previewer.
  * `Axle::AssemblyEditorPanel` (`AssemblyEditorPanel.hpp`): Isolated 3D viewport for designing and modifying prefab assemblies.

---

## 10. Engine/Vehicle (Gameplay Toolkit)

A high-level automotive mechanics and acoustics gameplay toolkit built on top of generic Engine subsystems (`Engine/Physics`, `Engine/Scene`, `Engine/Audio`).

* **Primary Classes & Headers**:
  * `Axle::Vehicle` (`Vehicle.hpp`): Multithreaded vehicle physics controller utilizing Jolt's `VehicleConstraint`. Implements N-wheeled drivetrains, Ackermann steering geometry, 4-wheel drive, and suspension spring/damper calculations.
  * `Axle::VehicleAudio` (`VehicleAudio.hpp`): Real-time procedural and sample-based vehicle acoustics:
    * Granular Engine RPM synthesizer with throttle load modulation.
    * Dynamic tire screech based on longitudinal and lateral slip angles.
    * Suspension travel impact thuds.
    * Body collision impact crunch acoustics.

---

## 11. Editor Application (`AxleEditor.exe`)

The full WYSIWYG game development environment:
* Dockable multi-panel workspace.
* 3D Viewport with interactive **ImGuizmo** translation/rotation/scale gizmos and mouse raycast picking.
* Live parameter tuning through reflected C++ script fields.
* Play, Pause, and Single-Frame simulation controls.
* In-Editor C++ script compiler (`F9`) with dynamic DLL hot-reloading (`AxleScripts.dll`).

---

## 12. Standalone Runtime Player (`AxleRuntime.exe`)

The lightweight, distribution-ready game launcher:
* Zero editor or ImGui overhead.
* Loads project configuration and boots directly into the startup scene (`StartScene`).
* Executes full runtime physics, 3D audio, native scripts, and renders to the display screen.
* Capable of running directly from packed `.axlepak` archives.
