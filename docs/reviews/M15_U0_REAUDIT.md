# M15 U0 Re-audit — Sequential checkpoint

## M15-U0-01.1 — Freeze dirty-path policy and source commit

Result: **PASS**.

- Code source boundary: `19ac2a1` (M14 final handoff).
- Current repository HEAD: `bae680e` (documentation/exchange follow-up only).
- `git diff --name-only 19ac2a1..HEAD -- Engine Tests scripts`: empty; no Engine,
  Tests or script source changed after the accepted M14 code boundary.
- Current dirty path: `docs/agents/CODEX_GEMINI_EXCHANGE.json` only; this is the
  coordination record for the active M15 re-audit and is not implementation work.
- The M15 plan and recovery plan both identify `19ac2a1` as the M14 prerequisite
  source boundary and keep M15 implementation `HOLD`.
- The recovery plan preserves the user-authored scene policy and prohibits hard
  reset/overwrite/staging of that scene.
- The exchange task card remains M14 accepted; the new M15 re-audit scope is
  recorded in status message `CG-20261114T161457291196Z-CODEX-27f2bc`.

The item is complete because the source boundary, dirty-path policy, active task
scope and preservation rule are all recorded and no implementation source is
mixed into the re-audit.

Next sequential leaf: `M15-U0-01.2 — Preserve user-authored scene changes`.

## M15-U0-01.2 — Preserve user-authored scene changes

Result: **PASS**.

- `Samples/Sandbox/assets/scenes/Test.axlescene` exists and was not modified by this checkpoint.
- `git status --porcelain -- Samples/Sandbox/assets/scenes/Test.axlescene` is empty.
- `git diff --name-status 19ac2a1..HEAD -- Samples/Sandbox/assets/scenes/Test.axlescene` is empty.
- The recovery plan explicitly prohibits hard reset, overwrite, staging or commit of this user-authored scene.
- Existing scene history remains available for recovery; no scene file was staged or rewritten.

The item is complete because the user-authored scene is present, untouched and protected by the active M15 preservation rule.

Next sequential leaf: `M15-U0-02.1 — Create smooth-sphere fixture`.

## M15-U0-02.1 — Create smooth-sphere fixture

Result: **PASS**.

- The deterministic fixture `smooth_sphere_surface` exists in
  `Tests/Fixtures/M14/P0_REFERENCE_FIXTURES.json` with a 64x128 sphere, 128x128
  capture, untextured material, disabled post effects and checks for defined
  normal/no triangle banding/finite radiance.
- `python scripts/test_m14_p0_baseline.py` passed 5/5 and
  `python scripts/m14_p0_baseline.py --check` passed, confirming manifest schema,
  fixture identity and diagnostic channels.
- The Debug `AxleRenderUnitTests` target was rebuilt from the current tree and
  passed (exit 0); the log is `build/m15-u0-02-1-unit.log`.
- The user supplied an Editor screenshot showing `Axle Engine - Sandbox - [Test.axlescene]` with the sphere rendered in the Scene viewport.
- Visual result: the sphere appears smooth without visible triangle bands/faceting, black pixels or invalid radiance; the scene renders stably with the plane and light.
- This screenshot is the required visual evidence for the leaf; no Computer Use claim is made.

## M15-U0-02.2 — Create roughness/metal sweep fixture

Result: **PARTIAL / NOT RUN for the required visual gate**.

- Added tracked Editor fixture `Tests/Fixtures/M15/M15_RoughnessSweep.axlescene`
  and project `M15_RoughnessSweep.axleproj` in commit `93c44c3`.
- Fixture contains five builtin spheres tagged `roughness_004`, `roughness_012`,
  `roughness_025`, `roughness_050` and `roughness_080`, with a shared metallic
  material policy and roughness values 0.04/0.12/0.25/0.50/0.80.
- Fixture schema/content check passed.
- Editor visual confirmation is **NOT RUN** because the Computer Use native pipe
  remains unavailable. Expected display: five spheres in order with progressively
  broader highlights/reflections and no intensity jump.

Next action: open the fixture project in Editor and confirm the visual sweep; do
not advance the sequential checklist until the screenshot/readback is recorded.

## M15-U0-02.2 — Follow-up fixture repair and automated verification

Result: **PARTIAL / NOT RUN for the required visual gate**.

- Repaired the fixture project `Tests/Fixtures/M15/M15_RoughnessSweep.axleproj` so
  its `StartScene` resolves to the tracked roughness scene while retaining the
  Sandbox asset root required by the renderer's shader/material resources.
- Repaired the scene structure: the five sweep spheres now have distinct entity
  records `100004` through `100008`. The previous file nested them under the
  directional-light entity, so the Editor loaded only three entities.
- Added `scripts/test_m15_roughness_fixture.py` and registered CTest
  `M15RoughnessFixture`. It validates eight entities and the exact metallic
  roughness values `0.04, 0.12, 0.25, 0.50, 0.80`.
- Fixture validation passed: `python scripts/test_m15_roughness_fixture.py`,
  exit 0; `ctest --test-dir build -C Debug -R ^M15RoughnessFixture$`, exit 0.
- Rebuilt `AxleEditor` Debug and Release, exit 0. D3D12 strict startup verification
  loaded the fixture scene and rendered five UI frames in both configurations;
  both runs reported eight scene entities and exit 0.
