# OPT-12 C12-01 Material Graph Document and CPU IR

Date: **2026-11-10**. Base: `158629123ad841ce19044922332f76b8cebe5f27`.
Status: **C12-01 CPU implementation complete, verified and independently rechecked**.

## Released boundary

The user explicitly assigned C12-01 to Codex. This overrides the default Gemini
implementation role for this package. The approved
[authoring format](../design/OPT_12_MATERIAL_GRAPH_FORMAT.md) remains the contract.
Source changes implement CPU document parsing, validation, serialization,
identity-safe persistence and a separate validated IR. No shader generation,
GPU material deployment, Slang, MaterialX or graph-editor UI is released.

## Design and APIs

`Engine/Asset/include/Axle/MaterialGraph.hpp` exposes `MaterialGraph::Create`,
`Parse`, `Validate`, `Serialize`, `Load` and `Save`. The Asset module still links
only Core. All fallible output APIs publish a candidate only on success and
return a first-error diagnostic with a stable code, original-source location,
and node/field context where applicable. Text and file parsing require the
existing first-line uint64 identity envelope. New documents allocate identity
through `UUID`; disk persistence uses `FileSystem::WriteIdentifiedTextFile` and
its atomic replacement and identity-conflict protections. VFS package loading
uses the same parser. No sidecars or authored registry are introduced.

### Extensible node definitions

The user rejected the initial node-type conditional chain during implementation.
The final design uses `MaterialNodeRegistry`, an explicitly owned collection of
value-only definitions. Builtins register in `BuiltinMaterialNodes.cpp`; the
parser, serializer and generic validator do not branch on builtin node names.
`MaterialGraph::Parse/Validate/Serialize/Load/Save` accept an explicit registry,
with an immutable builtin registry as the default. Registration validates the
definition before publication and rejects duplicate type keys rather than
overwriting a definition used by another caller.

Each `MaterialNodeDefinition` owns a type key, nonzero revision, role, supported
pin signatures and typed property/attribute schemas. Schemas describe required
fields, optional defaults, string choices, canonical asset paths, Boolean values
and finite scalar/vector values. Parameter-reference behavior is metadata:
`ParameterAttribute` selects a required string attribute and signature pins can
bind to that parameter's declared type. Surface-output selection uses the
registered role. Neither behavior depends on a special node name in graph code.

The document stores generic typed `Attributes` (node-level fields such as
`parameter`) and `Properties` (fields inside `properties`). IR materializes
effective schema defaults without rewriting authored state and includes each
used definition revision in its canonical key. Registry copies own their data;
there are no callbacks or borrowed plugin-library objects. Callers register
definitions before publishing a const registry for use; concurrent mutation is
not supported. Loading a file cannot register new executable semantics.

The custom-node regression registers `test.ColorOffset`, with Float3 pins,
numeric/vector, Boolean, optional string and enum-valued fields, and runs it
through parse, validation, IR, serialization and real-file save/load. A separate
`test.ParameterAlias` proves parameter-type binding is also generic. Neither
extension changes core graph or codec code. Registries remain isolated;
missing definitions fail and a definition revision change affects IR identity.
Backend lowering and plugin SDK registration hooks remain later work.

### Authored state and compiler projection

`MaterialGraphDocument` preserves node IDs, exact case-sensitive parameter keys,
all authored pins/defaults, graph connections, editor positions and unused data.
Serialization uses canonical multiline blocks and round-trip Float precision,
independent of the process numeric locale. Comments, source whitespace and exact
original scalar spellings are not preserved. Unknown fields/schema versions
fail instead of being silently discarded.

`MaterialGraphIR` is separate from the authoring document. Validation checks all
authored nodes, including disconnected ones. It then keeps only nodes reachable
from the unique output, in deterministic topological order; it sorts used
parameters and pin bindings. Connected inputs discard their authored default in
IR while the document retains it. IR carries explicit exact/scalar-splat input
conversions and no editor positions. `SemanticKey` is the full canonical CPU
semantic description, including effective defaults and render state; it excludes
display name, UUID, source locations, layout and unused data. It is not the
C12-03 shader/pipeline cache key or parameter-layout ABI.

