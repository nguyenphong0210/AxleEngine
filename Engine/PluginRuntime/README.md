# Native plugin loader

The loader consumes the canonical artifact identity from Axle::Plugin and the
versioned ABI from Axle::PluginSDK. It does not discover plugins or run CMake.
Shipping Runtime DLL loading is explicitly rejected; selected static Runtime
composition belongs to its separate product package.

Before loading, it validates every receipt field and the actual x64 DLL bytes.
A Windows read handle denies writes/replacement from before hashing through the
last code lease. The descriptor separately validates plugin/module identity,
ABI, SDK, toolchain and actual configuration before `ax_plugin_start`.
Each load receives an owner token. Registrations made during startup are
rolled back when startup fails and are destroyed before the module is unloaded.
Logical Stop revokes services and rolls registrations back first. Components,
callbacks and jobs retain code leases; the durable host table remains valid but
rejects calls after revocation. The final lease invokes `ax_plugin_stop` and
FreeLibrary on the loading thread. Worker Stop and final releases enter an
intrusive retirement queue; sessions must call `CollectRetiredModules` on their
owner thread each frame and before service shutdown. Registry construction and
destruction also collect that thread's retirements. NativePlugin instance calls
are exclusive (no concurrent Load/Stop/access to the same instance).

`Tests/plugin/PluginRuntimeFixture.cpp` builds real good/failing DLLs and
`PluginRuntimeTests` verifies identity rejection, rollback/revocation, shorter
registry lifetime, worker retirement and artifact file pinning. The external
build fixture also loads a real source-built DLL with a nontrivial destructor.
These focused checks do not establish the unfinished scene/UI/product workflow.
