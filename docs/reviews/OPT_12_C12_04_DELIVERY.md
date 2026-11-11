# OPT-12 C12-04 Resource Semantics

Date: **2026-11-11** (Asia/Saigon). Base: `4cd15bc`.
Implementation: `a899bb492529625b6c84103bac4c12ece7df094d`.
Status: **C12-04 complete and accepted within the released API/native boundary.**

## Released boundary

The user directly requested C12-04. The package defines explicit graph texture
interpretation, sampler, fallback and dependency contracts, and resolves/uploads
those resources inside C12-03's transactional native generations. Asset resolution
is CPU-only and links Core only. The existing custom HLSL consumer also supports
the four sampler annotations, preserving its unannotated default behavior.

Automatic product graph loading/selection, node-editor UI, file watchers,
asynchronous resource jobs, graph texture-instance overrides, complete offline
texture compression/import tooling, Slang and later lighting remain separate.
The native API consumes explicit documents/source, not an Editor asset selector.

## Authoring contract

- Existing approved syntax remains valid. `SampleTexture2D` accepts all four
  previously authored samplers: `LinearRepeat`, `LinearClamp`, `PointRepeat`,
  `PointClamp`. Linear uses trilinear filtering, Point uses nearest filtering for
  minification/magnification/mips, Clamp means clamp-to-edge, and anisotropy is 1.
  No mode silently maps to another. Shared explicit sampler policy conflicts fail
  binding-plan validation. Unannotated custom shaders retain their existing sampler.
- The optional string property `fallback` defaults to `"Error"`; supported explicit
  values are `"White"`, `"FlatNormal"`, `"Magenta"`. `FlatNormal` requires Linear.
  Missing, empty, unreadable or undecodable image bytes may use a declared fallback.
  A fallback is a real 1x1 RGBA image and always produces a node/path warning; it is
  never reported as a valid source asset. Invalid paths, descriptor contracts,
  color-space contradictions and native GPU failures still fail reload.
- Builtin `UnpackNormal` has input `rgb: Float3` and output `normal: Float3`. It maps
  `[0,1]` RGB to signed XYZ and normalizes, with `(0,0,1)` for a zero vector.
  Its input is linearly encoded tangent normal data; +Y is preserved, with no hidden
  green-channel inversion. Connect its output to the surface's tangent-space Normal.
  Normal filtering is linear RGB filtering followed by shader normalization.
  The graph does not infer a texture's role from its filename.
- Color textures use hardware sRGB views: RGB is decoded before filtering; alpha
  remains linear. Linear textures use UNORM views; HDR uses linear RGBA32_FLOAT.
  HDR with sRGB is a diagnostic. Image rows follow the existing material convention
  (`FlipVertically=true`). Mips average RGB in linear light and alpha independently;
  graph textures are unpremultiplied and do not use alpha-weighted RGB filtering.

## Paths, descriptors and dependencies

`MaterialGraphResources::Resolve` accepts only canonical project-root-relative
references. It resolves symlink/junction ancestors and rejects root escapes, uses
exact mounted package entries first, and reads only rooted regular files otherwise.
It does not search Engine Resources or CWD, discover companion descriptors, use
filename role heuristics or borrow a process-global last-good decoded image.
Content is decoded through production `DecodeImage`, not mock metadata.

A raw path uses graph color space/sampler, full resolution and generated mips. An
explicit `.axleasset` Texture descriptor supplies a root-relative raw `source`,
`generateMips` and `maxResolution`. Descriptor dependencies include both files.
Duplicate/unknown/malformed fields, nested descriptors, noncanonical source paths,
unsupported types and prefiltering fail with the authored graph node/path attached.
Only 2D `Color`, `Data`, `NormalMap` descriptors are accepted; Data/NormalMap require
Linear graph sampling. Color descriptors may be read as Linear explicitly. Graph
sampler/color-space fields take precedence rather than importing descriptor filter
or wrap settings. Descriptor filter/wrap strings are validated as metadata.

The existing descriptor's `compression` is offline import metadata: this runtime
uploads decoded uncompressed RGBA8 or RGBA32_FLOAT and does not claim BC compression.
Exposure must be 1 and cubemap/prefilter descriptors are rejected by this 2D API.
No automatic sidecars, renamed metadata files or authored root registry are created.

