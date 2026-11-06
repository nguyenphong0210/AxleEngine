"""Collect typed baseline evidence; infrastructure failures never become Engine defects."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def run_scenario(cmd, name, timeout_sec, expected_type, workspace, *, proof=None, hang_proof=None, env=None):
    workspace = Path(workspace)
    workspace.mkdir(parents=True, exist_ok=True)
    log_path = workspace / f"{name}.log"
    started = time.monotonic()
    timed_out, launched, exit_code = False, False, None
    with log_path.open("w", encoding="utf-8") as out:
        try:
            proc = subprocess.Popen(cmd, stdout=out, stderr=subprocess.STDOUT, cwd=workspace, env=env)
            launched = True
            try:
                proc.wait(timeout=timeout_sec)
            except subprocess.TimeoutExpired:
                timed_out = True
                proc.kill()
                proc.wait()
            exit_code = proc.returncode
        except OSError as error:
            out.write(f"Failed to launch process: {error}\n")
    lines = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
    success = f"AXLE_TEST_PASS {name}" in lines
    defect = (proof or f"AXLE_TEST_DEFECT {name}") in lines
    ready = (hang_proof or f"AXLE_TEST_READY {name}") in lines
    intentional = (proof or f"AXLE_TEST_EXPECTED_FAILURE {name}") in lines
    has_error = any(line.startswith("AXLE_TEST_ERROR ") for line in lines)
    completed = f"AXLE_TEST_COMPLETED {name}" in lines or success or defect or intentional
    unexpected = True
    if not launched:
        status = "LAUNCH_ERROR"
    elif timed_out:
        if ready and not has_error and not completed and hang_proof and expected_type == "REPRODUCED_DEFECT":
            status, unexpected = "REPRODUCED_DEFECT", False
        elif ready and not has_error and not completed and expected_type == "PROBE_TIMEOUT":
            status, unexpected = "WATCHDOG_TERMINATED", False
        else:
            status = "UNEXPECTED_TIMEOUT"
    elif has_error or exit_code == 2:
        status = "INFRASTRUCTURE_FAILURE"
    elif expected_type == "REPRODUCED_DEFECT" and exit_code == 1 and defect:
        status, unexpected = "REPRODUCED_DEFECT", False
    elif expected_type == "REPRODUCED_DEFECT" and exit_code == 0 and success:
        status, unexpected = "DEFECT_NOT_REPRODUCED", False
    elif expected_type == "PROBE_FAILURE" and exit_code == 1 and intentional:
        status, unexpected = "PROBE_VERIFIED", False
    elif expected_type == "PASS" and exit_code == 0 and (proof in lines if proof else success):
        status, unexpected = "PASS", False
    else:
        status = "UNEXPECTED_FAILURE"
    return dict(scenario=name, type=expected_type, status=status, unexpected=unexpected,
                exit_code=exit_code, timed_out=timed_out, elapsed_seconds=round(time.monotonic()-started, 3),
                log=str(log_path), log_preview="\n".join(lines)[-1000:])


def summarize(results):
    return dict(pass_count=sum(r["status"] == "PASS" for r in results),
                reproduced_defects=sum(r["status"] == "REPRODUCED_DEFECT" for r in results),
                not_reproduced=sum(r["status"] == "DEFECT_NOT_REPRODUCED" for r in results),
                probes_verified=sum(r["status"] in {"PROBE_VERIFIED", "WATCHDOG_TERMINATED"} for r in results),
                unexpected_failures=sum(r["unexpected"] for r in results))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--workspace", type=Path, default=Path("build/harness_workspace"))
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--report", type=Path, default=Path("build/jobs-ecs-results.json"))
    parser.add_argument("--timeout", type=float, default=10.0, help="Watchdog for ordinary native cases")
    parser.add_argument("--case", action="append", dest="cases", help="Collect only this case; repeatable")
    args = parser.parse_args()
    binary, root = args.binary.resolve(), args.repo_root.resolve()
    if not binary.is_file() or args.timeout <= 0:
        parser.error("an existing executable and positive timeout are required")
    args.workspace.mkdir(parents=True, exist_ok=True)
    workspace = Path(tempfile.mkdtemp(prefix="run-", dir=args.workspace.resolve()))
    scenarios = [
        dict(name="arch-boundary-checks", cmd=[sys.executable, str(root/"scripts/check_architecture.py"), "--root", str(root)],
             type="PASS", timeout=15, proof="AXLE_ARCH_CHECK PASS"),
        dict(name="arch-forbidden-fixture", cmd=[sys.executable, str(root/"scripts/check_architecture.py"), "--fixture", str(root/"Tests/Fixtures/ArchitectureViolationFixture")],
             type="PROBE_FAILURE", timeout=10, proof="AXLE_ARCH_CHECK VIOLATIONS"),
    ]
    native = [
        ("ecsj-01-compiler-thread", "PASS", max(args.timeout, 20)),
        ("ecsj-02-thread-identity", "PASS", args.timeout),
        ("ej01-lifecycle-admission", "PASS", args.timeout),
        ("ej01-review-fixes", "PASS", args.timeout),
        ("ej02-deferred-executor", "PASS", args.timeout),
        ("ej03-compiler-handoff", "PASS", max(args.timeout, 20)),
        ("ej04-reload-commit", "PASS", args.timeout),
        ("ej05-physics-lifetime", "PASS", args.timeout),
        ("ej06-physics-waits", "PASS", args.timeout),
        ("ej07-handles-shutdown", "PASS", args.timeout),
        ("ecsj-03-bridge-hang", "PASS", args.timeout),
        ("ecsj-03-bridge-control", "PASS", args.timeout),
        ("ecsj-04-entt-tombstone", "PASS", args.timeout),
        ("queued-delivery-control", "PASS", args.timeout),
        ("current-format-lifecycle", "PASS", args.timeout),
        ("deliberate-failure", "PROBE_FAILURE", args.timeout),
        ("watchdog-hang", "PROBE_TIMEOUT", 1),
    ]
    for name, kind, timeout in native:
        case = dict(name=name, cmd=[str(binary), name, str(workspace)], type=kind, timeout=timeout)
        scenarios.append(case)
    if args.cases:
        unknown = set(args.cases)-{s["name"] for s in scenarios}
        if unknown:
            parser.error(f"unknown scenarios: {sorted(unknown)}")
        scenarios = [s for s in scenarios if s["name"] in args.cases]
    results = []
    for case in scenarios:
        result = run_scenario(case["cmd"], case["name"], case["timeout"], case["type"], workspace,
                              proof=case.get("proof"), hang_proof=case.get("hang_proof"))
        results.append(result)
        print(f"[{result['status']}] {result['scenario']} (exit {result['exit_code']})")
    report = dict(timestamp=time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), binary=str(binary),
                  workspace=str(workspace), total_scenarios=len(results), summary=summarize(results), results=results)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    print(f"Report: {args.report.resolve()}")
    return 1 if report["summary"]["unexpected_failures"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
