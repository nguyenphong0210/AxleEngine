"""Regression tests for M15 artifact identity validation."""

import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from validate_artifact_identity import validate_manifest


def manifest_for(root: Path, artifact: Path) -> Path:
    relative = artifact.relative_to(root).as_posix()
    data = {
        "schema": "axle.m15.artifact-identity",
        "version": 1,
        "source_commit": "a23404f",
        "backend": "dx12",
        "configuration": "Debug",
        "fixture": "M15_RoughnessSweep",
        "resolution": [128, 128],
        "comparison_space": "linear",
        "artifact_path": relative,
        "artifact_sha256": hashlib.sha256(artifact.read_bytes()).hexdigest(),
        "artifact_size_bytes": artifact.stat().st_size,
    }
    manifest = root / "identity.json"
    manifest.write_text(json.dumps(data, indent=2), encoding="utf-8")
    return manifest


class ArtifactIdentityTests(unittest.TestCase):
    def test_valid_identity_passes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            artifact = root / "capture.ppm"
            artifact.write_bytes(b"P6\n1 1\n255\n\x00\x00\x00")
            report = validate_manifest(manifest_for(root, artifact))
            self.assertTrue(report["valid"])
            self.assertEqual(report["identity"]["artifact_sha256"], hashlib.sha256(artifact.read_bytes()).hexdigest())

    def test_stale_hash_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            artifact = root / "capture.ppm"
            artifact.write_bytes(b"capture-a")
            manifest = manifest_for(root, artifact)
            artifact.write_bytes(b"capture-b")
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                validate_manifest(manifest)

    def test_missing_identity_field_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            artifact = root / "capture.ppm"
            artifact.write_bytes(b"capture")
            manifest = manifest_for(root, artifact)
            data = json.loads(manifest.read_text(encoding="utf-8"))
            del data["backend"]
            manifest.write_text(json.dumps(data), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "backend"):
                validate_manifest(manifest)

    def test_path_traversal_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            artifact = root / "capture.ppm"
            artifact.write_bytes(b"capture")
            manifest = manifest_for(root, artifact)
            data = json.loads(manifest.read_text(encoding="utf-8"))
            data["artifact_path"] = "../capture.ppm"
            manifest.write_text(json.dumps(data), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "relative path"):
                validate_manifest(manifest)


if __name__ == "__main__":
    unittest.main()
