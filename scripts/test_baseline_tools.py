"""Regression checks for the baseline's failure and dependency contracts."""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT/"scripts"/f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


runner, arch = load("run_jobs_ecs_harness"), load("check_architecture")
CONFIG = json.loads((ROOT/"scripts/architecture_rules.json").read_text(encoding="utf-8"))


class HarnessTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.workspace = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def run_case(self, code, kind="REPRODUCED_DEFECT", **kwargs):
        return runner.run_scenario([sys.executable, "-c", code], "case", kwargs.pop("timeout", 3),
                                   kind, self.workspace, **kwargs)

    def test_launch_error_is_unexpected(self):
        r = runner.run_scenario([str(self.workspace/"missing.exe")], "case", 1, "REPRODUCED_DEFECT", self.workspace)
        self.assertEqual(r["status"], "LAUNCH_ERROR")
        self.assertTrue(r["unexpected"])
        self.assertEqual(runner.summarize([r])["unexpected_failures"], 1)

    def test_unrelated_exit_and_wrong_scenario_evidence_are_rejected(self):
        for code in ["raise SystemExit(73)", "print('AXLE_TEST_DEFECT other'); raise SystemExit(1)"]:
            with self.subTest(code=code):
                self.assertTrue(self.run_case(code)["unexpected"])

    def test_successful_scenario_and_actual_defect_have_distinct_results(self):
        defect = self.run_case("print('AXLE_TEST_DEFECT case'); raise SystemExit(1)")
        good = self.run_case("print('AXLE_TEST_PASS case')")
        self.assertEqual(defect["status"], "REPRODUCED_DEFECT")
        self.assertEqual(good["status"], "DEFECT_NOT_REPRODUCED")
        self.assertEqual(runner.summarize([defect, good])["not_reproduced"], 1)

    def test_setup_error_never_counts_as_defect(self):
        r = self.run_case("print('AXLE_TEST_ERROR case'); raise SystemExit(2)")
        self.assertEqual(r["status"], "INFRASTRUCTURE_FAILURE")
        self.assertTrue(r["unexpected"])

    def test_timeout_requires_exact_checkpoint(self):
        for prefix, expected in [("", "UNEXPECTED_TIMEOUT"), ("print('AXLE_TEST_READY case', flush=True);", "REPRODUCED_DEFECT")]:
            r = self.run_case(prefix+"import time; time.sleep(5)", timeout=0.4, hang_proof="AXLE_TEST_READY case")
            self.assertEqual(r["status"], expected)

    def test_hang_after_completed_checkpoint_is_not_bridge_destructor_defect(self):
        r = self.run_case("print('AXLE_TEST_READY case', flush=True); print('AXLE_TEST_COMPLETED case', flush=True); import time; time.sleep(5)",
                          timeout=0.4, hang_proof="AXLE_TEST_READY case")
        self.assertEqual(r["status"], "UNEXPECTED_TIMEOUT")

    def test_failure_probe_requires_proof_and_exact_exit(self):
        self.assertTrue(self.run_case("raise SystemExit(73)", "PROBE_FAILURE")["unexpected"])
        r = self.run_case("print('AXLE_TEST_EXPECTED_FAILURE case'); raise SystemExit(1)", "PROBE_FAILURE")
        self.assertEqual(r["status"], "PROBE_VERIFIED")

    def test_checker_configuration_failure_cannot_validate_forbidden_fixture(self):
        r = runner.run_scenario([sys.executable, str(ROOT/"scripts/check_architecture.py"),
                                 "--config", str(self.workspace/"missing.json")],
                                "case", 5, "PROBE_FAILURE", self.workspace,
                                proof="AXLE_ARCH_CHECK VIOLATIONS")
        self.assertTrue(r["unexpected"])
        self.assertNotEqual(r["status"], "PROBE_VERIFIED")


class ArchitectureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def write(self, path, text):
        p = self.root/path
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text, encoding="utf-8")

    def inspect(self):
        return arch.inspect(self.root, copy.deepcopy(CONFIG), ROOT)

    def test_direct_and_nested_forbidden_links(self):
        self.write("Engine/Core/CMakeLists.txt", "add_library(AxleCore INTERFACE)\nadd_subdirectory(nested)\n")
        self.write("Engine/Core/nested/CMakeLists.txt", "target_link_libraries(AxleCore INTERFACE Axle::UI)")
        self.assertTrue(any(e["kind"] == "CMakeLink" for e in self.inspect()["violations"]))

    def test_variable_does_not_borrow_unrelated_allowance(self):
        self.write("Engine/Asset/CMakeLists.txt", "set(EDGE Axle::UI)\ntarget_link_libraries(AxleAsset PRIVATE ${EDGE})")
        r = self.inspect()
        self.assertEqual(r["target_links"][0]["to_module"], "UI")
        self.assertTrue(r["violations"])

    def test_relative_project_include(self):
        self.write("Engine/Core/src/Boundary.cpp", '#include "../../Scene/include/Axle/Scene.hpp"')
        self.write("Engine/Scene/include/Axle/Scene.hpp", "// fixture")
        self.assertTrue(self.inspect()["violations"])

    def test_exact_existing_allowance_and_new_header_in_same_module(self):
        self.write("Engine/Core/src/Application.cpp", "#include <Axle/Input.hpp>")
        self.assertFalse(self.inspect()["violations"])
        self.write("Engine/Core/src/Application.cpp", "#include <Axle/NewBoundary.hpp>")
        self.write("Engine/Platform/include/Axle/NewBoundary.hpp", "// fixture")
        self.assertTrue(self.inspect()["violations"])

    def test_unresolved_variables_and_generator_expressions_fail_coverage(self):
        for link in ["${UNKNOWN}", "$<$<CONFIG:Debug>:Axle::UI>"]:
            with self.subTest(link=link):
                self.write("Engine/Asset/CMakeLists.txt", f"target_link_libraries(AxleAsset PRIVATE {link})")
                self.assertTrue(any(e["kind"] == "CoverageError" for e in self.inspect()["violations"]))

    def test_included_helper_outside_module_is_checked(self):
        self.write("Engine/Core/CMakeLists.txt", 'include("../../helpers/edges.cmake")')
        self.write("helpers/edges.cmake", "target_link_libraries(AxleCore PRIVATE Axle::UI)")
        self.assertTrue(any(e["kind"] == "CMakeLink" for e in self.inspect()["violations"]))

    def test_missing_composition_fails_coverage(self):
        self.write("Engine/Core/CMakeLists.txt", 'include("missing-helper.cmake")')
        self.assertTrue(any(e["kind"] == "CoverageError" for e in self.inspect()["violations"]))

    def test_root_cmake_links_are_checked(self):
        self.write("CMakeLists.txt", "target_link_libraries(AxleCore PRIVATE Axle::UI)")
        self.assertTrue(self.inspect()["violations"])

    def test_closed_package_disables_its_allowance(self):
        self.write("Engine/Core/src/Application.cpp", "#include <Axle/Input.hpp>")
        self.assertFalse(self.inspect()["violations"])
        self.assertTrue(arch.inspect(self.root, CONFIG, ROOT, ["AF-02"])["violations"])

    def test_unknown_link_and_property_based_construction_are_not_silently_allowed(self):
        for command in ["target_link_libraries(AxleAsset PRIVATE new_unknown_library)",
                        "set_property(TARGET AxleAsset PROPERTY LINK_LIBRARIES Axle::UI)"]:
            with self.subTest(command=command):
                self.write("Engine/Asset/CMakeLists.txt", command)
                self.assertTrue(self.inspect()["violations"])


if __name__ == "__main__":
    unittest.main()
