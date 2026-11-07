"""Compare real Lit scene pixels on D3D12, Vulkan and OpenGL."""
import argparse
import csv
import json
import os
from pathlib import Path
import re
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--workspace", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = args.workspace.resolve()
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, AXLE_COLOR_CAPTURE_DIR=str(output))
    env["VK_LAYER_ENABLES"] = "VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT"
    results, captures = [], {}
    for backend in ("dx12", "vulkan", "gl"):
        log = output / f"{backend}.log"
        started = time.time()
        with log.open("wb") as stream:
            try:
                code = subprocess.run(
                    [str(args.binary.resolve()), backend, "surfaces"], cwd=root,
                    env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=180,
                ).returncode
            except subprocess.TimeoutExpired:
                code = "TIMEOUT"
        content = log.read_text(encoding="utf-8", errors="replace")
        issues = re.findall(r"^.*(?:\[NRI Error\]|\[GL API Error\]|Validation Error:|SYNC-HAZARD).*$", content, re.M)
        if backend == "vulkan" and "Synchronization" not in content:
            issues.append("Vulkan synchronization validation was not confirmed")
        capture = output / f"{backend}.csv"
        if capture.exists() and capture.stat().st_mtime >= started - 1:
            with capture.open(encoding="utf-8") as stream:
                captures[backend] = {row[0]: list(map(float, row[1:])) for row in csv.reader(stream)}
        else:
            issues.append("This run did not produce a fresh pixel capture")
        results.append(dict(backend=backend, exit=code, issues=issues, passed=code == 0 and not issues, log=str(log)))
    comparisons = []
    expected = {"no_light", "ambient", "environment", "emission", "point", "point_falloff2", "spot", "removed_lights"}
    expected |= {f"{kind}{index}" for kind in ("diffuse", "metal") for index in range(5)}
    for backend, values in captures.items():
        if set(values) != expected:
            results.append(dict(backend=backend, passed=False, issues=["Incomplete pixel chart"]))
    if set(captures) == {"dx12", "vulkan", "gl"} and all(set(v) == expected for v in captures.values()):
        for case, reference in captures["dx12"].items():
            for backend in ("vulkan", "gl"):
                actual = captures[backend][case]
                delta = max(abs(a - b) for a, b in zip(reference, actual))
                passed = delta <= 2.5
                if case in ("no_light", "removed_lights"):
                    passed &= reference == actual == [0, 0, 0]
                comparisons.append(dict(case=case, backend=backend, reference=reference, actual=actual, max_rgb_delta=delta, passed=passed))
    passed = all(row["passed"] for row in results) and len(comparisons) == 36 and all(row["passed"] for row in comparisons)
    report = dict(passed=passed, runs=results, comparisons=comparisons)
    (output / "results.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    maximum = max((row["max_rgb_delta"] for row in comparisons), default=0)
    print(f"Render color probe: {'PASS' if passed else 'FAIL'}; {sum(r['passed'] for r in comparisons)}/36 comparisons; max RGB delta {maximum:.4f}/255")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
