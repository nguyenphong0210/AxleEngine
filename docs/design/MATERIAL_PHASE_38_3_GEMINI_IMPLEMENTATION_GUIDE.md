# Phase 38.3 Material System: Implementation Handoff for Gemini

Date: 2026-11-02

Repository inspected: AxleEngine

Baseline inspected: `01da2d3` (`added icons`)

## 1. Assignment and execution limits

Complete the remaining **implementation and CPU unit-test work** for Phase 38.3.
The intended result is a reliable material authoring pipeline with the automotive
features already listed in `ROADMAP.md`. This assignment does not promise complete
feature parity with every Godot material feature.

You can edit code and run unit tests. You cannot operate the editor, inspect GPU
images, run a rendering acceptance session, or establish GPU performance. Design
your implementation so its decisions can be unit tested without a graphics device.

Follow these rules throughout the assignment:

1. Inspect the current branch before editing. This document is a baseline, not a
   reason to overwrite subsequent changes. Read applicable `AGENTS.md` files.
2. Preserve existing user changes and existing material/scene assets. Use temporary
   fixtures or the dedicated reference assets for tests.
3. Write code, comments, diagnostics, documentation and commit messages in English.
4. Implement one discrete sub-step at a time. Run its relevant CPU tests, then make
   a separate local commit. Do not combine the complete phase into one commit.
5. Do not push, launch GUI applications or request GPU/UI access as part of this
   handoff. Local commits are authorized by the user's standing preference.
6. Do not describe generated code, mocked bindings or CPU test results as proof of
   successful rendering. Keep runtime acceptance explicitly pending.
7. Do not mark the entire phase complete. Its final renderer/UI acceptance requires
   a separate operator or an agent with the necessary environment.
8. If a toolchain or dependency prevents a unit test from running, report the exact
   command and failure. Do not label an unexecuted test as passed.

Continue through the code and CPU-test sub-steps that do not depend on runtime
acceptance. A pending visual check is not a reason to stop all implementation.

## 2. Verified checkout state

The inspected worktree was clean. These features already have implementation
commits and should be preserved:

| Feature | Existing commit |
| --- | --- |
| Shared material inspector and searchable texture slots | `757e077` |
| Texture-driven clearcoat | `de82b56` |
| Anisotropy flow maps | `3cb17cf` |
| Masked detail normals and independent detail UVs | `71cf156` |
| Opaque-scene color/depth refraction | `1704e50` |
| Imported UV seams and normals | `5939e32` |
| Automotive reference materials and fixed-light scene | `b6ffa79` |

The following gaps were observed in source, rather than inferred from screenshots:

- `MaterialComponent` stores `UseCustomShader`, `CustomShaderPath` and
  `CustomProperties`, but the inspected `RHISceneRenderer` does not consume these
  fields to select and bind an executable custom material program.
- `Editor/src/Tests/Test_MaterialAcceptance.cpp` is absent and is not registered in
  `Editor/CMakeLists.txt`, although `MATERIAL_REFERENCE_SCENE.md` describes its
  intended acceptance behavior.
- The inspected material rendering test does not implement the documented
  `AXLE_MATERIAL_TEST_BACKEND` and `AXLE_MATERIAL_CAPTURE_DIR` workflow. Implement
  the documented workflow or correct the documentation; do not leave a false claim.
- `CustomShaderPicker.cpp` reads a `CONTENT_BROWSER_ITEM` payload as `wchar_t*`.
  Verify the producer in `ContentBrowserPanel.cpp`; the existing material texture
  picker uses the narrow-character payload convention. Fix this contract mismatch.
- The HLSL Studio template uses a legacy object/scene constant-buffer layout that
  differs from the current built-in renderer. Reusing its bytecode with the
  built-in layout is not a valid custom-shader implementation.
- Roadmap parent checkboxes remain pending even where implementation children are
  checked. Reconcile status using evidence, not by checking every remaining box.

