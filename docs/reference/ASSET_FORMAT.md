# Asset Format and Identity Reference

Reviewed: **2026-11-01**. This document describes current syntax and integration boundaries. It does not claim that all legacy assets have been migrated or that every recognized asset kind has a complete renderer/import pipeline.

## 1. Format roles

| Format | Role |
|---|---|
| `.axleasset` | Typed, human-readable descriptor interpreted by a domain loader |
| Raw media/model file | Source data; some consumers load directly, while others use optional descriptors |
| `.meta` | Companion metadata storing persistent asset UUID/type/path |
| `.axlescene` | Scene/entity/component persistence |
| `.axle` / `.axlemesh` | Assembly/mesh text forms; interpret through the corresponding loader |
| `.axlebin` | Binary mesh cache |
| `.axlepak` | Distribution container with header, chunks and table of contents |
| `.axleui` | Legacy UI format/path still present in code |
| `.axleterrain` | Terrain payload; surveyed examples are binary |

Descriptors, raw resources, caches and shipping packages have different responsibilities. Keep stable identity with source data; caches should be reproducible.

## 2. DSL model

[AssetDSL](../../Engine/Asset/include/Axle/AssetDSL.hpp) parses a document into AssetDocument, a shared root AssetBlock, value AssetProperty records and child blocks. AssetProperty retains a raw value and exposes typed conversion helpers. The document's kind is inferred from the root type.

Current AssetKind values include Material, Canvas, ParticleEmitter, Mesh, Texture, Terrain and PhysicsMaterial. Recognizing a root kind does not prove all fields or all operations for that kind are implemented. Refer to the consuming loader for schema, defaults and validation.

Typical syntax:

```text
Material {
    name: "ExamplePaint"
    albedoColor: "#EBEFF5"
    metallic: 0.92
    roughness: 0.14
    uvScale: [1.0, 1.0]
}
```

Blocks use braces and `property: value` entries. Common values include quoted strings, numeric/boolean values, arrays and hex colors. Comment stripping supports line/block comments; quoted strings and raw-value parsing follow the actual parser implementation. This is not a general JavaScript/QML execution environment.

Sources: [parser/serializer](../../Engine/Asset/src/AssetDSL.cpp), [material example](../../Editor/assets/materials/SilverCarPaint.axleasset), [canvas example](../../assets/ui/RacingHUD.axleasset).

## 3. Domain consumers

| Consumer | Responsibility |
|---|---|
| Material / RHIMaterial | Interpret material parameters, textures and graphics-facing state |
| UIParser / UIDocument | Build and persist canvas nodes and bindings |
| ParticleEmitterSerializer | Interpret particle emitter configuration |
| TextureDescriptor | Optional texture-loading metadata |
| TerrainAsset | Terrain descriptor interpretation |
| AssetSerializer | Model conversion, mesh text/binary and manifest operations |

The common syntax is shared; property meaning and resource creation are domain-specific. Use existing assets as examples rather than treating historical phase specifications as implemented schemas.

## 4. Identity and lookup

AssetMetadataSerializer stores UUID, type and path in a companion `.meta`. AssetManager maps path/UUID and caches shared resources. GetOrCreateMetadata reads metadata or creates a new random UUID when it is absent/invalid.

Scene components can store UUID references as well as paths and resource references. Preserve stable UUIDs when renaming or saving source assets; update references deliberately when duplicating assets. The current `.gitignore` excludes `.meta` files, which creates clean-checkout identity risk. See [assessment A3](../architecture/ASSESSMENT.md#3-limitations-impact-and-priority).

Source APIs: [metadata serializer](../../Engine/Asset/src/AssetMetadataSerializer.cpp), [manager](../../Engine/Asset/src/AssetManager.cpp), [scene serialization](../../Engine/Scene/src/SceneSerializer.cpp).

## 5. Loading, caching and packaging

FileSystem checks mounted package entries and then disk/resource paths. AssetManager combines registry lookup with Texture2D/Material/RHIMaterial caches. Mesh import/cache operations are handled through AssetSerializer; descriptor changes do not imply every resource cache and GPU pipeline has automatically refreshed.

AssetCooker traverses the configured asset directory and optionally bakes/copies resources. PackageWriter writes chunks and a table of contents indexed by path/UUID. Current chunk writing is uncompressed; compression fields alone do not establish compression support in this path.

There is no basis here for promising a universal texture compression pipeline, automatic migration of every extension or a package containing only transitively referenced assets. Those require explicit implementation and validation.

## 6. Changes to a schema

When adding a property or asset type, update parsing/conversion, domain validation, serialization, loading/cache invalidation, component references and authoring UI as applicable. Consider compatibility with existing files and binary caches. Add behavioral round-trip/loading coverage rather than tests that only repeat setter/getter behavior.

See [materials](../guides/MATERIALS_AND_SHADERS.md), [UI](../guides/UI.md), [SDD asset design](../architecture/SDD.md#9-assets-dsl-and-package-storage) and the [target architecture](../architecture/TARGET_LAYERED_ARCHITECTURE.md).
