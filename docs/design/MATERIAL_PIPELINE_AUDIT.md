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