Resolved entries record node, authored/source paths, byte-content revisions,
interpretation options, fallback state and warnings. Dependencies represent readable
nonempty bytes; decode validity is separate. Runtime resolution is restricted to
actually reflected live textures, so eliminated bindings do not load missing files.
Unused authored nodes remain in the document. Dependency diagnostics attach the
original graph node/source and include the referenced path; descriptor errors also
identify the failing source/field in their message.

## Native publication and cache behavior

Native `MaterialGraphRuntime::Reload` now prepares an entire candidate: compiled
program, layout/pipeline, decoded images, uploaded native textures and explicit
samplers. It publishes only after every required native resource succeeds. CPU-only
runtime targets retain their code-only contract; the CPU resource resolver is a
separate explicit API and does not require a graphics device.

Resource fingerprints include source generation/root, references, content revisions,
descriptor bytes, interpretation and fallback state. They are process-local content
fingerprints, not a persistent or cryptographic cache ABI. Same-size/timestamp-preserved
edits, descriptor/source edits and package mount changes invalidate resources without
baking image paths/contents into shaders. Shader code reuse and pipeline state are
separate. Graph sampler/color-space changes may change generated annotations/code keys.

Compatible native textures and samplers are shared across generations and repeated
bindings. Different color-space interpretations of the same file get different views.
Old generation snapshots keep their own images, resource metadata and native objects.
`WriteResourceDescriptors` fills actual texture/sampler ranges in caller-owned fresh
descriptor sets; the caller packs cbuffers and records the draw. Existing RHI leases
retain descriptors/resources through recorded/submitted command lifetime, including
when the generation and caches have already been released.

Missing/corrupt replacements preserve last-known-good ready generations by default.
Authored fallbacks publish warnings and recover to real images on a later successful
reload. Native creation/upload/sampler failures never masquerade as successful image
fallback. No asynchronous watcher or automatic project reload is implied.

## Verification

Initial Debug build and focused matrix: **11/11 PASS**, exit 0, including compiler,
document, runtime, material and physical D3D12/Vulkan compiler/runtime/resource modes.
Log: `build/agent-runs/c12_04_extended_tests.log`. The final rooted regular-file read
and sampler-field diagnostic polish followed this checkpoint; final-source checks
must supersede it before acceptance.

CPU fixtures use real TGA/HDR bytes and production decoding. They cover sRGB/linear
mips and linear alpha, odd dimensions, raw/descriptor precedence, strict descriptors,
missing/corrupt images and fallback recovery, root/CWD isolation, UTF-8 paths, package
descriptor/source dependencies, optimized-out resources, and actual DXIL/SPIR-V
compilation/reflection for all four samplers and `UnpackNormal`.

Native fixtures draw returned compiler bytecodes through actual production
texture/sampler descriptors, pack uniform buffers and read back 64x64 RGBA8 targets.
Independent center-pixel checks use tolerance 1:

| Probe | Expected RGB8 |
| --- | --- |
| Uniform byte-128 through sRGB / Linear | 55 / 128 |
| Uniform byte-200 through sRGB | 147 |
| Simultaneous sRGB + Linear views of byte-128, 50% blend | 92 |
| Point Repeat / Clamp at `u=1.25` on black-white image | 0 / 255 |
| Linear / Point at the black-white midpoint | 128 / 255 |
| Minified sRGB black/byte-128 mip | 27 |
| Alpha from the same minified image | 128 |
| Top-left UV under material row convention | blue `(0,0,255)` |
| Declared Magenta / FlatNormal decoded to emission | `(255,0,255)` / `(1,1,255)` |

Tests also verify visible normal-lighting changes, pointer/metadata sharing and
separation, descriptor source edits and package mount/unmount without shader recompiles,
content-only reload with restored timestamps, old-generation draws, injected native
upload/sampler failure rollback, and recorded texture/sampler survival after generation
and pipeline-cache destruction. Missing native prerequisites fail instead of skipping.
API/NRI validation counters are checked through device shutdown. Artifacts remain in
ignored `build/Tests/MaterialGraphResources/<config>/<mode>` directories.

Final-source verification (all commands exit 0):

