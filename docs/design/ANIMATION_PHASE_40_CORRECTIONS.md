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

Other review findings remain open until a correction row and its evidence are
added here. The original review documents the pre-fix behavior, not the current
acceptance result.
