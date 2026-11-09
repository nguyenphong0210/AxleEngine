# OPT-01 AF-01 / P41-00 / PLG-00 review and corrections

Review baseline: `8e3450a`, 2026-11-05. Reviewer/corrector: **Codex**.
User authority: review Gemini's latest work, fix issues directly, then prepare the next English prompt. The latest storage instruction is raw-resource paths plus UUIDs embedded in native asset files, without legacy support.

## Scope and evidence ownership

Reviewed original Gemini implementations `7e79d22` (AF-01), `8d1fece` (P41-00) and `179d66e` (PLG-00), their delivery reports, source/build wiring and exchange messages. The original root-registry implementation followed earlier recorded steering. Replacing it is a **user-directed scope correction**, not proof that Gemini ignored its then-recorded contract.

Before corrections, Codex rebuilt and directly ran the three Release test executables. Each exited 0; logs are `build/opt01-final-review/build-before.log`, `*-before.log` and `before-results.json`. This independently confirms their narrow original coverage; it does not independently confirm Gemini's historical CTest/product/export claims. Subsequent fixes and verification are Codex-authored, with no external post-fix independent review claimed. The original implementations required corrections; the final packages are accepted for baseline progression on the evidence below.

## Findings and resolutions

| ID / severity | Trigger and original observed/source-confirmed behavior | Resolution |
| --- | --- | --- |
| SCOPE-01 / contract correction | Latest user wants PNG/raw paths and in-file native UUIDs; original AF-01 requires AssetRegistry.axleregistry. | Removed the authored registry serializer/fixtures. Raw UUID is zero; native identity is a versioned first-line comment. Read-only discovery derives an in-memory index from files or mounted package entries. No sidecar or old-registry reader. |
| AF01-R1 / P1 | Original AF-01 replaced [material cache code](../../Engine/Asset/src/AssetManager.cpp): GetMaterial no longer attached authored textures; ReloadMaterial evicted/recreated objects, leaving existing Scene references unchanged. A broad integration run reproduced the stale live material. | Restored texture attachment while retaining disabled-map flags. Reload updates the existing shared Material and RHI material instances. Added permanent pointer/value/UUID regression coverage. |
| AF01-R2 / P1 | Original Sync inserts new IDs into live maps before SaveRegistryLocked; failed registry persistence leaves unpublished identities visible in memory. Source-confirmed; original happy-path suite misses it. | New native import persists the embedded identity before rebuilding/publishing index state. Deterministic Windows deny-delete tests verify failed assignment changes neither bytes nor published ID. Discovery never writes. |
| AF01-R3 / P1 | Original RelinkAsset/RelocateAsset error branches dereference optional outError unconditionally for diagnostics. Relative relocation also uses unrooted physical paths, and a missing source can update registry state. | Root-constrained physical operations, source/destination validation and null-safe error handling. Native relink verifies the actual embedded ID. Regression calls omit the error pointer and cover missing source and collision. |
| AF01-R4 / P1 | Original [generic atomic writer](../../Engine/Core/src/FileSystem.cpp) accepts a null buffer with nonzero size, writes an empty temporary and replaces valid content. Close errors are unchecked; cleanup can overwrite the original OS error. | Reject invalid buffer before mutation, verify flush and close, capture replacement error before cleanup. Tests compare previous bytes for invalid input, directory target and actual Windows replacement failure. |
| P41-R1 / P2 | [CTest wiring](../../Tests/CMakeLists.txt) declares AXLE_ENABLE_RENDER_GPU_TESTS but registers no tests. P41-00 requires separately opt-in hardware/window/product checks. | Register shared physical D3D12/Vulkan readback tests and separately opt-in real Editor startup/UI-frame smoke tests. CPU runs stay device-free. Successful fallback is recorded as fallback, not native requested-backend equivalence. |
| P41-R2 / P1, pre-existing | The new real Editor smoke passes five GL UI frames, then Debug aborts during teardown: [PhysicsDebugRenderer](../../Engine/PhysicsDebug/src/PhysicsDebugRenderer.cpp) manually clears Jolt's singleton before the Jolt base destructor validates its ownership. Release masks the vendor assertion. | Removed redundant subclass singleton writes/clears; Jolt's base owns its lifetime. Added a permanent CPU lifecycle assertion and reran the actual Debug/Release OpenGL Editor smoke. No vendor or scheduler code changed. |
| HARNESS-R1 / P2 | Original render/plugin WILL_FAIL tests accept any nonzero exit, including unrelated setup errors. AF-01 lacks an intentional-failure CTest. | A bounded wrapper requires the exact expected event and exit 1 using the existing typed process collector. Add the asset failure probe; verify wrong-marker and missing-program controls fail. |
| PLG-R1 / P2 | Original inventory invents layer members/APIs, claims equal startup/order and reflection coverage, and mislabels future Vehicle packages. Actual Editor updates Scene before physics; Runtime does physics before Scene. Runtime has no matching Spline/Terrain Initialize calls. | Rewrote the [factual inventory](../architecture/PLUGIN_BASELINE_INVENTORY.md) under docs/architecture with source links, real ownership/order, accurate retirement names and PLG-14..18 mapping. Lifecycle flag/reentry tests are not described as reflection registration tests. Generic telemetry test keys no longer imply retained Vehicle behavior. |
| TEST-R1 / P2, pre-existing | Broad integration aborts in Step 68 because its mesh cache test searches removed Engine/Resources model files instead of owning its inputs. This is not attributed to the three new implementations. | [Step 68](../../Tests/src/Test_Milestone8_Part3.cpp) now writes tiny real native meshes in its owned test workspace, loads them through the production mesh registry and verifies pointer/count cache behavior. No old Vehicle asset/runtime support was reintroduced. |

