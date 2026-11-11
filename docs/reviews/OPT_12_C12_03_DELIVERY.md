# OPT-12 C12-03 Reflection, Layout and Reload

Date: **2026-11-11** (Asia/Saigon). Base: `64827bb`.
Implementation: `a597f1ea8f2dc8097ad0cc2a8df8dee047c568b6`.
Status: **C12-03 complete and accepted within the released API/native boundary.**

## Released scope

The user directly requested C12-03 after C12-02. This implements graph-specific
reflection validation, descriptor/storage compatibility identities, shader and
pipeline keys, bounded owned compilation caching, immutable ready generations and
transactional last-known-good reload. It also provides typed authored-name instance
overrides and an atomic in-memory rename operation. The explicit node and lowering
registries remain the extension mechanism.

Automatic graph selection/loading in Editor/Runtime products, project-wide
instance discovery/persistence, a file watcher, asynchronous compilation, node UI,
complete C12-04 resource dependency/import/sampler policy, Slang and GL are outside
this package. Callers explicitly supply documents/source to the runtime and apply
resolved properties. This is a production API with direct native consumer tests,
not an automatic product integration claim.

## Implementation

- `MaterialGraphProgram::BuildLayout` validates the graph's canonical engine and
  material storage and descriptor namespace. It rejects legacy/unknown scalar
  metadata, unsupported resources/stages/arrays, duplicate or mismatched descriptors,
  invalid cbuffer storage, malformed parameter widths, incorrect engine offsets,
  row-major matrices and incompatible vertex input. Only actual reflected live
  resources are bound; compiler-eliminated resources require no phantom descriptor.
- Reflected resource, buffer and variable enumeration is canonicalized. This is
  necessary for descriptor range indices to agree when a compatible native layout
  is reused. `LayoutKey` describes descriptor addresses/types/counts/stages, buffer
  sizes/member storage and semantic vertex inputs. It omits backend vertex locations
  and the target bytecode format, so the tested DXIL/SPIR-V interfaces compare equal.
  Engine member offsets follow the fixed template ABI. Parameter offsets use valid
  compiler-reflected packing rather than independently reimplementing DXC packing;
  actual offsets/widths are validated, packed and included in the compatibility key.
- `ShaderKey` includes actual assembled HLSL, participating definition/lowering
  revisions, target bytecode format and a versioned fixed compiler contract (SM6_0,
  HLSL2021, O3, VSMain/PSMain, Vulkan1.3 DX cbuffer layout). It excludes uniform
  default values and texture paths. Embedded template/BRDF changes are source changes.
  Existing `SemanticKey` retains values/resource metadata for authored-state updates.
- Keys use complete length-delimited content, not a truncated hash or process
  pointer. They are process-local identities, not a persistent on-disk cache ABI.
  `PipelineKey` adds layout identity, culling, depth writes, target color/depth
  formats and sample count. The opaque/template revision fixes remaining state.
- `MaterialGraphRuntime` has an immutable asset identity and target. Its bounded
  FIFO code cache stores stage bytecodes/reflection only; default/resource/schema
  metadata is rebuilt from the current document on every reload. Capacity zero
  disables code retention. Cache ownership and counters are explicit; no global
  mutable graph registry or background worker was added.
- A candidate is published only after parsing/validation, real compilation (or
  code reuse), strict layout validation and, for native targets, actual native
  layout and graphics pipeline construction. Failures preserve the previous ready
  generation and expose diagnostics. Compatible native layouts are shared; native
  pipelines use the existing RHI pipeline cache. Returned pipeline pointers are
  for immediate recording, and command buffers retain native resource leases.
- Shared read-only generation snapshots preserve old defaults and layouts across
  successful reloads. Equivalent editor/name/order changes retain the generation.
  Native selection checks device readiness/owner and device epoch. Lost/stopped or
  replaced devices cannot draw stale generations; reload can rebuild on a new epoch.
- `MaterialGraphInstance` stores exact authored parameter names and finite typed
  values. Resolution uses the current generated symbol mapping, rejects missing
  names and incompatible types transactionally, and preserves unused authored
  overrides without creating GPU bindings.
- `RenameMaterialGraphParameter` validates/copies the document and all supplied
  instances, updates parameter references through registry `ParameterAttribute`
  metadata, migrates overrides and publishes all changes together. Conflicts or
  invalid instances leave every input unchanged. It handles custom parameter aliases
  without node-type dispatch. The caller owns disk persistence and supplying the
  complete set of affected instances; no automatic project-wide rename is claimed.

All APIs are synchronous and require one externally synchronized owner. Lowering
callbacks remain trusted and deterministic, with explicit revision changes when
their behavior changes. No plugin DLL lifecycle or cross-process cache is implied.
The Scene composition layer owns the graph/RHI bridge; Asset still links Core only.

