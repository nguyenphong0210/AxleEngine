# VFX Editor authoring guide

Updated: 2026-11-03. This guide describes the implemented authoring workflow.
The editor is inspired by modular particle editors; it does not claim complete
Unity Particle System or VFX Graph feature parity.

## Create and organize an effect

1. Use Content Browser **Create VFX** or the VFX Editor **New** command.
   A new effect contains **zero emitters**. Opening the window never creates
   fire, smoke, sparks, textures or scene entities.
2. Add the first emitter with **+** in the Effect pane or **Add Emitter** in
   Modules. A neutral emitter uses white particles, a point origin, a fixed
   lifetime/speed/size and continuous emission. Optional lifetime, shape,
   physics and texture-sheet modules start disabled.
3. Edit its name and Main settings. Names are display labels; stable emitter
   IDs preserve selection, references and event routing across renames/reorder.
4. Right-click anywhere on an emitter row to duplicate, move, delete, solo or
   restore visibility. Duplicate is explicit; Add never copies the selection.
   Deleting the last emitter returns to a valid empty effect.

The row checkbox is the **authored enabled state** and is saved. **V/H** and
Solo affect preview visibility only. Trigger Only suppresses automatic emission
while allowing events from another emitter to spawn particles.

Wide windows provide resizable Effect / Preview / Modules columns. Narrow
windows use these three tabs. The title is **VFX Editor**.

## Edit modules

Search by module name, or select **Enabled modules only**. A module checkbox
controls simulation, not just the display of its fields. Disabling a module
retains its authored settings; re-enabling uses those settings again.

Right-click a module header to Reset, Copy or Paste that module. Paste requires
the same module type and rejects an invalid sub-emitter graph. Reset/paste
restart preview for a reproducible result. Main copies simulation settings,
including Start Color, without changing emitter identity or placement.
Renderer operations cover its material binding and rendering settings; the
independent texture slots remain in Textures.

| Group | Implemented authoring and behavior |
| --- | --- |
| Main | Emitter transform/trigger-only, duration, looping, autoplay, simulation space, pool size, initial lifetime/speed/size/rotation/color, gravity and drag. |
| Emission | Rate over time, legacy periodic bursts, and authored timed bursts with count range, cycles, interval and probability. Disabled emission advances scheduling without replaying skipped bursts when enabled again. |
| Shape | Point, sphere, hemisphere, box, cone, line, circle and OBJ mesh-surface emission. Disabled shape emits at the origin along the default upward axis. |
| Size / Speed over Lifetime | Independent scalar curves. Size combines sampled start/end size with its curve; speed modulates motion. Disabled size retains start size; disabled speed uses multiplier one. |
| Color / Alpha over Lifetime | Independent gradient and alpha multiplier curve. Enabled color multiplies Start Color by the gradient; disabled color uses Start Color alone. Alpha multiplier is independent of the gradient's alpha keys. |
| Rotation over Lifetime | Sampled angular velocity; disabling stops further rotation and keeps start/current orientation. |
| Forces | Constant acceleration, directional wind/influence, buoyancy and authored force fields. Main gravity/drag and Noise remain independent. |
| Noise | Curl frequency, strength, octaves, lacunarity, persistence and scroll. |
| Collision | Floor, authored planes/boxes, restitution, friction, radius, minimum bounce speed and kill settings. Scene Physics requires the bound physics service. |
| Sub Emitters | Choose an emitter actually present in this effect; Birth/Collision/Death, count, probability and color/velocity inheritance. Missing targets, self-links and cycles are rejected before applying the link. |
| Textures / Texture Sheet | Search/drag/replace/disable/clear albedo and normal textures independently of animation. Sheet rows/columns, frame count, cycles and frame interpolation are authored. Disabling sheet animation restores full-texture UVs. |
| Renderer | Camera-facing, axial-Y or velocity-aligned billboards; blend mode, supported particle material, lighting, camera fades and soft intersections. |

Curve editing supports Bezier control points and numeric linear keys. Gradients
have separate color and alpha tracks. Reverse and Reset are generic operations;
effect-specific Flame/Smoke/Sparks buttons are absent from the authoring UI.
Existing gameplay preset factories and checked-in acceptance effects are retained
for their explicit callers; they are not applied to new editor documents.

