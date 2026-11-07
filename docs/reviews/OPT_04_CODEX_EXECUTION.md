Latest user steering (2026-11-07): implement all P41-01..11, then perform one consolidated final review. This supersedes intermediate review checkpoints; package verification and native acceptance gates remain required.

# OPT-04 Codex execution record

Date: 2026-11-07. Starting clean HEAD: `977f68c42755f1cfc0042854dc06dc74fc8cebc8`.
Scope: P41-01 through P41-11 only. No push or OPT-05 implementation.

Implementation and self-verification are separate from the consolidated final
fresh-context review requested by the user. That review has not completed.
Gemini has not supplied supplementary feedback. Two preliminary Codex readers
provided advisory source audits; these are not checkpoint reviews. A subsequent
advisory request failed because the reviewer account hit its usage limit.

## Environment and evidence

- Windows x64; physical NVIDIA GeForce RTX 3070, NVIDIA driver reported by GL as
  610.88. Native tests reject software adapters and backend fallback.
- NRI remains clean at `6998e3fbbaed1b132f386a9d49743a2481003487`, API 190.
  No vendor source upgrade/edit was made. The initial build directory had no
  CMake cache; normal configuration populated the pinned build dependencies.
- Generated scripts, logs and JSON results are under ignored
  `build/agent-runs/OPT-04/`.
- Graphics API validation is **NOT RUN**: the host has no Vulkan Khronos
  validation layer, and D3D12/DXGI debug prerequisites are unavailable. Vulkan
  layer enumeration was checked directly against the installed loader. An
  initial D3D12 debug request failed in `CreateDXGIFactory2` with `0x887A0001`.
  The Engine now rejects unavailable requested validation instead of accepting
  NRI's silent Vulkan layer filtering. Native operation tests separately enable
  embedded NRI validation. These tests do not establish API-validation acceptance
  for P41-07 or close any later gate requiring those layers.
- Builds retain the existing `LNK4098` CRT-library conflict warning. No claim is
  made that this warning has been resolved by OPT-04.

## P41-01 — backend readiness and startup transaction

Implemented and self-verified. Consolidated separate-context review remains due after P41-11.
Local implementation commit: `aba9a3c`.

Device, interfaces and queue remain private candidates until every required
operation succeeds. Native readiness and context readiness are explicit; generic
backend selection cannot authorize NRI resource calls. Explicit GL and fallback
GL now use the same selection/product/resource path. Strict selection survives
CLI argument order and cannot fall through to a manually selected GL backend.

Product composition owns the complete startup attempt. Window, renderer and UI
failure unwind before another backend is attempted. UI device objects are created
and checked during startup. Vulkan window construction checks each native step
and destroys partial state safely. Late Editor failure destroys panel owners while
their UI/window/device still exist. GLFW state and input context are cleared on
failed startup/teardown. Renderer readiness is checked for both product paths.
Runtime continues to declare its existing GL scene-rendering capability; its
full modern renderer cutover remains P41-19.

The production `ScopedRHIFault` seam injects a result at a named native operation;
all surrounding native creation and cleanup remain real. `AxleRHIFoundationTests`
covers policy, required native interfaces/queue, strict rejection, physical backend
recovery, explicit/fallback GL mesh pixels and nine partial Vulkan UI construction
failures followed by successful real presentation. Editor verification additionally
injects rejection after UI readiness and after the full renderer attaches.

Reproduction commands (run after building the changed targets):

```powershell
cmake -S . -B build
cmake --build build --config Debug --target AxleRHIFoundationTests AxleRenderUnitTests AxleEditor AxleRuntime --parallel 6 -- /p:UseMultiToolTask=true /p:CL_MPCount=4
cmake --build build --config Release --target AxleRHIFoundationTests AxleRenderUnitTests AxleEditor AxleRuntime --parallel 6 -- /p:UseMultiToolTask=true /p:CL_MPCount=4
ctest --test-dir build -C Debug -R '^(RenderUnitTests|RenderHarnessFailure|RHIFoundationPolicy)$' --output-on-failure
ctest --test-dir build -C Release -R '^(RenderUnitTests|RenderHarnessFailure|RHIFoundationPolicy)$' --output-on-failure
# Repeat for Debug and Release, each mode as a separate process:
build/Tests/Debug/AxleRHIFoundationTests.exe policy
build/Tests/Debug/AxleRHIFoundationTests.exe dx12
build/Tests/Debug/AxleRHIFoundationTests.exe vulkan
build/Tests/Debug/AxleRHIFoundationTests.exe gl
build/Tests/Debug/AxleRHIFoundationTests.exe vulkan-window
```

Run Editor commands from its executable directory. Each uses
`--verify-empty-startup`, plus one of:

- `--rhi=dx12 --strict-rhi`, `--rhi=vulkan --strict-rhi`, or `--rhi=gl --strict-rhi`.
- `--rhi=dx12 --verify-startup-rejection=CreateDevice` (complete GL fallback).
- `--rhi=dx12 --verify-startup-rejection=ProductUI` or `ProductRenderer`.
- `--rhi=dx12 --strict-rhi --verify-startup-rejection=CreateDevice` (expected exit 1).

All positive cases must exit 0; the negative strict case must exit 1. Logs and
case-by-case exits are in `p41-01-{debug,release}-results.json` and
`p41-01-editor-{debug,release}-results.json`. Build logs are
`p41-01-final-build-{debug,release}.log`; CTest logs are
`p41-01-ctest-{debug,release}.log`. Earlier incomplete/stale-build attempts are
not acceptance evidence. The final current-source runs all passed:

