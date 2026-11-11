# OPT-12 C12-05 Optional Slang Evaluation

Date: **2026-11-11** (Asia/Saigon). Base: `0ae14d1`.
Status: **Prototype implemented; final enabled/disabled gates and review pending.**

## Release and decision boundary

The user directly requested C12-05. Its plan calls for an optional Slang prototype
that existing materials do not require. This package evaluates the existing generated
HLSL graph profile with real DXIL/SPIR-V output, reflection/storage adaptation and
native comparison against DXC. It does not change authoring syntax, introduce Slang
source assets, switch existing shaders/materials, or implement C12-06 OpenGL policy.

**Assessment at this checkpoint:** feasible as an explicit optional graph backend.
Keep DXC as the product default. The tested images/layouts match, while measured
Slang compilation is slower on this fixture. No default compiler migration is accepted.
Full Slang language/generic/module authoring, arbitrary shader stages/options and
cross-platform SDK deployment are not established by this bounded prototype.

## Toolchain and references

The machine already has Vulkan SDK **1.4.363.1**, supplying Slang
**2026.17-25-gc7954ebd1**. The adapter uses this installed SDK; no vendor library in
the repository was upgraded or redistributed. SDK DLLs/headers remain external.
Slang DXIL invokes the existing bundled DXC downstream; Slang SPIR-V is emitted
directly. This is two different frontend paths, not an assertion that DXIL removes DXC.