Some earlier sessions reported GPU checks, but their local log/capture artifacts
are not present in the inspected checkout. Do not copy those reports into a new
acceptance result as if you reproduced them.

## 3. Read these files before implementation

Paths below are relative to the repository root.

| Area | Files and purpose |
| --- | --- |
| Scope/status | `ROADMAP.md`, `docs/design/MATERIAL_PIPELINE_AUDIT.md` |
| Existing limitations | `docs/design/MATERIAL_REFRACTION.md`, `docs/design/MATERIAL_REFERENCE_SCENE.md` |
| Authoring model | `Engine/Renderer/include/Axle/Material.hpp`, `Engine/Renderer/src/Material.cpp` |
| Entity/resource mapping | `Engine/Scene/include/Axle/Components.hpp`, `Engine/Scene/src/SceneSerializer.cpp`, `Engine/Asset/src/AssetManager.cpp` |
| GPU data | `Engine/RHI/include/Axle/RHI/RHIMaterial.hpp`, `Engine/RHI/src/RHIMaterial.cpp`, `Engine/RHI/include/Axle/RHI/RHIMaterialConstants.hpp` |
| Render integration | `Engine/Scene/include/Axle/RHISceneRenderer.hpp`, `Engine/Scene/src/RHISceneRenderer.cpp` |
| Actual surface shading | `Engine/Resources/Shaders/ForwardPBR.hlsl` |
| Texture resolution/upload | `Engine/Scene/include/Axle/MaterialTextureCache.hpp`, `Engine/Scene/src/MaterialTextureCache.cpp` |
| Compiler/reflection | `Engine/RHI/include/Axle/RHI/RHIShader.hpp`, `Engine/RHI/src/RHIShader.cpp` |
| Editable shader properties | `Engine/Renderer/include/Axle/ShaderPropertyReflection.hpp`, `Engine/Renderer/src/ShaderPropertyReflection.cpp` |
| Typed value persistence | `Engine/Renderer/include/Axle/ShaderPropertyPersistence.hpp`, `Engine/Renderer/src/ShaderPropertyPersistence.cpp` |
| Shared inspector | `Engine/UI/src/MaterialInspectorControls.cpp`, `Engine/UI/src/CustomShaderPicker.cpp` |
| Shader creation | `Engine/UI/src/HLSLStudioPanel.cpp`, `Engine/UI/include/Axle/HLSLStudioPanel.hpp` |
| Preview caller | `Engine/UI/src/AssetPreviewPanel.cpp` |
| Existing tests | `Editor/src/Tests/Test_Milestone10.cpp`, `Editor/src/Tests/Test_MaterialRendering.cpp`, `Editor/src/Tests/Test_MaterialMeshImport.cpp` |
| Test dispatch/build | `Editor/src/Test.cpp`, `Editor/src/Tests/TestCommon.hpp`, `Editor/CMakeLists.txt`, `Engine/Scene/CMakeLists.txt`, `Engine/Renderer/CMakeLists.txt` |

Keep the module dependency direction intact. In particular, RHI must not depend
on Scene, UI, or the high-level `MaterialComponent` to obtain custom properties.

## 4. Preserve the current rendering contracts

### 4.1 Built-in object and material constants

The built-in object push/root constants are **128 bytes**. Do not increase that
portable push-constant budget to fit custom parameters.

`RHIMaterialConstants` is **256 bytes**, aligned to 256, with this inspected layout:

| Field | Byte offset | Meaning |
| --- | --- | --- |
| `UVTransform` | 0 | Base UV scale.xy and offset.xy |
| `Surface` | 16 | Normal strength, AO strength, direct specular, environment reflection |
| `CoatAnisotropy` | 32 | Coat strength/roughness, anisotropy strength/rotation |
| `Flags` | 48 | Texture assignment mask and authored-material flag |
| `DetailUVTransform` | 64 | Independent detail UV scale/offset |
| `DetailParameters` | 80 | Detail normal strength |
| `Refraction` | 96 | Strength, IOR, thickness, enable |
| `RefractionTint` | 112 | Transmission tint |
| `RefractionFrame` | 128 | View size and snapshot availability |
| Reserved storage | 144 | Remaining bytes to 256 |

