# Milestone 09 — Production Hardening, Vulkan 1.3 Backend Parity & Next-Gen Architecture Plan

**Status:** Completed Historical Milestone.
**Parent Document:** [`ROADMAP.md`](../../ROADMAP.md).

---

## Work Breakdown & Historical Checklist

- [x] **Phase 35: High-Performance Vulkan 1.3 Graphics Backend & Editor Parity (`Engine/RHI` & `Engine/UI`)**
  - [x] **Step 1 - Vulkan Swapchain Presentation Engine & ImGuiLayer Backend Integration (`Engine/UI`)**:
    - [x] Create native `VkSurfaceKHR` from GLFW window (`glfwCreateWindowSurface`) and query surface capabilities, color formats (`VK_FORMAT_B8G8R8A8_UNORM`), and presentation modes (`VK_PRESENT_MODE_FIFO_KHR` / `VK_PRESENT_MODE_MAILBOX_KHR`).
    - [x] Construct Vulkan swapchain (`VkSwapchainKHR`, 3 backbuffers), render pass (`VkRenderPass`), image views, and framebuffers.
    - [x] Initialize `ImGui_ImplVulkan_InitInfo` with persistent `VkDescriptorPool`, graphics queue, and dynamic loader.
    - [x] Implement `ImGuiLayer::End()` Vulkan dispatch: Record `ImGui_ImplVulkan_RenderDrawData` into a command buffer, submit to graphics queue, and present via `vkQueuePresentKHR`.
    - [x] Completely resolve crash when launching or switching to `--rhi=vulkan` in `AxleEditor`.
  - [x] **Step 2 - Offscreen Viewport Vulkan Descriptor Bridge (`Engine/UI` & `Engine/Renderer`)**:
    - [x] Register offscreen HDR/SDR render target (`RHITexture`) to Dear ImGui via `ImGui_ImplVulkan_AddTexture(sampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)`.
    - [x] Synchronize layout transitions in `RHISceneRenderer` (`ColorAttachment` -> `ShaderReadOnly`) to eliminate validation layer hazards.
    - [x] Implement descriptor set recycling in `RHIViewportDescriptor` on viewport resize without leaking descriptor pool slots.
  - [x] **Step 3 - Dynamic Window Resize, Swapchain Recreation & Zero-Leak Recovery (`Engine/UI`)**:
    - [x] Gracefully handle `VK_SUBOPTIMAL_KHR` and `VK_ERROR_OUT_OF_DATE_KHR` during window drag resize, minimize, and restore.
    - [x] Synchronize device queues via `vkQueueWaitIdle` / `vkDeviceWaitIdle` during recreation.
  - [x] **Step 4 - Triple-Backend Parity & Automated Verification Suite (`AxleTests.exe --step98`)**:
    - [x] Headless unit test Step 98 verifying Vulkan physical device query, swapchain lifecycle, descriptor set binding, offscreen texture bridge, and headless render parity.
- [x] **Phase 36: Unified `.axleasset` Architecture & Multi-Backend Component Resource Standardization (`Engine/Asset`, `Engine/Scene`, `Engine/UI`)**
  - [x] **Step 1 - Core `AssetDSL` Lexer/Parser & Shared Resource Pipeline (`Engine/Asset`)**:
    - [x] Implement centralized `AssetDSL` tokenizer supporting C/QML-style declarative blocks (`Material`, `Canvas`, `ParticleEmitter`, `Mesh`, `Texture`, `Terrain`, `PhysicsMaterial`).
    - [x] Support native Hex color `#RRGGBB` / `#RRGGBBAA`, vectors `[x, y]`, floats, strings, single-line `//` and multi-line `/* */` comments.
    - [x] Built-in lightweight peek utility `AssetDSL::PeekAssetKind()` to identify asset type for Content Browser icons with zero full-parse cost.
    - [x] 100% Single Pure Standard: Zero legacy parser debt; directly migrate existing `.axlemat` and `.axleui` files to `.axleasset`.
  - [x] **Step 2 - Material & Texture Standardization, Drag-and-Drop Image Ingest & RHI Preview Parity (`Engine/Asset`, `Engine/UI`, `Engine/Scene`)**:
    - [x] Standardize `Material { ... }` and `Texture { ... }` in `.axleasset` with bidirectional load/save in `MaterialComponent` and `AssetManager`.
    - [x] **External Drag-and-Drop Image Ingest Pipeline**: Dragging external `.png`, `.jpg`, `.tga`, `.hdr` files from Windows Explorer into Content Browser or Viewport automatically ingests them, intelligently detects texture type from filename suffixes (`_diffuse`/`_albedo`/`_d` -> sRGB Color, `_normal`/`_norm`/`_n` -> Linear Normal, `_roughness`/`_orm` -> Linear Data, `.hdr` -> CubemapHDR), and auto-generates companion `.axleasset` descriptors.
    - [x] **Viewport Entity Drag & Drop**: Dropping an image texture or material `.axleasset` directly onto an Entity in the 3D Viewport auto-assigns it to the corresponding PBR slot under the cursor.
    - [x] Eliminate legacy OpenGL dependencies in material pipeline (`Window::HasActiveContext()`, `Texture2D::Create`), wiring directly to `RHI::RHIMaterial` and Tier-2 Bindless descriptors.
    - [x] Upgrade `AssetPreviewPanel` from legacy OpenGL FBO to multi-backend RHI descriptor bridge (`ImGui_ImplVulkan_AddTexture` / `ImGui_ImplDX12_AddTexture`), restoring live 3D PBR turntable preview on Vulkan 1.3 & D3D12.
    - [x] Add 3D preview mesh selector (Sphere, Cube, Cylinder, Plane) & 1-click PBR material presets bar in Material Inspector.
    - [x] Content Browser `Create -> Material Asset (.axleasset)` and `Create -> Texture Asset (.axleasset)` integration with PBR Sphere/Texture icons.
  - [x] **Step 3 - VFX Particle System Asset & Dedicated Inspector Docking (`Engine/VFX`, `Engine/UI`)**:
    - [x] Standardize `ParticleEmitter { ... }` in `.axleasset` (kinematics, shapes, curves, flipbook, force fields).
    - [x] Add `Create -> VFX Asset (.axleasset)` in Content Browser.
    - [x] Add `[Open in VFX Editor]` shortcut button directly on `ParticleEmitterComponent` in Inspector to dock and link the editor.
    - [x] Upgrade `VFXEditorPanel` 3D preview viewport to multi-backend RHI (Vulkan / D3D12).
  - [x] **Step 4 - UI Canvas & 3D Geometry Integration (`Engine/UI`, `Engine/Asset`)**:
    - [x] Migrate `.axleui` to `.axleasset` under `Canvas { ... }` root block cleanly replacing `.axleui`.
    - [x] Unify `.axlemesh` into `Mesh { ... }` in `.axleasset` with automatic `.axlebin` baking and zero-overhead GPU loading.
    - [x] Standardize `Terrain { ... }` and `PhysicsMaterial { ... }` into `.axleasset`.
    - [x] Automated test suite Step 99 (`AxleTests.exe --step99`) validating AST parsing, round-trip serialization, backward compatibility, and Vulkan RHI preview rendering.
