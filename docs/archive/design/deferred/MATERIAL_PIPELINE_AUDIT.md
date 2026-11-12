# Material pipeline audit (Phase 38.3)

> **Deferred reference:** outside the active architecture cleanup. Follow the
> [optimization roadmap](../../../ROADMAP.md); embedded prompts and old priorities
> are historical guidance, not a current assignment. Recheck APIs before resuming.

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

## Implementation history

The checkpoints below are chronological. Current validation status and supported
interfaces are recorded in the runtime completion section and acceptance report.

- Inspector saves now use the complete component-to-asset mapping. Floating-point
  colors retain alpha and HDR values; separate-map assignments and enable states
  survive reload. Clearing emission no longer resets packed ORM. Inline scene
  serialization now preserves existing advanced scalar and map settings.
- Release build and test steps 101, 105, and 106 passed. Existing LNK4098 runtime
  library conflict warnings remain; they are not material implementation errors.
- A dedicated 256-byte material constant layout now carries UV X/Y, normal and
  AO strengths, independent coat/anisotropy values, and reflection flags. Component
  synchronization clears disabled texture paths and transfers all existing scalar
  settings. CPU layout and zero-cutoff separation checks passed before the GPU
  binding checks described in subsequent checkpoints.
- Scene and preview rendering now bind per-draw material buffers and textures.
  Image uploads distinguish sRGB color from linear data, and every fallback face
  and mip is initialized. An immutable sampler avoids the DX12 sampler-heap limit.
- Step 106 now compiles the actual ForwardPBR to DXIL and SPIR-V and reads DX12
  images back to verify distinct material maps, map disabling, normal/AO strength,
  black emission masking, independent Y tiling, and an explicit zero cutoff.
  Steps 101 and 105 also pass. Layered BRDF rendering followed in the next checkpoint.
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

## Custom material runtime completion (2026-11-02)

The Gemini handoff supplied useful CPU helpers, but its runtime path was not
accepted at that revision. Review found descriptor sets being treated as root
descriptors, missing scalar/vector reflection types, unbound custom textures,
incomplete reload invalidation, and an acceptance suite that was never called.
The following fixes now have real DX12 and Vulkan GPU coverage:

- DXC reflection retains scalar type, vector dimensions, array metadata and matrix
  layout. Binding plans own the active property schema. HDR Tint, uint/int/bool,
  scalar and vector values pack at reflected offsets; float3 writes exactly 12
  bytes. Unsupported arrays, storage resources, ambiguous names/addresses,
  out-of-bounds fields and unavailable vertex streams fail with diagnostics.
- The canonical template uses EngineObject at b0/space0, EngineScene at b1/space0
  and MaterialParameters at b2/space0. Textures and samplers use space1. CPU
  allocations align CBVs to 256 bytes; logical reflected buffers have their own
  sizes. Supported vertex inputs are POSITION0, NORMAL0, TANGENT0, TEXCOORD0 and
  COLOR0 from the existing 64-byte mesh vertex. UV1 does not alias UV0.
- Custom draws bind reflected descriptor sets containing CBVs, Texture2D SRVs and
  samplers. Each draw retains its generation, buffers and textures. Buffers grow
  across reloads; allocation/upload failures use a diagnosed built-in fallback.
  Built-in binding state is restored after a custom draw and after sky rendering.
- Candidate compilation, interface validation, layout creation and a baseline
  graphics PSO must succeed before publishing a generation. Failed edits retain
  the last usable generation for that shader key. Missing main files select
  built-in rendering. Include changes, including macro-expanded includes observed
  by DXC, invalidate the cache; deletion and restoration are covered by GPU tests.
  Fingerprints are reused for entities sharing a shader within one frame.
- Known legacy ObjectConstants/SceneConstants templates remain supported.
  Preprocessing expands includes before a compile-view adapter remaps legacy
  texture registers to space1; authored files are not rewritten. Samplers keep
  their NRI binding offset.
- Authoring keeps compatible values and initializes new typed defaults. Comment
  annotations support Default, Range, Color, Header and Tooltip; source scanning
  excludes comments and local declarations, while compiled reflection determines
  the active schema. Gain and Enabled default to one in new templates. Reserved
  fields and the standard u_BaseMap slot are not duplicate editable properties.
- Asset Inspector and preview share the same material instance from the first
  frame, so runtime schema and reload diagnostics are visible before any edit.
  Custom Texture2D properties reuse searchable project texture controls with
  thumbnails, validated drag/drop, replacement and clearing.
- Step 106 now calls the automotive acceptance suite. Explicit backend selection
  disables fallback and checks the actual device. RGBA16F readback uses eight
  bytes per pixel, aligned rows, half-float decoding and finite-value checks.
  Save/reopen and shared-asset versus detached-inline renders compare equal.
  Capture/report write failures fail the test. Timings compare baseline,
  detail-normal and detail-normal-plus-refraction configurations.

See [the acceptance report](../reports/MATERIAL_PHASE_38_3_ACCEPTANCE_REPORT.md) for commands,
hardware, output paths, timing results and the remaining interactive UI checks.
CPU suites use temporary filesystem fixtures without initializing a GPU/window.
These results establish the Phase 38.3 rendering paths on the tested hardware,
not complete Godot feature parity or a texture-importer overhaul.
