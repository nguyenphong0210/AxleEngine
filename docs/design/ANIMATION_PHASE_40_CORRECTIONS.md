# Phase 40 foundation corrections

Date: 2026-11-03. Follow-up to the
[review](ANIMATION_PHASE_40_GEMINI_REVIEW.md) of `a6efa8c`.
Each row records an implementation commit with permanent production-API tests.
Full Phase 40 rendering/editor acceptance remains separate.

| Review finding | Correction and evidence |
| --- | --- |
| R3 | Canonical declared parameter storage by stable ID; names resolve to that slot. Type/finite checks, collision-safe renames and runtime-only trigger defaults. Permanent regressions cover both alias directions, consumption, wrong-type writes and invalid conditions. Release Animation CTests PASS; `build/animation-fix-parameters-build.log`. |

| R6 / R7 / R9 | Clone rejects opaque predicates with a diagnostic, snapshots mutable legacy motions, and isolates animator rigs. Scene authoring copies reset separately from snapshots. Explicit entry/reset and per-cycle exit gates preserve residual time. Permanent tests compare coarse/fine steps, late trigger conditions, reset and clone isolation. Release Animation CTests PASS; `build/animation-fix-runtime-build.log`. |

| R2 / rig validation | Skeleton text now preserves local matrices and inverse binds; missing binds require reimport rather than silent identity fallback. All decoders publish only validated candidates. Compatibility signatures include the complete bind basis. Pose mixing rejects incompatible rigs/nonfinite values; failed channel edits retain last-good data. Release Animation CTests PASS; `build/animation-fix-bind-codecs-build.log`. |

| R8 | A validated, explicit little-endian v2 geometry codec retains submesh/material partitions and bounds; v1 remains readable. Both bake/load reject malformed indices, joints, weights, attributes, counts and bounds; decode accepts an expected rig signature and retains last-good output. Permanent valid-sized-corruption and metadata tests PASS; `build/animation-fix-geometry-build.log`. |

| R4 / R5 | glTF import retains helper ancestors without changing original joint indices, preserves explicit signed bind TRS, evaluates/persists STEP and cubic Hermite tangents, validates source buffers and skin associations, preserves primitive material partitions, and rejects ambiguous multi-skin inputs. Tiny checked-in fixtures cover interior samples, helper/bind basis, signed scales and last-good failures. Release Animation CTests PASS; `build/animation-fix-gltf-build.log`. The basis/interpolation contract follows the [Khronos glTF specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#animations). |

| R1 controller core | Stable controller/state/transition identity and graph metadata survive compilation. Typed legacy rules migrate into serializable conditions; self-contained rig/clip fallbacks retain keyed poses, including cubic data. FileSystem resource resolution validates rig compatibility and fails explicitly for missing motions or unsupported 2D runtime. Referenced mesh loading uses cooked geometry; snapshot helpers preserve rig/mesh data. Permanent keyed-pose and entry/reset tests PASS; `build/animation-fix-controller-build.log`. Scene/assembly integration is tracked separately below. |

| R1 scene/assembly integration | One shared binding adapter resolves cooked and inline resources, honors unique versus referenced controllers, and gives stable-ID overrides precedence. All three scene/assembly writers capture current keyed clips and complete rig/geometry snapshots before atomic file replacement. Regression tests compare actual skinning matrices, exercise assembly document save/reopen, independent clocks, owner-relative cooked dependencies and failed-save preservation. Release Animation CTests PASS; `build/animation-fix-scene-release.log`, `build/animation-fix-scene-ctest.log`. |

| A2 FBX hardening | Separate CPU importer preserves helper ancestors and cluster indices, triangulates face corners with source normals/UV seams and material partitions, selects the strongest four influences and validates before publication. Canonical bind-basis conversion compensates inverse binds and transforms positions/normals/winding together. Missing, ambiguous, oversized or unsupported skin contracts fail with diagnostics and retain previous outputs. Two authored FBX fixtures verify keyed poses and reflected source axes. Release Animation CTests PASS; `build/animation-fix-fbx-build.log`, `build/animation-fix-fbx-ctest.log`. FBX curves use an explicit bounded 30 Hz linear TRS resampling policy; adaptive/error-bounded curve baking and a broader exporter corpus remain pending, so A2 acceptance is not closed by these fixtures alone. |

Other review findings remain open until a correction row and its evidence are
added here. The original review documents the pre-fix behavior, not the current
acceptance result.
