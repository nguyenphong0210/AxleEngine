# M14-U8 — Migration, Tests and Tooling

Status: **Accepted**

## Caller migration

- Editor viewport resize, GPU picking, debug-line telemetry and IBL control now go through `RenderService`; `EditorLayer` no longer stores concrete backend renderer pointers or includes their headers.
- Runtime submits through `RenderService` and was audited for concrete renderer access.
- Preview fixture control uses `RenderService::SetUseIBL`; no preview product test reaches through `GetRHIRenderer`.
- Headless/native fixtures continue to submit immutable `RenderWorld` snapshots through `RenderService`/`RenderWorker`.

## Regression and ownership checks

- `RenderService` facade tests cover safe pre-initialization viewport, picking, telemetry and IBL seams.
- Existing `AxleRenderUnitTests` cover `RenderWorld`, material synchronization, strict graph dependencies, descriptor lifetime, temporal reset/history, backend policy and pass failure behavior.
- Native and OpenGL preview fixtures cover material/environment parity paths with explicit authored environment-lighting state.

## Tooling and generated map

- `scripts/check_architecture.py` now enforces the M14 product boundary: Editor, Runtime and UI cannot include concrete `RHISceneRenderer` or compatibility `SceneRenderer` headers.
- `scripts/generate_m14_dependency_map.py` audits product callers, contract owners and filtered architecture edges.
- Generated artifacts: `U8_M14_DEPENDENCY_MAP.json` and `U8_M14_DEPENDENCY_MAP.md`.

## Verification

- `cmake --build build --config Debug --target AxleRenderUnitTests AxleEditor AxleRuntime AxlePreviewRenderTests --parallel 6` — passed.
- `build/Tests/Debug/AxleRenderUnitTests.exe` — passed.
- `build/Tests/Debug/AxlePreviewRenderTests.exe dx12 surfaces` — passed.
- `build/Tests/Debug/AxlePreviewRenderTests.exe gl surfaces` — passed.
- `python scripts/generate_m14_dependency_map.py` — passed; zero architecture and caller violations.
- `python scripts/check_architecture.py --report build/m14-u8-architecture.json` — PASS.
- `python scripts/check_architecture.py --root Tests/Fixtures/ArchitectureViolationFixture --expect-violation` — expected forbidden-edge detection passed.
- `python scripts/test_baseline_tools.py` — 23 tooling regression tests passed.
- `git diff --check` — passed.