Keep matching C++ offset/size assertions and matching HLSL declarations. Alpha
cutoff must remain independent of clearcoat. Do not reintroduce overloaded fields.

### 4.2 Built-in resource bindings

The current built-in layout uses scene constants at `b1, space0`, light constants
at `b2, space0`, and material constants at `b3, space0`. Its texture descriptors
are in `space1`:

| Registers | Resources |
| --- | --- |
| `t0..t5` | Albedo, base normal, roughness/ORM, metallic, AO, emission |
| `t6..t8` | Irradiance cube, prefiltered environment cube, BRDF LUT |
| `t9` | Clearcoat map: R strength, G glossiness |
| `t10` | Anisotropy flow: RG direction, A strength |
| `t11` | Detail normal |
| `t12` | Detail mask: R |
| `t13..t14` | Opaque scene color and depth snapshots |

The built-in shader uses repeat sampling for material textures and a separate
clamped screen sampler. Preserve linear sampling of data maps and the existing
color-space handling of albedo/emission maps.

### 4.3 Feature behavior and ownership

- Disabled maps retain their authored path for future re-enabling, while effective
  GPU bindings/masks stop using the map. Clearing a slot removes the assignment.
- A missing texture must select an initialized role-appropriate fallback and must
  not retain a previous draw's texture.
- Detail normal strength zero or a zero detail mask preserves the base normal.
- Normals use inverse-transpose behavior for nonuniform transforms. Mirrored
  transforms affect tangent handedness. Preserve imported UV seams.
- Glass snapshots are taken after opaque/sky rendering and before transparent
  rendering. Never read the render target currently being written.
- Existing refraction is a screen-space, opaque-only approximation. MSAA, missing
  inputs and transparent preview backgrounds use the documented alpha fallback.
- Preview and scene share material semantics. Their different cameras/lights can
  produce different images; parity comparisons must control those inputs.
- Shared assets update their users; `MakeUnique()` detaches resource identity and
  mutable rendering state. A per-draw update must not mutate another entity.
- Current `RenderScene` waits for GPU queue completion before reusing retained draw
  resources. Preserve that lifetime guarantee. Do not add synchronization redesign
  to this task; a future asynchronous renderer will require fence-based retirement.

## 5. Work package A: establish genuinely CPU-only unit tests

Do this before introducing a new runtime custom-shader path.

1. Add a dedicated executable such as `AxleMaterialUnitTests` with its own main.
   Register it with CTest. Use existing repository conventions where practical.
2. Do not make it depend on `AxleEditor` or the existing `AxleTests` executable.
   The existing test target depends on editor/runtime deployment and Step 106 also
   executes graphics integration tests; it is not a CPU-only unit-test command.
3. Link only the libraries the tests actually need. Linking an RHI library is
   acceptable; initializing an RHI device is not.
4. Do not create a window, GL context, swapchain, descriptor pool or GPU texture.
   Do not call the real shader compiler in the unit suite. Compiler integration
   tests belong to a separately gated group.
5. Use explicit checks that fail the process in Release. `assert` alone is not a
   valid test mechanism when `NDEBUG` removes the check.
6. Use unique temporary directories and clean up only fixtures owned by the test.
   Never rewrite `DefaultMaterial.axleasset`, `CarTest.axlescene` or user shaders.
7. If full scene construction pulls in runtime initialization, test an extracted
   serialization/mapping helper at this layer and leave the full scene test in
   the integration suite. Do not fake a successful scene integration test.

Suggested test files:

```text
Editor/src/Tests/MaterialUnitTestMain.cpp
Editor/src/Tests/Test_MaterialSerializationUnit.cpp
Editor/src/Tests/Test_MaterialBindingPlanUnit.cpp
Editor/src/Tests/Test_MaterialProgramCacheUnit.cpp
Editor/src/Tests/Test_MaterialTextureSelectionUnit.cpp
```

