# M15 U4 Batch 2 — Independent Codex Review

> Re-review, 2026-11-17: the historical acceptance below covered CPU contract
> math. Production contact separation and CSM composition are reviewed and
> repaired in [the consolidated U4-A/B re-review](M15_U4_AB_CODEX_REVIEW.md).

## Scope

Reviewed Gemini delivery `ccc4ba61` plus exchange metadata `75661471` from
base `5afdce70` for the released contract checkpoint `M15-U4-04.1` and
`M15-U4-04.2`.

## Finding and correction

### U4B-R1 — MEDIUM: legacy direct terms disappeared when no sun was present

`EvaluateShading` correctly separated the new `Sun*` and `Local*` fields, but
its U4-A backward-compatibility fallback always routed legacy
`DirectDiffuse`/`DirectSpecular` into the sun bucket. A caller using the legacy
fields with `HasSun=false` therefore lost those direct terms entirely instead
of retaining them as local direct lighting.

**FIXED in this review:** legacy direct terms now route to the sun bucket when
`HasSun=true`, and to the local bucket when `HasSun=false`. Added focused tests
for legacy diffuse+specular and specular-only cases.

The helper verification also now computes the effective CSM visibility as
`1.0` when CSM is disabled, covering stale `CSMVisibility=0` inputs.

## Verification

- Debug `AxleRenderUnitTests.exe --m15-occlusion-contract` — PASS, exit 0.
- Release `AxleRenderUnitTests.exe --m15-occlusion-contract` — PASS, exit 0.
- Debug/Release full `AxleRenderUnitTests.exe` — PASS, exit 0.
- Debug native D3D12 and Vulkan environment fixtures — PASS, exit 0.
- Release native D3D12 and Vulkan environment fixtures — PASS, exit 0.
- `python scripts/check_architecture.py` — PASS (`611` files, `25` CMake files).
- `git diff --check` — PASS.

The native fixtures are regression/parity gates; the new U4-B invariants are
validated by the focused CPU contract suite because this checkpoint is the
released RenderContracts contract package.

## Verdict

**ACCEPTED.** U4-04.1 and U4-04.2 satisfy the released contract scope after
the backward-compatibility correction. The correction and review evidence are
delivered together in one Codex commit.
