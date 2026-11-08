# OPT-06 consolidated review

Date: **2026-11-08**. Reviewer: **Codex**. Candidate: `20e0714`.
Reviewed implementation range: `0ca38cd..20e0714` (including the earlier loader,
metadata, selection and runtime-receipt foundation commits, not just the four
commits named in the final delivery).

## Decision

**NOT ACCEPTED.** The candidate supplies useful metadata, loader and UI foundations,
but does not deliver the PLG-01..13 product workflow required by G0/G1/G2.
The delivery message `CG-20261108T025845874637Z-GEMINI-c7dae3` is an implementer
claim, not independent acceptance. Existing tests passing does not close the
missing shared host, scene extension, real source plugin or restart/recovery gates.

Codex repaired the isolated Apply persistence transaction and verified it with a
separate-context source recheck. Remaining work includes absent framework layers
and reproduced defects; it cannot be closed by approving the existing slices.
OPT-07 and later work remain outside this review.

## Findings

### OPT06-R1 / P1 — Shared Engine/Editor host and SDK are absent (OPEN)

`Engine/Core/CMakeLists.txt:1`, `Engine/Physics/CMakeLists.txt:1` and
`Engine/PluginRuntime/CMakeLists.txt:1` still build static libraries.
`Engine/Project/include/Axle/Project.hpp` keeps the active project in an inline
static member. No shared Runtime/Editor host, supported SDK export/import surface,
shared Jolt/ImGui ownership contract, or two-DLL host-state proof was added.

The fixture DLL owns an integer and registers a destructor callback. It never
uses Project, Scene/EnTT pools, physics, jobs, rendering or Editor services.
Consequently its successful load cannot establish PLG-05/07 host ownership.
The loader's own README explicitly says PLG-04/05 acceptance is still required
before product use. Required correction: implement and verify the shared host/SDK
before attaching actual project components and systems to products.

### OPT06-R2 / P1 — Build/load/restart/recovery are not connected (OPEN)

Source-wide caller inspection finds no product caller of `BuildPlugins`,
`CreateBuildPlan`, `GenerateCMakeInputs` or `NativePlugin::Load`.
Editor links the libraries but only persists selection and invokes the existing
backend restart helper. `Editor/src/Main.cpp` handles `--safe-mode`/`--recovery`
by setting a panel flag; it supplies no native plugin startup/session or failure
recovery. Thus selecting, applying and restarting cannot activate a project DLL.

`Editor/src/EditorLayer.cpp:444` also reports a successful scene save regardless
of `SaveScene` failure, skips an untitled scene and has no private-document guard.
The restart helper carries project/backend but no validated plugin receipt or
startup handoff. Save/Discard/Cancel, private documents, build failures, close races
and a single successful restarted plugin session remain unproven. These are
PLG-10..12 deliverables, not later Vehicle or selected shipping Runtime work.

### OPT06-R3 / P1 — Source builder has no configure/staging workflow or real DLL proof (OPEN)

`Editor/src/PluginBuild.cpp` only calls `cmake --build` on an already configured
directory; `ProjectDirectory` is unused. Generated CMake inputs contain variables
but no configured target/module composition. There is no caller that turns a
resolved project selection into a standalone configured plugin project.

Successful outputs are read directly from `BinaryDirectory`, with no immutable
generation/staging boundary and no artifact-bound receipt. Failure preservation
tests compare only receipt text, not an already active DLL generation. A global
atomic flag excludes concurrent calls in one process, not writers in two Editor
processes.

`Tests/src/PluginBuildFixture/CMakeLists.txt` declares `project(... NONE)` and a
custom target; `WritePlugin.cmake:9` writes plain text to `CustomPlugin_Runtime.dll`.
This verifies process cancellation/logging but **does not compile or link a DLL**.
It cannot satisfy the PLG-11/13 external source authoring proof. The native loader
fixture and this fake build fixture are separate disconnected paths.

### OPT06-R4 / P1 — Receipt validation accepts incomplete or mismatched identities (OPEN)

`Engine/PluginRuntime/src/PluginRuntime.cpp:165` requires only `schema=1`;
plugin/configuration comparisons are conditional on fields being present. A real
fixture DLL loads successfully with a receipt containing only `schema=1`.
Omitting a receipt entirely also bypasses this check. Module identity, SDK,
toolchain, product, fingerprint and binary provenance are not validated.
The loader uses one expected ID for descriptor `module_id` and receipt `plugin_id`,
although the build planner models plugin and module IDs separately.

