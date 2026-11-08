# OPT-06 PLG-03 schema gate

The current project selection schema is version `1` and is emitted as
`SchemaVersion: 1` by `ProjectSerializer`. A project that contains no schema
version, or an unsupported version, is rejected before its parsed values are
applied. Failed loads leave the existing `ProjectConfig` unchanged.

Existing files such as `Samples/TestProject/TestProject.axleproj` and
`Tests/Fixtures/LegacyProject.axleproj` intentionally remain untouched. They
are historical fixtures until a separate migration/template decision is made;
they must not be silently treated as current-format plugin projects. The
integration gate is therefore to update only current-format templates or to
provide an explicit legacy inspection path before enabling plugin selection in
those products.

The CPU fixture `AxlePluginFoundationTests` covers current-format round-trip,
unsupported-schema rejection, missing-schema rejection and state preservation
after a failed load.
