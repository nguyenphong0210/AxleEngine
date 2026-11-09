# OPT-01 shared baseline inventory

Captured: **2026-11-05**, from correction implementation **`af5721b`**.
Production Engine baseline remains `95a6c9d`: the corrections change tooling and
tests, not Engine modules. [Machine-readable inventory](OPT_01_BASELINE_INVENTORY.json)
is the captured evidence; [correction verification](../reviews/OPT_01_BASELINE_CORRECTIONS.md)
distinguishes implementation checks from the original independent review.

## Reproduce the inventory

```powershell
python scripts/capture_architecture_baseline.py --build build --report build/opt01-fix/inventory.json
python scripts/check_architecture.py --report build/opt01-fix/source-graph.json
```

The first command requests CMake File API codemodel v2 and reconfigures the selected
build directory. It records repository HEAD, gitlink pins and actual submodule
checkouts, vendor source-tree hashes, CMake version/generator and actual Debug/Release
Axle target build prerequisites and compiler include directories. Repository/build
paths are normalized; external include paths retain only their leaf name. It is a
dated inventory, not a runtime performance or packaged-product test.

The snapshot contains **29 Axle targets in each configuration**, including products,
modules and explicit tests. File API dependency lists include transitive build
prerequisites; they are not mislabeled direct link statements. It verifies that
`AxleJobsECSTests` and `AxleAllTests` have no Editor/Runtime build prerequisite.
CMake **3.31.5**, Visual Studio 17 2022, x64, multi-config were observed.

Separately, the source checker records **104 source-declared link edges**, **694
cross-module source includes** grouped into **83 module pairs**, **342 unique source
files** and **17 CMake files**. Source includes are unique file scans: UI2D is no
longer counted again under UI. It follows nested module CMake files and literal/set
based include/add_subdirectory composition. It checks the union of conditional
branches, including inactive source files; this conservative source graph is
explicitly different from the actual configured File API graph.

This is not a complete CMake interpreter. Unresolved dependency variables,
generator-expression dependencies, macro/loop-based link construction, link
property mutation, dynamic evaluation and unresolvable composition fail coverage.
Such an error exits 2 and cannot validate a deliberate forbidden-edge fixture.
ThirdParty/Tests composition is outside the module source policy; vendor identities
are pinned below and test product separation is measured through the File API.

## Vendor identity

The JSON records all 15 ThirdParty entries: gitlink revision for eight submodules
and repository tree/blob identity for the remaining vendored content (including
DXC, glad, stb, cgltf, ufbx, miniaudio and ImGuizmo). No vendor was upgraded.

| Submodule | Pinned and observed checkout |
| --- | --- |
| JoltPhysics | `a63aa3b8e24cf95f3fab2613f9a3015b164ef62c` |
| NRI | `6998e3fbbaed1b132f386a9d49743a2481003487` |
| enkiTS | `404a3bf8f855039dfff2052184d6308655286c07` |
| EnTT | `4a2d1a8541228a90e02a873dba6a980506c42c03` |
| GLFW | `7b6aead9fb88b3623e3b3725ebb42670cbe4c579` |
| GLM | `0af55ccecd98d4e5a8d1fad7de25ba429d60e863` |
| ImGui | `778992face67ecefaa5c2932db4b2101545ea4df` |
| spdlog | `27cb4c76708608465c413f6d0e6b8d99a4d84302` |

All eight observed checkouts match the pins and were clean when captured. Branch
names in `.gitmodules` do not substitute for these actual revisions.

## Current asset identity and persistence policy: source observations

**Scope update, 2026-11-05:** The user now requires no per-asset sidecars. The
accepted AF-01 review uses source-controlled native embedded UUIDs and raw
root-relative paths, without an authored central registry. It retires active .meta generation. The .meta behavior below is a
historical source observation at `af5721b`, not a requirement to retain or commit
sidecars. Source capture and vendor graph evidence remain unchanged.

These are current implementation facts/risks from source inspection, not claims
that AF-01 is implemented or failure-injection tests already pass.

