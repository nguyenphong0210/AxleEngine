# Milestone 20 — Asset Cooker, Linter and Packaged Runtime Plan

**Status** — Proposed. Release implementation by checkpoint after M19 acceptance.
**Phase 49** — Dependency and linting (49.1), derived products (49.2), packaging and acceptance (49.3).
**Architecture** — [Dependency graph, derived assets and publication](ARCHITECTURE.md).
**Inputs** — [M17 animation](../MILESTONE_17_ANIMATION/PLAN.md), [M18 VFX](../MILESTONE_18_VFX/PLAN.md), [M19 Canvas2D](../MILESTONE_19_CANVAS2D/PLAN.md).
**Handoff** — [M21 geometry products](../MILESTONE_21_GEOMETRY_VISIBILITY/PLAN.md).

## 1. Starting point and deliverable

Reuse `AssetCooker`, `PackageWriter`, mounted VFS, selected Runtime snapshots and the [detached Runtime export verifier](../../../scripts/verify_runtime_export.py). The [OPT-09 review](../../reviews/OPT_09_IMPLEMENTATION.md) documents the existing export contract. Verify that baseline before introducing project reachability, a common linter and versioned derived products. Keep asset enumeration and packaging coverage in M17–M19. M20 owns the consolidated traversal and delivery of the integrated project.

Ship `Data.axlepak` from declared project roots. Package a real character, multi-emitter effect and functioning Canvas HUD with shaders, fonts and textures. Provide headless dependency and validation diagnostics, texture processing, shader prewarm products and archive compression. Release hardware tuning and final default promotion remain in M24.

## 2. Execution and acceptance checklist

| Checkpoint | Implementation and independently reproducible gate |
| --- | --- |
| **M20-A1 · Input snapshot and dependency contract** | Establish the project manifest, typed references, schema/provenance, stable ordering and plugin dependency boundary. Resolve the transitive graph for real scene and assembly roots. |
| **M20-A2 · Linter and reachability** | Expose the shared validator through an offline command with stable exit codes. Compare reachable files to the archive and verify missing, unsafe, duplicate and cyclic input behavior. |
| **M20-B1 · Derived inputs and cache** | Add content/profile/tool-version provenance, targeted invalidation, staged replacement and reusable derived-product interfaces for M21. |
| **M20-B2 · Texture products** | Select platform/profile formats, produce mip chains and BCn data where supported, and validate dimensions, color and fallback through the shipping texture decoder. |
| **M20-B3 · Shader products** | Compile reachable shader variants for selected backends offline. Report diagnostics and show startup use of valid cache entries with safe rebuild behavior. |
| **M20-C1 · Archive production** | Extend the versioned reader/writer for bounded compression and robust verification. Publish atomically and prove a failed build retains the last good delivery. |
| **M20-C2 · Product and acceptance** | Export and validate the integrated project in a clean Runtime directory, complete the matrix and hand the derived-product contract to M21. |

A1–A2 implement Phase 49.1, B1–B3 implement Phase 49.2, and C1–C2 close Phase 49.3.

- [ ] **M20-01 · Dependency graph (A1).** Snapshot selected project, entries, scene/assembly references, explicit dynamic roots and selected plugin declarations. Resolve native identity and raw paths using production asset APIs. Verify nested/transitive references, duplicate UUIDs, unresolved required paths, optional fallbacks, excluded orphan assets and cycle semantics for each relevant schema.
- [ ] **M20-02 · Headless diagnostics (A2).** Run without Editor UI or a GPU. Report typed issues with source asset, field/node, target, severity and a deterministic dependency chain. Cover missing or dangling data, oversized textures and raw texture data that violates its selected profile. Provide stable machine-readable output and nonzero error status. Use the same preflight from CLI and Editor export.
- [ ] **M20-03 · Cache and derived products (B1).** Validate schema and provenance using content, settings, profile and tool identity. Verify clean/incremental agreement, dependency reimport invalidation, selective reuse, corrupt cache recovery, concurrent/retried cook behavior and base product fallback for incompatible optional output.
- [ ] **M20-04 · Texture cooking (B2).** Resize or reject per profile, generate mips in the appropriate color domain and encode supported BCn families for color/normal/scalar/HDR cases. Cover alpha, normal mip behavior, small/non-block-aligned textures and declared uncompressed fallbacks. Verify native upload/readback and no accidental sRGB conversion.
- [ ] **M20-05 · Shader prewarm (B3).** Cook reachable backend-specific bytecode and variant metadata. Verify key invalidation, bad shader diagnostic propagation, valid packaged cache hits on D3D12/Vulkan and a clearly declared compatibility path. Keep API/driver-specific PSO caches optional and keyed separately.
- [ ] **M20-06 · Pak verification and publication (C1).** Preserve versioned VFS lookup and strict package-only Runtime startup. Require bounded compression, per-entry integrity, checked sizes/offsets and deterministic TOC ordering. Test corruption, truncation, oversized decode, normalized path collision and failure during publication. Compression fulfills the archive requirement. Encryption can be scoped separately if required by a product.
- [ ] **M20-07 · Packaged product (C2).** Test project selection, plugin/content overlays, all three authored systems, strict detached Runtime, linter machine output, loose/cooked logical parity and required/optional failures. Record package size, reachable entry count, cache behavior, shader/texture product use, artifacts and future M21 integration points.

## 3. Evidence and exit

Freeze one reusable gameplay fixture. A controlled action changes an animator state, emits an authored effect and updates a HUD value. A Canvas control dispatches at least one real action. Compare ordered fixed-tick state and event traces on equivalent loose and packaged runs. Capture corresponding frames with identical view and quality settings, use a recorded tolerance for equivalent image checks, and report time-dependent GPU particle invariants where exact image comparisons are unsuitable. Supply M21 with the saved project, dependency provenance, replay inputs and expected trace as well as the pak. M21 can then rerun the same workload with its geometry features disabled or enabled.

Rebuild `AxleAssetUnitTests`, `AxleRuntimeProductParityUnitTests`, `AxleBuildWorkflowTests`, `AxleEditor` and `AxleRuntime` in Debug and Release. Reuse the Runtime export verifier, then extend coverage with positive and intentionally failing tiny projects. Exercise source-tree isolation and explicit file deletion/corruption in disposable fixtures. Compare canonical graph/manifest data for same inputs and record deterministic artifacts supported by the codec. Exercise strict D3D12/Vulkan shipping execution with actual frames and UI action dispatch, plus the documented OpenGL route. CPU-only linter cases require no GPU.

Archive commands, exit codes, manifests, receipts, support matrix, recovered failures and requirement dispositions in `docs/reviews/`. Consolidated independent review must verify M21 can add static/skeletal LOD and meshlet payloads using the exposed provenance, package and base-fallback interfaces.