### Supported version-1 contract

- `DefaultLit` / `Opaque`, culling `Back`, `Front` or `None`, explicit depthWrite.
- Finite `Float`, `Float2`, `Float3`, `Float4` parameters/defaults. Parameter names
  are the sole stable keys; node IDs are nonzero uint64 values.
- `InputUV`, `ParameterRef`, `Multiply`, `Lerp`, `SampleTexture2D`,
  `PBRMasterOutput`. Declared pins must exactly match the supported definition,
  irrespective of authored ordering. Output pins cannot have defaults.
- Multiply supports matching Float widths or scalar/vector operands; Lerp has
  matching a/b/result types and Float alpha. Connections support exact types or
  Float-to-vector splats; there is no implicit vector truncation/widening.
- Exactly one registered surface output (PBRMasterOutput in the builtin catalog);
  valid defaults or incoming connections for every input;
  unique IDs/parameter names/pins, output-to-input endpoints, at most one incoming
  connection per input, and no cycles, including in unused subgraphs.
- Texture properties require canonical root-relative paths, `sRGB` or `Linear`,
  and `LinearRepeat`, `LinearClamp`, `PointRepeat` or `PointClamp`. These are CPU
  metadata contracts. File existence, decoding, sampler realization and Normal
  space/backend interpretation are C12-04 work.

Unsupported shading models/surface modes receive diagnostics; this package does
not imply rendering support for any graph. Parameter rename transactions,
instance overrides and dependency resolution are not implemented by C12-01.

## Shared parser and existing callers

The strict block reader lives in Core `BlockText`, without asset kinds or graph
semantics. AssetDSL imports its syntax tree and preserves source locations.
The reader supports inline comma-separated properties, nested numeric arrays,
quoted strings/commas/brackets, comments and escaped strings. It rejects missing
closing delimiters/values, duplicate properties, stray tokens and trailing root
text. Nesting is bounded at 128 levels. String arrays decode quoted delimiters
and escapes consistently. Native identity parsing is shared between text, disk
and identified writes; version tokens cannot wrap through signed extraction.

Separate-context review found that existing flat material readers/writers could
otherwise accept a graph and overwrite it with flat fields. `Material` and
`RHIMaterial` now use the same Core syntax reader to reject structured, versioned
or malformed native material documents before live-state mutation or destination
replacement. This keeps module dependencies unchanged. The Inspector respects
failed loads, offers an explicit retry, and does not reach editing/save/apply for
that document. Save-dependent cache invalidation occurs only after a successful
save. A graph is authored through `MaterialGraph`; it is not loaded as a flat
renderable material. The approved fixture is isolated under
`Tests/Fixtures/MaterialGraph`, outside normal renderable fixture discovery.

## Verification

All paths below are under `build/agent-runs/` unless stated otherwise.