Suggested commands, once the existing platform toolchain is configured:

```powershell
cmake --build build --config Release --target AxleMaterialUnitTests
ctest --test-dir build -C Release -R '^MaterialUnitTests$' --output-on-failure
```

Suggested commit: `test(material): add a CPU-only material unit test target`

## 6. Work package B: testable custom-material binding plans

### 6.1 Separate decisions from graphics API calls

Introduce a small CPU representation of a compiled material's binding plan.
Suitable locations are Renderer for packing/metadata helpers and Scene for
renderer-owned runtime resources. Suggested names are descriptive, not mandatory:

```text
Engine/Renderer/include/Axle/MaterialBindingPlan.hpp
Engine/Renderer/src/MaterialBindingPlan.cpp
Engine/Scene/include/Axle/CustomMaterialProgram.hpp
Engine/Scene/src/CustomMaterialProgram.cpp
```

Use the existing reflection structures as input:
`RHIShaderReflection`, `RHIShaderConstantBuffer`, `RHIShaderVariable`,
`RHIShaderResourceBinding`, and `RHIVertexAttribute`.

The CPU plan should contain:

- A validated shader/interface revision and supported resource declarations.
- Constant-buffer identities, sizes, and reflected variable offsets.
- A property-to-buffer mapping with validated value types and byte widths.
- Texture/sampler mappings with register, register space and resource role.
- A semantic-to-`RHIVertex3D` input mapping.
- The information needed to allocate a layout and per-draw resources later.
- A structured error when a declaration cannot be supported safely.

Avoid opaque callbacks for every single rendering command just to make mocks
possible. Unit test the plan and packed bytes; runtime code should consume the
same plan rather than recomputing a second interpretation.

### 6.2 Uniform packing requirements

For each reflected buffer, allocate a zero-initialized CPU byte array of its
logical size. Write engine values and compatible editable properties into their
reflected offsets. Allocate GPU buffers with the RHI-required alignment separately.

- Treat shader `bool` as a 32-bit integer; never copy `sizeof(bool)` bytes.
- Write float/int as 4 bytes, float2 as 8, float3 as 12, float4/color as 16.
- Respect HLSL cbuffer padding. Do not copy 16 bytes into a 12-byte float3 field.
- Keep HDR color values and alpha. Do not clamp them to display color ranges.
- Validate `offset <= bufferSize` and `valueSize <= bufferSize - offset` before
  writing. Use bounds checks that cannot overflow.
- A 4-byte field size alone does not distinguish float, int and bool. If existing
  reflection lacks type metadata, extend it or use a validated interface/type
  contract; do not guess the type from size.
- Engine matrices must follow the existing GLM/HLSL multiplication convention.
  Document transpose rules, and test with an asymmetric matrix rather than identity.
- Editable properties must not overwrite reserved engine uniforms.
- Match properties by buffer identity and variable name. Existing assets with
  name-only keys may use a compatibility lookup only when that name is unique.
- Preserve compatible values on reflection reload. Use declared defaults for new
  or incompatible fields. A removed property must not leak stale bytes.
- Unsupported arrays, storage resources and complex property types must produce
  an explicit unsupported-interface result unless you implement their full binding.

CPU tests must cover all eight current `ShaderPropertyType` values, including
texture properties as resource references rather than cbuffer bytes. Use synthetic
reflection fixtures with real padding, reordered fields, duplicate names across
buffers, invalid bounds, and incompatible types.

Suggested commit: `feat(material): validate and pack reflected material bindings`

## 7. Work package C: define and preserve a custom shader interface

### 7.1 Resolve the legacy template mismatch explicitly

The inspected HLSL Studio template declares:

- Object constants: model matrix, base color, then legacy `u_Params` containing
  roughness, metallic, AO and tiling.
- Scene constants: view-projection, then camera and sun fields directly after it.
- `u_BaseMap` at `t0, space0`.

