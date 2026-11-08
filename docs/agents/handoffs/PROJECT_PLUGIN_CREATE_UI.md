# Create Project Plugin UI

Date: 2026-11-08. User explicitly requested implementation after the earlier
discussion. Completed source: **c3698f5**, based on **9a390e5**.

The saved project's **Edit > Plugins > Create Plugin...** dialog suggests editable
module/ID fields from a display name. It creates the shipped Runtime/Editor template
inside that project's Plugins folder without Python, refreshes the catalog and
shows the new row without changing Draft, Desired or Active selections.
Creation is blocked during simulation or busy work and requires a saved project.
Names, duplicate IDs/modules and existing destinations are validated. Generated
files are staged under Intermediate/Plugins/Creation, checked through production
manifest discovery, then published without replacement. An exclusive project file
lock spans collision discovery through publication and staging cleanup.

## Actual verification

- Debug: built AxleProjectPluginCreatorTests, AxlePluginsPanelWorkflowTests and
  AxleEditor, exit 0. Focused CTest 4/4, exit 0.
- Release: built those targets plus the initially missing AxlePluginSelectionTests,
  exit 0. Final focused CTest 4/4, exit 0. Initial Release Not Run was corrected;
  it is not counted as a pass.
- Focused expression for both configurations:
  `ctest --test-dir build -C <configuration> -R '^(ProjectPluginCreatorTests|PluginsPanelWorkflowTests|PluginSelectionTests|ArchitectureBoundaryChecks)$' --output-on-failure`.
- Creator regression covers saved-project and busy/Play guards, invalid names,
  unavailable/broken templates, existing files/duplicate ID preservation, cleanup,
  controlled concurrent-writer lock rejection, retained pending selections,
  refresh callback failure and unchanged project selection bytes. It compiles
  actual native-created Runtime and dependent Editor DLLs from Unicode paths
  with the matched SDK in both configurations.
- Native Debug computer use: opened Edit > Plugins, checked modal layout and name
  suggestions, Cancel with no files, Create with immediate Project row/status,
  duplicate creation error retaining the dialog and original files, Cancel and
  clean Alt+F4 shutdown. Only a disposable project was edited. Packaged template
  loading worked from the running Editor executable.
- Separate-context source reviewer found a collision race between two Editors
  creating different folders with the same ID. The exclusive project lock and
  controlled-writer regression closed it; reviewer rechecked and reported no
  remaining concrete P1/P2 in this bounded change. Source-only recheck, no
  independent builds/native UI execution claimed.

Logs: `build/plugin-create-ui/build-debug-final.log`, `tests-debug-final.log`,
`build-release.log`, `build-release-selection.log`, `tests-release-final.log`,
`native-ui.log` and disposable `native-fixture.json` (generated/ignored).
No full-suite rerun or native Release UI interaction is claimed.

No Engine source, authored sample, pinned vendor or OPT-07/08 implementation was
changed. Their delivered reports remain pending their own review; this feature
does not accept those phases. Local commits only; no push.
