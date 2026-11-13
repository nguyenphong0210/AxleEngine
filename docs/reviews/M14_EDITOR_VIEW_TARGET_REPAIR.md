# M14 Editor view target repair

User-reported symptom: the Sandbox Editor repeats `Editor view failed: View preparation, recording or submission failed` every frame.

## Cause and correction

Reproduced with the real windowed Debug Editor loading the user's current `Samples/Sandbox/assets/scenes/Test.axlescene`. Before correction, `--verify-startup` logged five failed scene views but still printed PASS because it only counted UI frames.

Commit `44eb036` propagates the complete render settings into viewport-owned target generations: Hi-Z, bloom, half-resolution GTAO, exposure and DDGI. Previously the Editor forwarded only normal/storage flags while `RHISceneRenderer` required the added target flags, rejecting the mismatched generation before recording. Asset/VFX previews use the same settings overload. DDGI target allocation now follows the setting consistently in the renderer, RenderService and viewport; the dispatch still requires an authored grid.

The startup verifier now requires at least one successful scene view and zero failed views, including viewport reconfiguration failures. The native resource-view fixture exercises combined M14 effects with explicit viewport targets, optional exposure/DDGI without a grid, and disabling effects again. Each new draw checks configuration and renderer success rather than a stale success flag.

## Verification on the correction

- Debug and Release build: `cmake --build build --config CONFIG --target AxleEditor AxleRHIResourceViewTests --parallel 6`, exit 0.
- Actual windowed Editor: `build/Editor/CONFIG/AxleEditor.exe --project Samples/Sandbox/Sandbox.axleproj --rhi=BACKEND --rhi-validation --verify-startup`, Debug/Release x dx12/vulkan, all four exit 0. All load the current five-entity Test scene and pass the strengthened verifier. No `Editor view failed` or NRI validation error in these logs.
- `build/Tests/CONFIG/AxleRHIResourceViewTests.exe BACKEND`, Debug/Release x dx12/vulkan, all four exit 0; each fixture requires zero recorded RHI validation errors, including teardown. Expected injected allocation failures remain part of this test.
- Rebuilt Debug `AxleRenderUnitTests`; focused CTest `RenderUnitTests|RenderAcceptancePolicy|EnvironmentBakeTests`: 3/3 PASS.
- `python scripts/check_architecture.py`: PASS (576 source files, 25 CMake files).
- Separate-context read-only correction review found two verification gaps (configuration failures were not counted; test draws could reuse stale success), both corrected and rechecked with no remaining findings in this patch.

Logs are under `build/m14_editor_*` and `build/m14_view_*`; generated logs are not authored repository artifacts.

## Limits

This accepts the Editor target configuration repair, not full Milestone 14 fidelity or performance. The native automation helper fails to connect its pipe, so no direct UI screenshot or mouse-driven visual inspection is claimed. A Release D3D12 Editor is opened separately for continued use after the automated checks. User scene changes were not modified or staged by this repair.
