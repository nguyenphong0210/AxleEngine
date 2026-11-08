"""Real OPT-09 export/build/import/player evidence; never substitutes fake players."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import uuid
import shutil
from create_project_plugin import create

ROOT = Path(__file__).resolve().parents[1]
CASES = {"empty": [], "vehicle": ["axle.vehicle"], "spline": ["axle.spline"],
         "terrain": ["axle.terrain"], "project": ["axle.vehicle", "project.mover"]}

def main():
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("editor", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cases", nargs="+", choices=CASES, default=list(CASES))
    parser.add_argument("--configuration", choices=["Debug", "Release"])
    args = parser.parse_args()
    editor = args.editor.resolve()
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    results = []
    def run(name, command, cwd, expected=0, timeout=1200):
        log = output / (name + ".log")
        with log.open("wb") as stream:
            process = subprocess.run([str(c) for c in command], cwd=cwd, stdout=stream,
                stderr=subprocess.STDOUT, timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)
        text = log.read_text(encoding="utf-8", errors="replace")
        results.append({"case": name, "exit": process.returncode, "expected": expected, "log": str(log)})
        (output / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
        print(name, process.returncode, flush=True)
        if process.returncode != expected:
            raise RuntimeError(f"{name}: expected {expected}, got {process.returncode}: {text[-5000:]}")
        return text
    dumpbin = sorted(Path(os.environ["ProgramFiles"]).glob("Microsoft Visual Studio/2022/*/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe"))[-1]
    for case in args.cases:
        # External Unicode project exercises absolute paths and ships outside the checkout.
        project = Path(tempfile.gettempdir()) / "AxleEngine" / ("OPT09 đồ " + case + " " + uuid.uuid4().hex[:8])
        assets = project / "assets"; assets.mkdir(parents=True)
        scene = "Scene: ExportFixture\nEntities:\n  - Entity: 42\n    TagComponent:\n      Tag: Authored export content\n    TransformComponent:\n      Translation: [0, 0, 0]\n      Rotation: [1, 0, 0, 0]\n      Scale: [1, 1, 1]\n"
        if "axle.vehicle" in CASES[case]:
            scene = (ROOT / "Plugins/Vehicle/Content/Scenes/SampleVehicle.axlescene").read_text(encoding="utf-8")
        if "project.mover" in CASES[case]:
            key = "project.mover/Mover".encode().hex()
            payload = '2 "Export mover"'.encode().hex()
            scene += f"  - Entity: 42\n    TagComponent:\n      Tag: Export mover\n    AxlePlugin_{key}:\n      Schema: 1\n      Required: true\n      EntityReferences: false\n      Payload: {payload}\n"
            scene += "    NativeScriptComponent:\n      ScriptName: StaticExportScript\n"
            (project / "Scripts").mkdir()
            (project / "Scripts/StaticExportScript.hpp").write_text('#pragma once\n#include <Axle/ScriptableEntity.hpp>\n#include <Axle/ScriptRegistry.hpp>\n#include <Axle/Log.hpp>\nclass StaticExportScript : public Axle::ScriptableEntity { public: void OnCreate() override { AXLE_INFO("Static export script created"); } };\nAXLE_REGISTER_SCRIPT(StaticExportScript)\n', encoding="utf-8")
        (assets / "Start.axlescene").write_text(scene, encoding="utf-8")
        selected = "".join("    - " + p + "\n" for p in CASES[case])
        config = project / "Project.axleproj"
        config.write_text("Project:\n  SchemaVersion: 1\n  Name: ExportFixture\n  StartScene: Start.axlescene\n  AssetDirectory: assets\n  Plugins:\n" + selected, encoding="utf-8")
        if "project.mover" in CASES[case]:
            create(config, "project.mover", "Mover", "Project mover")
        exported = project / "Export"
        command = [editor, "--project=" + str(config), "--export", exported]
        if args.configuration:
            command.append("--runtime-configuration=" + args.configuration)
        export_log = run(case + "-export", command, ROOT)
        product_build = Path(re.search(r"Runtime product build: (.+?); generation:", export_log)[1])
        solution = (product_build / "AxleEngine.sln").read_text(encoding="utf-8-sig")
        assert not re.search(r'"(?:AxleUI|AxleEditor|AxleEditorHost|AxleScripts|AxleSplineRuntime|AxleTerrainRuntime|\w+Editor)"', solution)
        selected_modules = {"empty": [], "vehicle": ["VehicleRuntime"], "spline": ["SplineRuntime"],
                            "terrain": ["SplineRuntime", "TerrainRuntime"], "project": ["VehicleRuntime", "Mover_Runtime"]}[case]
        for name in ("VehicleRuntime", "SplineRuntime", "TerrainRuntime", "Mover_Runtime"):
            assert ('"' + name + '"' in solution) == (name in selected_modules), (case, name)
        runtime_project = (product_build / "Runtime/AxleRuntime.vcxproj").read_text(encoding="utf-8-sig")
        assert not re.search(r"AxleUI\.lib|AxleEditor|imgui|AxleScripts\.dll", runtime_project, re.I)
        player = exported / "AxleRuntime.exe"
        assert player.is_file() and (exported / "Data.axlepak").is_file()
        receipt = (exported / "AxleRuntime.build.receipt").read_text(encoding="utf-8")
        configuration = re.search(r"^configuration=(.+)$", receipt, re.M)[1]
        if args.configuration:
            assert configuration == args.configuration
        linkmap = product_build / "Runtime" / configuration / "AxleRuntime.map"
        symbols = linkmap.read_text(encoding="utf-8", errors="replace")
        for module in ("VehicleRuntime", "SplineRuntime", "TerrainRuntime", "Mover_Runtime"):
            symbol = "axle_static_" + module.encode().hex() + "_start"
            assert (symbol in symbols) == (module in selected_modules), (case, module)
        shutil.copy2(linkmap, output / (case + "-link.map"))
        assert not list(exported.glob("Axle*Host.dll")) and not (exported / "AxleScripts.dll").exists()
        text = run(case + "-packaged-preflight", [player, "--check-content"], exported, timeout=60)
        assert "Compiled Runtime content preflight passed" in text
        imported = run(case + "-imports", [dumpbin, "/dependents", player], exported)
        assert not re.search(r"Axle\w*\.dll|Mover_\w+\.dll|Vehicle\w+\.dll|Spline\w+\.dll|Terrain\w+\.dll|imgui", imported, re.I)
        # Detached cwd and package lookup prove the source checkout is not an asset fallback.
        for backend in ("dx12", "vulkan"):
            text = run(case + "-" + backend, [player, "--rhi=" + backend, "--no-fallback", "--frames=5"], exported, timeout=120)
            assert "Successfully loaded startup scene" in text and "Simulation lifecycle started successfully" in text
            if case == "project":
                assert "Static export script created" in text
        # Missing package/project must fail before GPU startup.
        pak = exported / "Data.axlepak"; hidden = exported / "Data.axlepak.hidden"
        pak.rename(hidden)
        try:
            text = run(case + "-missing-package", [player, "--rhi=dx12", "--frames=5"], exported, expected=1, timeout=60)
            assert "project is missing" in text
        finally:
            hidden.rename(pak)
        results.append({"case": case + "-artifacts", "project": str(config), "export": str(exported)})
        (output / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    print("Selected static Runtime exports and detached native startup: PASS", flush=True)

if __name__ == "__main__":
    main()
