# Editor, tests and standalone build workflows

## Editor development

```powershell
cmake -S . -B build
cmake --build build --config Release --target AxleEditor --parallel 6
```

The default `cmake --build build --config Release` also excludes Runtime and test
executables. Editor still builds its required engine libraries and `AxleScripts`
for gameplay/hot reload. It does not build `AxleRuntime` or any test executable.
Regenerate the CMake project after updating from an older revision.

## Development sample, empty startup and explicit projects

Development builds open `Samples/Sandbox/Sandbox.axleproj` when launched
without an explicit project. This loads the sample's authored `scenes/Test.axlescene`;
it does not inject sample entities into every scene. The CMake cache entry
`AXLE_EDITOR_DEFAULT_PROJECT` embeds an absolute startup project path in Editor
only. Relative configured paths resolve from the repository root; runtime working
directory does not select a project. Engine resources remain in `Engine/Resources`.

Startup priority is: explicit `--project`/positional project, then `--empty-project`,
then the configured development sample, then a fresh untitled project if the
sample is disabled or missing. An invalid explicit project fails instead of
opening another project. The Editor never scans nearby legacy configs or staged
`assets` folders. Cooking/export CLI still requires an explicit `--project`.

```powershell
# Open the configured development sample (the default after building).
./build/Editor/Release/AxleEditor.exe
# Open a fresh project with zero entities and zero project assets.
./build/Editor/Release/AxleEditor.exe --empty-project
# Change the build's default project.
cmake -S . -B build -DAXLE_EDITOR_DEFAULT_PROJECT=D:/Projects/MyGame/MyGame.axleproj
# Disable the development convenience, including for distributed Editor builds.
cmake -S . -B build '-DAXLE_EDITOR_DEFAULT_PROJECT:FILEPATH='
```

Rebuild Editor after changing the cache entry. The sample remains a real project
in `Samples`; edits/save operations affect that project. No sample content is
copied into Editor or Engine, and Runtime/test builds retain their separate paths.

The built-in sample is named **Sandbox**. Existing build trees configured with
the former built-in `Samples/TestProject/TestProject.axleproj` path update to
Sandbox on regeneration; explicit custom defaults and a disabled default remain
unchanged.

The untitled asset root is unique under `%TEMP%/AxleEngine/UntitledProjects`.
Use **Save Project As** to preserve authored assets in the chosen project folder.
Save As rejects a nonempty destination asset folder rather than overwriting it.
Temporary workspaces are retained for recovery; they are not a permanent project
location. Saving the project configuration does not replace **Save Scene**.

Open an existing project through **Open Project** or explicitly on launch:

```powershell
./build/Editor/Release/AxleEditor.exe --project D:/Projects/MyGame/MyGame.axleproj
```

The selected project's configured start scene opens. A missing asset folder
stays associated with that project; it never falls back to another project.
The CLI cooker likewise requires an explicit `--project` argument.

For automated five-frame startup checks (real update/UI/render loop):

```powershell
./build/Editor/Release/AxleEditor.exe --verify-empty-startup --rhi=dx12
# Check normal project selection without forcing an empty project.
./build/Editor/Release/AxleEditor.exe --verify-startup --rhi=dx12
```

## Tests, explicitly

Build all existing test executables without building Editor or Runtime:

```powershell
cmake --build build --config Release --target AxleAllTests --parallel 6
ctest --test-dir build -C Release --output-on-failure
```

`AxleAllTests` builds tests; it does not run them. Legacy interactive suites in
`AxleTests.exe` retain their own runner. Physical GPU tests remain explicitly
selected/configured; normal CTest registration is not a claim of GPU acceptance.

For a smaller check:

```powershell
cmake --build build --config Release --target AxleVFXUnitTests --parallel 6
ctest --test-dir build -C Release -R '^VFXUnitTests$' --output-on-failure
```

Test executables are excluded from the default build but registered with CTest;
build the selected executable before running its test. Legacy integration tests
stage their own resources instead of depending on the Editor executable.

Sources and CMake targets now live under top-level `Tests/`; binaries are in
`build/Tests/<configuration>/`. Fixtures moved from `Editor/assets` and root
`assets` to `Tests/Fixtures/Assets` and `Tests/Fixtures/LegacyAssets`. Tests stage
writable copies under `build/Tests/Work/<configuration>` and never use the active
Editor project. See [Tests/README.md](../../Tests/README.md) for the layout.
The Editor and Runtime post-build steps do not bundle these test assets.

## Standalone from the Editor

Open **File/Build -> Export Standalone Game**, keep **Build and bundle standalone
player** enabled, choose the output directory, and click **Export & Cook Game**.

The exporter captures the saved project, selected Runtime modules, project Scripts,
assets, selected plugin Content, Engine resources/sources, pinned vendors and
matched toolchain into an immutable request. It builds an Editor-free static
player against that snapshot in a separate product/cache tree. The active Editor
build does not supply its player. Matching receipts and headless content preflight
must pass before a staged package replaces an earlier successful export.

Shipping contains selected static Runtime modules and statically registered project
scripts, without plugin/script/host/Editor DLL imports. Development Runtime remains
an explicit target with its matching development script and Runtime host DLLs.
Asset-only export follows the same snapshot rules and does not build a player.
Closing/cancelling the owned build preserves the earlier successful output.

