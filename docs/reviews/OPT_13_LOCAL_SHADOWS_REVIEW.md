# OPT-13 local shadows: separate-context source review

Review boundary: `9bd4856..e50c811`, followed by the working-tree corrections in
this checkpoint. Reviewer: Codex agent `/root/local_shadows_final_review` in a
separate context, read-only. This is a source review, not independent GPU execution.

## Findings and corrections

- **LS-R1, P2 ? Assembly persistence:** new shadow settings were only stored by
  SceneSerializer. Scene and Assembly now share the shadow-field codec;
  AssemblyNode retains Point/Spot data through Assembly Editor load/save too.
  Actual entity/data round-trips verify every shadow setting.
- **LS-R2, P2 ? Descriptor capacity:** the new seventeenth forward texture could
  reduce the former capacity. Capacity now derives from the actual texture count
  and reserves default/sky sets in addition to 4096 material sets. A real 4096-draw
  regression exposed the missing second reserved set during repair and passes
  after correcting the reservation.
- **LS-R3, P2 contract concern ? Mesh mutation:** cached generation bounds/bytes
  must agree. Mutable RHIMesh buffer accessors were removed; inspection is const
  and geometry replacement uses Init/StreamUpload. Untracked native-pointer
  writes are outside the published-mesh contract. Static API checks and actual
  mesh replacement invalidation/readback cover the supported path.
- **LS-R4, P2 ? Malformed floats:** the new shared parser catches invalid/out-of-
  range numeric conversion and retains defaults, matching surrounding light
  fields. Both Assembly paths and scene parsing cover invalid and overflow text.

The reviewer rechecked all corrections and reported no remaining actionable
source findings. Additional source inspection covered GPU slot prefixes, six-face
selection, per-light radiance ownership, tile-clamped PCF, exact cache keys,
cutout invalidation, submission publication, frame drain, resource leases, CSM
shared-depth behavior, OpenGL policy and starter assets.

## Evidence boundaries

The reviewer independently ran `git diff --check` and structural checks for seven
unique nonzero embedded material identities. The reviewer did not run builds or
GPU tests. Implementer verification after repair: Debug RenderUnitTests passes;
physical D3D12/Vulkan local tests pass, including 4096 draws, replacement geometry
and cache publication after successful submission; RHIResource tests pass on both
backends. See final delivery for rebuilt product/full-suite and Release evidence.

The abandoned-recording cache fixture explicitly resets/drains recording pins
before Prepare, matching the production frame contract. Recording by itself does
not make the map reusable; actual successful submit plus OnSubmitted does.