| Check | Debug | Release |
| --- | --- | --- |
| Four changed targets above | exit 0 | exit 0 |
| Five foundation modes | 5/5, exit 0 | 5/5, exit 0 |
| Seven Editor startup/rollback cases | 7/7 expected exits | 7/7 expected exits |
| Focused CTest | 3/3, exit 0 | 3/3, exit 0 |
| Real Editor/Runtime simulation probe | PASS, exit 0 | PASS, exit 0 |

Simulation was rerun using `python scripts/run_simulation_product_probe.py
build/Editor/<Config>/AxleEditor.exe build/Runtime/<Config>/AxleRuntime.exe
--report build/agent-runs/OPT-04/p41-01-product-simulation-<config>.json`.
Architecture AF-04 check passed (377 files, 18 CMake files); baseline tooling
regressions passed 18/18. `git diff --check` passed. The pinned NRI working tree
was rechecked clean. API-validation acceptance remains NOT RUN as recorded above.

## P41-02 - Transactional resource construction

Starting from `aba9a3c`. Texture and buffer creation now separates real wrapper,
allocation, binding, required views and uploads. Candidates clean up on rejection
and failed replacement preserves the old complete object. Descriptors are checked
before narrowing, including queried extent/format/sample/memory limits. D3D12
depth sampling checks the native SRV alias that the pinned NRI bridge actually
uses, rather than mistaking the DSV format's sampling bits for SRV capability.

Buffer placement borrows memory; successful adoption transfers ownership, failed
adoption leaves it with the caller. Group replacement is transactional. Map/update
ranges use checked subtraction and nested mappings honor their starting offset.
The pinned NRI helper uploads a whole buffer: device-local partial updates now
reject explicitly instead of reading beyond the source or ignoring the offset.
Existing production partial updates use host-visible buffers; no supported caller
was migrated to a smaller device upload. Mesh vertex/index uploads, sampler,
descriptor-pool and layout replacement preserve prior objects. Layout descriptor
ranges are deep-copied. Streamed meshes check staging results before recording.

Viewport color/depth/GUI registration, picking pairs and scene MSAA/post-processing
target sets are candidates. Late snapshot allocation failure abandons recording.
Recording/submission retirement and device ownership remain P41-05/06/10 work;
this package does not claim those lifetimes are already solved.

`AxleRHIResourceTests` is active in both configurations: policy, physical D3D12
and Vulkan passed initial current-library runs (all exits 0), with embedded NRI
validation. Tests cover every texture/buffer allocation/binding/view boundary,
real pixel and byte readbacks after failed replacement, invalid descriptors,
borrow/adopt/group ownership, helper rollback and partial streamed-mesh failure.
Logs: `p41-02-{debug,release}-{policy,dx12,vulkan}.log` and corresponding results
JSON under `build/agent-runs/OPT-04/`. No unexpected NRI errors in those runs.

`AxleRHIResourceViewTests` adds real native windows/ImGui registration and production
scene rendering, mesh pixel preservation, MSAA target recovery and picking-pair
rollback. All final Debug/Release checks passed after rebuilding the changed targets:

| Check | Debug | Release |
| --- | --- | --- |
| Resource tests (policy, physical D3D12/Vulkan) | 3/3, exit 0 | 3/3, exit 0 |
| Native view/product resource tests | 2/2, exit 0 | 2/2, exit 0 |
| P41-01 foundation regression modes | 5/5, exit 0 | 5/5, exit 0 |
| Editor startup/rollback regression cases | 7/7 expected exits | 7/7 expected exits |
| Focused rendering/material/VFX CTest | 6/6, exit 0 | 6/6, exit 0 |
| Physical VFX GPU regression, both APIs | 2/2, exit 0 | 2/2, exit 0 |

Final builds include AxleRHIResourceTests, AxleRHIResourceViewTests,
AxleRHIFoundationTests, AxleRenderUnitTests, AxleVFXGPUTests,
AxleMaterialUnitTests, AxleVFXUnitTests, AxleEditor and AxleRuntime; both exit 0.
Build logs: `p41-02-final-build-{debug,release}.log`. Commands/results/log paths
are recorded in `p41-02-final-{debug,release}-results.json`; the ignored runner
`verify02.py <Config>` reproduces the final test batch. Existing VFX allocation
failure probes now intercept allocation after real buffer-wrapper creation.

NRI validation exposed existing pipeline-layout visibility declarations using
`ALL` alongside fragment-only ranges. Scene/post-processing/picking/particle
layouts now specify the actual vertex/fragment stages; native production drawing
passes with embedded NRI validation and no unexpected NRI errors. The view test
initializes the real job owner before creating a Scene and tears it down normally.
Architecture AF-04, clean pinned NRI and `git diff --check` were rechecked.
P41-02 is implemented and self-verified; the final consolidated review remains open.

## Remaining work

P41-03..11 are not implemented. Next: structured errors, failed submit/present and bounded loss/teardown policy. Continue P41-03..11, then perform the consolidated final review requested by the user.

Full phase builds, CTest, integration, jobs/ECS, final native product simulation, GPU
readbacks and final window/lifetime checks remain open. API-validation prerequisites
must be available before claiming dependent P41-07 acceptance. The pre-existing
OPT-00 manual/full-product gap remains separate.
