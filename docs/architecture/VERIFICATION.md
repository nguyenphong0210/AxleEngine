# Verification, Evidence and Limitations

> **Dated source snapshot:** this survey predates the 2026-11-04 Vehicle
> retirement and later sample cleanup. It is retained for architectural context,
> not current API/feature support. Follow the [optimization roadmap](../ROADMAP.md)
> for execution; unavailable source paths below are historical references.

Review date: **2026-11-01**. Source revision inspected: `c58a650` (working tree, not an immutable filesystem snapshot).

## 1. Survey coverage

- 420 first-party source/text-asset/configuration files scanned completely by tooling, totaling 113,222 lines.
- 445 lexical C++ class/struct declaration locations, including nested/local/test/sample types. This is not a count of unique public engine classes.
- 114 direct Axle target link edges and 92 cross-directory include pairs, extracted from first-party configuration/source.
- Manual interpretation focused on public APIs, target boundaries, ownership, entry points and major integration paths. Existing domain guides were cross-checked where they contradicted current code.

ThirdParty implementation internals, media, generated build output and cache/binary payloads are outside the class audit. Textual scanning does not establish that every algorithm or shader line was manually audited. Lexical extraction does not resolve the C++ AST, templates, namespaces, conditional compilation or complete symbol/call graphs.

## 2. Documentation structure and maintenance

SAD/SDD describe implemented behavior. Target Layered Architecture describes unimplemented changes. Class diagrams prioritize architectural relationships; the declaration index supplies broader coverage. Source links are relative to this directory and use line anchors where useful. Line numbers can move after edits.

HLSL Studio is included with the current Editor callback/UI wiring. The older external survey is not required to read or maintain this repository documentation. Inventory/dependency tables are a dated source survey, not a configured CMake file-api export.

When updating the engine: recheck target/include edges, class ownership and the relevant sequences; refresh inventories and source anchors; then update this review date/revision. Do not copy performance/pass-rate claims into the architecture documents without execution evidence.

## 3. Evidence for key findings

| Finding | Source |
|---|---|
| Core depends upward through Application | [Engine/Core/src/Application.cpp:6](../../Engine/Core/src/Application.cpp#L6) |
| Higher-level include directories are exported by Core | [Engine/Core/CMakeLists.txt:17](../../Engine/Core/CMakeLists.txt#L17) |
| Runtime links plugin libraries | [Runtime/CMakeLists.txt:21](../../Runtime/CMakeLists.txt#L21) |
| Spline publicly links editor UI | [Plugins/Spline/CMakeLists.txt:44](../../Plugins/Spline/CMakeLists.txt#L44) |
| Terrain publicly links editor UI | [Plugins/Terrain/CMakeLists.txt:53](../../Plugins/Terrain/CMakeLists.txt#L53) |
| Scene links the separate UI2D target | [Engine/Scene/CMakeLists.txt:33](../../Engine/Scene/CMakeLists.txt#L33) |
| Tests depend on the Editor build | [Editor/CMakeLists.txt:126](../../Editor/CMakeLists.txt#L126) |
| Asset metadata is ignored by Git | [.gitignore:76](../../.gitignore#L76) |
| Missing metadata creates a new UUID | [Engine/Asset/src/AssetManager.cpp:110](../../Engine/Asset/src/AssetManager.cpp#L110) |
| Scene copy preserves entity UUIDs | [Engine/Scene/src/Scene.cpp:1343](../../Engine/Scene/src/Scene.cpp#L1343) |
| Editor runs Scene update before its physics step | [Editor/src/EditorLayer.cpp:1607](../../Editor/src/EditorLayer.cpp#L1607) |
| Runtime steps physics before Scene update | [Runtime/src/RuntimeLayer.cpp:285](../../Runtime/src/RuntimeLayer.cpp#L285) |
| Canvas completion does not submit GPU work | [Engine/UI/src/UI2D/Canvas2D.cpp:39](../../Engine/UI/src/UI2D/Canvas2D.cpp#L39) |
| Package data is written with equal compressed/raw size | [Engine/Core/src/Package.cpp:351](../../Engine/Core/src/Package.cpp#L351) |
| Compiler completion is invoked on a worker thread | [Engine/Scene/src/ScriptCompiler.cpp:141](../../Engine/Scene/src/ScriptCompiler.cpp#L141) |
| Editor callback performs reload directly | [Editor/src/EditorLayer.cpp:832](../../Editor/src/EditorLayer.cpp#L832) |
| HLSL Studio open callbacks are wired | [Editor/src/EditorLayer.cpp:246](../../Editor/src/EditorLayer.cpp#L246) |
| HLSL Studio invokes the active-backend compiler | [Engine/UI/src/HLSLStudioPanel.cpp:106](../../Engine/UI/src/HLSLStudioPanel.cpp#L106) |

## 4. Documentation checks performed

- Relative local links and their target files/directories were checked in the new architecture set and updated architecture/module/developer entry points.
- Markdown fenced blocks were checked for balanced delimiters. Mermaid blocks were checked for recognized diagram types and basic declaration structure.
- New architecture documents were checked for leftover machine-specific filesystem links and untranslated Vietnamese text.
- Source hashes matched the pre-publication survey at verification time. Git changes were checked to remain confined to Markdown documentation.

## 5. Checks not performed

- Engine configure/build/test execution: not run for this documentation-only change.
- FPS, memory, compilation performance and final binary size: not measured.
- Windows/Linux build parity, shader visual parity and asset integrity on a fresh checkout: not executed.
- Hot-reload ABI, failure rollback and concurrency safety: documented from source, not proven by execution.
- Full Mermaid rendering: no Mermaid renderer was available locally; fenced content and structural checks do not replace a renderer/parser. Use a Mermaid-enabled viewer to display the diagrams.

No source, asset, CMake, feature implementation or build behavior is modified by this update. Existing domain guides are not a replacement for checking the current implementation.

## 6. Documentation maintenance review

The documentation is organized through the [main hub](../README.md): architecture/design records in this directory, feature guides under guides, asset syntax under reference, and future editor work under design. The [assessment](ASSESSMENT.md) ranks current limitations and defines improvement exit criteria.

Superseded phase execution plans were removed after their useful UI contracts were incorporated into the current UI guide. Material/UI/asset/visual-editor specifications were consolidated into maintained topic documents. Their proposed capabilities are not treated as completed functionality.

The maintenance review checks repository-relative file links and Markdown anchors, fences and basic Mermaid block types across the documentation set. Native source examples were aligned with current declarations where revised; snippets were not compiled. No engine build/test or Mermaid rendering result is asserted.
