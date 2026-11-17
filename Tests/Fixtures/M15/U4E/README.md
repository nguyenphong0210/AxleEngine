# M15 U4-E Editor fixture

Open `U4E.axleproj`. The scene contains two normal-mapped spheres in a gradient HDR environment: one has mirrored, nonuniform scale, the other has positive nonuniform scale. Both sample the actual linear `normal.tga` (RG=192/255,128/255).

Focus each sphere; inspect its smooth normal response while toggling GTAO/contact and changing transform rotation/scale. Expected: finite continuous shading and no new triangle bands or stale occlusion after movement. This authored scene supports manual inspection; it does not claim skeletal animation coverage.

The automated native test uses freshly published deformed vertex generations moving across a real rear receiver, including current depth/normal readback, disocclusion and comparison with fresh view targets. Its reference and command matrix are recorded in `docs/reviews/M15_U4_BATCH_5_CODEX_REVIEW.md`. An Editor startup run alone does not constitute visual inspection.
