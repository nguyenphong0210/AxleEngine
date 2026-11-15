# M15 U2 S2.3 - Environment consumers and failure behavior

## Scope

This review covers U2-03.1 through U2-03.4 only:

- verify procedural sky and direct-sun coupling;
- add and verify authored HDR environment rotation as an independent control;
- verify environment intensity and exposure without rebaking the published generation;
- verify missing, corrupt and failed source behavior with explicit last-good or neutral fallback.

U2-04 and U2-05 remain open and are not included in this acceptance.

## Implementation

The authored environment contract now has EnvironmentRotation in SkyboxComponent,
RenderSkyboxData, serialization, the Inspector, the native skybox constants and
the OpenGL skybox/IBL path. CustomAzimuth and CustomElevation remain procedural
sun controls. The native and OpenGL PBR consumers rotate authored HDR sampling
directions while leaving procedural sky sampling untouched.

The rotation is a sampling control, so it does not invalidate or rebake the
published environment generation. Intensity and source exposure continue to be
applied at consumption time. Missing, corrupt and upload-failed sources retain
the last-good same-source generation or select the named neutral fallback with a
diagnostic.

## Verification

Builds completed with exit code 0:

    cmake --build build --config Debug --target AxleEnvironmentRenderingTests AxleEditor --parallel 4
    cmake --build build --config Release --target AxleEnvironmentRenderingTests AxleEditor --parallel 4

The native fixture was run in both configurations on all available backends:

| Configuration | Backend | Result | Log |
|---|---|---|---|
| Debug | D3D12 | PASS, exit 0 | build/agent-runs/M15-U2-S2.3/debug-dx12.log |
| Debug | Vulkan 1.3 | PASS, exit 0 | build/agent-runs/M15-U2-S2.3/debug-vulkan.log |
| Debug | OpenGL 4.5 fallback | PASS, exit 0 | build/agent-runs/M15-U2-S2.3/debug-gl.log |
| Release | D3D12 | PASS, exit 0 | build/agent-runs/M15-U2-S2.3/release-dx12.log |
| Release | Vulkan 1.3 | PASS, exit 0 | build/agent-runs/M15-U2-S2.3/release-vulkan.log |
| Release | OpenGL 4.5 fallback | PASS, exit 0 | build/agent-runs/M15-U2-S2.3/release-gl.log |

The D3D12 and Vulkan logs end with:

    PASS HDR native subresources, sky/specular/diffuse, cache, exposure, reload, fallback and upload retry

The OpenGL logs end with:

    PASS OpenGL background-only and explicit IBL pixels, snapshot and ECS bridge gates

The native fixture includes a dedicated colored HDR lobe assertion: after a
positive 90 degree authored rotation, the lobe moves into the positive-X view.
The same fixture verifies direct-sun coupling, intensity/exposure readback,
source reload, missing/corrupt input diagnostics, upload retry and last-good or
neutral fallback. Generated logs and readback assets remain under build and are
not committed.

## Acceptance

ACCEPTED. U2-03.1, U2-03.2, U2-03.3 and U2-03.4 pass in Debug and Release on
D3D12, Vulkan and the OpenGL fallback path. The implementation stops before
U2-04 and U2-05 as released by the task scope.