Correction verification also caught the UI grammar needing to skip the native identity comment, and GPU/raw-media fixtures needing explicit owned asset roots. These were corrected before final verification. Read-only type inspection still accepts external import sources; metadata publication and resource references remain root-constrained.

The shared legacy `TestCommon.hpp` already keeps assert active in Release. Inspection confirmed that existing behavior; no mechanical assertion rewrite was retained.

Implementation corrections: `bff13f0` (native identity, raw paths and cache/save behavior), `d0b11bb` (baseline probes, fixture independence, inventory and Jolt debug-renderer lifetime).

## Final storage and save behavior

Native text files begin with `# AxleFileIdentity: 1 <decimal uint64>`. Core owns the envelope and atomic IO, without an upward Asset/Scene dependency; format bodies stay with their serializers. Scene, Assembly, material/RHI material, DSL, mesh/package manifest, texture descriptor, terrain, UI, animation and VFX native saves use identified persistence. Generic text/binary writes retain their generic semantics.

New explicit native import allocates once after successful save; save retains a valid ID. Native copy creates a new UUID. External native move and root relocation preserve the ID, while raw-resource copies retain bytes and change path. Missing/malformed/unsupported IDs are diagnosed without scan-time rewriting. Duplicate native IDs quarantine all copies. UUID-based material resolution does not silently follow a stale path hint to another asset.

Raw rename/relink updates recognized serialized path fields. Multi-file changes provide synchronous best-effort rollback; they are **not crash-atomic transactions**. Unknown plugin codecs and later AF-04 resource/format extraction are not implemented here. Mounted-package tests have no loose files and resolve actual native material UUIDs plus raw PNG paths. Runtime initializes the asset index after project selection.

## Verification

Working logs live under build/opt01-final-review/; these are generated evidence, not tracked project assets. The [tracked results](OPT_01_AF01_P41_PLG_RESULTS.json) preserve exact commands, exit codes and log paths.

| Check | Debug | Release |
| --- | --- | --- |
| Rebuild AxleAllTests, AxleEditor and AxleRuntime | Exit 0 | Exit 0 |
| Direct native identity suite | 14/14 suites, exit 0 | 14/14 suites, exit 0 |
| Default CTest | 21/21, exit 0 | 21/21, exit 0 |
| Full AxleTests integration, including current-format physics/serialization and VFX Step 47 | Exit 0 | Exit 0 |
| Jobs/ECS typed collector | 11 scenarios: 4 PASS, 4 reproduced faults, 3 validated probes; no unexpected failures | Same |
| Wrong expected failure marker / missing failure executable | Both rejected, exit 1 | Both rejected, exit 1 |
| Opt-in physical render/VFX readback and Editor smoke | 5/5 | 5/5 |

Build commands: cmake --build build --config <Debug or Release> --target AxleAllTests AxleEditor AxleRuntime --parallel <4 or 6>. Final build logs are build-debug-jolt-fix.log and build-release-jolt-fix.log. Test commands are ctest --test-dir build -C <config> --output-on-failure, the freshly built AxleAssetUnitTests.exe and AxleTests.exe, scripts/run_jobs_ecs_harness.py with its report/workspace arguments, and scripts/verify_failure_probe.py negative controls. The structured results include complete arguments.

The opt-in configuration uses AXLE_ENABLE_RENDER_GPU_TESTS=ON and AXLE_ENABLE_RENDER_WINDOW_TESTS=ON; ctest --test-dir build -C <config> -L gpu --output-on-failure runs two hardware readback cases and three real Editor startup/UI-frame cases. Each readback explicitly rejects software adapters and backend fallback. A physical NVIDIA GeForce RTX 3070 was observed. Editor logs confirm native D3D12, native Vulkan and explicit OpenGL; each reached the five-frame empty-project check and exited. This is automated smoke, not manual editing/Play/Stop or selected packaged export. No injected automatic-fallback equivalence test is claimed. Both options were restored OFF afterward.

Architecture checks pass without new allowances; all 18 baseline-tool regressions pass. Source/vendor/CMake File API evidence is captured separately in the final generated inventory at d0b11bb (32 Axle targets per configuration, 104 source link edges). Historical af5721b inventory remains intact as a dated snapshot.

Intermediate correction runs are retained with iteration-prefixed logs: an open read handle initially blocked Windows replacement, a generic animation IO wrapper initially acquired an identity envelope, and UI comments/fixture root assumptions required correction. All were resolved before the final checks; intermediate failures are not presented as final evidence. The pre-existing full-suite logger warning about a null string pointer and MSVC LNK4098 warnings remain visible in logs; no warning suppression or assertion weakening was added.

## Remaining boundaries

EJ production fixes were not implemented. The jobs/ECS collector must continue to identify ECSJ-01/02/03/04 defects accurately. PLG-00 does not implement discovery/resolution/DLL loading or prove selected static Runtime export. P41-00 is a baseline and harness acceptance, not full renderer acceptance. Manual Editor Play/Stop and selected packaged export are separate from automated smoke, CPU tests and builds. The historical OPT-00 independent product acceptance gap remains recorded separately.

The next bounded package was **OPT-02 / EJ-01**. Its accepted result is recorded in [OPT-02/03 review](OPT_02_03_CODEX_REVIEW.md). No later package was released by this review.
