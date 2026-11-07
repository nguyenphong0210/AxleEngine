# OPT-04 Codex execution record

Date: 2026-11-07. Starting clean HEAD: `977f68c42755f1cfc0042854dc06dc74fc8cebc8`.
Scope: P41-01 through P41-11 only. No push or OPT-05 implementation.

Implementation and self-verification are separate from the required fresh-context
reviews after P41-03, P41-06 and P41-11. None of those reviews has completed.
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

Implemented and self-verified. Separate-context review remains due at P41-03.

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

## Remaining work

P41-02..11 are not implemented. The next package is transactional construction:
real wrapper/allocation/binding/view/upload failure paths, last-good preservation,
borrowed/adopted/group allocation ownership and affected target callers. Finish
P41-03 and its required separate-context review before starting P41-04.

Full phase builds, CTest, integration, jobs/ECS, final native product simulation, GPU
readbacks and final window/lifetime checks remain open. API-validation prerequisites
must be available before claiming dependent P41-07 acceptance. The pre-existing
OPT-00 manual/full-product gap remains separate.
