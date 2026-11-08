# Terrain plugin foundation

`axle.terrain` resolves its declared `axle.spline` dependency. Apply, Build and
Restart activates Runtime and Editor DLLs in dependency order through the SDK.
Add `axle.terrain/Terrain` through the native Inspector.

The native payload owns bounded height samples and settings in NativeTerrain.
Its versioned codec saves the actual height values, deep-clones authored state
and keeps runtime activity separate. Play contributes transformed grid lines
through the host renderer; Inspector fields use narrow Runtime exports. Missing
required data prevents simulation and cooking rather than silently discarding it.

Advanced sculpting, chunked mesh/LOD, spline carving and Jolt collider routines
remain in excluded development prototype libraries with their existing tests.
These features are not connected to the native SDK DLL yet. Foundation acceptance
does not establish their completeness. Integrate future features through supported
generic host operations; never reinterpret diagnostic addresses as Engine objects.
