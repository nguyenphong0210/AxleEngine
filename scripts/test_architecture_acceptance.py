"""Evidence integrity: crashes, absent tools and timeouts cannot validate acceptance."""
from pathlib import Path
import sys
import tempfile
import unittest
from run_architecture_acceptance import Evidence, StopAcceptance, digest


class EvidenceTests(unittest.TestCase):
    def test_mandatory_failure_stops_after_persisting_exact_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            evidence = Evidence(Path(directory), stop_on_failure=True)
            with self.assertRaises(StopAcceptance):
                evidence.run("failed", [sys.executable, "-c", "raise SystemExit(1)"])
            self.assertEqual(evidence.rows[0]["status"], "FAIL")
            self.assertTrue((Path(directory) / "commands.json").is_file())

    def test_expected_rejection_requires_exact_exit(self):
        with tempfile.TemporaryDirectory() as directory:
            evidence = Evidence(Path(directory))
            self.assertFalse(evidence.run("crash", [sys.executable, "-c", "raise SystemExit(73)"], expected=1))
            self.assertFalse(evidence.run("unrelated", [sys.executable, "-c", "raise SystemExit(1)"], expected=1, proof="EXPECTED_REJECTION"))
            self.assertTrue(evidence.run("rejected", [sys.executable, "-c", "print('EXPECTED_REJECTION'); raise SystemExit(1)"], expected=1, proof="EXPECTED_REJECTION"))
            self.assertEqual([row["status"] for row in evidence.rows], ["FAIL", "FAIL", "PASS"])

    def test_missing_native_binary_and_watchdog_stay_open(self):
        with tempfile.TemporaryDirectory() as directory:
            evidence = Evidence(Path(directory))
            self.assertFalse(evidence.run("missing", [str(Path(directory) / "missing.exe")]))
            self.assertFalse(evidence.run("hang", [sys.executable, "-c", "import time; time.sleep(60)"], timeout=0.15))
            self.assertEqual([row["status"] for row in evidence.rows], ["NOT RUN", "FAIL"])
            self.assertTrue((Path(directory) / "commands.json").is_file())

    def test_artifact_identity_changes_with_content(self):
        with tempfile.TemporaryDirectory() as directory:
            artifact = Path(directory) / "receipt"
            artifact.write_bytes(b"first")
            previous = digest(artifact)
            artifact.write_bytes(b"other")
            self.assertNotEqual(digest(artifact), previous)


if __name__ == "__main__":
    unittest.main()