`Editor/src/PluginBuild.cpp` similarly accepts a matching prebuilt receipt with
no configuration field, and an invalid plan with an empty item list reports
successful/up-to-date. Both defects were reproduced through production APIs.
Required correction: one canonical validated artifact identity used by planner,
builder, loader and restart session, with incomplete/invalid inputs rejected.

### OPT06-R5 / P1 — Per-scene plugin ownership and scheduling are not implemented (OPEN)

The candidate introduces generic `void*` registration and codec owner numbers,
but no plugin scene-instance registry, typed pool/module lifetime pin, scene
attach/detach/start/stop extension, nontrivial component copy/remap contract,
owned simulation/job completion integration or retained render contribution.
No changes to Scene's lifecycle/scheduler, JobSystem or renderer implement the
PLG-07/09 consumers. Existing shared simulation/script tests establish their
existing services; they do not establish native plugin ownership through those
services. Empty typed pools, detached/private scenes and pending work remain
outside the fixture's lifetime coverage.

### OPT06-R6 / P1 — Assembly parsing drops nested plugin data (OPEN)

`Engine/Scene/src/AssemblySerializer.cpp:471` recognizes built-in headers after
trimming indentation. At line 487, any nested `Children:` ends a custom section
and switches to hierarchy parsing. A custom component containing `Value`, nested
`Children` and `Tail` retains only its first line after load. This was reproduced
with real `Serialize`/`LoadAssemblyData` calls. The existing flat two-line fixture
does not exercise the failure.

`Engine/UI/src/AssemblyEditorPanel.cpp` builds preview entities from known fields
without restoring/interpreting the new passive blocks. There is also no
owner/type/schema/required-data readiness contract to block Play/cook or unsafe
opaque-reference duplication. PLG-08 is partial. This finding concerns current
plugin data; legacy Vehicle format retention is not an acceptance requirement.

### OPT06-R7 / P1 — Mutable codec storage breaks scoped ownership (OPEN)

`Engine/SceneData/include/Axle/ComponentCodec.hpp:71` exposes mutable codec storage
while ownership lives in a separate positional vector. Padding only handles append
growth. Register owners A/B, erase A through `GetMutableCodecs`, then unregister B:
B's codec remains registered because its index no longer matches its owner.
The review probe reproduces this with the public API. Unloading B can then leave
its virtual callback/destructor resident in a host registry.

Required correction: encapsulate codec/owner/lifetime in one registration record
and migrate the mutable runtime registration caller; owner cleanup must not rely
on callers preserving parallel vector indices. This is separate from the absent
typed EnTT pool lifetime pin in R5.

### OPT06-R8 / P1 — Resolver discards duplicate-ID diagnostics and compatibility policy (OPEN)

`Engine/Plugin/src/Plugin.cpp:367` records a duplicate ID and keeps one manifest.
`Resolve` at line 393 ignores the catalog diagnostic and accepts that selected ID,
silently choosing whichever manifest sorted first. A two-root duplicate fixture
reproduces a valid resolution despite invalid discovery.

Manifest/resolve data also has no Engine-versus-project origin, Engine API/SDK or
platform compatibility fields. A selected manifest declaring Engine API `999` and
an unsupported platform resolves successfully. Engine-to-project dependency
prohibition therefore cannot be enforced either. Preserve diagnostics per row,
reject ambiguous selected IDs and validate the effective graph's compatibility.

### OPT06-R9 / P2 — Architecture gate is not maintained (OPEN)

The current `check_architecture.py --closed-package AF-04` run exits **2**, with
seven diagnostics: unknown owners for both plugin targets, unresolved plugin
includes and undeclared Editor link edges. `scripts/architecture_rules.json`
does not describe either new module. Update the explicit module boundaries and
their checks without suppressing violations or weakening unrelated policy.

### OPT06-R10 / P1 — Apply acknowledges persistence failure (FIXED by Codex)

The candidate advances Desired before saving, derives a filename from the display
name, saves the global active project instead of its held project and reports
success even if saving fails. A new regression first failed against the candidate
with `FAIL: Apply requires a saved project` (exit 1).

Correction commit: **`25b4e7c`**. The correction requires a saved project path, propagates the actual New/Open/Save
As path, atomically persists the held project's draft, restores config on failure
and advances Desired/restart only after persistence succeeds. Project switching
clears old pending notifications. Expanded tests cover no project, untitled Apply,
scene-save refusal, project-save failure, preserved bytes/draft/Desired, different
display name/filename, reopen, and an unrelated global active project.

A separate-context source reviewer found no remaining P1/P2 in this correction;
that reviewer inspected the Debug log but did not independently rerun tests.
This closes R10 only, not R2's missing document/build/recovery transaction.

## Package assessment

