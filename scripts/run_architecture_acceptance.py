"""Current-source OPT-10 evidence. Skipped stages and missing native prerequisites stay NOT RUN.

Each invocation writes a new run directory, exact argv/cwd/exit/logs, source identity
and executable hashes. This runner verifies automated gates; native UI observations
and independent review must be recorded separately before architecture acceptance.
"""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
STAGES = ("build", "unit", "native", "export")


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def source_identity():
    paths = subprocess.check_output(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"], cwd=ROOT).split(b"\0")
    hashes = {}
    for raw in paths:
        if not raw:
            continue
        relative = os.fsdecode(raw)
        path = ROOT / relative
        if path.is_file() and (relative.startswith(("Engine/", "Editor/", "Runtime/", "Plugins/", "Tests/", "Templates/", "scripts/", "cmake/")) or relative == "CMakeLists.txt"):
            hashes[relative] = digest(path)
    vendors = subprocess.check_output(["git", "submodule", "status", "--recursive"], cwd=ROOT, text=True)
    identity = json.dumps(hashes, sort_keys=True) + vendors
    return dict(head=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                sha256=hashlib.sha256(identity.encode()).hexdigest(), files=hashes, vendor_revisions=vendors)


class StopAcceptance(RuntimeError):
    pass


class Evidence:
    def __init__(self, directory, stop_on_failure=False):
        self.directory = directory
        self.rows = []
        self.stop_on_failure = stop_on_failure

    def run(self, name, argv, cwd=ROOT, expected=0, timeout=180, env=None, proof=None):
        log = self.directory / (name + ".log")
        row = dict(name=name, command=list(map(str, argv)), cwd=str(cwd), expected_exit=expected,
                   log=str(log), status="NOT RUN", required_proof=proof)
        started = time.monotonic()
        try:
            with log.open("wb") as stream:
                flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
                process = subprocess.Popen(row["command"], cwd=cwd, stdout=stream, stderr=subprocess.STDOUT,
                                           env=env, creationflags=flags)
                try:
                    row["exit"] = process.wait(timeout=timeout)
                    proved = proof is None or proof in log.read_text(encoding="utf-8", errors="replace")
                    row["status"] = "PASS" if process.returncode == expected and proved else "FAIL"
                except subprocess.TimeoutExpired:
                    # Kill only the process tree launched by this row, then reap it.
                    if os.name == "nt":
                        subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                                       stdout=stream, stderr=subprocess.STDOUT, timeout=30, check=False)
                    if process.poll() is None:
                        process.kill()
                    process.wait(timeout=30)
                    row.update(status="FAIL", error="owned process watchdog expired", exit=process.returncode)
        except OSError as error:
            row["error"] = str(error)
        row["seconds"] = round(time.monotonic() - started, 3)
        self.rows.append(row)
        (self.directory / "commands.json").write_text(json.dumps(self.rows, indent=2) + "\n", encoding="utf-8")
        print(f"{row['status']}: {name} ({row.get('exit', 'not launched')})", flush=True)
        if self.stop_on_failure and row["status"] != "PASS":
            raise StopAcceptance(name)
        return row["status"] == "PASS"


