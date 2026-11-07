"""Run the real Editor/Runtime simulation probes and compare their native traces."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess


TRACE = re.compile(r"PRODUCT_SIM_PASS (Editor|Runtime) rate=(\d+) ticks=(\d+) held=(\d+) x=([-+0-9.eE]+)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("editor", type=Path)
    parser.add_argument("runtime", type=Path)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=120.0)
    args = parser.parse_args()
    args.report.parent.mkdir(parents=True, exist_ok=True)
    evidence = {"status": "FAIL", "products": {}, "pose_tolerance": 1e-4}
    errors = []
    for product, executable in (("Editor", args.editor), ("Runtime", args.runtime)):
        executable = executable.resolve()
        log = args.report.with_name(args.report.stem + "-" + product.lower() + ".log")
        command = [str(executable), "--verify-simulation"]
        if product == "Editor":
            command.append("--rhi=gl")
        entry = {"command": command, "log": str(log), "traces": {}}
        evidence["products"][product] = entry
        if not executable.is_file():
            errors.append(f"{product}: executable is unavailable (NOT RUN)")
            continue
        try:
            with log.open("wb") as output:
                result = subprocess.run(command, cwd=executable.parent, stdout=output,
                                        stderr=subprocess.STDOUT, timeout=args.timeout, check=False)
            entry["exit_code"] = result.returncode
        except (OSError, subprocess.TimeoutExpired) as error:
            errors.append(f"{product}: {error}")
            continue
        rows = TRACE.findall(log.read_text(encoding="utf-8", errors="replace"))
        if result.returncode != 0 or len(rows) != 3:
            errors.append(f"{product}: exit {result.returncode}, {len(rows)} valid phase traces; see {log}")
            continue
        for source, rate, ticks, held, position in rows:
            if source != product or rate in entry["traces"] or int(ticks) != 60 or int(held) != 60:
                errors.append(f"{product}: invalid or duplicate trace")
            entry["traces"][rate] = {"ticks": int(ticks), "held": int(held), "x": float(position)}
        if set(entry["traces"]) != {"30", "60", "144"}:
            errors.append(f"{product}: missing required frame partition")
    for rate in ("30", "60", "144"):
        editor = evidence["products"]["Editor"]["traces"].get(rate)
        runtime = evidence["products"]["Runtime"]["traces"].get(rate)
        if editor and runtime and abs(editor["x"] - runtime["x"]) > evidence["pose_tolerance"]:
            errors.append(f"{rate} Hz: Editor/Runtime physical state mismatch")
    evidence["errors"] = errors
    evidence["status"] = "FAIL" if errors else "PASS"
    args.report.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print(f"SIMULATION_PRODUCT_PROBE {evidence['status']}: {len(errors)} errors; {args.report}")
    for error in errors:
        print(error)
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