The built-in object block has additional fields and different parameter meanings.
The built-in scene block contains view and projection matrices before camera data.
Blindly uploading built-in blocks to the legacy template gives incorrect values.

Use a dedicated custom-program binding path. Bind engine matrices/colors/camera
values by the validated custom interface and reflected offsets. Reuse a whole
constant block only when its ABI has been explicitly validated as matching.

Define a documented interface for newly created shaders. It must specify:

1. Vertex and fragment entry points, initially `VSMain` and `PSMain`.
2. Engine-owned object/scene/material names and their meanings.
3. Reserved registers/spaces and the location of editable material parameters.
4. Texture names/roles, sampler behavior and the normal-map convention.
5. Supported vertex semantics and the behavior for unsupported semantics.
6. The surface/culling/depth state controlled by the material asset.

Newly created shaders must use this interface. Existing files must not be silently
rewritten. Support the known legacy generated template through an explicit adapter
or validated binding rules, and provide a diagnostic for unsupported layouts.

### 7.2 Backend binding constraints

Inspect `RHIDevice.cpp` and the DXC options before choosing a layout. Existing
Vulkan offsets use sampler registers shifted by 128, while texture and cbuffer
registers both start at zero. Therefore a legacy `b0` and `t0` in the same register
space can collide after SPIR-V mapping.

Keep the new custom interface's resource namespaces unambiguous. For legacy
compatibility, an explicitly scoped compile-view normalization may remap the known
legacy texture declaration to a texture space, while leaving the asset file intact.
If you use such a transformation, handle tokens/comments/strings and includes
properly; do not perform a broad regular-expression replacement of HLSL source.

The compiled reflection and the NRI layout must describe the same transformed
interface. Do not change global device binding offsets without updating and
validating every affected shader/layout in the engine.

Do not assume `BuildPipelineLayoutDesc()` solves all of this automatically. Inspect
its root-constant name heuristics and ownership of temporary descriptor arrays.
If custom data uses descriptor-bound cbuffers, do not accidentally promote them
to push constants or exceed the 128-byte portable budget.

### 7.3 Vertex inputs

Construct custom vertex input attributes from the compiled vertex reflection.
Match semantic name/index to `RHIVertex3D` fields and their actual byte offsets.
Use reflected input locations where the backend requires them. The legacy template
has fewer attributes than ForwardPBR, so copying the built-in attribute-location
array without validation is unsafe.

CPU tests: legacy and new interface recognition, matrix/parameter mapping, Vulkan
register-space collisions, semantic order changes, missing/unsupported semantics,
and generated-template declarations. Real DXC compilation remains an integration
check, even if its source or fixtures are prepared by Gemini.

Suggested commit: `feat(shader): define compatible custom material shader interfaces`

## 8. Work package D: execute custom programs in the renderer

### 8.1 Program selection

Extend the scene draw preparation so a draw carries the custom shader request and
its property values from the effective material. Both entity overrides and shared
material assets must reach this path. The asset preview must use the same path.

Selection behavior:

| Input | Required behavior |
| --- | --- |
| Custom disabled | Built-in PBR |
| Custom enabled, empty/missing path | Built-in PBR plus actionable diagnostic |
| Valid supported custom program | Custom vertex/fragment program and its bindings |
| First compilation/interface failure | Built-in PBR plus compiler/interface diagnostic |
| Reload failure after a successful compile | Retain the last known good generation and expose the failure |
| Later successful reload | Switch to the new valid generation |
| Shader cleared or custom disabled | Stop using the old custom program |

### 8.2 Cache and reload

Create a renderer-owned program cache. Include resolved source identity, backend,
entry points, compiler/interface options and relevant variants in the key. Track
source and include dependencies. A source edit with unchanged file size must still
invalidate the program; a timestamp alone must not be treated as a content proof.

Inject the compiler and dependency fingerprint provider at the program-cache
boundary so CPU tests can simulate success/failure and include changes. Avoid
recompiling a known failed unchanged shader every frame, and avoid logging the
same diagnostic every draw.

