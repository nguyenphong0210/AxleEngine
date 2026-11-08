# OPT-09 selected Runtime and export

Date: 2026-11-08. Base: `45c059a`. Implementer: Codex, explicitly assigned by
the user. Source commit: `22c74ae`. Scope: PLG-22, PLG-23 and P41-26. Status: implemented and verified;
separate-context source review completed with no remaining concrete P1/P2.
OPT-10 and advanced plugin domain features are not released by this work.

## Delivered design

- `AXLE_BUILD_EDITOR`, `AXLE_BUILD_RUNTIME` and `AXLE_BUILD_TESTS` control real
  target configuration. Editor-free CPU tests have their own small suite.
  Editor-only configuration has no dependency on a Runtime target.
- Selected SDK Runtime modules compile as static libraries. Generated, ordered
  direct entrypoint references prevent registration stripping; module names are
  encoded into distinct C symbols. No Editor module is configured in shipping.
  Static activation reuses native ABI identity checks, registration rollback,
  code/service leases, owner-thread retirement and the canonical service session.
- Shipping uses one static Runtime host and project `Scripts/` sources. It has
  no scripts/plugin/Editor-host DLL dependency. Editor development Runtime retains
  its matched development script DLL and now stages its Runtime host explicitly.
- UI and CLI export capture an immutable request: saved project, Runtime selection,
  selected plugin installations, project assets/scripts, selected `Content/`, Engine
  sources/resources, pinned vendor sources, configuration and matched toolchain.
  Compiler/linker content and SDK version participate in the identity; the VS
  toolset and Windows SDK are pinned when configuring the product.
- An Engine source/configuration/toolchain cache is immutable and protected by a
  process-owned writer lock. It reuses libraries across selections. Every product
  has separate frozen plugin/content inputs, generated registration, player and
  receipt; the active Editor build tree never supplies its player artifact.
- `--check-content` runs without a window/GPU, against actual compiled services
  and scripts, before receipt publication. It supports loose and package-backed
  scene/Assembly documents. Required plugin payloads and absent scripts fail.
- Requests, materialized inventories, generated code and player/resource hashes
  are revalidated before export. Packaged project identity/selection must match
  the compiled player. Missing packaged project data fails before GPU creation.
- Asset-only cooking uses the same frozen project/content overlays. Selected plugin
  content is mounted at `Plugins/<plugin-id>/...` within the asset namespace.
  Declared Content modules use their manifest SourceRoot; code-only manifests use
  the conventional optional `Content/` directory. Overlay collisions fail.
  Export normalizes the packaged asset directory to `assets`, independent of the
  source project's asset directory.
- Cooking writes a sibling staging directory, checks every archive/copy result,
  and publishes only after final preflight. Output collisions/overlapping authored
  inputs are rejected; failure preserves the earlier successful export. Mesh
  binaries are derived in output-local scratch storage, never in frozen inputs.

## Review corrections

The separate-context source reviewer identified and rechecked generated content
tampering, compiler/link inputs outside the snapshot, authored output overlaps,
live shader fallback, Editor-only configuration, missing development host staging,
shipping startup without project data, ignored archive/audio failures, asset-only
snapshot bypass, extra binary/resource inputs, actual toolchain cache identity and
the loose/package/Assembly headless validation cases. Review is source evidence;
the reviewer did not execute builds or native cases. Final correction rechecks
reported no remaining concrete P1/P2 in this scope.

The full-suite build also exposed missing explicit test links to the Terrain
authoring prototype and the shared Runtime startup composition. Those test targets
now link their owning libraries; shipping does not configure these prototypes.

Actual detached package startup additionally exposed asymmetric Windows 8.3
normalization in the core package lookup. Both the active asset root and absolute
virtual descendant now resolve existing ancestors before deriving the package key;
lookups outside the root remain rejected. This correction received a focused
separate-context source recheck with no additional concrete P1/P2 finding.
Detached native execution then exposed ANSI shader-path conversions and two shader
consumers treating package paths as disk files. DXC/compiler/custom-program paths
now preserve UTF-8/native encodings; Particle/ObjectID use the common VFS reader.
These corrections were rechecked and the factory-generated players rerun.

## Verification

Logs are under `build/opt09-*`; generated outputs are ignored.

- Editor-free, Runtime-free CPU configuration/build/CTest: **2/2 PASS (Debug)**,
  `opt09-cpu-{configure,build,tests}.log`.
- Full final Debug/Release builds: **exit 0** for
  `cmake --build build --config <configuration> --target AxleEditor AxleAllTests --parallel 8`.
  Logs: `opt09-delivery-{debug,release}-build.log`.
- Full final CTest: **69/69 PASS per configuration**, exit 0 for
  `ctest --test-dir build -C <configuration> --output-on-failure`.
  Logs: `opt09-delivery-{debug,release}-tests.log`. These include actual owned
  process failure/cancellation, stale/tampered data and artifacts, late preflight
  failure, foreign output/authoring-root collisions, content-only mounts,
  static startup rollback/delayed Stop, architecture rejection fixtures and the
  existing DLL/script/render/data regressions.
- Native product matrix: **30/30 expected command outcomes per configuration**.
  Five products: empty, Vehicle, Spline, Terrain with automatic Spline, and project
  Mover plus Vehicle. Every product builds with Editor/tests OFF, has exact selected
  module configuration, imports no plugin/script/host/Editor DLL, passes packaged
  headless preflight, presents five frames on both D3D12 and Vulkan, and rejects
  a missing package with exit 1. The project case instantiates required owned
  Vehicle/Mover data and runs its statically compiled project script.
- Native logs/receipts/results: `build/opt09-native-{debug,release}/` and
  `opt09-native-{debug,release}-console.log`. The verifier captures link maps and
  checks each selected registration symbol against excluded symbols. Additional
  image identity presence/absence checks cover all ten player images;
  `build/opt09-native-closure-summary.json` records hashes and artifact paths.
  Reproduce using the optimized exporter independently of player configuration:
  `python scripts/verify_runtime_export.py build/Editor/Release/AxleEditor.exe --configuration Debug --output build/opt09-native-debug`
  and the same command with `--configuration Release --output build/opt09-native-release`.
- Editor-only configure: **exit 0** with `AXLE_BUILD_EDITOR=ON`,
  `AXLE_BUILD_RUNTIME=OFF`, `AXLE_BUILD_TESTS=OFF`; `opt09-editor-only-final.log`.
  Final architecture scan: **PASS**, 522 source files / 24 CMake files. Its
  deliberately forbidden-edge and tool regression probes pass in both full suites.

An initial fresh Release cache download of the pinned Agility SDK was interrupted;
the configure failed without publishing an export. A retry succeeded with the same
pinned vendor inputs. Earlier rejected/stopped intermediate builds and the manual
diagnostic Unicode candidate are not substituted for the final factory matrix.

## Limits

Vehicle, Spline and Terrain use the foundation behavior accepted by OPT-07/08.
Jolt driving, track meshing, sculpting/LOD/carving and audio/VFX prototypes do not
become completed shipping features through static composition.

`AXLE_ENABLE_GL_COMPATIBILITY=OFF` rejects OpenGL admission/fallback. Compatibility
implementation and GL dependencies remain compiled as transition adapters; this is
not a claim of binary GL exclusion or completion of the later renderer cutover.
No vendor upgrade, backend-default change, performance claim, push or OPT-10
acceptance is included.
