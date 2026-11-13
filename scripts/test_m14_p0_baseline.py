"""Regression tests for the M14 P0 fixture and baseline contract."""
import importlib.util
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("m14_p0_baseline", ROOT / "scripts" / "m14_p0_baseline.py")
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


class M14P0BaselineTests(unittest.TestCase):
    def test_manifest_has_deterministic_cases_and_diagnostics(self):
        manifest = MODULE.load_manifest()
        self.assertEqual(manifest["capture"]["comparison_backend_matrix"], ["dx12", "vulkan", "gl"])
        self.assertGreaterEqual(len(manifest["fixtures"]), 6)
        self.assertIn("smooth_sphere_surface", [fixture["id"] for fixture in manifest["fixtures"]])
        self.assertIn("metal_sphere_reflection_hit", [fixture["id"] for fixture in manifest["fixtures"]])
        self.assertIn("scene_normal", manifest["capture"]["diagnostics"])
        self.assertIn("hiz_level_7", manifest["capture"]["diagnostics"])

    def test_current_source_contracts_are_recorded_without_claiming_pass(self):
        findings = MODULE.source_contracts(ROOT)
        self.assertEqual(len(findings), 5)
        self.assertTrue(all(row["present"] for row in findings))
        self.assertTrue(all(row["status"] == "OPEN_AT_BASELINE" for row in findings))

    def test_report_preserves_dirty_scene_and_holds_acceptance(self):
        report = MODULE.build_report(ROOT)
        self.assertEqual(report["acceptance"], "P0_BASELINE_RECORDED; M14_REMAINS_HOLD")
        self.assertIn("F01_UNWRITTEN_SCENE_NORMAL_BASELINE", report["open_findings"])
        self.assertIn("F05_TIMESTAMP_PROBE_BASELINE", report["open_findings"])
        self.assertIn("git", report)

    def test_manifest_schema_rejects_duplicate_fixture_ids(self):
        manifest = MODULE.load_manifest()
        manifest["fixtures"].append(dict(manifest["fixtures"][0]))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            import json
            path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaises(ValueError):
                MODULE.load_manifest(path)


if __name__ == "__main__":
    unittest.main()
