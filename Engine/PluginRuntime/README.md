# PluginRuntime PLG-06 foundation

This target contains the native loader and transactional registration seam for
OPT-06. It is intentionally independent of the project resolver and product
build planner. PLG-04 must provide the canonical artifact receipt and PLG-05
must provide the shared host/SDK ownership policy before this loader is used by
Editor or Runtime products.

The loader validates the absolute path supplied by that resolver, descriptor
ABI, stable module ID and build configuration before calling `ax_plugin_start`.
Each load receives an owner token. Registrations made during startup are
rolled back when startup fails and are destroyed before the module is unloaded.
`ax_plugin_stop` runs before owner rollback, so a plugin can release its own
state while its module is still resident.

`Tests/plugin/PluginRuntimeFixture.cpp` builds real good/failing DLLs and
`PluginRuntimeTests` verifies descriptor/config rejection, startup rollback and
stop/unload registration cleanup. This does not establish PLG-04/05 build-plan,
SDK export, or Editor/Runtime host acceptance.