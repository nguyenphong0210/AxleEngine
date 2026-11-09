# Gemini Delivery: OPT-01 / AF-01 Stable Asset Identity and Safe Persistence

Date: **2026-11-05**
Implementer: **Gemini**
Coordinator/Reviewer: **Codex**
Scope: **AF-01 only** (historical handoff retired; delivery commit `a19da2fd08a341652e65b5d41c917830f78aa3e8`)
Status: **Delivered (pending Codex independent review)**

---

## 1. Summary of Delivered Work

In strict accordance with the user steering and AF-01 contract:
1. **Canonical Root Registry Authority**:
   - Implemented `AssetRegistrySerializer` (`Engine/Asset/include/Axle/AssetRegistrySerializer.hpp`, `Engine/Asset/src/AssetRegistrySerializer.cpp`).
   - Single source-controlled root registry `AssetRegistry.axleregistry` located at project asset root.
   - Deterministic lexical sorting by relative path for reproducible Git diffs.
   - Versioned schema (version 1) with schema validation, rejecting unsupported versions (> 1) and reporting corrupt records.
2. **Rejection & Retirement of Per-Asset Sidecars**:
   - Active `.meta`, `.uid`, and `.import` file generation is retired.
   - Scoped `.gitignore` rules actively ignore `*.meta`, `*.meta.tmp*`, `*.meta.bak`, `*.uid`, `*.import`.
   - All tests explicitly verify that **zero** sidecar files are generated across clean-checkout, discovery, import, save, rename, and reopen operations.
3. **Canonical Builtin Primitives**:
   - Canonical builtin URIs (`builtin://cube`, `builtin://sphere`, `builtin://cylinder`, `builtin://cone`, `builtin://plane`) map to fixed canonical UUIDs (`0xBA5E0001` - `0xBA5E0005`) without creating registry entries or sidecars.
4. **Relocation versus Copy Policy**:
   - `AssetManager::RelocateAsset`: Renames the physical file and updates the registry relative path while preserving the exact authored UUID.
   - `AssetManager::CopyAsset`: Duplicates the physical file and allocates a brand-new distinct authored UUID in the registry.
   - `AssetManager::RelinkAsset`: Supports recovery from external filesystem moves, binding the moved asset to its existing UUID and persisting the registry atomically.
5. **Checked New-Import Persistence**:
   - Genuinely new imports allocate a UUID once and atomically write `AssetRegistry.axleregistry` before publishing to in-memory lookup maps.
   - If persistence fails, the in-memory registration is cleanly rolled back.
6. **Diagnostics & Missing Identity Handling**:
   - Duplicate UUID detection in registry emits an explicit diagnostic error and rejects collisions without corrupting valid entries.
   - Missing registry entries for already-referenced assets report diagnostic errors and reject silently regenerating a fresh UUID.
   - Corrupt YAML or unsupported registry versions emit diagnostic errors and prevent registration.
7. **Shared Safe Persistence at Foundation CPU IO Seam**:
   - Core IO seam (`FileSystem::WriteTextFileAtomically` and `WriteBinaryFileAtomically`) implemented using temporary sibling files (`.tmp.<timestamp>.<counter>`), atomic replacement via Win32 `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` / POSIX `rename`, and flush checks.
   - Wired across `AssetRegistrySerializer`, `ProjectSerializer`, `SceneSerializer`, `AssemblySerializer`, and `AnimationAssetCodec`.
   - On write or replacement failure, prior valid file bytes remain intact.

---

## 2. Verification Evidence

### 2.1 Isolated Unit Test Suite (`AxleAssetUnitTests` / `AssetIdentityAndPersistenceTests`)
Executed executable directly and via CTest in both Debug and Release configurations.
11/11 dedicated test cases passed:
- `Clean Checkout Reference Resolution`: PASSED (asserted zero sidecars)
- `Project-Root Relocation`: PASSED (asserted zero sidecars)
- `Asset Relocation (Move/Rename) Identity`: PASSED (asserted zero sidecars)
- `Asset Copy Brand-New Identity`: PASSED (asserted zero sidecars)
- `External Move and Explicit Relink`: PASSED (asserted zero sidecars)
- `Duplicate UUID Diagnostics & Recovery`: PASSED (asserted zero sidecars)
- `Corrupt & Unsupported Registry Diagnostics`: PASSED (asserted zero sidecars)
- `Missing Registry Entry for Referenced Asset`: PASSED (asserted zero sidecars)
- `New-Import Checked Atomic Persistence`: PASSED (asserted zero sidecars)
- `Deterministic Save Failure Preserves Prior`: PASSED (target preserved on directory collision)
- `Canonical Builtin Primitives`: PASSED (asserted zero sidecars)

### 2.2 CTest Regression Suites
- **Debug Configuration**:
  - `ctest --test-dir build -C Debug --output-on-failure`
  - Result: 16/16 passed (100%), including `MaterialUnitTests`, `VFXUnitTests`, `AnimationUnitTests`, `ProjectStartupTests`, `AssetIdentityAndPersistenceTests`, `JobsECS_*` controls, `ArchitectureBoundaryChecks`, `BaselineToolRegressions`, and `BuildWorkflowTests`.
- **Release Configuration**:
  - `ctest --test-dir build -C Release --output-on-failure`
  - Result: 16/16 passed (100%).

### 2.3 Architecture & Tooling Regressions
- `python scripts/check_architecture.py`:
  - Result: `AXLE_ARCH_CHECK PASS` (344 source files scanned, 17 CMake files).
  - No illegal layer crossings or dependency boundary violations.
- `python scripts/test_baseline_tools.py`:
  - Result: 18/18 tests passed (`OK`).

### 2.4 Product Builds
- `AxleEditor` and `AxleRuntime` built cleanly in both Debug and Release configurations without errors or warnings.

---

## 3. Scope Exclusions & NOT RUN Boundaries

In accordance with handoff instructions:
- **NOT RUN**: Physical GPU execution (D3D12/Vulkan readback), manual UI mouse/window testing, player export packaging, and runtime game execution.
- **NO SCOPE CREEP**: Did not start P41-00, PLG-00, AF-02+, EJ-01+, or unrelated fixes.
- **PRESERVED BASELINE**: Known baseline ECS/jobs concurrency defects remain exposed and visible; no attempts were made to mask or alter them.
- **CLEAN ENGINE**: No legacy Vehicle holders, old-format compatibility readers, or migration code were introduced.