The implementation follows Slang's official
[compilation API](https://docs.shader-slang.org/en/stable/external/slang/docs/user-guide/08-compiling.html)
and [reflection API](https://docs.shader-slang.org/en/stable/external/slang/docs/user-guide/09-reflection.html),
verified against the installed SDK headers and actual emitted bytecodes. Target flags
are derived from the local `slangc -help`, not assumed to match DXC defaults.

## Implementation

- `AXLE_ENABLE_SLANG` defaults **OFF**. Enabling it requires an explicit
  `AXLE_SLANG_SDK_ROOT`; this evaluation adapter currently supports Windows/MSVC.
  Missing headers/import library/DLL fail enabled configuration rather than silently
  compiling a stub. Changing the SDK root refreshes header/import discovery.
- `RHISlangCompiler` exposes no SDK headers publicly. Enabled builds keep the SDK
  import library as an explicit configure/link contract, but resolve the global
  session C entry point from the configured absolute DLL only on Slang selection.
  Existing DXC startup/compilation does not load Slang. Disabled
  builds have a diagnostic stub and no Slang header/library dependency.
- The configured SDK is pinned for the process: header/DLL build tags must match,
  and a different already-loaded SDK or alternate usable SDK path is rejected.
  Instances release their sessions; imported SDK modules remain process-resident
  after first use. This is not a plugin unload/hot-swap design.
- The supported profile is generated HLSL, Vertex/Fragment, SM6_0, HLSL2021 contract,
  column-major matrices, DX cbuffer packing, optimized/unoptimized compilation and
  DXIL or SPIR-V. Unsupported stages/formats/options and embedded NUL source fail.
  Target macros mirror the existing DXC path. No compiler failure switches backend.
- Vulkan explicitly maps `b` and `t` registers with zero shifts, `s` registers with
  shift128, preserves source entrypoint names and selects SPIR-V1.3 (usable on the
  existing Vulkan1.3 backend). Applying sampler shifts alone was experimentally
  insufficient: the other resources otherwise received auto-assigned bindings.
- Reflection uses Slang's linked program layout and post-codegen usage metadata.
  Vulkan uses typed resource categories and actual physical binding indices, then
  translates sampler128 offsets back into the RHI's logical register model. A small
  bounded SPIR-V instruction reader verifies descriptor presence and vertex locations
  from the actual output, not a second DXC compilation of the original source.
- DXIL reflection is checked against the emitted container with the bundled DXC
  reflection API. Slang wraps source cbuffers in structs; the adapter checks their
  actual nested float member offsets/types/sizes/matrix layout without broadening
  the existing generic custom-shader struct ABI. Names are mapped by the source
  Slang layout; physical addresses/storage must agree with the container.
- `MaterialGraphCompilerBackend` is an explicit compile/target policy, default DXC.
  Shader keys include backend and Slang's toolchain identity; source-equivalent DXC
  and Slang artifacts cannot collide. Existing typed packing, descriptor validation,
  resource resolution, native generation publication and last-known-good behavior
  remain shared with C12-03/04. No separate graph node compiler dispatch was added.
- Node-local Slang errors map numeric virtual source names back to authored nodes.
  Warning blobs are retained across compilation phases. Invalid graphs, absent SDK,
  compiler errors, layout failures and native candidate failures preserve outputs.

The only new architecture links are RHI's explicitly declared optional external
`Slang::Compiler` and Windows `delayimp`. The C entry point is still resolved from
the explicitly loaded DLL. Asset remains Core-only. No source include
or target edge from Asset to RHI/Renderer/Slang was added.

## Verification design and checkpoints

The separate-context reviewer `/root/c12_05_review` inspected the complete source
diff, installed SDK metadata/com-pointer APIs, build optionality, cache/runtime
callers and shared native draw helper. It reported **no blocking source findings**
and independently ran architecture (560 sources/25 CMake files) and whitespace
checks successfully. It did not run builds/GPU tests; the final matrix below is
the implementing agent's executed evidence.

The final enabled Debug profile is **4/4 PASS** (selection, compiler policy, physical
D3D12 and Vulkan), `build/agent-runs/c12_05_enabled_final_profile_tests2.log`.
The Slang-disabled Debug material regression is **12/12 PASS**, including the same
native D3D12/Vulkan coverage, `build/agent-runs/c12_05_disabled_debug_tests.log`.
The final source rebuilds and regression matrix are:

| Configuration | Build/test result | Evidence |
| --- | --- | --- |
| Slang enabled, Debug | **105/105 CTest PASS**; `AxleAllTests`, Editor and Runtime build PASS | `build/agent-runs/c12_05_enabled_debug_build_final2.log`, `c12_05_enabled_debug_ctest_final.log` |
| Slang enabled, Release | Material Graph/MaterialUnit/Slang focused **15/15 PASS**; Slang test, Editor and Runtime build PASS | `build/agent-runs/c12_05_enabled_release_build_final.log`, `c12_05_enabled_release_ctest_final.log` |
| Slang disabled, Debug | Material Graph/MaterialUnit focused **12/12 PASS**; Slang test, Editor and Runtime rebuild PASS | `build/agent-runs/c12_05_disabled_debug_build_final2.log`, `c12_05_disabled_debug_tests_final.log` |
| Architecture/whitespace | **560 sources / 25 CMake PASS**, `git diff --check` PASS | final source checkout |

Selection probes verify default DXC output and that `slang-compiler.dll` is not loaded
before/after default DXC compilation, even in enabled builds. An unavailable instance
must return a diagnostic and no bytecode. Disabled explicit selection must preserve
the prior output; enabled selection must return a distinct compiler-aware identity.

Compiler tests use actual Slang output for both formats, compare strict canonical
layouts with DXC, pack authored-name overrides, preserve source asset round-trips,
exercise an owned custom lowering, reject malformed graphs/options, and attribute
real Slang syntax failures to the original node while preserving a ready generation.
A reachable but ignored texture is removed by target compilation and creates no
phantom sampler/texture dependency.

Native probes draw returned stage bytecodes directly with the shared production
binding/resource API. The approved complete rust/metal graph uses real generated PNG
textures and the existing 64x64 two-sided triangle fixture (CullMode=None for this
fixture only). Nonuniform model scale/translation, camera and directional light verify
actual constant/matrix packing rather than only an identity transform. Tests compare
baseline, typed parameter overrides, PointClamp, texture-content reload without shader
recompilation, and last-known-good after malformed input. No software/GPU fallback or
successful skip is allowed. API/NRI validation counters are checked through shutdown.

The predeclared comparison threshold is maximum RGB byte error1, mean error0.1,
and nontrivial image energy>10000. Checkpoint results were **maximum0 / mean0** in
all five cases on both APIs. A darker initial override fixture was changed to a
visible dielectric material while retaining the original energy threshold.
These 1x1 texture fixtures establish binding/content and compiler equivalence;
they do not independently prove PointClamp versus LinearRepeat filtering behavior.
The existing C12-04 filtering fixtures remain the coverage for that distinction.

Warm compile measurements time complete VS+PS graph compilation, reflection and
strict layout validation, with three uncached repetitions per compiler/format.
SDK/global initialization is outside the warm measurements. Native runtime code-cache
hits are a separate feature and do not count as compiler speedups. Checkpoint sizes:

| Target | DXC VS/PS bytes | Slang VS/PS bytes |
| --- | ---: | ---: |
| DXIL | 6064 / 6864 | 6292 / 7080 |
| SPIR-V | 2948 / 5380 | 2916 / 5304 |

Checkpoint Debug timings were approximately DXC22ms vs Slang85ms per DXIL pair and
DXC36ms vs Slang70ms per SPIR-V pair. These are local fixture observations, not a
general compiler benchmark or FPS claim. Final configuration/timing summaries,
hashes and artifacts will be recorded after the final matrix.

## Final gates

The final source review and architecture/whitespace checks pass. The complete
enabled/disabled regression matrix and Release focused results are listed here after
their source rebuilds; no missing gate is implied by the earlier checkpoint.

## Limits

Only the installed SDK/toolchain and tested NVIDIA RTX3070 are established.
No Slang adoption as the default, CPU parallel compiler ownership, SDK unloading,
full Slang language/feature support, non-Windows toolchain, deployment packaging,
OpenGL/GLSL policy, graph UI, FPS/VRAM improvement or later package is accepted.
No vendor upgrade or push is included.
