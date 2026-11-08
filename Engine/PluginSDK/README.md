# Matched native plugin SDK

`AxlePluginSDKPackage` produces a configuration-specific relocatable directory
under `build/PluginSDK/<configuration>`. Copy the directory as one unit. It
contains supported headers, shared-host import libraries/DLLs and
`AxlePluginSDK.cmake`; plugins import those hosts without private Engine, EnTT,
Jolt, enkiTS or ImGui implementations. No blanket symbol exports are used.

The reviewed implementation direction replaces the proposed direct vendor/C++
component SDK with an opaque C/POD service facade. Component memory is allocated
and destroyed by its plugin callbacks; its EnTT wrapper pool is compiled by the
host. Physics and GUI operations use the Engine's existing implementations via
host functions. This keeps one implementation of each vendor/service and avoids
cross-DLL EnTT template/vtable identities. It remains a matched Windows x64 MSVC
SDK, with exact compiler, configuration, CRT, ABI and header/recipe fingerprints.
It is not a general compiler-independent plugin ABI.

The standalone builder configures only selected plugin roots. It freezes all
authored plugin inputs (including common include/cmake files), excludes generated
caches and product-ineligible module roots, and compiles a private source snapshot.
Per-module content identities enter the compiler command line, so preserved
source/header timestamps cannot bypass recompilation. A changed/deleted manifest
requires discovery/resolution again. Native targets must build shared libraries;
old mutable outputs cannot be stamped as current. Published generations are
immutable and become discoverable only after complete receipt/PE/hash validation.

The shared table gateways, builder and loader are verified independently. Actual
scene/backend/Editor service binding and the full authoring/restart workflow are
still being completed; the mere existence of these DLLs does not close OPT06-R1.
