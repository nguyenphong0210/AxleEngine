# OPT-06 consolidated review and acceptance

Date: **2026-11-08**. Reviewer/final repair owner: **Codex**.
Original candidate: `20e0714` (`0ca38cd..20e0714`); resumed base: `c3a862d`.
Final corrected production source: **`af3f7b2`**.

## Decision

**ACCEPTED for OPT-06 / PLG-01..13 (G0, G1 and G2).** The original candidate
was not accepted. Codex completed the missing product composition and repaired
the reproduced defects, rather than treating its passing foundation tests as
phase acceptance. All ten findings below are closed against the corrected source.
Significant repairs received separate-context rechecks.

The release ends at the general native Editor plugin framework and external
authoring proof. OPT-07+ is not activated. Fresh Vehicle, Spline/Terrain conversion,
script conversion, selected static player composition and packaged plugin content
remain their recorded later packages. See the [authoring guide](../design/PLUGIN_SDK_AUTHORING.md).

## Findings and corrections

| Finding | Final status and verified correction |
| --- | --- |
| OPT06-R1 / P1: shared hosts/SDK absent | **FIXED**, `52a24ce`, `22cfca0`. Runtime/Editor host DLLs expose matched C/POD gateways to canonical application services. Three real external DLLs prove shared Project, registry, jobs, physics, renderer and ImGui ownership in Debug/Release. |
| OPT06-R2 / P1: disconnected product workflow | **FIXED**, `e2b9cc4`, `af3f7b2`. Editor composes selection, immutable build generations, startup activation, private-document guard, safe recovery and acknowledged project/backend restart. Real Editor workflow and negative startup tests pass. |
| OPT06-R3 / P1: simulated source build | **FIXED**, `52a24ce`, `c263840`, `af3f7b2`. Standalone matched SDK configures/compiles actual selected DLLs from frozen sources; locks writers, stages immutable generations and preserves earlier outputs on configure/compiler/cancel failure. Live absolute source/include escapes are rejected. |
| OPT06-R4 / P1: incomplete artifact identity accepted | **FIXED**, `8a46ac7`, `52a24ce`, `af3f7b2`. One strict receipt validates plugin/module, SDK, toolchain, configuration, product, plan and actual DLL hash. Restart additionally binds canonical generation and a locked receipt snapshot; same-plan replacement and incomplete identities are rejected. |
| OPT06-R5 / P1: native scene ownership absent | **FIXED**, `52a24ce`, `22cfca0`, `e2b9cc4`. Owned scene extensions retain descriptors, values, empty pools, callbacks, jobs and code through teardown. Whole-graph remaps, transactional Clone/Start, shared simulation phases and actual GPU submission lifetime are tested. |
| OPT06-R6 / P1: nested Assembly data loss | **FIXED**, `8a46ac7`, `22cfca0`, `e2b9cc4`. Indentation-scoped passive sections preserve nested keys; private previews restore owned data. Stable authored UUIDs replace arithmetic preview keys. Missing required/schema data blocks Play/cook; opaque references reject unsafe copy. Wide/deep Assembly tests pass. |
| OPT06-R7 / P1: mutable codec ownership | **FIXED**, `8a46ac7`. Codec, owner and executable lease live in one encapsulated registration record. Mutable positional storage is retired; revocation/reentry/owner cleanup tests pass. |
| OPT06-R8 / P1: duplicate/compatibility resolution | **FIXED**, `8a46ac7`. Selected ambiguous IDs fail; origin, Engine API, platform and Engine-to-project dependency constraints are enforced. Physical aliases are deduplicated and identities deterministic. |
| OPT06-R9 / P2: architecture gate broken | **FIXED**, `8a46ac7`, `52a24ce`, `22cfca0`, `af3f7b2`. Explicit Plugin/host/service/product ownership and link edges are maintained; architecture and forbidden-edge checks pass in both configurations. |
| OPT06-R10 / P1: Apply reports failed persistence as success | **FIXED**, `25b4e7c`, `af3f7b2`. Apply atomically saves the held project's actual file, rolls back on failure and advances Desired only after success. Active remains the running graph. Untitled/failed-save/unrelated-global-project cases pass. |

The original seven explicit contract probes now reproduce **0/7 defects**, exit 0,
in both configurations. They remain a separate explicit acceptance executable.
The historical failed candidate review and original reproductions remain in Git
history; they are not evidence of a failure in the accepted corrected source.

## Delivered package boundary

| Packages | Accepted result |
| --- | --- |
| PLG-01..04 | Side-effect-free Engine/project/content-only metadata discovery; deterministic compatible graph; durable explicit Desired selection; canonical product plans and artifact identities. Content-only discovery does not claim later content mounting/packaging. |
| PLG-05..06 | Shared gateway ownership, frozen SDK and real Debug/Release loader handshake; scoped registration, startup rollback, owner-thread retirement and safe mode. |
| PLG-07..09 | Native nontrivial component lifecycle across edit/Play/duplicate/Assembly/private scenes; persistence/readiness/remap; shared phases/jobs and retained submitted render contributions. |
| PLG-10..12 | Accessible Plugins window, dependency transactions, Draft/Desired/Active states, actual async build/log/cancel, guarded documents, restart handoff, failure preservation and recovery/retry. |
| PLG-13 | Shipped template/generator and external Unicode-path project proof with Runtime component, dependent Editor Inspector, update, save/reopen, Play/clone/Stop and native restart. No Engine/root CMake edit is needed by the plugin author. |

