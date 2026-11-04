"""Run real acceptance scenarios separately; known failures remain failures."""
import argparse
import json
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("workspace", type=Path)
    parser.add_argument("--timeout", type=float, default=60)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    workspace = args.workspace.resolve()
    workspace.mkdir(parents=True, exist_ok=True)
    scenarios = ["scene-basic", "scene-copy", "scene-nested", "scene-alias",
                 "assembly-basic", "assembly-editor", "assembly-editor-authored", "assembly-physics",
                 "assembly-nested", "duplicate-physics", "lifecycle"]
    results = []
    for scenario in scenarios:
        started = time.monotonic()
        log = workspace / f"{scenario}.log"
        timed_out = False
        with log.open("w", encoding="utf-8") as output:
            try:
                process = subprocess.run([str(binary), scenario, str(workspace / scenario)],
                                         stdout=output, stderr=subprocess.STDOUT,
                                         timeout=args.timeout, check=False)
                exit_code = process.returncode
            except subprocess.TimeoutExpired:
                timed_out = True
                exit_code = None
        passed = (exit_code == 0 and not timed_out and
                  f"RESULT {scenario} PASS" in log.read_text(encoding="utf-8", errors="replace"))
        result = {"scenario": scenario, "exit_code": exit_code,
                  "passed": passed, "timed_out": timed_out,
                  "elapsed_seconds": round(time.monotonic() - started, 3), "log": str(log)}
        results.append(result)
        print(f"{scenario}: {'PASS' if passed else 'FAIL'} (exit {exit_code})", flush=True)
    report = {"binary": str(binary), "timeout_seconds": args.timeout,
              "passed": sum(r["passed"] for r in results), "total": len(results), "results": results}
    (workspace / "results.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    return 0 if all(r["passed"] for r in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