- Native Computer Use was retried twice and remains unavailable, so the required
  Editor screenshot/readback confirming monotonic highlight broadening and no
  intensity jump is still **NOT RUN**. The checklist remains open.

Next action: capture the Editor visual result after the native Computer Use pipe is
available; do not mark this leaf PASS from startup or schema evidence alone.

## M15-U0-02.2 — Inline material isolation correction

User visual inspection found a fixture defect: all five sweep entities referenced
the shared `materials/Metal_Gold.axleasset`, so editing `roughness_080` changed the
other four spheres through the shared material identity.

- Removed the shared `MaterialHandle` and `AssetPath` from each sweep entity.
- The fixture now uses the serialized inline material values for each sphere, while
  retaining the common metallic/base-color policy.
- Extended `M15RoughnessFixture` to reject shared material identity in sweep blocks.
- Debug/Release strict startup still pass with eight entities; Debug/Release
  `M15RoughnessFixture` CTest still passes.

Result remains **PARTIAL / NOT RUN for the visual gate** until the user reopens the
repaired fixture, edits one sphere, and confirms the other four retain their own
roughness values and highlights.

## M15-U0-02.2 — Visual gate pass

Result: **PASS**.

- The user supplied an Editor screenshot after reopening the repaired fixture.
- Hierarchy shows all five sweep entities: `roughness_004`, `roughness_012`,
  `roughness_025`, `roughness_050` and `roughness_080`.
- The viewport shows smooth spheres with a continuous roughness response: the
  low-roughness sphere has a sharp, narrow highlight and the high-roughness sphere
  has a broad, soft highlight.
- No visible black pixels, triangle faceting or abrupt intensity jump is present in
  the supplied image.
- Combined with the inline-material isolation correction and the automated fixture
  validator, the U0-02.2 visual gate is closed.

The supplied screenshot is the visual artifact for this leaf; no local artifact path
was available from the native Computer Use pipe.

## M15-U0-06.2.1 — Freeze Hi-Z depth convention and reduction operator

Result: **PASS**.

- Current production shader `Engine/Resources/Shaders/HiZ.hlsl` uses the engine's
  zero-to-one depth convention.
- The conservative nearest-blocker operator is `min` over the 2x2 source samples.
- Out-of-range source coordinates clamp to the last valid source pixel, defining the
  edge policy for odd dimensions.
- `RHIViewTargets` allocates each level with `max(1, floor(size / 2^level))` and
  stores levels as independent `R32_FLOAT` textures.
- No Editor visual evidence is required for this contract-freeze leaf. The CPU
  reference and edge/identity tests remain separate sequential leaves.

## M15-U0-06.2.2 — Independent CPU Hi-Z reference

Result: **PASS**.

- Added `scripts/test_m15_hiz_reference.py`, which independently builds a complete
  Hi-Z pyramid from arbitrary depth arrays without importing production shader or
  renderer reduction code.
- Canonical fixture: `4x4 -> 2x2 -> 1x1`, with explicit expected min-reduction
  values and finite-sample validation.
- Registered CTest `M15HiZReference`.
- `python scripts/test_m15_hiz_reference.py`: exit 0.
- Debug and Release `M15HiZReference` CTest: exit 0.
- No Editor evidence is required for this CPU-only leaf.

## M15-U0-06.2.3 — Hi-Z reference edge cases

Result: **PASS**.

- Extended `scripts/test_m15_hiz_reference.py` with explicit edge fixtures:
  `1x1`, `3x5` and `1919x1079`.
- The test validates every expected pyramid dimension and explicit edge values for
  the small cases, plus finite output across the large odd-dimension pyramid.
- `python scripts/test_m15_hiz_reference.py`: exit 0.
- Debug and Release `M15HiZReference` CTest: exit 0.
- No Editor evidence is required for this CPU-only leaf.

## M15-U0-06.1 — Smooth-normal reference implementation

Result: **PARTIAL / NOT RUN for the required Editor visual gate**.

- Extended `Test_RenderPrimitiveIdentityUnit.cpp` with a high-fidelity sphere
  reference using radius `1.0`, `64` stacks and `128` sectors.
- The reference checks finite values, unit normal length, analytic radial normal
  error and the maximum adjacent triangle-edge normal angle.
- Current measured reference: `8385` vertices, maximum position/normal error
  `1.46001e-07`, maximum adjacent normal angle `3.97675` degrees.
- Debug and Release `AxleRenderUnitTests` builds pass; Debug and Release
  `RenderUnitTests` CTest pass.
- The Editor visual gate is still open. The user must open the smooth sphere fixture
  and supply a screenshot showing a smooth sphere without triangle bands or invalid
  pixels before this leaf can be checked PASS.

## M15-U0-06.1 — Visual gate pass

Result: **PASS**.

- The user supplied a Debug Editor screenshot of the roughness sweep fixture after
  the smooth-normal reference test was installed.
- All five spheres show continuous smooth shading with no visible triangle bands,
  faceting, black pixels or invalid regions.
- Combined with the analytic reference metrics and Debug/Release RenderUnitTests,
  the U0-06.1 gate is closed.

The supplied screenshot is the visual artifact for this leaf; no local artifact path
was available from the native Computer Use pipe.
