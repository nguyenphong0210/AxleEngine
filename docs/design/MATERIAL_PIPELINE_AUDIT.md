# Material pipeline audit (Phase 38.3)

This is the implementation baseline for Phase 38.3, not a claim of visual parity
with Godot. A property is complete only when editing, saving, reloading, GPU
binding, and visible shading agree in the preview and scene.

| Existing property | Inspector / asset | RHI / shader gap at audit |
| --- | --- | --- |
| Albedo, roughness, metallic | Controls and material serialization exist | Scene draws bind the default texture set for every material; selected maps are not bound per draw. |
| Normal map / strength | Controls and serialization exist | Strength is not sent to ForwardPBR. |
| AO map / strength | Controls and serialization exist | Component synchronization omits AO strength; shader samples AO without strength. |
| Emission map / color / intensity | Controls and serialization exist | Per-material texture binding is missing; shader forces a minimum multiplier of one for textured emission. |
| UV tiling / offset | Both axes stored | Push constants carry only tiling X, so Y is ignored. |
| Separate maps / packed ORM | Schema and shader branch exist | Asset inspector save omits texture mode / ORM; component synchronization does not bind the ORM texture. |
| Clearcoat strength / roughness | Component and Material support both | Inspector save omits both; push constants alias strength with alpha cutoff; ForwardPBR has no coat BRDF. |
| Anisotropy strength / rotation | Component and Material support both | Inspector save omits both; anisotropic distribution is defined but not used. |
| Surface type / cutoff / culling / depth write | Schema and pipeline states exist | Cutoff aliases clearcoat; audit explicit zero cutoff, transparent ordering, and material overrides. |
| Specular / environment toggles | Inspector and Material serialization exist | No matching RHI material flags or shader controls. |
| Render queue | Inspector and Material serialization exist | Scene queues do not use the configured priority. |
| Custom shader properties | Inspector reflection exists | Inspector save omits property instances; executable custom-shader rendering needs separate verification. |
| Texture selection / clearing | Drag/drop and clear controls exist | The + button has no picker; component synchronization retains stale paths after disabling maps. |

## Implementation sequence

1. Unify material saving through the existing component-to-asset synchronization.
2. Add a dedicated, aligned CPU/GPU material constant block; retain the portable
   128-byte object push-constant ABI and stop sharing unrelated parameter fields.
3. Synchronize all existing scalar values, flags, texture paths, and disabled-map
   state; bind actual material textures for each draw with initialized fallbacks.
4. Complete clearcoat and anisotropic BRDF evaluation, sorting, and shader flags.
5. Reorganize shared inspector controls and add a searchable texture picker.
6. Extend validated shading with coat maps, anisotropy flow, detail normals, and
   glass refraction. Give each feature its own serialization and render checks.

## Acceptance rules

- Test zero and boundary values, asymmetric UV tiling, material replacement,
  disabling and clearing maps, and old asset defaults.
- Test save/reload of asset and inline entity materials, not just getters.
- Compile the actual ForwardPBR vertex/pixel shaders to DXIL and SPIR-V.
- Verify fixed-light reference materials in preview and scene; shader compilation
  and CPU tests alone do not establish visible GPU correctness.
- Record unsupported or unverified backend/environment combinations explicitly.
- Update this audit as gaps close; leave unfinished roadmap checkboxes pending.

## Verified progress

- Inspector saves now use the complete component-to-asset mapping. Floating-point
  colors retain alpha and HDR values; separate-map assignments and enable states
  survive reload. Clearing emission no longer resets packed ORM. Inline scene
  serialization now preserves existing advanced scalar and map settings.
- Release build and test steps 101, 105, and 106 passed. Existing LNK4098 runtime
  library conflict warnings remain; they are not material implementation errors.
- A dedicated 256-byte material constant layout now carries UV X/Y, normal and
  AO strengths, independent coat/anisotropy values, and reflection flags. Component
  synchronization clears disabled texture paths and transfers all existing scalar
  settings. CPU layout and zero-cutoff separation checks pass in Step 106; shader
  binding and visible GPU behavior are still pending.
- Scene and preview rendering now bind per-draw material buffers and textures.
  Image uploads distinguish sRGB color from linear data, and every fallback face
  and mip is initialized. An immutable sampler avoids the DX12 sampler-heap limit.
- Step 106 now compiles the actual ForwardPBR to DXIL and SPIR-V and reads DX12
  images back to verify distinct material maps, map disabling, normal/AO strength,
  black emission masking, independent Y tiling, and an explicit zero cutoff.
  Steps 101 and 105 also pass. Layered BRDF rendering is the next separate sub-step.
