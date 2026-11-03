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

## Empty Editor startup and explicit projects

Launching `build/Editor/Release/AxleEditor.exe` without a project opens a fresh
untitled workspace with **zero scene entities and zero project assets**. It does
not search for `AxleEditor.axleproj`, reopen a sample scene, or scan old staged
`assets` folders. Engine shaders, icons and fonts remain in `Engine/Resources`.

The untitled asset root is unique under `%TEMP%/AxleEngine/UntitledProjects`.
Use **Save Project As** to preserve authored assets in the chosen project folder.
Save As rejects a nonempty destination asset folder rather than overwriting it.
Temporary workspaces are retained for recovery; they are not a permanent project
location. Saving the project configuration does not replace **Save Scene**.

Open an existing project through **Open Project** or explicitly on launch:

```powershell
./build/Editor/Release/AxleEditor.exe --project D:/Projects/MyGame/MyGame.axleproj
```

An explicit project's configured start scene still opens. A missing asset folder
stays associated with that project; it never falls back to another project.
The CLI cooker likewise requires an explicit `--project` argument.

For an automated initialization/shutdown check (no interactive frame loop):

```powershell
./build/Editor/Release/AxleEditor.exe --verify-empty-startup --rhi=dx12
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

1. Editor starts `cmake --build <configured build> --config <Editor configuration>
   --target AxleRuntime` on a worker, without opening a console window.
2. The UI reports build progress status. Closing the application cancels its
   owned build process tree; another export cannot start concurrently.
3. Only after a successful build does asset cooking/package export run.
4. Packaging selects the exact Runtime output directory for that configuration,
   including its matching `AxleScripts.dll`. A failed build never falls back to an
   old executable elsewhere in the checkout.

The player build uses the same configuration as the running Editor (for example,
Release Editor builds Release Runtime). Build output is saved as
`runtime-build-*.log` in the configured build directory; failures show the log path.
Asset-only export, with the player checkbox disabled, does not build Runtime.

This on-demand Windows player build requires the configured engine checkout,
CMake and the configured compiler/toolchain. Moving only the Editor executable
does not install a build SDK. Missing build tools/output files produce an export
error. Native button interaction is a separate check from the process tests.

Manual Runtime build remains available for developers:

```powershell
cmake --build build --config Release --target AxleRuntime --parallel 6
```

Runtime output stays in `build/Runtime/<configuration>`; its build no longer
copies the player into the Editor output directory. The command-line cooker uses
prebuilt executables when executable bundling is requested; it does not invoke
the Editor's asynchronous build workflow. Missing required binaries fail bundling.

## Relationship to Phase 41

This is build-workflow separation, not completion of Phase 41's Runtime/rendering
or module-isolation packages. Engine/plugin libraries still have the existing
transitive dependencies. `AXLE_BUILD_EDITOR=OFF` and the new render harness targets
in the Phase 41 design remain proposed until their packages implement them.

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
