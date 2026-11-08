# OPT-06 PLG-03 schema gate

The current project selection schema is version `1` and is emitted as
`SchemaVersion: 1` by `ProjectSerializer`. A project that contains no schema
version, or an unsupported version, is rejected before its parsed values are
applied. Failed loads leave the existing `ProjectConfig` unchanged.

The user renamed the development sample to `Samples/Sandbox/Sandbox.axleproj`
on 2026-11-08. It uses schema 1 and an empty optional plugin selection, preserving
the authored scene and asset paths. The Editor can open that current-format
project without selecting optional plugins. Historical fixtures such as
`Tests/Fixtures/LegacyProject.axleproj` remain unchanged; missing/unsupported
schemas are still rejected, with no implicit legacy fallback.

The CPU fixture `AxlePluginFoundationTests` covers current-format round-trip,
unsupported-schema rejection, missing-schema rejection and state preservation
after a failed load.
