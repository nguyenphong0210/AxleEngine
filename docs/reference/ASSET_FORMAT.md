# Asset Format and Identity Reference

Reviewed: **2026-11-06** (post OPT-03). This document describes current asset syntax, identity standard, and subsystem boundaries.

## 1. Format roles

| Format | Role |
|---|---|
| `.axleasset` | Typed, human-readable DSL descriptor with embedded native UUID |
| Raw media/model file | Source data (textures, audio, meshes); identified by canonical root-relative paths (`assets/...`) |
| `.axlescene` | Scene, entity, and component persistence with embedded native UUID |
| `.axle` / `.axlemesh` | Assembly and mesh text representations |
| `.axlebin` | Binary mesh cache |
| `.axlepak` | Shipping distribution container with header, chunks, and table of contents |
| `.axleui` | UI layout document |
| `.axleterrain` | Binary terrain height/surface payload |

Companion `.meta` sidecars are completely retired. Native descriptors embed their persistent 64-bit UUID directly, while raw assets are addressed by canonical root-relative paths.

## 2. DSL model

[AssetDSL](../../Engine/Asset/include/Axle/AssetDSL.hpp) parses human-readable documents into `AssetDocument`, containing a shared root `AssetBlock`, typed `AssetProperty` entries, and nested blocks.

Current recognized `AssetKind` values: `Material`, `Canvas`, `ParticleEmitter`, `Mesh`, `Texture`, `Terrain`.

Typical syntax:

```text
Material {
    uuid: 1234567890123456789
    name: "ExamplePaint"
    albedoColor: "#EBEFF5"
    metallic: 0.92
    roughness: 0.14
    uvScale: [1.0, 1.0]
}
```

Blocks use braces and `property: value` entries. Supported types include quoted strings, numeric and boolean values, arrays, and hex color strings.

Sources: [parser/serializer](../../Engine/Asset/src/AssetDSL.cpp), [material example](../../Tests/Fixtures/Assets/materials/SilverCarPaint.axleasset), [canvas example](../../Tests/Fixtures/Assets/ui/RacingHUD.axleasset).

## 3. Domain consumers

| Consumer | Responsibility |
|---|---|
| Material / RHIMaterial | Interpret material descriptors, texture maps, and shader parameters |
| UIParser / UIDocument | Build and persist canvas hierarchy, layout, and UI bindings |
| ParticleEmitterSerializer | Interpret particle emitter configuration |
| TextureDescriptor | Optional texture loading metadata and sampler hints |
| TerrainAsset | Terrain heightmap descriptor interpretation |
| AssetSerializer | Model conversion, mesh text/binary caching, and manifest operations |

## 4. Identity standard and pure CPU AssetManager

- **Native Embedded UUIDs**: Documents (`.axleasset`, `.axlescene`) embed their persistent 64-bit UUID directly within the serialized payload.
- **Root-Relative Raw Paths**: Raw assets (images, audio clips, external meshes) are referenced via canonical root-relative paths (`assets/textures/...`).
- **Pure CPU AssetManager**: `AssetManager` is a pure CPU registry that tracks registered assets by UUID and path. It has no upward dependency on GPU devices or graphics backends.
- **GPU Resource Separation**: Rendering caches (such as `MaterialCache` or `RHIMaterial`) reside in renderer bridges and consume CPU asset descriptors without coupling the core asset library to graphics hardware.

## 5. Loading, caching and packaging

- **FileSystem**: Checks mounted `.axlepak` packages first, falling back to disk asset directories.
- **AssetManager**: Maps UUID $\leftrightarrow$ path and maintains pure CPU asset descriptors.
- **AssetCooker**: Traverses configured asset directories, compiles descriptors and meshes into binary form, and generates package tables of contents.
- **PackageWriter**: Writes contiguous data chunks and a table of contents indexed by UUID and path for distribution.

## 6. Schema extension checklist

When adding an asset property or type:
1. Update `AssetDSL` parsing and serialization routines.
2. Update the domain consumer/loader with schema validation and sensible defaults.
3. Keep CPU asset structs free of GPU device handles or rendering contexts.
4. Add behavioral round-trip tests in `AxleTests.exe` validating serialization fidelity.
