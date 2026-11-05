#!/usr/bin/env python3
"""
AxleEngine Architecture Boundary Checker (AF-00)
Validates architectural layering, CMake target links, and source #include boundaries
against strict rules and explicit, narrow expiring baseline allowances.
"""

import argparse
import json
import os
import re
import sys
from collections import defaultdict
from pathlib import Path


def load_config(config_path: Path):
    with open(config_path, "r", encoding="utf-8") as fp:
        return json.load(fp)


def map_headers_to_modules(root_dir: Path, module_roots: dict):
    header_map = {}
    for mod_name, rel_path in module_roots.items():
        mod_dir = root_dir / rel_path
        if not mod_dir.exists():
            continue

        inc_dir = mod_dir / "include"
        if inc_dir.exists():
            for p in inc_dir.rglob("*"):
                if p.is_file() and p.suffix in (".h", ".hpp"):
                    rel = p.relative_to(inc_dir).as_posix()
                    # Disambiguate UI vs UI2D
                    assigned_mod = mod_name
                    if mod_name == "UI":
                        if rel.startswith("Axle/UI/") or "/UI/" in rel:
                            assigned_mod = "UI2D"
                        else:
                            assigned_mod = "UI"

                    header_map[rel] = assigned_mod
                    if rel.startswith("Axle/"):
                        header_map[rel[5:]] = assigned_mod
                    elif rel.startswith("AxleSpline/"):
                        header_map[rel[11:]] = assigned_mod
                    elif rel.startswith("AxleTerrain/"):
                        header_map[rel[12:]] = assigned_mod

        src_dir = mod_dir / "src"
        if src_dir.exists():
            for p in src_dir.rglob("*"):
                if p.is_file() and p.suffix in (".h", ".hpp"):
                    assigned_mod = mod_name
                    if mod_name == "UI":
                        if "UI2D" in p.parts:
                            assigned_mod = "UI2D"
                        else:
                            assigned_mod = "UI"
                    header_map[p.name] = assigned_mod

    return header_map


def check_cmake_target_links(root_dir: Path, config: dict, verbose: bool):
    violations = []
    allowances = config.get("baseline_allowances", [])
    allowed_links = config.get("allowed_target_links", {})
    module_roots = config.get("module_roots", {})

    target_to_mod = {
        "AxleCore": "Core", "Axle::Core": "Core",
        "AxlePlatform": "Platform", "Axle::Platform": "Platform",
        "AxleRHI": "RHI", "Axle::RHI": "RHI",
        "AxleRenderer": "Renderer", "Axle::Renderer": "Renderer",
        "AxleAudio": "Audio", "Axle::Audio": "Audio",
        "AxleAsset": "Asset", "Axle::Asset": "Asset",
        "AxleAnimation": "Animation", "Axle::Animation": "Animation",
        "AxleVFX": "VFX", "Axle::VFX": "VFX",
        "AxleProject": "Project", "Axle::Project": "Project",
        "AxleUI2D": "UI2D", "Axle::UI2D": "UI2D",
        "AxleUI": "UI", "Axle::UI": "UI",
        "AxleScene": "Scene", "Axle::Scene": "Scene",
        "AxleSpline": "Spline", "Axle::Spline": "Spline",
        "AxleTerrain": "Terrain", "Axle::Terrain": "Terrain",
        "AxleScripts": "Editor",
        "AxleEditor": "Editor",
        "AxleRuntime": "Runtime"
    }

    for mod_name, rel_path in module_roots.items():
        cm_path = root_dir / rel_path / "CMakeLists.txt"
        if not cm_path.exists():
            continue

        content = cm_path.read_text(encoding="utf-8", errors="ignore")
        for m in re.finditer(r"target_link_libraries\s*\(\s*(\w+)\s+(.*?)\)", content, re.DOTALL):
            target_name = m.group(1)
            raw_links = m.group(2).split()
            links = [l for l in raw_links if l not in ("PUBLIC", "PRIVATE", "INTERFACE")]

            allowed_set = set(allowed_links.get(mod_name, []))

            for link in links:
                if link in allowed_set:
                    continue

                target_mod = target_to_mod.get(link)
                # Check if covered by an allowance
                rel_cm = cm_path.relative_to(root_dir).as_posix()
                allowed = False
                for allow in allowances:
                    if allow["from_module"] == mod_name and (target_mod is None or allow["to_module"] == target_mod):
                        for f in allow.get("files", []):
                            if f == rel_cm or f.endswith(rel_cm) or rel_cm.endswith(f):
                                allowed = True
                                break
                    if allowed:
                        break

                if not allowed:
                    violations.append({
                        "type": "CMakeLink",
                        "from_module": mod_name,
                        "file": rel_cm,
                        "details": f"Target '{target_name}' in {mod_name} links unallowed target '{link}'"
                    })
                elif verbose:
                    print(f"[ALLOWED LINK] {mod_name} -> {link} in {rel_cm}")

    return violations