| Verification | Actual result | Evidence |
| --- | --- | --- |
| `cmake --build build --config Debug --target AxleAllTests AxleEditor AxleRuntime --parallel 12` | PASS, exit 0 | `c12_01_final_debug_build.log` |
| `cmake --build build --config Release --target AxleMaterialGraphTests AxleAssetFormatUnitTests AxleAssetUnitTests AxleMaterialUnitTests AxleAnimationUnitTests AxleVFXUnitTests AxleProjectUnitTests --parallel 8` | PASS, exit 0 | `c12_01_final_release_build.log` |
| Rebuild `AxleMaterialUnitTests` after current-format fixture correction, Debug/Release | PASS, exit 0 each | `c12_01_final_debug_material_build.log`, `c12_01_final_release_material_build.log` |
| `ctest --test-dir build -C Debug --output-on-failure` | First run 91/92, exit 8; sole failure was the obsolete YAML fixture described below | `c12_01_final_debug_ctest.log` |
| `ctest --test-dir build -C Debug --rerun-failed --output-on-failure` after the fixture correction/rebuild | PASS, 1/1, exit 0; all 92 distinct checks now passed across the full run and corrected rerun | `c12_01_verified_debug_rerun.log` |
| Release focused CTest command below | PASS, 9/9, exit 0 | `c12_01_verified_release_ctest.log` |
| Reviewer direct graph executable runs, final Debug/Release | PASS, 15/15 groups, exit 0 each | `c12-final-review/Debug`, `c12-final-review/Release` |
| Reviewer final MaterialUnit CTest, Debug/Release | PASS, 1/1, exit 0 each | Separate-context `c12_01_review` report |
| `python scripts/check_architecture.py` | PASS, exit 0; 545 sources / 25 CMake files | Current-source run and independent reviewer run |
| `git diff --check`, exchange schema validation, approved-example/fixture equality | PASS, exit 0 | Current-source checks |

The focused Release command was:

```powershell
ctest --test-dir build -C Release -R '^(MaterialGraphTests|AssetFormatUnitTests|AssetIdentityAndPersistenceTests|AssetHarnessFailure|MaterialUnitTests|AnimationUnitTests|AnimationHarnessFailure|VFXUnitTests|ProjectStartupTests)$' --output-on-failure
```

The first material-suite failure came from `TestOldDefaultsOnReload` writing old
YAML into `.axleasset`, which the new native-format guard deliberately rejects.
Under the user's clean-current-format contract, the fixture now uses current
Material blocks and is named `TestOmittedFieldsResetOnReload`. Every original
default assertion remains; the test now seeds non-default clearcoat, anisotropy
and refraction values before reload, so it actually verifies their reset.
No production failure was suppressed and no assertions were weakened. The
independent reviewer checked this migration and executed the rebuilt suite.
An interrupted earlier Release build overlapped the registry header transition;
its failure is superseded by the final successful builds above. Existing
LNK4098 runtime-library link warnings remain; no warning-free build is claimed.

The new always-active headless harness contains 15 groups, including the complete
approved example, three declaration line styles, syntax/schema rejection,
pin/reference/cycle validation, defaults/unused preservation, scalar conversions,
canonical IR, numeric precision/locale, Unicode-path atomic persistence and
package-only loading, custom-node registration and invalid-definition rejection.
It links only `Axle::Asset` (and transitively Core).
The existing material harness additionally verifies that both flat APIs preserve
live values and graph bytes on rejection, and uncached graph lookups do not
publish flat materials.

## Review and limits

Separate-context reviewer `c12_01_review` reported:

- C12-R1 (P1): flat material/UI data-loss exposure. Corrected in both material
  types, Inspector and regression coverage; source recheck confirms the repair.
- C12-R2 (P3): malformed separator placement around declaration punctuation.
  Corrected with whitespace/comment-only skipping; negative tests added and
  source rechecked.
- C12-R3 (P3, registry redesign): duplicate signatures depended on declaration
  order while validation matched by name. Registration now canonicalizes pins
  and checks every possible parameter binding, rejecting reordered duplicates
  and overlapping parameter-bound/concrete signatures. Regressions cover both.

The reviewer rechecked all three corrections and found no remaining P1/P2.
Final-source independent execution passed 15/15 graph groups and 1/1 material
CTest in both Debug and Release, all exit 0. The reviewer also ran architecture
and whitespace checks. The parent executed the full Debug/native regression
suite and Release focused suite; the reviewer did not independently run the
full native matrix. No Gemini implementation, review or approval is claimed.

The entire Release CTest matrix was not rerun. Native graph rendering, manual
Inspector UI interaction and later OPT-12 package
acceptance remain outside this CPU foundation. C11-07 deferred product paths and
vendor versions are unchanged. No push is authorized.