| Package | Verified status |
| --- | --- |
| PLG-01 | Partial metadata discovery; origin/compatibility/duplicate handling open |
| PLG-02 | Partial deterministic graph checks; R8 blocks closure |
| PLG-03 | Versioned selection persistence exists; Apply repair verified; malformed schema/product integration gates still required |
| PLG-04 | Fingerprints/receipt writer exist; configured selected-module graph and identity validation incomplete |
| PLG-05 | Shared host/SDK and two-DLL service proof absent |
| PLG-06 | Integer-resource native loader foundation only; identity/session gates incomplete |
| PLG-07 | Native component/scene lifecycle and retained typed pools absent |
| PLG-08 | Passive Assembly blocks added; nested data loss and readiness/preview gates open |
| PLG-09 | Native plugin phase/job/render ownership integration absent |
| PLG-10 | Menu/window/view model added; full native workflow/layout acceptance NOT RUN |
| PLG-11 | Cancellable build-process helper exists; real external DLL configure/build/staging absent |
| PLG-12 | Selection save and backend restart hooks only; integrated recovery/documents/build transaction absent |
| PLG-13 | External project SDK/template and complete authoring proof absent |

## Verification record

Logs are under `build/review-opt06/` (generated/ignored). All results below are
actual Codex runs, distinct from Gemini's delivery evidence.

- Candidate Debug builds of plugin, project, asset, scene-data, runtime-build
  tests and Editor/Runtime: exit **0** (`build-Debug.log`). An earlier command
  accidentally named the CTest name `AxleAssetIdentityAndPersistenceTests` as a
  target and exited 1; the corrected command uses `AxleAssetUnitTests`.
- Candidate targeted Debug CTest:
  `ctest --test-dir build -C Debug -R 'Plugin|Asset|SceneData|ProjectStartup|BuildWorkflow' --output-on-failure`:
  **16/16 PASS**, exit **0** (`ctest-candidate-Debug.log`).
- Apply regression before correction: exit **1** (`apply-before-fix.log`).
- Corrected Debug panel/Editor rebuild and focused panel tests in **both Debug
  and Release**: exit **0**, **1/1 PASS per configuration**
  (`build-apply-fixed-Debug.log`, `ctest-apply-fixed-Debug.log`,
  `ctest-apply-fixed-Release.log`).
- Seven additional defect probes compiled against production APIs and real
  loader fixture: all **7 defects reproduced** (`probes-results.log`).
  This is failure evidence, not a successful acceptance gate.
- Final architecture check: exit **2**, seven diagnostics
  (`architecture-final.log`, original run in `architecture.log`).
- `python scripts/test_baseline_tools.py`: exit **0**, **18/18 PASS**
  (`baseline-tools.log`).
- Final Debug targeted CTest after correction: **16/16 PASS**, exit **0**
  (`ctest-final-Debug.log`), using the same targeted command above.
- Release build:
  `cmake --build build --config Release --target AxlePluginFoundationTests AxlePluginSelectionTests AxlePluginsPanelWorkflowTests AxlePluginRuntimeTests AxleAllTests AxleEditor AxleRuntime`:
  exit **0** (`build-final-Release.log`). This rebuilds the broad test executables
  before using their results; existing linker runtime-library warnings remain.
- Full Release `ctest --test-dir build -C Release --output-on-failure`:
  **55/56 PASS**, exit **8**. The failing test is
  `ArchitectureBoundaryChecks`, matching R9 (`ctest-final-Release.log`).
  The other 55 results do not waive this failure or the absent native gates.
- Committed contract probe Debug rebuild: exit **0**
  (`build-contract-probes-Debug.log`). Running the command below returns **1**,
  with **7/7 defects reproduced** (`contract-probes-Debug.log`).
- `git diff --check` and exchange schema/reference validation: exit **0**.

The committed explicit negative gate can be rerun with:

```powershell
cmake --build build --config Debug --target AxleOpt06ReviewProbes
build/Tests/Debug/AxleOpt06ReviewProbes.exe build/Tests/Debug/AxlePluginRuntimeGood.dll build/review-opt06/contract-probes
```

It returns nonzero while any reproduced contract violation remains. It is
deliberately separate from the existing routine CTest suite: a routine green
result must not hide this failing acceptance gate. Fixture descriptors currently
hardcode `Debug` even in Release builds; existing Release loader tests therefore
do not establish a genuine Release ABI/configuration handshake.

Required native Plugins UI, real project component/Inspector/update/save/reopen,
restart/recovery and shared-service DLL gates are **NOT RUN / implementation
absent**. No acceptance of those paths is inferred from a linked Editor executable.
No sample files, vendor revisions, later OPT package or remote branch was changed.
