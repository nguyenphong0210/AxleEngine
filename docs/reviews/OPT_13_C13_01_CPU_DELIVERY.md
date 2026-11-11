# C13-01.1 CPU cascaded shadow checkpoint

Date: 2026-11-11. Source baseline: `3095780`.
Status: CPU checkpoint implemented, self-verified and separately reviewed.
Full C13-01 remains in progress; native shadows and acceptance are pending.

## Result

Added `Engine/Renderer/include/Axle/CascadedShadowMap.hpp` and its implementation,
with three/four camera-space cascade splits, fixed-world texel stabilization,
right-handed ZO light matrices, bounded caster-depth extension, validated bias
settings and explicit configuration/update failure behavior. No native resource
allocation or Scene/RHI shader integration was introduced.

The focused executable is registered with CTest and both `AxleAllTests` target
branches. Its checks remain active in Release. Coverage includes perspective
and positive-near orthographic cameras, lambda 0/0.85/1, three/four cascades,
32/1024/2048/4096 resolutions, split endpoints, frustum corner containment,
determinism, sub-texel and whole-texel camera motion, vertical lights, malformed
settings/matrices, invalid direction, stale-frame prevention and numeric edges.

## Separate-context review and corrections

Read-only reviewer `c13_contract_review` inspected the CPU implementation and
CMake wiring. Two P2 findings were reproduced and fixed:

- **C13-CPU-R1:** adjacent representable near/max distances could publish
  zero-width float cascades. Updates now reject collapsed output intervals and
  invalid float texel/depth dimensions, clearing the frame. A regression uses
  `nextafter(near, +infinity)`.
- **C13-CPU-R2:** a valid perspective camera with clips `.01..10000` was rejected
  when far-plane unprojection amplified float projection error. Validation now
  uses projected clip endpoints and reconstruction uses near/interior depth.
  A regression independently checks analytic frustum corners over the actual
  shadow range.

Reviewer recheck: PASS on both corrections, no new CPU findings; independently
compiled MSVC probe and focused Debug CTest passed. This is a separate-context
Codex review, not an invented Gemini response or a native graphics review.

## Commands executed against final code

| Command | Result |
| --- | --- |
| `cmake --build build --config Debug --target AxleCascadedShadowMapTests --parallel 6` | Exit 0 |
| `ctest --test-dir build -C Debug -R CascadedShadowMapTests --output-on-failure` | Exit 0, 1/1 passed |
| `cmake --build build --config Release --target AxleCascadedShadowMapTests --parallel 6` | Exit 0 |
| `ctest --test-dir build -C Release -R CascadedShadowMapTests --output-on-failure` | Exit 0, 1/1 passed |
| `python scripts/check_architecture.py` | Exit 0, PASS; 562 source files / 25 CMake files |
| `git diff --check` | Exit 0 |

CTest logs: `build/Testing/Temporary/LastTest.log` (the latest configuration
overwrites this standard CTest log). No full CTest, product build, native GPU
image test or interactive UI verification was run for this isolated CPU addition.
MSVC emits the pre-existing unused Slang delay-load import warning; both builds
complete successfully.

## Remaining work

Follow [the execution checkpoints](../design/OPT_13_C13_01_EXECUTION_PLAN.md).
C13-01.2 must allocate and record actual shadow depth resources, bind receiver
data, implement PCF 5x5 and apply biases. C13-01.3 must prove native backend and
product behavior, failure/resize handling and image quality before changing
capability claims. The existing OpenGL renderer and C12 paths are untouched.