```powershell
./build/Editor/Release/AxleEditor.exe --project=D:/Projects/MyGame/MyGame.axleproj --export D:/Exports/MyGame --runtime-configuration=Release
./build/Editor/Release/AxleEditor.exe --project=D:/Projects/MyGame/MyGame.axleproj --cook D:/Exports/MyGameData
```

The UI uses its running Editor configuration by default. CLI
`--runtime-configuration=Debug|Release` can select the player's configuration.
Build logs and matching request/product receipts are retained in the generated
Runtime build cache. A failed build never bundles an unrelated old executable.
Export requires the engine checkout, configured CMake/MSVC/Windows SDK and pinned
vendor dependencies; moving only Editor.exe does not install a build SDK.

## Product switches and architecture gates

`AXLE_BUILD_EDITOR`, `AXLE_BUILD_RUNTIME` and `AXLE_BUILD_TESTS` control target
configuration. Editor-only builds use Runtime/tests OFF. Editor-free tests have
an independent CPU suite. Shipping export configures Editor/tests OFF and the
validated selected Runtime graph. `AXLE_ENABLE_GL_COMPATIBILITY=OFF` rejects GL
admission/fallback; compatibility code is still compiled, so this is not binary
GL exclusion or the deferred full renderer cutover.

```powershell
cmake -S . -B out/player -DAXLE_BUILD_EDITOR=OFF -DAXLE_BUILD_RUNTIME=ON -DAXLE_BUILD_TESTS=OFF
cmake --build out/player --config Release --target AxleRuntime --parallel 8
python scripts/check_architecture.py
python scripts/run_architecture_acceptance.py --build build --output build/acceptance
```

See [architecture acceptance](ARCHITECTURE_ACCEPTANCE.md) for current APIs,
strict evidence semantics and native/manual gates. The records below describe
historical verification, not results newly produced by these commands.

## Verification of the layout/startup change (2026-11-03)

Release `AxleEditor` and `AxleAllTests` builds succeeded. CTest passed **9/9**,
including `ProjectStartupTests`, physical `VFXGPU_D3D12`/`VFXGPU_Vulkan`, and the
build-workflow success/failure/cancellation fixture. Running
`build/Tests/Release/AxleBuildWorkflowTests.exe --real-runtime` built the player
through the Editor's production build helper successfully.

The real Editor was initialized and shut down with `--verify-empty-startup` on
D3D12, Vulkan and OpenGL (exit 0 each, 0 entities/0 project assets). The D3D12 and
Vulkan logs confirmed the requested active backend. This checks initialization,
not interactive rendered frames. Default/Editor project dependency graphs contain
neither test targets nor Runtime; test targets contain neither Editor nor Runtime.
No source `assets` or `Editor/assets` directory was recreated.

Local evidence is in ignored `build/empty-verification-build.log`,
`build/empty-ctest.log`, `build/empty-runtime-build.log` and
`build/empty-startup-{dx12,vulkan,opengl}.log`. Debug builds, full legacy interactive
suites and manual project-dialog interaction were not run for this change.
Existing MSVC `APIENTRY` macro and CRT link warnings remain outside this change.

### Follow-up: passive UI Library creation fixed (2026-11-03)

The initial initialization-only check missed UI Library's unconditional
`GetOrCreateCanvasEntity()` during drawing: the first UI frame added a `UICanvas`
to an otherwise empty scene. Canvas creation now happens only in explicit widget
creation/drop actions. The panel's passive drawing/searching does not modify the
scene; deleting a canvas does not cause it to reappear.

`ProjectStartupTests` now renders the real panel in a headless ImGui context across
multiple frames and checks explicit widget creation and post-deletion behavior.
`--verify-empty-startup` now runs the real application loop for five UI frames and
checks emptiness after the Editor's UI each frame. The updated Release Editor
passed this check on D3D12, Vulkan and OpenGL (exit 0, 0 entities/0 assets), with
logs in `build/canvas-frames-{dx12,vulkan,opengl}.log`. The initial checks above
remain historical initialization evidence. Manual UI interaction was not tested.

### Follow-up: configured development sample (2026-11-03)

`AXLE_EDITOR_DEFAULT_PROJECT` now selects `Samples/TestProject/TestProject.axleproj`
by default, as requested for development. Explicit project selection still wins;
empty/missing/disabled defaults preserve untitled startup. CPU tests cover these
precedence cases, including a missing explicit project failing rather than falling
back to the sample.

Release Editor and project tests built successfully; CTest passed 9/9. The real
Editor ran five frames with the sample on D3D12, Vulkan and OpenGL, each reporting
its sample root and the 10 entities authored in `Test.axlescene`. Additional native
D3D12 runs passed for explicit override, `--empty-project`, the empty-startup
assertion and launch from the executable directory. CMake generation was checked
with the default disabled and with a repository-relative sample path, then the
normal sample configuration was restored and rebuilt. Logs are in ignored
`build/sample-*.log`. These checks verify project selection and startup, not
rendering parity or gameplay-script execution. No sample content was changed.
