"""Validate identity metadata for deterministic M15 image/readback artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


SCHEMA = "axle.m15.artifact-identity"
COMMIT_RE = re.compile(r"^[0-9a-fA-F]{7,40}$")
HASH_RE = re.compile(r"^[0-9a-fA-F]{64}$")
BACKENDS = {"dx12", "vulkan", "gl"}
CONFIGURATIONS = {"Debug", "Release"}
SPACES = {"linear", "srgb"}


def _fail(message: str) -> None:
    raise ValueError(message)


def _required_text(manifest: dict, key: str) -> str:
    value = manifest.get(key)
    if not isinstance(value, str) or not value.strip():
        _fail(f"missing non-empty field: {key}")
    return value.strip()


def validate_manifest(path: Path) -> dict:
    manifest_path = path.resolve()
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        _fail(f"cannot read manifest: {error}")
    if not isinstance(manifest, dict) or manifest.get("schema") != SCHEMA or manifest.get("version") != 1:
        _fail(f"expected schema {SCHEMA} version 1")

    source_commit = _required_text(manifest, "source_commit")
    if not COMMIT_RE.fullmatch(source_commit):
        _fail("source_commit must be a hexadecimal Git commit identifier")
    backend = _required_text(manifest, "backend").lower()
    if backend not in BACKENDS:
        _fail(f"backend must be one of {sorted(BACKENDS)}")
    configuration = _required_text(manifest, "configuration")
    if configuration not in CONFIGURATIONS:
        _fail(f"configuration must be one of {sorted(CONFIGURATIONS)}")
    fixture = _required_text(manifest, "fixture")
    comparison_space = _required_text(manifest, "comparison_space").lower()
    if comparison_space not in SPACES:
        _fail(f"comparison_space must be one of {sorted(SPACES)}")

    resolution = manifest.get("resolution")
    if (not isinstance(resolution, list) or len(resolution) != 2 or
            any(not isinstance(value, int) or isinstance(value, bool) or value <= 0 for value in resolution)):
        _fail("resolution must be [positive_width, positive_height]")

    artifact_name = _required_text(manifest, "artifact_path")
    artifact_relative = Path(artifact_name)
    if artifact_relative.is_absolute() or ".." in artifact_relative.parts:
        _fail("artifact_path must be a relative path inside the manifest directory")
    artifact_path = (manifest_path.parent / artifact_relative).resolve()
    if not artifact_path.is_file():
        _fail(f"artifact does not exist: {artifact_name}")

    expected_hash = _required_text(manifest, "artifact_sha256").lower()
    if not HASH_RE.fullmatch(expected_hash):
        _fail("artifact_sha256 must be a 64-character hexadecimal SHA-256")
    actual_hash = hashlib.sha256(artifact_path.read_bytes()).hexdigest()
    if actual_hash != expected_hash:
        _fail(f"artifact SHA-256 mismatch: expected {expected_hash}, found {actual_hash}")

    actual_size = artifact_path.stat().st_size
    if "artifact_size_bytes" in manifest and manifest["artifact_size_bytes"] != actual_size:
        _fail("artifact_size_bytes does not match the artifact")

    identity = {
        "source_commit": source_commit.lower(),
        "backend": backend,
        "configuration": configuration,
        "fixture": fixture,
        "resolution": resolution,
        "comparison_space": comparison_space,
        "artifact_path": artifact_relative.as_posix(),
        "artifact_sha256": actual_hash,
    }
    return {"schema": SCHEMA, "version": 1, "valid": True, "identity": identity, "artifact_size_bytes": actual_size}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    try:
        report = validate_manifest(args.manifest)
    except ValueError as error:
        print(f"M15 artifact identity: FAIL: {error}")
        return 1
    serialized = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.report:
        args.report.write_text(serialized, encoding="utf-8")
    print(serialized, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
