# Tests and fixtures

Tests are a separate opt-in build product. Neither Editor nor Runtime depends on
test executables, fixture staging, or the sample gameplay script DLL.

```powershell
cmake -S . -B build
cmake --build build --config Release --target AxleAllTests --parallel 6
ctest --test-dir build -C Release --output-on-failure
```

- `src/`: unit suites, legacy integration runner and build-workflow fixture.
- `Fixtures/Animation/`: immutable importer/controller fixtures.
- `Fixtures/Assets/`: former Editor demo/reference assets, retained for tests.
- `Fixtures/LegacyAssets/`: former root assets, including VFX acceptance references.
- `Fixtures/LegacyProject.axleproj`: explicitly opened legacy fixture project;
  the Editor never discovers it on startup.

Executables are written to `build/Tests/<configuration>`. `AxleTestResources`
stages assets and engine resources into `build/Tests/Work/<configuration>`;
`TestWorkspace.hpp` selects that writable root at test entry. New tests must use
that workspace for generated files. Resolve immutable source fixtures through
`AXLE_TEST_SOURCE_DIR`; do not write into fixture source directories.

`AxleTestScripts` builds fixture scripts into a separate `AxleScripts.dll` in the
test output. Production `AxleScripts` does not register sample car/controller
scripts. `AxleAllTests` only builds tests; CTest runs registered CPU/build suites.
Physical GPU registration requires `-DAXLE_ENABLE_VFX_GPU_TESTS=ON`. Legacy
interactive `AxleTests.exe` remains separately invoked and is not registered as
a full unattended CTest suite.

`ProjectStartupTests` checks explicit/development/empty startup selection, fresh
untitled projects, authoritative missing asset
roots, Save As asset preservation and explicit project opening. A headless ImGui
regression renders the visible UI Library across multiple frames, checks that a
deleted canvas stays deleted, and verifies explicit canvas/widget creation. Native Editor
startup can be checked separately with `--verify-empty-startup`; it runs five
real update/UI frames and checks for unintended scene/asset creation after each
UI frame. It does not certify manual UI interaction or visual rendering quality.