| Source | Current behavior | Owning follow-up |
| --- | --- | --- |
| [AssetMetadata](../../Engine/Asset/include/Axle/AssetMetadata.hpp), [metadata serializer](../../Engine/Asset/src/AssetMetadataSerializer.cpp), lines 15-89 | Sidecar is `assetPath + .meta`; stores UUID, Type, FilePath; no explicit schema version. Metadata writes directly with ofstream. | AF-01 version/identity rules and safe persistence |
| [AssetManager](../../Engine/Asset/src/AssetManager.cpp), lines 83-122 | Valid metadata UUID is reused; missing or corrupt metadata generates a new UUID. Serialize result is ignored before publication into maps. Duplicate UUID map entries can replace previous metadata without a collision diagnostic. | AF-01 stable identity, recovery and checked publication |
| [AssetManager](../../Engine/Asset/src/AssetManager.cpp), lines 25-35 and 190-216 | Keys are lowercased relative paths; rename moves the sidecar and updates registry paths, but does not surface rename failure as a recovery transaction. | AF-01 root/relocation/copy policy and diagnostics |
| [Ignore policy](../../.gitignore) | Blanket `*.meta` ignore; `git ls-files '*.meta'` finds zero tracked sidecars. Fresh-checkout asset UUID continuity is not established. | AF-01 authored metadata versus generated workspace ignores |
| [Scene persistence](../../Engine/Scene/src/SceneSerializer.cpp), lines 56-58; [assembly persistence](../../Engine/Scene/src/AssemblySerializer.cpp), lines 356 and 1344-1360 | Uses AnimationAssetCodec's atomic text writer. Mesh paths include canonical `builtin://cube/sphere/cylinder/cone/plane` spellings; builtins are not authored sidecar identities. | AF-01 shared IO seam and canonical identity contract; no serializer rewrite |
| [Atomic writer](../../Engine/Animation/src/AnimationAssetCodec.cpp), lines 49-110 | Writes sibling temporary file, checks flush, uses MoveFileExW replacement on Windows or filesystem rename elsewhere. This reusable CPU behavior currently lives in Animation. | AF-01 shared foundation IO ownership; preserve diagnostics and failure semantics |
| [Project persistence](../../Engine/Project/src/ProjectSerializer.cpp), lines 21-41; [Core IO](../../Engine/Core/src/FileSystem.cpp), WriteTextFile | Direct file writes exist alongside atomic scene/assembly persistence. | AF-01 checked atomic project/metadata saves |

User policy: no support for retired data formats, Vehicle payload holders,
compatibility readers or automatic migration. Current supported authored UUIDs
must remain stable; unsupported/corrupt input should produce a diagnostic rather
than silently create a different referenced identity. No developer-generated
sidecars were bulk-added or rewritten while taking this baseline.

## Boundary allowance ledger and expiration

Ten allowances now enumerate **29 exact dependency edges** (kind, exact file,
source target for CMake links, destination header/target). A module-level or
unknown-target match cannot authorize another dependency. The JSON contains the
complete edge ledger and original evidence; owners are AF-02, AF-04, P41-25,
PLG-19 and PLG-20. ALLOW-008 belongs to AF-04's CPU asset-format extraction rather
than AF-01 identity work; ALLOW-010 belongs to Terrain PLG-20. Those two stale
owner labels were corrected without changing production dependencies.

At an owning package's cutover, run `check_architecture.py --closed-package ID`:
its allowances cease to apply and remaining violating edges fail. Remove obsolete
allowances with that package; do not extend their file/target scope to conceal new
violations. Completing AF-01 does not require the later AF-04 module extraction.

## Build workflows and current acceptance gaps

[Maintained build guide](../guides/BUILD_WORKFLOWS.md) and
[Tests layout](../../Tests/README.md) define product/fixture locations. Tests are
EXCLUDE_FROM_ALL and grouped by AxleAllTests. The corrections rebuild the changed
jobs/ECS target in Debug/Release and run CTest **15/15** in each configuration.
Native baseline collection has four ordinary PASS results, four real recorded
Engine defects and three intentional harness validations. The defects are not
registered as passing Engine tests.

ECSJ-01 compiler delivery, ECSJ-02 external identity, ECSJ-03 Jolt/enki teardown and
ECSJ-04 EnTT tombstones remain owned by EJ-01..08. ECSJ-05 remains a source-traced
Editor/Runtime scheduling mismatch, owned by EJ-11..14; no gameplay parity capture
is claimed. Rendering/device and plugin/product changes remain later packages.

The corrected current-format scene/assembly/copy/duplicate and ordinary physics
lifecycle checks pass. OPT-00's full product/startup verification remains pending.
Editor/Runtime rebuilds, full AxleTests/GPU execution, interactive UI, packaged
export, sanitizer and performance checks were not rerun for these tool corrections.
Gemini's earlier product/integration results remain reported evidence. This
baseline does not mark OPT-00 or all OPT-01 complete.