def native_cases(build, config):
    tests = build / "Tests" / config
    for backend in ("dx12", "vulkan"):
        for target in ("RHIFoundation", "RHIResource", "RHIIdentity", "RHIRetirement", "RHIState", "RHIFrame", "RHIPresentation"):
            yield f"{target}-{backend}", [tests / f"Axle{target}Tests.exe", backend]
        yield f"error-{backend}", [tests / "AxleRHIErrorTests.exe", backend]
        yield f"error-{backend}-window", [tests / "AxleRHIErrorTests.exe", backend + "-window"]
        for mode in ("base", "targets", "leases", "epoch", "errors"):
            yield f"views-{backend}-{mode}", [tests / "AxleRHIResourceViewTests.exe", backend, mode]
        for mode in ("base", "consumers", "canvas"):
            yield f"service-{backend}-{mode}", [tests / "AxleRenderServiceTests.exe", backend, mode]
        for mode in ("base", "surfaces", "physics", "particles"):
            yield f"preview-{backend}-{mode}", [tests / "AxlePreviewRenderTests.exe", backend, mode]
        for mode in ("loose", "cooked"):
            yield f"runtime-{backend}-{mode}", [tests / "AxleRuntimeRenderTests.exe", backend, mode]
    for backend in ("dx12", "vulkan", "gl", "fallback"):
        yield f"image-{backend}", [tests / "AxleImageSourceTests.exe", backend]
        yield f"primitive-{backend}", [tests / "AxlePrimitiveTests.exe", backend]
    for mode in ("gl", "fallback"):
        yield f"runtime-{mode}", [tests / "AxleRuntimeRenderTests.exe", mode]
    yield "foundation-gl", [tests / "AxleRHIFoundationTests.exe", "gl"]
    yield "foundation-vulkan-window", [tests / "AxleRHIFoundationTests.exe", "vulkan-window"]
    yield "service-gl", [tests / "AxleRenderServiceTests.exe", "gl"]
    yield "canvas-gl", [tests / "AxleRenderServiceTests.exe", "gl", "canvas"]
    editor = build / "Editor" / config / "AxleEditor.exe"
    for backend in ("dx12", "vulkan", "gl"):
        yield f"editor-{backend}", [editor, "--verify-empty-startup", "--rhi=" + backend, "--strict-rhi"]
    for fault in ("CreateDevice", "ProductUI", "ProductRenderer"):
        yield f"editor-fallback-{fault}", [editor, "--verify-empty-startup", "--rhi=dx12", "--verify-startup-rejection=" + fault]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--configurations", nargs="+", choices=("Debug", "Release"), default=["Debug", "Release"])
    parser.add_argument("--stages", nargs="+", choices=STAGES, default=list(STAGES))
    parser.add_argument("--parallel", type=int, default=8)
    parser.add_argument("--keep-going", action="store_true", help="Collect independent gates after a failure; default stops before downstream work")
    args = parser.parse_args()
    build = args.build.resolve()
    directory = args.output.resolve() / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    directory.mkdir(parents=True)
    before = source_identity()
    evidence = Evidence(directory, stop_on_failure=not args.keep_going)
    report = dict(source=before, machine=dict(platform=platform.platform(), cpu=platform.processor(), threads=os.cpu_count()),
                  configurations=args.configurations, stages=args.stages,
                  not_run_stages=[stage for stage in STAGES if stage not in args.stages],
                  external_gates={"native_ui": "NOT RUN by this runner", "independent_review": "NOT RUN by this runner"})
    try:
        evidence.run("architecture", [sys.executable, ROOT / "scripts/check_architecture.py", "--root", ROOT, "--report", directory / "architecture.json"])
        for config in args.configurations:
            if "build" in args.stages:
                if not evidence.run(config + "-build", ["cmake", "--build", build, "--config", config, "--target", "AxleEditor", "AxleRuntime", "AxleAllTests", "--parallel", str(args.parallel)], timeout=2400):
                    report.setdefault("blocked_configurations", []).append(config)
                    continue
            tests = build / "Tests" / config
            editor = build / "Editor" / config / "AxleEditor.exe"
            if "unit" in args.stages:
                evidence.run(config + "-ctest", ["ctest", "--test-dir", build, "-C", config, "--output-on-failure"], timeout=1800)
                evidence.run(config + "-integration", [tests / "AxleTests.exe"], timeout=600)
                evidence.run(config + "-stress-repeat", ["ctest", "--test-dir", build, "-C", config, "-R", "^(ArchitectureStressTests|ScriptLifecycleReviewTests)$", "--repeat", "until-fail:3", "--output-on-failure"], timeout=600)
                evidence.run(config + "-jobs", [sys.executable, ROOT / "scripts/run_jobs_ecs_harness.py", tests / "AxleJobsECSTests.exe", "--workspace", directory / (config + "-jobs"), "--report", directory / (config + "-jobs.json")], timeout=180)
                evidence.run(config + "-simulation", [sys.executable, ROOT / "scripts/run_simulation_product_probe.py", editor,
                             build / "Runtime" / config / "AxleRuntime.exe", "--report", directory / (config + "-simulation.json")], timeout=300)
            if "native" in args.stages:
                for name, command in native_cases(build, config):
                    cwd = editor.parent if name.startswith("editor-") else ROOT
                    evidence.run(config + "-" + name, command, cwd=cwd)
                evidence.run(config + "-color", [sys.executable, ROOT / "scripts/run_render_color_probe.py", tests / "AxlePreviewRenderTests.exe", "--workspace", directory / (config + "-color")], timeout=600)
                evidence.run(config + "-strict-rejection", [editor, "--verify-empty-startup", "--rhi=dx12", "--strict-rhi", "--verify-startup-rejection=CreateDevice"], cwd=editor.parent, expected=1,
                             proof="STARTUP_FAULT_REACHED CreateDevice")
                for backend in ("dx12", "vulkan"):
                    name = config + "-native-plugin-fixture-" + backend
                    ready = evidence.run(name, [tests / "AxlePluginSessionTests.exe", shutil.which("cmake") or "cmake",
                                         sys.executable, ROOT / "scripts/create_project_plugin.py"], timeout=180,
                                         proof="Generated external SDK plugin and Editor session")
                    if ready:
                        text = (directory / (name + ".log")).read_text(encoding="utf-8", errors="replace")
                        match = re.search(r"^Session fixture: (.+)$", text, re.M)
                        projects = list(Path(match[1].strip()).glob("*.axleproj")) if match else []
                        if len(projects) != 1:
                            evidence.rows[-1].update(status="FAIL", error="missing or ambiguous actual native fixture")
                            (directory / "commands.json").write_text(json.dumps(evidence.rows, indent=2) + "\n", encoding="utf-8")
                            print("FAIL: missing or ambiguous actual native fixture", flush=True)
                            if not args.keep_going:
                                raise StopAcceptance(name)
                            continue
                        project = projects[0]
                        report.setdefault("native_plugin_fixtures", []).append(str(project))
                        evidence.run(config + "-plugin-submission-" + backend, [tests / "AxlePluginSubmissionTests.exe", project, backend])
                        evidence.run(config + "-plugin-product-" + backend, [editor, "--project=" + str(project), "--rhi=" + backend,
                                     "--strict-rhi", "--verify-plugin-workflow"], cwd=editor.parent,
                                     proof="Native plugin product workflow: PASS")
            if "export" in args.stages:
                evidence.run(config + "-export", [sys.executable, ROOT / "scripts/verify_runtime_export.py", editor,
                             "--configuration", config, "--output", directory / (config + "-export")], timeout=7200)
    except StopAcceptance as error:
        report["stopped_after_failure"] = str(error)
    report["commands"] = evidence.rows
    report["binary_hashes"] = {str(path): digest(path) for config in args.configurations
        for path in [build / "Editor" / config / "AxleEditor.exe", build / "Runtime" / config / "AxleRuntime.exe",
                     *sorted((build / "Tests" / config).glob("*.exe"))] if path.is_file()}
    report["source_unchanged"] = source_identity()["sha256"] == before["sha256"]
    failed = any(row["status"] == "FAIL" for row in evidence.rows) or not report["source_unchanged"]
    missing = bool(report["not_run_stages"]) or any(row["status"] == "NOT RUN" for row in evidence.rows)
    report["automated_status"] = "FAIL" if failed else "INCOMPLETE" if missing else "PASS"
    report["architecture_acceptance"] = "OPEN: requires recorded native UI and independent review"
    (directory / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"AUTOMATED {report['automated_status']}; evidence: {directory}", flush=True)
    return 1 if failed else 2 if missing else 0


if __name__ == "__main__":
    raise SystemExit(main())