- [x] **Phase 37: Raw Media Asset Ingestion Pipeline & Opt-in Descriptor Architecture (`Engine/Asset`, `Engine/UI`, `Editor`)**
  - [x] **Step 1 - Raw Media Ingest (Single-File Zero-Companion Ingestion in `EditorLayer`)**:
    - [x] Update `EditorLayer::OnFileDrop` for images (`.png`, `.jpg`, `.jpeg`, `.tga`, `.bmp`, `.hdr`): Copy only the image file itself to destination (`textures/` or active Content Browser directory) without auto-generating companion `.axleasset` files.
    - [x] Update `EditorLayer::OnFileDrop` for audio (`.wav`, `.mp3`, `.ogg`, `.flac`): Copy only the audio file to `audio/` or active Content Browser folder.
    - [x] Update `EditorLayer::OnFileDrop` for video (`.mp4`, `.mkv`, `.avi`, `.webm`): Copy only the video file to `video/` or active Content Browser folder.
    - [x] Keep assets directory 100% clean, avoiding file duplication and orphaned descriptors.
  - [x] **Step 2 - In-Memory Texture Descriptor Synthesis & Direct Image Loading (`Engine/Asset`)**:
    - [x] Enhance `TextureDescriptor::Load(filepath, outDesc)` to detect raw image formats (`.png`, `.jpg`, `.hdr`...) and synthesize in-memory descriptors on the fly via `CreateFromImageFile` with convention heuristics (`_normal` -> NormalMap Linear BC5, `.hdr` -> CubemapHDR, default -> Color sRGB Anisotropic16x) with zero disk footprint.
    - [x] Ensure `AssetManager::GetTexture(path)` and `MaterialComponent` load raw image files directly without requiring `.axleasset`.
  - [x] **Step 3 - Opt-in Texture Descriptor Generation in Content Browser (`Engine/UI`)**:
    - [x] In `ContentBrowserPanel`, add `"Create Texture Descriptor (.axleasset)"` to the right-click context menu of image items.
    - [x] Allow users to explicitly generate `<stem>.axleasset` only when deep customization is needed (Point filter for Pixel Art, BC7 compression, Clamp wrapping, custom max resolution).
  - [x] **Step 4 - Standalone Runtime & Packaging Alignment (`Engine/Asset` & `Runtime`)**:
    - [x] In `AssetCooker`, package raw image and audio files directly into `Data.axlepak` using convention defaults, or using companion `.axleasset` if explicitly present.
    - [x] In `RuntimeLayer`, verify seamless asset resolution from both loose disk files and monolithic `.axlepak` packages.
  - [x] **Step 5 - Automated Test Suite Step 100 (`AxleTests.exe --step100`)**:
    - [x] Implement Step 100 automated test suite in `AxleTests.exe` validating raw media ingestion, in-memory descriptor synthesis, opt-in `.axleasset` generation, and runtime texture resolution.
    - [x] Verify 100% pass rate across all test suites.

---