Build new generations transactionally. A failure must not partially replace the
working shaders, reflection, layout or property mapping. Never restore an older
valid generation while presenting it as a successfully compiled new one.

Suggested commit: `feat(material): cache custom programs and retain valid reload generations`

### 8.3 Per-draw resources and pipeline state

Connect real compilation, layout creation and PSO selection to the validated plan.
Extend the existing retained draw resources or add equivalent renderer-owned
storage for custom constant buffers, descriptor sets and texture references.

- Bind constants/textures for each draw. Two entities using one shader and different
  parameters must receive different draw values.
- Keep resources and program generations alive until submitted work has finished.
- Rebind the correct layout, pools, engine constants and PSO when switching between
  built-in and custom draws, including draws after the sky pass.
- Include active color/depth formats, sample count, surface type, culling, depth
  state and shader generation in the appropriate pipeline cache identity.
- Preserve transparent priority/distance ordering. Shader selection must not bypass
  the existing material state decisions.
- Bind texture properties through the material texture resolver with an explicit
  color/data role. If role metadata is unavailable, use a documented default and
  expose an override; do not guess from filenames.
- Missing/cleared texture values must choose compatible initialized fallbacks.
- Validate texture dimension/type. The current resource binding metadata does not
  fully describe every texture dimension, so extend/validate it before treating an
  arbitrary declared resource as a Texture2D or cube.
- Respect descriptor/sampler capacity. Do not allocate one large sampler table per
  draw that can exceed DX12 sampler heap limits.

CPU tests verify program selection, pipeline keys, buffer contents, resource
identities, fallback decisions and lifetime ownership through a small fake backend
or resource-owner fixture. GPU correctness remains pending until the external run.

Suggested commit: `feat(renderer): execute custom material programs per draw`

## 9. Work package E: finish shader authoring and property exposure

1. Update the HLSL Studio creation template to the documented interface. Keep
   user-supplied shader names and existing duplicate-name protection.
2. Fix shader drag/drop using the actual payload encoding and `DataSize`. Validate
   termination/length before constructing a path; do not blindly cast or scan past
   the payload. Prefer a shared CPU payload-decoding helper over separate guesses.
3. In compiled-program metadata, expose editable properties that correspond to
   supported reflected user uniforms/resources. Function-local variables and
   engine uniforms must not become editable material properties.
4. Preserve source annotations such as display name, range, header and tooltip,
   but obtain actual binding offsets from compiled reflection.
5. Preserve compatible saved values when a shader is reassigned/reloaded. Keep
   existing typed property serialization backward compatible.
6. Surface compile/reload errors in the inspector and HLSL Studio through existing
   diagnostics or a small shared status model. Include path and useful error text.
7. Keep asset and entity inspectors on the shared controls. Do not add a second
   independent set of material fields.

CPU tests: narrow UTF-8 path payloads, non-ASCII paths, empty/truncated/unterminated
payloads, uppercase `.HLSL`, wrong extensions, missing files, default values,
property filtering, name ambiguity and compatible value preservation.

Suggested commits, separately:

- `fix(editor): decode shader asset drag payloads consistently`
- `feat(editor): expose validated custom material properties and diagnostics`

## 10. Work package F: restore acceptance infrastructure without claiming a run

Add and register `Editor/src/Tests/Test_MaterialAcceptance.cpp` as a graphics
integration test helper, not as part of the CPU-only target. Reuse the real
renderer/readback helpers already present rather than creating a parallel renderer.

Implement these integration checks in code:

1. Load `Editor/assets/scenes/MaterialReference.axlescene` through the project
   filesystem and check that the expected material samples and backdrop exist.
2. Save to a temporary path, reopen, and compare material values and rendered output
   under an identical camera, lighting and render configuration.
3. Compare asset-backed and detached inline versions of each reference material
   under controlled inputs. This checks common material semantics; it does not
   prove that two differently lit editor viewports must look identical.