| Command | Result | Log under `build/agent-runs/` |
| --- | --- | --- |
| `cmake --build build --config Debug --target AxleAllTests AxleEditor AxleRuntime --parallel 12` | PASS | `c12_04_final_source_debug_build.log` |
| `ctest --test-dir build -C Debug --output-on-failure` | **101/101 PASS**, 235.86 seconds | `c12_04_final_debug_ctest.log` |
| `cmake --build build --config Release --target AxleMaterialGraphResourceTests AxleMaterialGraphRuntimeTests AxleMaterialGraphCompilerTests AxleMaterialGraphTests AxleMaterialUnitTests AxleEditor AxleRuntime --parallel 8` | PASS | `c12_04_final_source_release_build.log` |
| `ctest --test-dir build -C Release -R '^MaterialGraph(ResourceTests\|ResourceGPU_.*\|Tests\|CompilerTests\|RuntimeTests\|RuntimeGPU_.*\|GPU_.*)$\|^MaterialUnitTests$' --output-on-failure` | **11/11 PASS** | `c12_04_final_release_ctest.log` |
| `python scripts/check_architecture.py` | **558 sources / 25 CMake PASS** | Session output and independent review |
| Whitespace, exchange schema/history, changed-document links | PASS, **142 local links** | Session tool output |

Both final builds follow all R1/R2/R3 source/test corrections, including the
nonexistent-relative-root edge. No production/test changes followed these builds.
GPU modes were enabled with `AXLE_ENABLE_MATERIAL_GRAPH_GPU_TESTS=ON` and remain
opt-in by default. All six compiler/runtime/resource native modes execute physical
D3D12/Vulkan in each configuration. This includes final corrected command-only
texture/sampler lifetime probes.

All four captured 64x64 sRGB readbacks have center RGB55/55/55 and identical SHA-256
`3cef4c24b34b852acff4721f3c26dc4a8a23820223b5b1ad371706a256b321d6`.
Paths: `build/Tests/MaterialGraphResources/{Debug,Release}/{dx12,vulkan}/`
`{dx12,vulkan}.rgba`. The parent revalidated byte lengths, hashes and center pixels
at closure. Complete Release CTest matrix and manual product graph UI: **NOT RUN**.

## Independent review and corrections

Separate-context reviewer `c12_04_review` found:

- **C12-04-R1, P2:** the general Package string API normalizes legacy prefixes,
  so a graph's `assets/foo` could incorrectly load package entry `foo`. Graph
  resolution now looks up the exact entries map and reads the matched entry object.
  Regressions cover every stripped prefix, the Shaders case alias, explicit fallback,
  and a real physical file at the actual requested path.
- **C12-04-R2, P3:** the original lifetime probe retained a separate snapshot
  sharing the texture, weakening its lease-only ownership claim. The corrected probe
  uses a fresh isolated path/content and verifies weak generation, texture-wrapper
  and sampler-wrapper expiration inside the pre-submit callback, then checks the
  recorded draw still returns the expected byte 96.
- **C12-04-R3, P3:** relative configured roots could alias resource keys across
  CWD changes with identical bytes, preserving stale source-path metadata. Keys now
  contain canonical rooted authored/source paths. The root is made absolute before
  `weakly_canonical`, including when a package-only/missing root does not exist.
  Regressions cover existing relative roots with identical content/generation and
  missing relative roots with explicit fallback, checking distinct absolute identity.

The reviewer source-rechecked all three final corrections, including the missing
relative-root edge, and found **no remaining actionable findings or P1/P2**. It
independently executed final-source `^MaterialGraphResourceTests$` in Debug and
Release, **1/1 PASS** each, exit 0, plus architecture **558 sources / 25 CMake**,
whitespace and exchange schema checks. Its initial pre-correction CPU runs remain
checkpoint evidence only.

The reviewer did not run native tests concurrently with the parent's matrix.
Parent final Release **11/11 PASS** includes both corrected native lease probes;
final Debug **101/101 PASS** also executes both corrected native probes, as recorded above.
No Gemini implementation or approval is claimed.

## Limits

No performance, FPS, VRAM reduction, GPU compression or manual Editor interaction
is claimed. Native evidence is bounded to the tested machine/bundled toolchain.
Transparent/alpha-test shading, cubemap/prefilter sampling, other GPUs, GL and Slang
remain unverified/separate. Full Release CTest matrix remains a separate gate.
No vendor upgrade or push is included.
