# M14 P0 — Baseline normalization

**Status:** Recorded; implementation acceptance remains **HOLD**.
**Source baseline:** `769a9f2`.
**Scope:** P0 only. This checkpoint establishes reproducible fixtures, diagnostic channels and an honest baseline report. It does not repair renderer algorithms or claim a smooth sphere/reflection pass.

## What P0 establishes

- `Tests/Fixtures/M14/P0_REFERENCE_FIXTURES.json` defines deterministic reference cases for a smooth sphere, metal reflection, roughness/metallic sweep, exposure/bloom, temporal motion/reset, and resource-edge dimensions.
- The capture contract names the backend matrix (D3D12, Vulkan, OpenGL), resolution defaults, warmup/sample counts, linear-HDR comparison space and diagnostic channels.
- `scripts/m14_p0_baseline.py` validates the fixture schema, records the Git baseline/dirty paths, checks the known source signatures and can emit a JSON report without editing project files.
- `scripts/test_m14_p0_baseline.py` protects manifest uniqueness, required diagnostics and the distinction between recorded open findings and PASS.
- The CTest entries `M14P0BaselineManifest` and `M14P0BaselineRegressions` validate this P0 contract.

## Current baseline findings

The tool deliberately records these as open at the baseline. Their presence in the report is not a feature PASS:

| ID | Baseline finding | Later package that closes it |
|---|---|---|
| F01 | ScreenNormal is allocated/bound but the current forward color pass has no normal producer. | P2 |
| F02 | The eight Hi-Z levels share a descriptor update path whose failures are ignored. | P1/P4 |
| F03 | SSR probe fallback is a constant gray value rather than a captured cubemap sample. | P5/P6 |
| F04 | Auto-exposure output is not the value consumed by tonemapping. | P8 |
| F05 | The timestamp probe uses the invalid 4096-byte structured stride and does not prove a valid renderer readback path. | P1 |

These are source-level baseline signatures carried forward from the independent review. P0 does not repeat the earlier mistake of calling allocated resources, compiled shaders or nonempty images evidence that the algorithms are correct.

## Run record

The following command is the reproducible P0 check from the repository root:

```text
python scripts/m14_p0_baseline.py --root . --check --report <report-path>
python scripts/test_m14_p0_baseline.py
```

The report contains the manifest SHA-256, fixture IDs, diagnostic channels, source-signature findings, current commit and dirty paths. The user-owned `Samples/Sandbox/assets/scenes/Test.axlescene` change is preserved and is not part of this P0 commit.

## Verification recorded for this checkpoint

- `python scripts/m14_p0_baseline.py --root . --check --report <temp>/m14_p0_baseline.json`: exit 0; 6 fixtures, 19 diagnostic channels and 5 open baseline findings recorded.
- `python scripts/test_m14_p0_baseline.py`: exit 0; 4/4 tests passed.
- `cmake -S . -B build`: exit 0; CTest registration regenerated.
- `ctest --test-dir build -C Debug -R M14P0 --output-on-failure`: exit 0; `M14P0BaselineManifest` and `M14P0BaselineRegressions` passed (2/2).
- No renderer build or GPU rendering test was run for P0; those remain later-package work and are **NOT RUN** here.
- At the final P0 check there is no uncommitted scene path; the P0 diff does not modify `Test.axlescene`.

## Acceptance boundary

P0 is complete when the manifest and regression checks pass and the report is reproducible. M14 remains on HOLD. P1 must close resource ownership and valid GPU timing before the surface/SSR work can be accepted.