4. Exercise SDR/HDR, resizing and the documented MSAA/refraction fallback.
5. Add a minimal custom shader fixture whose constant color is visibly distinct
   from built-in PBR, then change a parameter/texture and verify rendered readback.
6. Cover a successful custom reload, failed reload retaining the previous program,
   clearing/disabling custom mode, and switching built-in/custom draws in one frame.
7. Add an explicit backend selector and disable backend fallback for a requested
   backend, so a Vulkan test cannot silently pass by using DX12.
8. Add optional image capture to the requested directory, with clear filenames and
   no writes into user assets. Include backend and render configuration in results.
9. Add optional warmed-up timing collection. Report resolution, sample count,
   device and whether measurements include CPU recording/submission/queue waiting.
   Do not present end-to-end frame timings as isolated shader costs.

Implement the documented environment variables or change the documentation to the
actual supported command-line options in the same commit. A suggested contract is:

```text
AXLE_MATERIAL_TEST_BACKEND = dx12 | vulkan
AXLE_MATERIAL_CAPTURE_DIR = <explicit output directory>
```

Leave an unset backend selector compatible with existing tests. Reject an unknown
explicit backend instead of silently ignoring it. Validate a supplied capture
directory and report write failures as test failures when capture was requested.

Factor backend-option parsing, capture naming, timing statistics and scene
expectation checks into CPU-testable functions. A fake frame provider can test
capture/report behavior, but it must not replace the real render checks in the
integration executable.

Suggested commit: `test(material): restore automotive rendering acceptance infrastructure`

## 11. Minimum CPU unit-test matrix

| Area | Required cases |
| --- | --- |
| Standard material persistence | All existing scalars/maps/enables, independent UVs, HDR/alpha, old defaults on reload |
| Automotive persistence | Coat map, flow map, detail map/mask/UVs, refraction enable/strength/IOR/thickness/tint |
| Typed custom persistence | Float, Float2/3/4, Color, Int, Bool, Texture2D; ranges and escaped paths |
| Synchronization | Effective entity override, asset refresh, `MakeUnique`, two users with distinct mutable state |
| Packed layout | Existing size/offset contract, valid padding, bool width, float3 bounds, asymmetric matrix |
| Reflection mapping | Reserved engine fields, duplicate names, reordered/removed/new fields, incompatible types |
| Shader interface | Known legacy template, canonical template, unsupported declaration, backend namespace collision |
| Vertex plan | Semantic/index mapping, reordered locations, unsupported semantic, correct source byte offsets |
| Cache/reload | Initial success/failure, unchanged failure suppression, include edit, same-size edit, backend variants |
| Generation ownership | Last-good retention, transactional replacement, old generation remains owned by in-flight draw |
| Program selection | Built-in/custom toggles, missing/cleared path, invalid program, recovery after successful edit |
| Resource plan | Disabled, cleared, missing and replaced textures; per-draw isolation; role-correct fallback |
| Picker helpers | Payload encoding/length, non-ASCII path, bad extension and unchanged value after invalid assignment |
| Acceptance options | Explicit backend without fallback, bad backend, requested capture failure, report labeling |

Add tests to the smallest production helpers that own these behaviors. Do not
duplicate a production algorithm inside a test and then compare it with itself.
Use fixture inputs with explicit expected bytes/values and failure conditions.

## 12. External acceptance checklist: prepared by Gemini, run elsewhere

These items remain **NOT RUN** in Gemini's delivery unless a separate operator
provides actual results for the exact delivered revision.

### Build and compiler integration

- [ ] Build the editor and full test executable with the supported toolchain.
- [ ] Compile actual built-in and generated custom VS/PS shaders to DXIL and SPIR-V.
- [ ] Verify legacy custom shader compatibility and actionable diagnostics.
- [ ] Run existing material-related regressions, including Steps 101, 104, 105, 106.
- [ ] Record newly introduced warnings separately from pre-existing build warnings.

### GPU behavior

