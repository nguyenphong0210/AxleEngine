"""Run serialized real-product acceptance; requires already rebuilt binaries."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import subprocess
import time


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", default="build")
    parser.add_argument("--config", choices=("Debug", "Release"), required=True)
    parser.add_argument("--output")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = (root / args.build).resolve()
    output = Path(args.output).resolve() if args.output else build / "c11_07_products" / args.config
    output.mkdir(parents=True, exist_ok=True)
    cases = []

    def persist():
        (output / "results.json").write_text(json.dumps({"config": args.config, "cases": cases}, indent=2), encoding="utf-8")

    def run(name, command, *, expected=0, markers=(), cwd=root, timeout=120):
        entry = {"name": name, "command": [str(v) for v in command], "cwd": str(cwd), "expected_exit": expected}
        log = output / (name + ".log")
        begin = time.monotonic_ns()
        try:
            with log.open("wb") as stream:
                result = subprocess.run(entry["command"], cwd=cwd, stdout=stream, stderr=subprocess.STDOUT, timeout=timeout)
            text = log.read_text(encoding="utf-8", errors="replace")
            exit_ok = result.returncode == expected
            entry.update(exit_code=result.returncode, status="PASS" if exit_ok and all(m in text for m in markers) else "FAIL")
            if expected != 0 and "Initializing Axle RHI" in text:
                entry["status"] = "FAIL"
                entry["error"] = "Strict/malformed policy reached native initialization"
            if "[NRI Error]" in text or "[GL API Error]" in text:
                entry["status"] = "FAIL"
                entry["error"] = "Native validation output contains errors"
        except FileNotFoundError as exc:
            text = ""
            entry.update(status="NOT RUN", error=str(exc))
        except subprocess.TimeoutExpired as exc:
            text = log.read_text(encoding="utf-8", errors="replace")
            entry.update(status="FAIL", error=f"Timed out after {exc.timeout}s")
        entry.update(elapsed_ns=time.monotonic_ns() - begin, log=str(log))
        cases.append(entry)
        persist()
        print(f"{args.config} {name}: {entry['status']}", flush=True)
        if entry["status"] != "PASS":
            print(text[-2000:], flush=True)
        return text, entry["status"] == "PASS"

    test = lambda name: build / "Tests" / args.config / (name + ".exe")
    editor = build / "Editor" / args.config / "AxleEditor.exe"
    runtime = build / "Runtime" / args.config / "AxleRuntime.exe"
    feature_flags = ["--render-thread=worker", "--render-parallel-recording=on", "--render-transient-aliasing=on"]
    for product, binary in (("editor", editor), ("runtime", runtime)):
        for index, flag in enumerate(feature_flags + ["--render-thread=typo"]):
            run(f"{product}_strict_{index}", [binary, flag, "--strict-render-features"], expected=1,
                markers=("deferred" if index < 3 else "Invalid or unknown",))
    for backend in ("dx12", "vulkan", "gl"):
        flags = [f"--rhi={backend}", "--strict-rhi", "--rhi-validation"] + feature_flags
        for mode, marker in (("empty-startup", "Editor empty startup verification: PASS"),
                             ("simulation", "PRODUCT_SIM_PASS Editor rate=144")):
            run(f"editor_{backend}_{mode}", [editor, "--empty-project", f"--verify-{mode}"] + flags,
                markers=("RENDER_PRODUCT_POLICY mode=synchronous", "fallback=explicit", marker), cwd=editor.parent)
        run(f"service_{backend}", [test("AxleRenderServiceTests"), backend], markers=("OPT05 RenderService PASS",))
        run(f"canvas_{backend}", [test("AxleRenderServiceTests"), backend, "canvas"], markers=("OPT05 RenderService PASS",))
        if backend != "gl":
            run(f"thumbnail_{backend}", [test("AxleRenderServiceTests"), backend, "consumers"], markers=("OPT05 RenderService PASS",))
        for suffix in ("default", "physics", "particles", "surfaces"):
            command = [test("AxlePreviewRenderTests"), backend] + ([] if suffix == "default" else [suffix])
            run(f"preview_{backend}_{suffix}", command, markers=("OPT05 preview PASS",))
        for packaging in ("loose", "cooked"):
            command = [test("AxleRuntimeRenderTests"), backend] + (["cooked"] if packaging == "cooked" else [])
            text, passed = run(f"runtime_fixture_{backend}_{packaging}", command, markers=("OPT05 Runtime PASS",))
            prefix = "OPT05_RUNTIME_FIXTURE="
            lines = [line[len(prefix):].strip() for line in text.splitlines() if line.startswith(prefix)]
            if passed and len(lines) == 1:
                fixture = Path(lines[0])
                run(f"runtime_exe_{backend}_{packaging}", [runtime, f"--project={fixture / 'Project.axleproj'}", "--frames=8"] + flags,
                    markers=("RENDER_PRODUCT_POLICY mode=synchronous", "fallback=explicit"), cwd=fixture)
            else:
                cases.append({"name": f"runtime_exe_{backend}_{packaging}", "status": "NOT RUN", "error": "Runtime fixture prerequisite failed"})
                persist()
    run("runtime_backend_fallback", [test("AxleRuntimeRenderTests"), "fallback"], markers=("OPT05 Runtime PASS",))
    failures = [c for c in cases if c["status"] != "PASS"]
    print(f"{len(cases) - len(failures)}/{len(cases)} PASS; results: {output / 'results.json'}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