def check_source_includes(root_dir: Path, config: dict, header_map: dict, verbose: bool):
    violations = []
    allowances = config.get("baseline_allowances", [])
    forbidden_rules = config.get("forbidden_include_rules", [])
    module_roots = config.get("module_roots", {})

    forbidden_map = defaultdict(set)
    for rule in forbidden_rules:
        frm = rule["from"]
        for to in rule["forbidden_to"]:
            forbidden_map[frm].add(to)

    include_regex = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')

    files_scanned = 0

    for mod_name, rel_path in module_roots.items():
        mod_dir = root_dir / rel_path
        if not mod_dir.exists():
            continue

        for p in mod_dir.rglob("*"):
            if not p.is_file() or p.suffix not in (".h", ".hpp", ".cpp"):
                continue

            if "build" in p.parts:
                continue

            # UI2D disambiguation for source files under Engine/UI
            current_mod = mod_name
            if mod_name == "UI" and "UI2D" in p.parts:
                current_mod = "UI2D"

            files_scanned += 1
            rel_file = p.relative_to(root_dir).as_posix()

            try:
                lines = p.read_text(encoding="utf-8", errors="ignore").splitlines()
            except Exception:
                continue

            for idx, line in enumerate(lines, 1):
                m = include_regex.match(line)
                if not m:
                    continue
                inc = m.group(1).replace("\\", "/")

                target_mod = None
                if inc in header_map:
                    target_mod = header_map[inc]
                elif inc.startswith("Axle/"):
                    sub = inc[5:]
                    if sub in header_map:
                        target_mod = header_map[sub]
                elif inc.startswith("AxleSpline/"):
                    sub = inc[11:]
                    if sub in header_map:
                        target_mod = header_map[sub]
                elif inc.startswith("AxleTerrain/"):
                    sub = inc[12:]
                    if sub in header_map:
                        target_mod = header_map[sub]

                if not target_mod or target_mod == current_mod:
                    continue

                if target_mod in forbidden_map[current_mod]:
                    # Check if matching allowance exists
                    allowed = False
                    for allow in allowances:
                        if allow["from_module"] == current_mod and allow["to_module"] == target_mod:
                            for allowed_file in allow.get("files", []):
                                if allowed_file == rel_file or rel_file.endswith(allowed_file) or allowed_file.endswith(rel_file):
                                    allowed = True
                                    break
                        if allowed:
                            break

                    if not allowed:
                        violations.append({
                            "type": "SourceInclude",
                            "from_module": current_mod,
                            "to_module": target_mod,
                            "file": f"{rel_file}:{idx}",
                            "details": f"Forbidden include: #{line.strip()} (resolves to module {target_mod})"
                        })
                    elif verbose:
                        print(f"[ALLOWED INCLUDE] {rel_file}:{idx} ({current_mod} -> {target_mod})")

    return violations, files_scanned


def main():
    default_config = Path("scripts/architecture_rules.json")
    if not default_config.exists():
        default_config = Path(__file__).resolve().parent / "architecture_rules.json"

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--config", type=Path, default=default_config)
    parser.add_argument("--fixture", type=Path, default=None)
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    root_dir = (args.fixture if args.fixture else args.root).resolve()
    config_path = args.config.resolve()

    if not config_path.exists():
        print(f"ERROR: Configuration file not found at {config_path}", file=sys.stderr)
        return 2

    config = load_config(config_path)

    # Header map should be derived from the main repository tree
    repo_root = config_path.parent.parent if (config_path.parent.parent / "Engine").exists() else root_dir
    header_map = map_headers_to_modules(repo_root, config.get("module_roots", {}))

    cmake_violations = check_cmake_target_links(root_dir, config, args.verbose)
    inc_violations, files_scanned = check_source_includes(root_dir, config, header_map, args.verbose)

    total_violations = cmake_violations + inc_violations

    print(f"=== AxleEngine Architecture Check ===")
    print(f"Root: {root_dir}")
    print(f"Files scanned: {files_scanned}")
    print(f"Known baseline allowances: {len(config.get('baseline_allowances', []))}")
    print(f"Unallowed violations found: {len(total_violations)}")

    if total_violations:
        print("\nVIOLATIONS:")
        for v in total_violations:
            print(f"  [{v['type']}] {v['file']}: {v['details']}")
        return 1

    print("\nRESULT: Architecture checks PASS (all dependencies conform to rules or narrow allowances).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
