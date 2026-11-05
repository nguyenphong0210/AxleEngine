"""Review AF-00/EJ-00 checker and harness contracts without production edits.

A nonzero review exit means one or more acceptance contracts are violated.
Fixtures and logs are created in a new owned directory under --workspace.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]


def load_module(path):
    spec = importlib.util.spec_from_file_location("reviewed_jobs_harness", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path, default=ROOT / "build/opt01-review/adversarial")
    parser.add_argument("--binary", type=Path, help="Newly built AxleJobsECSTests for native passing and setup-failure controls")
    args = parser.parse_args()
    args.workspace.mkdir(parents=True, exist_ok=True)
    workspace = Path(tempfile.mkdtemp(prefix="review-", dir=args.workspace.resolve()))
    results = []
    harness = load_module(ROOT / "scripts/run_jobs_ecs_harness.py")

    def record(name, expected, observed, passed, **evidence):
        results.append(dict(name=name, expected=expected, observed=observed,
                            contract_satisfied=passed, **evidence))
        print(f"{'PASS' if passed else 'FAIL'} {name}: {observed}")

    for name, cmd, timeout in [
        ("missing-executable-is-not-engine-defect", [str(workspace / "missing.exe")], 2),
        ("unrelated-exit-is-not-specific-defect", [sys.executable, "-c", "raise SystemExit(73)"], 2),
        ("early-timeout-is-not-specific-defect", [sys.executable, "-c", "import time; time.sleep(5)"], 0.2),
    ]:
        result = harness.run_scenario(cmd, name, timeout, "REPRODUCED_DEFECT", workspace)
        record(name, "do not classify unrelated infrastructure failures as REPRODUCED_DEFECT",
               result["status"], result["status"] != "REPRODUCED_DEFECT", result=result)

    mutations = {
        "direct-forbidden-link-control": {
            "Engine/Core/CMakeLists.txt": "add_library(AxleCore INTERFACE)\ntarget_link_libraries(AxleCore INTERFACE Axle::UI)\n",
        },
        "nested-cmake-forbidden-link": {
            "Engine/Core/CMakeLists.txt": "add_library(AxleCore INTERFACE)\nadd_subdirectory(nested)\n",
            "Engine/Core/nested/CMakeLists.txt": "target_link_libraries(AxleCore INTERFACE Axle::UI)\n",
        },
        "variable-forbidden-link-with-existing-allowance": {
            "Engine/Asset/CMakeLists.txt": "add_library(AxleAsset INTERFACE)\nset(NEW_DEP Axle::UI)\ntarget_link_libraries(AxleAsset INTERFACE ${NEW_DEP})\n",
        },
        "relative-forbidden-include": {
            "Engine/Core/src/NewBoundary.cpp": '#include "../../Scene/include/Axle/Scene.hpp"\n',
            "Engine/Scene/include/Axle/Scene.hpp": "// Minimal header for a resolvable relative include.\n",
        },
    }
    for name, files in mutations.items():
        fixture = workspace / name
        for relative, contents in files.items():
            path = fixture / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents, encoding="utf-8")
        result = subprocess.run(
            [sys.executable, str(ROOT / "scripts/check_architecture.py"),
             "--config", str(ROOT / "scripts/architecture_rules.json"), "--root", str(fixture)],
            capture_output=True, text=True, timeout=10)
        log = workspace / f"{name}.log"
        log.write_text(result.stdout + result.stderr, encoding="utf-8")
        record(name, "checker exit 1 for a new forbidden dependency", f"exit {result.returncode}",
               result.returncode == 1, log=str(log), fixture=str(fixture))

    if args.binary:
        result = subprocess.run([str(args.binary.resolve()), "queued-delivery-control"],
                                capture_output=True, text=True, timeout=15)
        log = workspace / "queued-delivery-native-control.log"
        log.write_text(result.stdout + result.stderr, encoding="utf-8")
        record("identity-probe-accepts-correct-deferred-control", "correct queued delivery is accepted",
               f"exit {result.returncode}", result.returncode == 0, log=str(log),
               evidence_kind="permanent registered-worker production queue control with before/after snapshots")
        import os
        env = os.environ.copy()
        env["PATH"] = ""
        native = harness.run_scenario([str(args.binary.resolve()), "ecsj-01-compiler-thread", str(workspace)],
                                      "ecsj-01-compiler-thread", 10, "REPRODUCED_DEFECT", workspace, env=env)
        record("compiler-prerequisite-failure-is-infrastructure", "setup failure is an unexpected infrastructure failure",
               native["status"], native["status"] == "INFRASTRUCTURE_FAILURE" and native["unexpected"], result=native)

    report = {"subject": "current AF-00/EJ-00 corrections; original failing reference 95a6c9d", "workspace": str(workspace),
              "results": results, "failed_contracts": sum(not r["contract_satisfied"] for r in results)}
    report_path = workspace / "results.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Report: {report_path}")
    return 1 if report["failed_contracts"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
