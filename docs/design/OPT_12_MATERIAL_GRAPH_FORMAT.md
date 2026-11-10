# OPT-12 Material Graph Format Decision

Recorded: **2026-11-10**. Status: **user-approved authoring format; C12-01 CPU
implementation verified and independently reviewed**.

The user approved the complete example below after reviewing the Gemini-style
block format. The original documentation decision did not release implementation.
The user subsequently assigned C12-01 directly to Codex: CPU document parsing,
validation, IR and persistence. Shader generation, a node-editor package,
MaterialX integration and the remainder of OPT-12 remain separate packages.
See the [C12-01 delivery](../reviews/OPT_12_C12_01_DELIVERY.md),
[program plan](POST_OPT10_PROGRAM_PLAN.md#opt-12--shader-and-material-pipeline)
for package boundaries and the [exchange](../agents/CODEX_GEMINI_EXCHANGE.json)
for the current assignment.

## Approved format choices

- Keep native material authoring in `.axleasset`, using AssetDSL-style blocks.
- Keep explicit `inputs` / `outputs` / `Pin` declarations for readability. The
  engine must validate these against supported node definitions; a file cannot
  invent executable pin semantics merely by declaring them.
- Put `Connections` **inside the same `Graph`** as its nodes. Use the compact
  `from: "nodeId.pin"` / `to: "nodeId.pin"` endpoint notation.
- Merge parameter ID and name into **`name` only**. Parameter names are unique,
  stable keys within the material. `ParameterRef.parameter` references that key;
  there is no numeric parameter `id` / `paramId`.
- Support compact **one-line, two-line and multiline** parameter declarations.
  Newlines and formatting do not change their meaning. The serializer's default
  formatting may be chosen later; byte-for-byte whitespace preservation is not
  part of this decision.
- Keep numeric node IDs, separate from parameter names, and keep `editorPos` on
  each node. Moving nodes must not invalidate compiled shader cache keys.
- Use the existing first-line native file identity envelope, not a second UUID
  property inside `Material`. The number in the example is illustrative; real
  creation uses the native identity/persistence service.
- Input defaults are valid without wires. Parameters and nodes may be unused;
  connecting every pin or parameter is **not** a compilation requirement.

Earlier alternatives using a separate parameter ID, array-of-object graph syntax,
omitted pin declarations, a separate Editor layout block or mandatory MaterialX
storage are not the approved example.

## Complete approved example

This is the approved authoring example and C12-01 CPU fixture, not an executed
rendering fixture. Comments are in English for repository use. The
five parameters are all wired here to demonstrate their effects, not to impose
a requirement that every parameter be consumed.

```text
# AxleFileIdentity: 1 1234567890123456789

Material {
    schemaVersion: 1
    name: "M_RustMetal"
    type: "Graph"

    shadingModel: "DefaultLit"
    surfaceType: "Opaque"
    cullMode: "Back"
    depthWrite: true

    Parameters {
        Parameter { name: "BaseColorTint", type: "Float3", default: [1.0, 1.0, 1.0] }
        Parameter { name: "UV_Tiling",     type: "Float2", default: [2.0, 2.0] }
        Parameter { name: "RustAmount",    type: "Float",  default: 0.65 }

        Parameter { name: "BaseMetallic", type: "Float",
                    default: 0.90 }

        Parameter {
            name: "BaseRoughness"
            type: "Float"
            default: 0.25
        }
    }

    Graph {
        // UV coordinates.
        Node {
            id: 1
            type: "InputUV"
            editorPos: [-800, -200]

            outputs {
                Pin { name: "uv", type: "Float2" }
            }
        }

        Node {
            id: 2
            type: "ParameterRef"
            parameter: "UV_Tiling"
            editorPos: [-800, 0]

            outputs {
                Pin { name: "value", type: "Float2" }
            }
        }

        Node {
            id: 3
            type: "Multiply"
            editorPos: [-550, -150]

            inputs {
                Pin { name: "a", type: "Float2", default: [0.0, 0.0] }
                Pin { name: "b", type: "Float2", default: [1.0, 1.0] }
            }
            outputs {
                Pin { name: "result", type: "Float2" }
            }
        }

        // Texture sampling.
        Node {
            id: 4
            type: "SampleTexture2D"
            editorPos: [-300, -250]

            properties {
                texturePath: "textures/metal_base_d.png"
                colorSpace: "sRGB"
                sampler: "LinearRepeat"
            }

            inputs {
                Pin { name: "uv", type: "Float2", default: [0.0, 0.0] }
            }
            outputs {
                Pin { name: "rgba", type: "Float4" }
                Pin { name: "rgb",  type: "Float3" }
                Pin { name: "a",    type: "Float" }
            }
        }

        Node {
            id: 5
            type: "SampleTexture2D"
            editorPos: [-300, 0]

            properties {
                texturePath: "textures/rust_d.png"
                colorSpace: "sRGB"
                sampler: "LinearRepeat"
            }

            inputs {
                Pin { name: "uv", type: "Float2", default: [0.0, 0.0] }
            }
            outputs {
                Pin { name: "rgba", type: "Float4" }
                Pin { name: "rgb",  type: "Float3" }
                Pin { name: "a",    type: "Float" }
            }
        }

        // Rust blending.
        Node {
            id: 6
            type: "ParameterRef"
            parameter: "RustAmount"
            editorPos: [-300, 250]

            outputs {
                Pin { name: "value", type: "Float" }
            }
        }

        Node {
            id: 7
            type: "Lerp"
            editorPos: [0, -150]

            inputs {
                Pin { name: "a",     type: "Float3", default: [0.0, 0.0, 0.0] }
                Pin { name: "b",     type: "Float3", default: [1.0, 1.0, 1.0] }
                Pin { name: "alpha", type: "Float",  default: 0.0 }
            }
            outputs {
                Pin { name: "result", type: "Float3" }
            }
        }

        // Color tint.
        Node {
            id: 8
            type: "ParameterRef"
            parameter: "BaseColorTint"
            editorPos: [0, 150]

            outputs {
                Pin { name: "value", type: "Float3" }
            }
        }

        Node {
            id: 9
            type: "Multiply"
            editorPos: [250, -150]

            inputs {
                Pin { name: "a", type: "Float3", default: [1.0, 1.0, 1.0] }
                Pin { name: "b", type: "Float3", default: [1.0, 1.0, 1.0] }
            }
            outputs {
                Pin { name: "result", type: "Float3" }
            }
        }

        // Surface parameters.
        Node {
            id: 10
            type: "ParameterRef"
            parameter: "BaseMetallic"
            editorPos: [250, 150]

            outputs {
                Pin { name: "value", type: "Float" }
            }
        }

        Node {
            id: 11
            type: "ParameterRef"
            parameter: "BaseRoughness"
            editorPos: [250, 350]

            outputs {
                Pin { name: "value", type: "Float" }
            }
        }

        // Surface output.
        Node {
            id: 999
            type: "PBRMasterOutput"
            editorPos: [550, -100]

            inputs {
                Pin { name: "BaseColor", type: "Float3", default: [1.0, 1.0, 1.0] }
                Pin { name: "Metallic",  type: "Float",  default: 0.0 }
                Pin { name: "Roughness", type: "Float",  default: 0.5 }
                Pin { name: "Normal",    type: "Float3", default: [0.0, 0.0, 1.0] }
                Pin { name: "Emission",  type: "Float3", default: [0.0, 0.0, 0.0] }
                Pin { name: "AO",        type: "Float",  default: 1.0 }
            }
        }

        Connections {
            // UV scaling.
            Connection { from: "1.uv",     to: "3.a" }
            Connection { from: "2.value",  to: "3.b" }

            // Texture coordinates.
            Connection { from: "3.result", to: "4.uv" }
            Connection { from: "3.result", to: "5.uv" }

            // Rust blending.
            Connection { from: "4.rgb",    to: "7.a" }
            Connection { from: "5.rgb",    to: "7.b" }
            Connection { from: "6.value",  to: "7.alpha" }

            // Color tint.
            Connection { from: "7.result", to: "9.a" }
            Connection { from: "8.value",  to: "9.b" }

            // Surface outputs.
            Connection { from: "9.result", to: "999.BaseColor" }
            Connection { from: "10.value", to: "999.Metallic" }
            Connection { from: "11.value", to: "999.Roughness" }
        }
    }
}
```

## Implementation requirements and limits

These requirements define the approved format. The delivery record separates
implemented CPU behavior from future compiler and Editor behavior.

1. **Parsing:** support the block syntax above and comma-separated inline
   properties, including numeric scalars. Distinguish property delimiters from
   commas inside strings/vectors. Accept equivalent line wrapping, comments and
   escaped strings. Diagnose malformed syntax with source locations; do not
   silently consume following properties or publish partial material state.
   C12-01 replaces the permissive scalar reader in
   [AssetDSL.cpp](../../Engine/Asset/src/AssetDSL.cpp) with the shared, domain-neutral
   [BlockText parser](../../Engine/Core/src/BlockText.cpp), including numeric-inline
   handling and original-source locations. Existing asset-parser coverage remains
   required alongside the new cases.
2. **References:** validate unique node IDs and unique case-sensitive parameter
   names. `ParameterRef` must resolve the exact name and match its declared output
   type. An Editor rename must update supported references and instance overrides;
   a missing key after a manual edit is a diagnostic, not an implicit new identity.
   Detailed rename transactions and instance storage remain implementation design.
3. **Node/pin contract:** validate declared pins, direction and types against
   supported definitions, including permitted polymorphic Multiply/Lerp variants.
   Validate connection endpoints, supported conversions, duplicate incoming wires,
   cycles and the supported surface-output contract. Unconnected required inputs
   without a default are errors; valid defaults and unused parameters/nodes are
   permitted. A valid incoming connection takes precedence over an input default.
4. **Authoring versus compiler state:** preserve the authored graph, defaults and
   layout on save/load, even if compilation eliminates unused operations. Build
   a separate validated compiler IR. Layout changes alone must not change semantic
   shader cache keys. Exact hashing/canonicalization belongs to implementation.
5. **Identity/resources:** use the existing native uint64 identity envelope and
   persistence service. Raw texture references use canonical root-relative paths.
   See the [AF-01 contract](ARCHITECTURE_FOUNDATION_MASTER_PLAN.md#af-01---native-asset-identity-raw-resource-paths-and-safe-persistence).
   Texture decoding, linear color operations, sampler policy, Normal-space
   semantics and missing-resource diagnostics require explicit backend contracts.
   `BaseColorTint` is Float3 here because it affects RGB, not opacity.
6. **Scope:** the example blends two textures using a uniform RustAmount. It
   does not claim a spatial rust mask, procedural noise, physically varying rust
   roughness/metalness or support for every shading model. MaterialX interoperability,
   Slang and a complete drag-and-drop Editor remain separate decisions/packages.

C12-01 verification should cover syntax equivalence for all three parameter line
styles, identity-preserving round trips, exact names/pin references, defaults,
unused data preservation, invalid graphs and failure rollback. Compiler/native
image evidence is separate from this documentation decision. No source tests or
native rendering runs were claimed by the original format decision. Current CPU
verification and implementation limits are recorded in the C12-01 delivery.