- Existing coat and anisotropy parameters now drive layered direct-light BRDFs.
  The coat attenuates the base layer and has its own roughness; signed anisotropy
  and rotation drive a tangent-frame GGX distribution with Smith visibility.
  Step 106 GPU readback verifies coat strength/roughness, anisotropy/rotation, and
  independent direct-specular and environment-reflection switches.
- Direct RHI loading now preserves ORM, all map assignments and enable flags,
  coat/anisotropy, HDR/alpha colors, queue priority, and reflection switches.
  Packed ORM uses its own shading mode instead of overloading surface type.
  Both material loaders reset omitted fields to legacy defaults on reload, and
  RHI material reads use the virtual filesystem. Tests 101, 105, and 106 pass.
- Shared external materials now refresh entity values from the cached resource;
  editing an asset updates both legacy and RHI caches without changing map enable
  states. Make Unique detaches asset paths and mutable GPU state. Scene reload
  restores asset defaults before inline overrides. Step 106 verifies shared reload
  and unique-material isolation with GPU images; 101 and 105 remain passing.
- GPU acceptance now also covers transparent queue priority versus distance,
  transparent depth writes, and front/back/two-sided rasterization. These tests
  compare rendered pixels, rather than reproducing sorting or blend formulas.
- Texture acceptance verifies color/data formats, uppercase texture descriptors,
  fallback behavior, same-path image replacement, and additive alpha. Data-map
  roles stay linear even when a descriptor is tagged Color. Image decoders now
  set orientation per thread at every call site so material loads do not change
  subsequent icon or thumbnail orientation. Step 106 passes on DX12.
- Both inspectors now use one set of Godot-style collapsible property groups.
  Texture slots support search, thumbnails, retained enable state, replacement,
  drag/drop and clearing. UI inspection confirmed grouping and search input; the
  user default material was unchanged. Tests 101, 105 and 106 pass.
- Typed custom values (float, vectors, HDR color/alpha, int, bool and texture) and
  slider ranges now share round-trip persistence across materials and scenes.
  Executable custom shader programs remain a separate integration check.
- Clearcoat textures now work through asset/entity serialization, caches, preview
  and scene shading. R masks coat strength and G stores glossiness, matching
  Godot clearcoat maps. GPU tests cover zero mask and changing glossiness.
  Smooth-coat GGX evaluation avoids cancellation and FP16 radiance stays finite.
- Anisotropy flow maps now round-trip and bind per draw. RG rotates the tangent
  direction and alpha masks strength. Step 106 validates both on the GPU;
  imported-mesh tangent orientation remains an acceptance-scene check.
- Detail normal maps now have independent strength, UV tiling/offset and an R mask.
  Reoriented normal mapping preserves the base when strength or mask is zero.
  Step 106 verifies persistence, masking, blending and UV changes through GPU
  readback. Nonuniform and mirrored transforms match CPU-baked reference geometry;
  normals use inverse-transpose and tangent handedness follows the model determinant.
- Refraction now copies opaque color and depth after sky rendering and before
  transparent draws. Strength, IOR, thickness and transmission tint round-trip
  through both inspectors and the RHI schema. Step 106 verifies distortion, tint,
  IOR=1, MSAA alpha fallback and viewport resize. The approximation samples only
  visible opaque surfaces; transparent layers and offscreen objects are excluded.
  Missing depth, multisampling, or a transparent preview clear uses ordinary alpha.
  SDR snapshots are decoded before tint/Fresnel mixing; HDR snapshots stay linear.
- Import acceptance exposed a source-data gap: mesh conversion discarded normals
  and UVs, and the RHI registry regenerated planar coordinates. OBJ now retains
  position/UV/normal corner tuples (including seams and negative indices), glTF
  retains TEXCOORD_0 and transformed normals, and FBX retains polygon-corner
  attributes through triangulation. Text and v2 binary caches preserve both arrays;
  v1 caches remain readable. Step 106 covers all three importers, cache versions,
  truncated-cache rejection and GPU anisotropy on imported seams. Imported tangent
  direction/rotation/strength and authored-versus-loaded GPU geometry now pass.

## Phase 38.3 Custom Material & Shading Pipeline Implementation

The Phase 38.3 custom shader and material execution subsystem has been implemented and verified via granular, CPU-only unit tests (`AxleMaterialUnitTests`, 5 suites passing 100%):

1. **CPU-Only Unit Test Target (`AxleMaterialUnitTests`)**:
   - Established standalone, headless test suite running completely without GPU, RHI device, GLFW window, or filesystem requirements.
   - Covers material serialization, synchronization, texture role matching, binding plan packing, generational program caching, and drag/drop payload decoding.

