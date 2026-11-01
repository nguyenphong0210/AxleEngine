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
