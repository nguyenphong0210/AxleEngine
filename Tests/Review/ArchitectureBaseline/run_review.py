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


def prepare_queued_control(folder):
    """Copy the reviewed probe, changing only its actor to an existing valid queue path."""
    folder.mkdir(parents=True, exist_ok=True)
    source = (ROOT / "Tests/src/JobsECSTestMain.cpp").read_text(encoding="utf-8")
    replacements = {
        "std::thread ext([&] {": "auto ext = Axle::JobSystem::Dispatch([&] {",
        "    ext.join();": "    while (!ext.IsComplete()) std::this_thread::yield();\n    ext.Wait();",
        "    // Pump main thread queue on actual main thread\n    Axle::JobSystem::RunPinnedTasks();":
            "    // Pump main thread queue on actual main thread\n    Axle::JobSystem::RunPinnedTasks();\n"
            '    std::cout << "review_after_pump incorrectlyMain=" << incorrectlyMain.load()\n'
            '              << " calledBeforePump=" << calledBeforePump.load()\n'
            '              << " callbackOnMain=" << callbackOnMain.load() << std::endl;',
    }
    for original, replacement in replacements.items():
        if source.count(original) != 1:
            raise ValueError("Reviewed probe changed; inspect the new implementation before regenerating this control")
        source = source.replace(original, replacement, 1)
    (folder / "QueuedIdentity.cpp").write_text(source, encoding="utf-8")
    cmake = (
        "if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)\n"
        f'  add_executable(AxleOPT01QueuedReview EXCLUDE_FROM_ALL "{(folder / "QueuedIdentity.cpp").as_posix()}")\n'
        "  target_compile_features(AxleOPT01QueuedReview PRIVATE cxx_std_20)\n"
        "  if(MSVC)\n    target_compile_options(AxleOPT01QueuedReview PRIVATE /bigobj /FS)\n  endif()\n"
        "  target_link_libraries(AxleOPT01QueuedReview PRIVATE Axle::Core Axle::Physics Axle::Scene)\n"
        f'  set_target_properties(AxleOPT01QueuedReview PROPERTIES RUNTIME_OUTPUT_DIRECTORY "{(folder / "bin").as_posix()}")\n'
        "endif()\n"
    )
    hook = folder / "inject.cmake"
    hook.write_text(cmake, encoding="utf-8")
    print(f"Generated review-only CMake hook: {hook}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path, default=ROOT / "build/opt01-review/adversarial")
    parser.add_argument("--queued-binary", type=Path, help="Optional built queued-delivery native review control")
    parser.add_argument("--prepare-queued-control", type=Path, help="Generate the opt-in native control and CMake hook, then exit")
    args = parser.parse_args()
    if args.prepare_queued_control:
        prepare_queued_control(args.prepare_queued_control.resolve())
        return 0
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

    if args.queued_binary:
        result = subprocess.run([str(args.queued_binary.resolve()), "ecsj-02-thread-identity"],
                                capture_output=True, text=True, timeout=15)
        log = workspace / "queued-delivery-native-control.log"
        log.write_text(result.stdout + result.stderr, encoding="utf-8")
        record("identity-probe-rejects-correct-deferred-control", "correct queued delivery is accepted",
               f"exit {result.returncode}", result.returncode == 0, log=str(log),
               evidence_kind="native production queue; actor is a registered enki worker; original probe predicate unchanged")

    report = {"reviewed_implementation": "95a6c9d", "workspace": str(workspace),
              "results": results, "failed_contracts": sum(not r["contract_satisfied"] for r in results)}
    report_path = workspace / "results.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Report: {report_path}")
    return 1 if report["failed_contracts"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
