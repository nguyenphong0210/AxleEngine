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