- [ ] Run Step 106 on requested DX12 and requested Vulkan with fallback disabled.
- [ ] Confirm both standard and custom parameters change real rendered pixels.
- [ ] Check two entities sharing a shader with different values in the same frame.
- [ ] Confirm texture replacement, disable, clear and missing-file fallback.
- [ ] Check imported/mirrored/nonuniform-scale normal and anisotropy behavior.
- [ ] Check glass over opaque geometry, edge/foreground depth rejection and fallback.
- [ ] Check SDR/HDR, MSAA, viewport resizing and layout switching after sky rendering.
- [ ] Capture the automotive scene and record optional-path timings with metadata.

### Actual editor UI

- [ ] Inspect asset and entity material groups at narrow and wide panel sizes.
- [ ] Search, assign, replace, disable and clear real project textures.
- [ ] Drag a shader from Content Browser; create one with a chosen name.
- [ ] Edit custom numeric and texture properties and observe a live preview change.
- [ ] Introduce a shader error; confirm diagnostics and last-good/fallback behavior.
- [ ] Save/reopen material and scene; verify edits persist and shared users update.
- [ ] Verify camera/lights stay fixed during the intended preview object rotation.

Use the reference assets for mutation checks. Do not use the user's default
material or vehicle scene as disposable test fixtures.

## 13. Work package G: honest documentation and roadmap reconciliation

Update `MATERIAL_PIPELINE_AUDIT.md` to distinguish historical gaps from implemented
changes and to identify any remaining runtime checks. Remove stale contradictory
progress statements only when the newer evidence is clear.

Update the reference-scene instructions so every described command/variable has
an implementation. Add a compact acceptance report template containing:

```text
Revision:
Platform / toolchain:
Unit-test command and exit code:
Editor / integration build: RUN or NOT RUN
DXC DXIL / SPIR-V compilation: RUN or NOT RUN
DX12 GPU acceptance: RUN or NOT RUN
Vulkan GPU acceptance: RUN or NOT RUN
UI acceptance: RUN or NOT RUN
Capture paths:
Performance method and measurements, if actually run:
Known failures / unsupported cases:
```

For roadmap status, distinguish **implemented**, **unit-tested**, and
**runtime-accepted** in notes or sub-items. A parent that includes GPU/visual
acceptance remains unchecked until those checks are performed. Do not infer
runtime acceptance from a code review or synthetic reflection fixture.

Suggested commit: `docs(material): document implementation status and pending runtime acceptance`

## 14. Scope boundaries and final delivery

Preserve the automotive features already implemented. Fix discovered regressions
in small separate commits, but do not use this assignment to add rim lighting,
subsurface scattering, height/parallax, billboards, UV2, transmission blur, or
other unrelated Godot features. Plan those after the current pipeline is accepted.

Texture descriptor mip/filter/wrap/max-resolution support is a separate concern
from assigning a material texture. Inspect it before claiming complete texture
importer parity; do not silently expand this handoff into an importer overhaul.

At the end, provide:

1. A list of atomic commits and the behavior each one implements.
2. Exact CPU unit-test commands, results and any unavailable toolchain limitations.
3. A list of real renderer integration and UI checks that are prepared but NOT RUN.
4. Known compatibility limits, unsupported shader declarations and fallback behavior.
5. The updated roadmap/audit paths and the next external acceptance action.

The correct handoff status can be **implemented and CPU-unit-tested; runtime
acceptance pending**. It must never be **fully verified on DX12/Vulkan** when only
code and unit tests were available.

## 15. Ready-to-use prompt for Gemini

> Implement the remaining AxleEngine Phase 38.3 work according to this document.
> Begin by inspecting the current branch and the listed files. Work through
> packages A-G in order, splitting each package into the stated atomic commits.
> You are authorized to edit code, add deterministic fixtures, run CPU unit tests
> and create local commits. You have no GPU/UI acceptance capability: separate
> CPU-testable binding/packing/cache decisions from runtime resource operations,
> prepare the real integration checks, and label them NOT RUN. Preserve existing
> user changes and material behavior. Do not push or mark the phase complete until
> its external runtime acceptance has actually been performed.