The supported SDK is an opaque host facade, documented in the
[architecture decision](../design/PLUGIN_SYSTEM_ARCHITECTURE.md#implemented-facade-decision-2026-11-08-correction).
Plugins allocate/destroy their own nontrivial payloads, while host-compiled wrappers
use the canonical EnTT registry. Plugins do not link private Engine/vendor state;
Jolt, enkiTS, ImGui and rendering operations execute through the host. This replaces
the initial direct vendor/C++ export proposal and is an explicit design correction.

## Executed verification

Generated logs are under **`build/opt06-completion/`**. These are Codex runs;
Gemini's initial delivery message is not substituted for verification.

| Gate | Debug | Release | Evidence |
| --- | --- | --- | --- |
| Build `AxleAllTests AxleEditor AxleRuntime AxleOpt06ReviewProbes` | PASS, exit 0 | PASS, exit 0 | `final-debug-build.log`, `final-debug-product-build.log`, `final-release-build.log` |
| Full CTest | **59/59 PASS**, exit 0 | **59/59 PASS**, exit 0 | `final-debug-tests.log`, `final-release-tests.log` |
| Original seven defect probes | **0 defects**, exit 0 | **0 defects**, exit 0 | `contract-final-{debug,release}.log` |
| External template and native product driver | All 17 checks meet expected exit | All 17 checks meet expected exit | `final-native-{debug,release}.json`, corresponding `native-final-*` logs |
| Actual submitted GPU/code lifetime | D3D12 and Vulkan PASS; **38 green pixels each** | D3D12 and Vulkan PASS; **38 green pixels each** | `native-final-submission-{dx12,vulkan}-{debug,release}.log` |
| Native Editor/Runtime simulation | Both backends; 30/60/144 Hz PASS | Both backends; 30/60/144 Hz PASS | `final-simulation-{debug,release}.json`, per-product logs |
| Default Sandbox startup | Both backends PASS | Both backends PASS | `final-default-startup-{dx12,vulkan}-{debug,release}.log` |

Build and routine test commands:

```powershell
cmake --build build --config Debug --target AxleAllTests AxleEditor AxleRuntime AxleOpt06ReviewProbes --parallel 6
ctest --test-dir build -C Debug --output-on-failure
cmake --build build --config Release --target AxleAllTests AxleEditor AxleRuntime AxleOpt06ReviewProbes --parallel 6
ctest --test-dir build -C Release --output-on-failure
build/Tests/Debug/AxleOpt06ReviewProbes.exe build/Tests/Debug/AxlePluginRuntimeGood.dll build/opt06-completion/contract-final-debug
build/Tests/Release/AxleOpt06ReviewProbes.exe build/Tests/Release/AxlePluginRuntimeGood.dll build/opt06-completion/contract-final-release
```

The native driver executes the generated external template through production
session/build APIs, then actual Editor workflow, exact receipt-bound child restart,
wrong-plan rejection, fifth-Present failure, abandoned-session recovery, partial
DLL startup failure after registration, restored startup, filesystem recovery and
exception teardown. Intentional rejection must exit **1**, not crash, timeout or
acknowledge the parent. The caught-exception probe intentionally exits 1; the
driver records that as its expected result. Temporary markers are explicitly
constructed/cleared in disposable fixtures; actual `ClearAbandonedSessions` retry
is separately exercised by the real-DLL session test. This is not a claim of
automated native UI retry clicks.

Native computer use also checked Plugins through Edit at normal and approximately
350px width, Draft/Apply/Active separation, A-to-empty-to-A reconciliation, actual
Runtime Inspector Speed 3.5-to-6 editing, close guard, Cancel retaining the edited
value, toolbar Save and clean close. These observations used a disposable Debug
fixture and are distinct from headless ImGui and Release verification.

The product probe checks failed Clone/Start rollback, unchanged authored physics
during Play, read-only dirty capture, new same-name Canvas saves, Cancel/Discard,
conflicting writers, selected-node hot reload, reopen and Save As with continued
private/UI saves into the copied project. A final probe repair uses physical-path
relative resolution across Windows short/long aliases. Startup cleanup now retires
device callbacks on every return/exception before static cache destruction, and
failed `Application::Run` is reported as failure. Both failure paths were rerun on
the final native binaries.

## Independent correction rechecks

Separate-context foundation/native/scene reviewers checked their respective repair
chunks. The product reviewer source-rechecked its findings, independently rebuilt
and ran **8/8** focused Debug checks, then independently rebuilt the strengthened
receipt-snapshot targets, ran **2/2** checks and rebuilt Debug Editor. Logs include
`independent-snapshot-debug-build.log`, `independent-snapshot-debug-tests.log` and
`independent-snapshot-editor-debug-build.log`.

The final focused reviewer found the startup-rejection cleanup bypass; the outer
product cleanup guard closed it. It rechecked that guard, physical-path probe
correction and `Run()` failure propagation and reported no remaining P1/P2 in
that scope. That final check was source-only. Independent reviewers did not run
Release, native computer use or GPU acceptance; those are the coordinator's
executed gates above. No Gemini reply or reviewer test execution is invented.

## Limits and preservation

- Native plugin activation is startup-only; live unload/hot swap is deferred.
- Selected static Runtime composition is PLG-22 and exact packaged plugin content
  is PLG-23. Editor export refuses a player that lacks selected native modules;
  data cooking preflights required native documents before output mutation.
- Content mounting, marketplace/distribution and arbitrary binary compatibility
  are not claimed by metadata discovery or the matched source SDK.
- No new Vehicle implementation, later OPT package, pinned vendor upgrade or push.
- Sandbox/default startup is preserved. Its authored scene SHA256 remains
  `0FA1BA5710996941D82512A7703727C18C75C1363748D680243EF103E4CC5E93`.
  Generated `crash.log` remains untracked and is not part of the delivery.
- Existing APIENTRY/runtime-library linker warnings remain; no failed check is
  waived by those warnings.
