#!/usr/bin/env python3
"""
AxleEngine Permanent Baseline Harness Runner (AF-00 + EJ-00)
Runs architecture checks, ECS/jobs fault probes, and passing controls
in isolated child processes with an owned watchdog.

Distinguishes:
  - PASS: clean successful verification
  - REPRODUCED KNOWN DEFECT: verified engine defect (asserts correct behavior, records failure honestly)
  - INTENTIONAL FAILURE PROBE: intentional harness validation (must fail or time out as expected)
"""

import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path


def run_scenario(cmd, name, timeout_sec, expected_type, workspace):
    log_path = workspace / f"{name}.log"
    timed_out = False
    exit_code = None
    started = time.monotonic()

    with log_path.open("w", encoding="utf-8") as out:
        try:
            proc = subprocess.Popen(
                cmd,
                stdout=out,
                stderr=subprocess.STDOUT,
                cwd=str(workspace)
            )
            try:
                proc.communicate(timeout=timeout_sec)
                exit_code = proc.returncode
            except subprocess.TimeoutExpired:
                timed_out = True
                proc.kill()
                proc.communicate()
                exit_code = -9
        except Exception as e:
            out.write(f"\nFailed to launch process: {e}\n")
            exit_code = -1

    elapsed = round(time.monotonic() - started, 3)
    log_content = log_path.read_text(encoding="utf-8", errors="replace")

    # Determine classification
    is_pass = False
    is_defect_reproduced = False
    is_fixture_success = False

    if expected_type == "PASS":
        is_pass = (exit_code == 0 and not timed_out)
        status_label = "PASS" if is_pass else "UNEXPECTED_FAILURE"
    elif expected_type == "REPRODUCED_DEFECT":
        # Defect correctly asserted and failed
        is_defect_reproduced = (exit_code != 0 or timed_out)
        status_label = "REPRODUCED_DEFECT" if is_defect_reproduced else "DEFECT_NOT_REPRODUCED"
    elif expected_type == "PROBE_FAILURE":
        # Intentional nonzero exit code expected
        is_fixture_success = (exit_code != 0 and not timed_out)
        status_label = "PROBE_VERIFIED" if is_fixture_success else "PROBE_UNEXPECTED_PASS"
    elif expected_type == "PROBE_TIMEOUT":
        # Intentional watchdog timeout expected
        is_fixture_success = timed_out
        status_label = "WATCHDOG_TERMINATED" if is_fixture_success else "PROBE_UNEXPECTED_EXIT"
    else:
        status_label = f"EXIT_{exit_code}"

    return {
        "scenario": name,
        "type": expected_type,
        "status": status_label,
        "exit_code": exit_code,
        "timed_out": timed_out,
        "elapsed_seconds": elapsed,
        "log": str(log_path),
        "log_preview": log_content.strip()[-300:] if len(log_content) > 300 else log_content.strip()
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path, help="Path to AxleJobsECSTests executable")
    parser.add_argument("--workspace", type=Path, default=Path("build/harness_workspace"))
    parser.add_argument("--repo-root", type=Path, default=Path("."))
    parser.add_argument("--report", type=Path, default=Path("docs/reviews/OPT_01_BASELINE_RESULTS.json"))
    parser.add_argument("--timeout", type=float, default=6.0, help="Default watchdog timeout in seconds")
    args = parser.parse_args()

    binary = args.binary.resolve()
    repo_root = args.repo_root.resolve()
    workspace = args.workspace.resolve()
    workspace.mkdir(parents=True, exist_ok=True)

    if not binary.exists():
        print(f"ERROR: Executable not found: {binary}", file=sys.stderr)
        return 2

    python_exe = sys.executable

    scenarios = [
        # AF-00 Architecture Checks
        {
            "name": "arch-boundary-checks",
            "cmd": [python_exe, str(repo_root / "scripts/check_architecture.py"), "--root", str(repo_root)],
            "type": "PASS",
            "timeout": 10.0
        },
        {
            "name": "arch-forbidden-fixture",
            "cmd": [python_exe, str(repo_root / "scripts/check_architecture.py"), "--fixture", str(repo_root / "Tests/Fixtures/ArchitectureViolationFixture")],
            "type": "PROBE_FAILURE",
            "timeout": 5.0
        },
        # EJ-00 Jobs/ECS Fault Probes & Controls
        {
            "name": "ecsj-01-compiler-thread",
            "cmd": [str(binary), "ecsj-01-compiler-thread", str(workspace / "compiler_probe_ws")],
            "type": "REPRODUCED_DEFECT",
            "timeout": 10.0
        },
        {
            "name": "ecsj-02-thread-identity",
            "cmd": [str(binary), "ecsj-02-thread-identity", str(workspace)],
            "type": "REPRODUCED_DEFECT",
            "timeout": 5.0
        },
        {
            "name": "ecsj-03-bridge-hang",
            "cmd": [str(binary), "ecsj-03-bridge-hang", str(workspace)],
            "type": "REPRODUCED_DEFECT",
            "timeout": 4.0
        },
        {
            "name": "ecsj-03-bridge-control",
            "cmd": [str(binary), "ecsj-03-bridge-control", str(workspace)],
            "type": "PASS",
            "timeout": 5.0
        },
        {
            "name": "ecsj-04-entt-tombstone",
            "cmd": [str(binary), "ecsj-04-entt-tombstone", str(workspace)],
            "type": "REPRODUCED_DEFECT",
            "timeout": 5.0
        },
        # Current-Format Scene / Assembly / Lifecycle (OPT-00 baseline evidence)
        {
            "name": "current-format-lifecycle",
            "cmd": [str(binary), "current-format-lifecycle", str(workspace)],
            "type": "PASS",
            "timeout": 6.0
        },
        {
            "name": "deliberate-failure",
            "cmd": [str(binary), "deliberate-failure", str(workspace)],
            "type": "PROBE_FAILURE",
            "timeout": 3.0
        },
        {
            "name": "watchdog-hang",
            "cmd": [str(binary), "watchdog-hang", str(workspace)],
            "type": "PROBE_TIMEOUT",
            "timeout": 2.5
        }
    ]

    print("=== AxleEngine AF-00 + EJ-00 Permanent Harness Runner ===")
    print(f"Binary: {binary}")
    print(f"Workspace: {workspace}\n")

    results = []
    for s in scenarios:
        res = run_scenario(s["cmd"], s["name"], s["timeout"], s["type"], workspace)
        results.append(res)
        print(f"[{res['status']:<20}] {res['scenario']:<26} (exit {res['exit_code']}, {res['elapsed_seconds']}s)")

    # Write JSON report
    report_data = {
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "binary": str(binary),
        "total_scenarios": len(results),
        "summary": {
            "pass_count": sum(1 for r in results if r["status"] == "PASS"),
            "reproduced_defects": sum(1 for r in results if r["status"] == "REPRODUCED_DEFECT"),
            "probes_verified": sum(1 for r in results if r["status"] in ("PROBE_VERIFIED", "WATCHDOG_TERMINATED")),
            "unexpected_failures": sum(1 for r in results if "UNEXPECTED" in r["status"])
        },
        "results": results
    }

    report_path = args.report.resolve()
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report_data, indent=2), encoding="utf-8")
    print(f"\nDurable baseline report written to: {report_path}")

    # The runner exits 0 if all normal checks and intentional probes behaved as expected
    all_ok = (report_data["summary"]["unexpected_failures"] == 0)
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
