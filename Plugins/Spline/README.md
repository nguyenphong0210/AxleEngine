# Spline plugin foundation

Select `axle.spline` in Plugins, Apply, Build and Restart. The standalone SDK
builds SplineRuntime and SplineEditor DLLs with no Engine/vendor implementation
linkage. Add `axle.spline/Spline` to an entity through the native Inspector.

The native payload owns points and settings in NativeSpline, with a bounded
versioned codec, deep clone, Start/Stop and transformed line contributions during
Play. Inspector access uses narrow Runtime exports and the host ImGui context.
Missing required data blocks simulation. Save/reopen retains the authored payload.

Advanced curve/track mesh, gizmo and collider algorithms are retained in the
excluded development prototype libraries and their tests. They are not wired
into these SDK DLLs, and foundation acceptance does not establish feature parity.
Future integration must use supported host operations, without casting diagnostic
service addresses into Engine objects or linking private Engine/Jolt/render state.