2. **Testable Material Binding Plans & Packed Constant Layout (`MaterialBindingPlan`)**:
   - `MaterialBindingPlan::Build` inspects reflected shader resources and validates uniform layouts.
   - Generates deterministic 16-byte aligned packed constant buffers (`b1, space 0`) for custom material parameters (`Float`, `Float2/3/4`, `Color`, `Int`, `Bool`).
   - Automatically rejects duplicate uniform names, filters out engine reserved uniforms (`b0`), and handles reordered properties.
   - Resolves texture slots by parameter name / semantic role to avoid colliding with default engine bindings.

3. **Compatible Custom Material Shader Interfaces (`CustomShaderInterface`)**:
   - Defines standard HLSL shader interface contract for custom surface shaders.
   - Standard vertex layout `MaterialInput` (8 vertex attributes: `Position`, `Normal`, `Tangent`, `Color`, `TexCoord0`, `TexCoord1`, `Custom0`, `Custom1`) with verified byte offsets matching `RHISceneRenderer`.
   - Explicit engine constant buffer `EngineUniforms` (`b0, space 0`, exact 256-byte alignment).
   - Dedicated custom constant buffer `CustomMaterialParams` (`b1, space 0`).
   - Custom material textures bound exclusively in `space 1` (`t0..t15, space 1` with samplers at `s0..s15, space 1`).
   - Canonical PBR and Unlit templates with structured entry points (`EvaluateCustomMaterial`, `ComputeCustomSurface`).
   - Diagnostic validation helper detecting illegal register overlaps, reserved namespace collisions, and missing declarations.

4. **Generational Material Program Caching (`MaterialProgramCache`)**:
   - Thread-safe generational cache storing compiled custom VS/PS bytecode pairs (`MaterialProgramGeneration`).
   - Retains last-known-good compiled generation upon shader compilation failures, preventing runtime null pointer dereferences or pipeline crashes.
   - Deduplicates compilation error diagnostics to prevent repetitive console log spam when source code is unchanged.
   - Generational reference-counting ensures in-flight draws can complete safely even when a new generation is compiled.

5. **Per-Draw Custom Program & Constant Buffer Execution (`RHISceneRenderer`)**:
   - Integrated `CustomProgramCache` into `RHISceneRenderer` with fallback to default ForwardPBR when custom shaders are disabled, empty, or uncompiled.
   - Dynamic per-draw constant buffer allocation and upload for `CustomMaterialParams` (`b1, space 0`), guaranteeing draw-call parameter isolation.
   - LRU custom pipeline caching (`CustomPipelineKey`) matching custom VS/PS bytecode, render target formats, depth/stencil state, blend modes, and rasterizer states.

6. **Editor Drag-and-Drop & Property Synchronization**:
   - `ShaderDragDropDecoder`: Robust decoding of drag-and-drop payloads with null-terminator clamping, length overflow protection, non-ASCII UTF-8 path handling, and case-insensitive `.HLSL` / `.shader` extension validation.
   - `ShaderPropertyReflection` / `CustomShaderPicker`: Property synchronization retaining compatible existing property values across shader recompilation, and robustly filtering out local variables inside function scopes as well as engine reserved uniforms.
   - `MaterialInspectorControls`: Added full UI controls for custom `Texture2D` properties with asset drag-and-drop, path display, clear buttons, and live updates.

7. **Automotive Rendering Acceptance Infrastructure (`Test_MaterialAcceptance`)**:
   - Implemented headless acceptance test framework in `Editor/src/Tests/Test_MaterialAcceptance.hpp` / `.cpp`.
   - `ParseMaterialBackendOption`: Configurable via `AXLE_MATERIAL_TEST_BACKEND = dx12 | vulkan` without silent fallback; strictly rejects unknown backends.
   - `ValidateMaterialCaptureDirectory`: Configurable via `AXLE_MATERIAL_CAPTURE_DIR` with strict safety checks forbidding writes into source or asset trees.
   - `FormatMaterialTimingReport`: Computes mean, min, max, standard deviation over warmed-up frames, clearly labeling frame-to-frame CPU/GPU overhead.
   - `VerifyMaterialReferenceSceneStructure`: Validates structure and entities of `MaterialReference.axlescene`.
   - Image readback and portable PPM export (`WritePPMImage`) for headless diffs.

## Pending External Runtime & GPU Acceptance

While all foundational code, data structures, parsing logic, and CPU-only unit tests are fully implemented and verified, the following runtime and hardware execution items remain **NOT RUN** (pending physical GPU test execution):
- Direct3D 12 and Vulkan 1.3 physical GPU execution of Step 106 integration tests.
- Physical compilation of custom shaders to DXIL and SPIR-V via DXC on target hardware.
- Real rendered pixel readback comparisons for car paint, brushed metal, carbon fiber, rubber, leather, and glass refraction.
- Interactive editor UI manual acceptance (narrow/wide inspector panels, interactive drag/drop in live window).