## Preview and transport

- Play/Pause, Stop, Restart, fixed Step, playback speed and timeline seek operate
  on panel-owned systems, independently of user scene simulation.
- Enter a count and press **Emit** for a manual burst in the selected emitter.
  This is a preview command and does not edit the Emission module.
- Preview Settings control background, grid, live bounds and camera framing.
  **Allow authored looping** only permits looping emitters to loop; it does not
  convert a non-looping emitter into a looping one.
- Drag LMB to orbit, RMB/MMB to pan horizontally and vertically, wheel to zoom,
  or press F over Preview to frame the visible effect. Bounds include particle
  size, local transforms and conservative billboard/velocity-stretch extents.

Preview preferences, visibility, camera and transport state are not asset data.
Authored module values, enable states, seed, transforms and resources are data.

## Save, open and use in a scene

**Open** includes project effect search and an editable path. **Save As** chooses
the asset destination/name; `.axleasset` and existing `.axle_vfx` effects are
supported. Suggested new paths use the project's asset directory. Ctrl+S saves,
Ctrl+Shift+S opens Save As, Ctrl+N creates a new effect, Ctrl+Z / Ctrl+Y undo/redo
when a text field does not own input.

New/Open/close protect unsaved edits through Save and Continue / Discard / Cancel.
Dialogs remain available when the Effect tab is hidden. File and validation
errors leave the working document intact. Saving uses atomic replacement; failed
saves preserve the previous file and dirty state. Property drags are grouped;
selection, add/delete, duplicate and reorder finish the preceding edit first.

With a scene bound, choose **Scene Position** and **Create Scene Instance**.
The entity uses the effect's name and complete definition. A clean saved
document creates an asset reference; an unsaved/modified definition creates an
inline instance. Scene and preview own separate simulation systems.

**Apply Working Copy** transfers the complete definition to the selected particle
entity and explicitly detaches its shared reference. **Make Unique** detaches
an existing reference. These actions do not modify the source asset or other
instances. The editor's document undo history does not undo scene creation/apply.
Save/discard the current document before explicitly opening a different scene
effect; unrelated scene selection preserves the working document.

## Compatibility and limits

Old assets without module enable flags preserve the previous always-evaluated
behavior. Their new Start Color multiplier defaults to white. New neutral
emitters opt out of optional modules explicitly. Both flags and inactive values
round-trip through the shared codec, inline scenes and referenced assets.

This remains a CPU particle simulation with billboard output. Mesh Surface is
an emission shape, not a mesh-particle renderer. Material support is the subset
described in the [Phase 39 acceptance report](VFX_PHASE_39_REVIEW_AND_ACCEPTANCE_REPORT.md).
OBJ sampling and the MSAA soft-depth fallback retain their documented limits.
There is no GPU graph editor or general-purpose shader graph here.

Further feature work needs separate schema/runtime/rendering/UI acceptance:

1. Trails/ribbons and mesh-particle output, including materials, bounds and sorting.
2. Rate over distance, start delay, prewarm and richer per-axis velocity/size/rotation
   or random-between-curves authoring.
3. Distance culling, simulation budgets and a dedicated particle performance view.
4. GPU simulation/graph authoring, if required by measured effects and platform budgets.

These are future capabilities, not disabled placeholder controls in this editor.

## Verification

`Test_VFXAuthoringUnit.cpp` covers module persistence/compatibility, runtime toggles,
history boundaries, rejected graph edits, scene instancing/apply, transformed
bounds and real ImGui layout generation at wide/narrow sizes. It also checks
unsaved dialogs with the Effect tab hidden and verifies drawing never inserts
an example emitter.

The physical D3D12 fixture renders the actual panel through ImGui, reads its
production preview, and checks empty -> authored emitter -> empty pixel changes.
It writes `build/vfx-captures/d3d12/vfx-editor-workspace.ppm`. This is an automated
offscreen capture; native mouse/keyboard acceptance and OpenGL remain separate.
