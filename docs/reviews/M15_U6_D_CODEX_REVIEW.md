# M15 U6-D independent Codex review

The user requested a fresh Codex review of U6-D. A separate-context reviewer audited the five released requirements from the accepted U6-C baseline `a56a78fa`. The reviewed implementation is `ad6c1ddf`, and the review started from `ac07755d`. The correction is `1f553588be0b80662cf4a8f6d7ff794fc8697177`. The earlier Codex delivery and Gemini review remain historical records for that source. The separate reviewer inspected the repairs and found no remaining blocking source issues.

## Findings and repairs

| Finding | Severity | Evidence and correction |
| --- | --- | --- |
| U6D-R1 | P1 | Mutable scene extraction advanced the transform publication counter on every frame. `ViewContext` interpreted the counter as a scene edit and discarded fog history even while the scene was stationary. Immutable motion fixtures held the counter constant. Extracted render worlds now carry a structural view revision. Capture and reflection consumers retain the transform publication revision, and explicit render-world callers can use their supplied revision. |
| U6D-R2 | P2 | Main and preview requests shared the active renderer context. Target switches discarded metadata even while the main target's 3D history was still live. Fog history metadata now follows weak target identity and generation, and each entry checks its own view, scene, feature and material identity. The reported view kind reaches the context. |
| U6D-R3 | P2 | A history texture or temporal constant buffer allocation failure could stop rendering before the existing static fog fallback. Product view owners can now admit a valid target with static volumes when the history pair is unavailable. The renderer preflights the optional constants before building the graph. The default low-level allocator retains its strict rollback contract. |
| U6D-R4 | P2 | Selecting the Editor fly camera again could snap its pose while the camera ID stayed the same. Fog settings also omitted local shadow distance and cascade count from the view policy key. The camera snap now latches a cut through successful rendering, and both shadow policy inputs are checked. |

## Requirement disposition

| Requirement | Reviewed source | Corrected implementation and independent evidence | Final |
| --- | --- | --- | --- |
| M15-U6-04.1 Reprojection | FAIL, live scene history was discarded | `SceneExtraction`, `ViewContext`, `FogReproject`, integration and composition. Mutable-scene and immutable-world fixtures both match projected history. Native injected layout, history texture and temporal constant allocation failures exercise static fog. | PASS |
| M15-U6-04.2 Rejection | PASS | `CSReproject` preserves current extinction and rejects frustum, depth, extinction and neighborhood mismatches. The separate reviewer compiled DXIL and Vulkan 1.3 SPIR-V and ran `spirv-val`. Native readbacks verify frustum and near-depth rejection and the six-neighbor bound. | PASS |
| M15-U6-04.3 Invalidation | FAIL, live scene revision and camera cuts were incomplete | `SceneData`, `ViewContext`, `RenderService` and Editor camera selection. A live scene cut and structural edit seed fresh history. GPU and unit cases exercise frame zero, media and feature changes, scene instance, epoch, failure and reset. | PASS |
| M15-U6-07.3 Camera motion | FAIL through the normal scene caller | `AxleEnvironmentRenderingTests` drives a mutable scene through `RenderService::RenderView(Scene&)`, interleaves an asset preview and verifies the opposite volume's extinction and scattering against the predicted world-space history address. | PASS |
| M15-U6-07.4 Resize | PASS | The native same-wrapper resize and quality switch checks verify the new target generation starts with fresh history, followed by ping-pong on the resized grid. | PASS |

## Verification

The separate-context reviewer independently inspected both source versions and ran the shader checks. The coordinating Codex session rebuilt and ran the corrected native and CPU checks. Commands ran from the repository root. The working logs and readbacks are in `build/agent-runs/M15-U6-D/codex-independent`.

```powershell
cmake --build build --config <Config> --target AxleEnvironmentRenderingTests AxleRenderUnitTests AxleRenderServiceTests AxleRHIResourceViewTests AxleRHIStateTests AxleEditor AxleRuntime AxleSceneMutationUnitTests AxleSceneDataUnitTests -- /m:4 /v:minimal
build/Tests/<Config>/AxleEnvironmentRenderingTests.exe <backend> build/agent-runs/M15-U6-D/codex-independent/<Config>/<backend>
build/Tests/<Config>/AxleRenderUnitTests.exe
ctest --test-dir build -C <Config> --output-on-failure -R "^(SceneMutationUnitTests|SceneDataUnitTests|M15FroxelFogContract|M15HiZContract|M15OcclusionContract|M15ReflectionCaptureContract|RenderHarnessFailure)$"
```

The final Debug and Release builds exited 0. The complete environment suite exited 0 with validation enabled on native D3D12 and Vulkan in both configurations. Each run reported 527 matching production reprojected samples, 126 matching samples in the controlled immutable-world test, 18 rejected frustum samples and zero measured trail after the light shut off.

The RenderService, RHI state, resource storage, viewport lifecycle and Editor empty-startup suites passed on both native backends in both configurations. The full renderer unit binary, five focused rendering CTest cases and two scene mutation/data CTest cases passed in each configuration. Architecture, diff and exchange validation passed. The report records automated and source evidence for U6-D.