## Verification

Initial Debug build and compiler/runtime/native/material focused tests passed
(**5/5**, exit 0) in `build/agent-runs/c12_03_extended_tests.log`. These precede the
final device-state guard and additional device-lost fixture; final-source checks
must supersede this checkpoint before acceptance.

`AxleMaterialGraphRuntimeTests` covers actual DXC compilation, strict malformed
reflection rollback, cross-backend layout equivalence, shader/pipeline key changes,
uniform/path cache reuse without stale metadata, immutable old generations, bounded
cache eviction, named instance mapping, rename conflicts and custom parameter aliases,
unused parameters, revision changes with identical HLSL, actual DXC error attribution
and recovery, parse/identity/format/device failures.

Opt-in physical D3D12/Vulkan modes draw directly from the returned stage bytecodes.
They do not serialize/recompile HLSL through the custom-source renderer. Production
binding plans resolve/pack uniforms into actual descriptor buffers; offscreen
readback checks independent emissive center-pixel values (tolerance 1):

| Draw | Expected RGB8 |
| --- | --- |
| Original default | 51, 102, 153 |
| Reloaded default | 153, 26, 77 |
| Instance override | 26, 204, 51 |

Native tests verify real pipeline cache reuse, explicit native layout/pipeline fault
injection with old-generation drawing afterward, changed-layout recovery, recorded
resource survival after wrapper/cache destruction, and device epoch rebuilds.
Missing DXC/native prerequisites are test failures. API/NRI validation counters are
checked through resource/device shutdown. Artifacts remain under the ignored
`build/Tests/MaterialGraphRuntime/<config>` directory.

Final-source verification (all commands exit 0):

| Command | Result | Log under `build/agent-runs/` |
| --- | --- | --- |
| `cmake --build build --config Debug --target AxleAllTests AxleEditor AxleRuntime --parallel 12` | PASS | `c12_03_final_debug_build.log` |
| `ctest --test-dir build -C Debug --output-on-failure` | **98/98 PASS**, 234.44 seconds | `c12_03_final_debug_ctest.log` |
| `cmake --build build --config Release --target AxleMaterialGraphRuntimeTests AxleMaterialGraphCompilerTests AxleMaterialGraphTests AxleMaterialUnitTests AxleEditor AxleRuntime --parallel 8` | PASS | `c12_03_final_release_build.log` |
| `ctest --test-dir build -C Release -R '^MaterialGraph(Tests\|CompilerTests\|GPU_.*\|RuntimeTests\|RuntimeGPU_.*)$\|^MaterialUnitTests$' --output-on-failure` | **8/8 PASS** | `c12_03_final_release_ctest.log` |
| `python scripts/check_architecture.py` | **555 sources / 25 CMake PASS** | Session tool output and independent review |
| `git diff --check`; exchange validation; changed-document local links | PASS; **137 links** | Session tool output |

The GPU modes were enabled with `AXLE_ENABLE_MATERIAL_GRAPH_GPU_TESTS=ON`; they
remain opt-in by default. All four native compiler/runtime modes ran in both
configurations, including the final device-lost readiness guard/epoch fixture.
Native runtime artifacts are `build/Tests/MaterialGraphRuntime/{Debug,Release}/`
`{dx12,vulkan}.{rgba,vs.bin,ps.bin}`. Their independent artifact inspection is
recorded below. No source changes followed the consolidated review.
The complete Release CTest matrix and manual product UI checks are **NOT RUN**.

## Independent review

Separate-context reviewer `c12_03_review` inspected the full source/test diff and
affected RHI/binding callers. It reported **no actionable findings or remaining
P1/P2**. It independently ran Debug and Release `^MaterialGraphRuntimeTests$`,
**1/1 PASS** in each configuration, and architecture **555 sources / 25 CMake**,
whitespace and exchange schema checks, all exit 0.

The reviewer inspected existing native artifacts rather than independently running
GPU tests concurrently with the final matrix. Debug/Release and D3D12/Vulkan raw
64x64 captures have identical SHA-256
`13d29b5e915e612da7524123b86b647524e41b17eaeb5b4e4bba0825e9e706d4`, maximum
byte difference 0 and center RGB 51/102/153. It also verified actual bytecode markers
and sizes (DXIL VS/PS 6064/5540 bytes; SPIR-V 2948/3740 bytes). This artifact inspection
is separate from the parent's executed native test evidence. No Gemini review,
implementation or approval is claimed.

## Limits

No FPS/performance/VRAM improvement is claimed. Native evidence is bounded to this
machine and the bundled toolchain. Full resource loading policy is still C12-04;
direct native reload tests use an emissive graph without textures, while C12-02's
complete texture graph remains a regression gate. No later package, vendor upgrade
or push is included.
