"""Validate and report the Milestone 14 P0 baseline contract.

This tool deliberately reports open implementation findings. It does not claim
that the renderer passes the fixtures and it never edits source or scene files.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "Tests" / "Fixtures" / "M14" / "P0_REFERENCE_FIXTURES.json"


def load_manifest(path: Path = MANIFEST) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema") != "axle.m14.p0.reference-fixtures":
        raise ValueError("unexpected fixture schema")
    if data.get("version") != 1:
        raise ValueError("unsupported fixture version")
    capture = data.get("capture")
    if not isinstance(capture, dict) or capture.get("default_resolution") != [128, 128]:
        raise ValueError("invalid capture defaults")
    diagnostics = capture.get("diagnostics", [])
    if len(diagnostics) != len(set(diagnostics)) or "scene_normal" not in diagnostics:
        raise ValueError("diagnostic channels must be unique and include scene_normal")
    fixtures = data.get("fixtures")
    if not isinstance(fixtures, list) or not fixtures:
        raise ValueError("fixture list is empty")
    ids = [fixture.get("id") for fixture in fixtures]
    if any(not isinstance(identifier, str) or not identifier for identifier in ids):
        raise ValueError("each fixture needs a non-empty id")
    if len(ids) != len(set(ids)):
        raise ValueError("fixture ids must be unique")
    for fixture in fixtures:
        checks = fixture.get("checks")
        if not isinstance(checks, list) or not checks:
            raise ValueError(f"fixture {fixture['id']} has no checks")
    return data


def source_contracts(root: Path = ROOT) -> list[dict[str, Any]]:
    signatures = [
        {
            "id": "F01_UNWRITTEN_SCENE_NORMAL_BASELINE",
            "status": "OPEN_AT_BASELINE",
            "path": "Engine/Scene/src/RHISceneRenderer.cpp",
            "needles": ["m_OpaqueSceneNormal = viewTargets->Get(RHIViewTarget::ScreenNormal);", "Get(RHIViewTarget::ScreenNormal)"],
            "context": "ScreenNormal is allocated/bound by the current renderer; P0 records the surface-data contract for P2.",
        },
        {
            "id": "F02_HIZ_DESCRIPTOR_REUSE_BASELINE",
            "status": "OPEN_AT_BASELINE",
            "path": "Engine/Scene/src/RHISceneRenderer.cpp",
            "needles": ["for (uint32_t level = 0; level < 8; ++level)", "m_HiZDescriptorPool.UpdateTexture(m_HiZTextureSet"],
            "context": "The current eight-level recording path must be repaired before SSR acceptance.",
        },
        {
            "id": "F03_PROBE_FALLBACK_BASELINE",
            "status": "OPEN_AT_BASELINE",
            "path": "Engine/Resources/Shaders/SSR.hlsl",
            "needles": ["float3 probeColor = float3(0.08, 0.08, 0.08) * u_ProbeBoxMax.w;"],
            "context": "The fixture requires a captured cubemap response rather than a constant probe color.",
        },
        {
            "id": "F04_EXPOSURE_CONSUMER_BASELINE",
            "status": "OPEN_AT_BASELINE",
            "path": "Engine/Scene/src/RHISceneRenderer.cpp",
            "needles": ["RHITexture* exposureSource", "float targetExp = std::clamp(settings.Exposure"],
            "context": "P0 records the requirement that the global exposure output must drive tonemapping.",
        },
        {
            "id": "F05_TIMESTAMP_PROBE_BASELINE",
            "status": "OPEN_AT_BASELINE",
            "path": "Tests/src/Test_EnvironmentRendering.cpp",
            "needles": ["RHIBufferDesc::Structured(1024, 4096"],
            "context": "P1 must make the probe submit and read back a valid timestamp rather than accepting a default zero.",
        },
    ]
    result = []
    for finding in signatures:
        path = root / finding["path"]
        text = path.read_text(encoding="utf-8") if path.is_file() else ""
        missing = [needle for needle in finding["needles"] if needle not in text]
        row = dict(finding)
        row["present"] = not missing
        row["missing_signatures"] = missing
        result.append(row)
    return result


def git_snapshot(root: Path = ROOT) -> dict[str, Any]:
    def run(*args: str) -> str:
        return subprocess.check_output(["git", *args], cwd=root, text=True, encoding="utf-8").strip()

    status = run("status", "--porcelain=v1").splitlines()
    return {
        "head": run("rev-parse", "HEAD"),
        "status": status,
        "preserved_user_scene_change": any(line.endswith("Samples/Sandbox/assets/scenes/Test.axlescene") for line in status),
    }


def build_report(root: Path = ROOT, manifest_path: Path = MANIFEST) -> dict[str, Any]:
    manifest = load_manifest(manifest_path)
    manifest_bytes = manifest_path.read_bytes()
    contracts = source_contracts(root)
    return {
        "schema": "axle.m14.p0.baseline-report",
        "version": 1,
        "baseline": "769a9f2",
        "manifest": str(manifest_path.relative_to(root)).replace("\\", "/"),
        "manifest_sha256": hashlib.sha256(manifest_bytes).hexdigest(),
        "fixture_ids": [fixture["id"] for fixture in manifest["fixtures"]],
        "diagnostic_channels": manifest["capture"]["diagnostics"],
        "source_contracts": contracts,
        "open_findings": [row["id"] for row in contracts if row["status"] == "OPEN_AT_BASELINE" and row["present"]],
        "git": git_snapshot(root),
        "acceptance": "P0_BASELINE_RECORDED; M14_REMAINS_HOLD",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--manifest", type=Path, default=None)
    parser.add_argument("--check", action="store_true", help="validate the manifest and source signatures")
    parser.add_argument("--report", type=Path, help="write a JSON baseline report")
    args = parser.parse_args()
    root = args.root.resolve()
    manifest = (args.manifest or (root / "Tests/Fixtures/M14/P0_REFERENCE_FIXTURES.json")).resolve()
    report = build_report(root, manifest)
    missing = [row for row in report["source_contracts"] if not row["present"]]
    if missing:
        for row in missing:
            print(f"M14 P0 contract signature missing: {row['id']}")
        return 1
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"M14 P0 baseline: {report['acceptance']}; {len(report['fixture_ids'])} fixtures; {len(report['diagnostic_channels'])} diagnostic channels; {len(report['open_findings'])} recorded open findings")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
