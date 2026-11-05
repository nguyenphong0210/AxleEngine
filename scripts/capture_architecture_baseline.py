"""Capture reproducible source/vendor and configured CMake dependency evidence."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/"scripts"))
import check_architecture as arch


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def capture(build, configure=True):
    build = build.resolve()
    query = build/".cmake/api/v1/query/codemodel-v2"
    query.parent.mkdir(parents=True, exist_ok=True)
    query.touch()
    if configure:
        subprocess.run(["cmake", "-S", str(ROOT), "-B", str(build)], check=True)
    reply = build/".cmake/api/v1/reply"
    index_path = max(reply.glob("index-*.json"), key=lambda p: p.stat().st_mtime)
    index = json.loads(index_path.read_text(encoding="utf-8"))
    model = json.loads((reply/index["reply"]["codemodel-v2"]["jsonFile"]).read_text(encoding="utf-8"))
    if Path(model["paths"]["source"]).resolve() != ROOT:
        raise ValueError("CMake codemodel belongs to another source tree")
    rules = json.loads((ROOT/"scripts/architecture_rules.json").read_text(encoding="utf-8"))
    source = arch.inspect(ROOT, rules)
    if source["violations"]:
        raise ValueError("source boundary/coverage violations must be resolved before capturing accepted inventory")

    def portable(value):
        path = Path(value)
        if not path.is_absolute():
            return value.replace("\\", "/")
        path = path.resolve()
        if path.is_relative_to(build):
            return "<build>/"+path.relative_to(build).as_posix()
        if path.is_relative_to(ROOT):
            return path.relative_to(ROOT).as_posix()
        return "<external>/"+path.name

    configurations = {}
    for config in model["configurations"]:
        if config["name"] not in {"Debug", "Release"}:
            continue
        targets = {}
        names = {t["id"]: t["name"] for t in config["targets"]}
        for entry in config["targets"]:
            if not entry["name"].startswith("Axle"):
                continue
            data = json.loads((reply/entry["jsonFile"]).read_text(encoding="utf-8"))
            targets[entry["name"]] = dict(
                type=data["type"], source_directory=portable(data["paths"]["source"]),
                # File API dependencies include transitive build prerequisites, not only direct links.
                build_dependencies=sorted(names[d["id"]] for d in data.get("dependencies", []) if d["id"] in names),
                include_directories=sorted({portable(i["path"]) for g in data.get("compileGroups", []) for i in g.get("includes", [])}))
        configurations[config["name"]] = targets
    if set(configurations) != {"Debug", "Release"}:
        raise ValueError("capture requires a multi-config Debug/Release build")
    for config, targets in configurations.items():
        for name in ("AxleJobsECSTests", "AxleAllTests"):
            if {"AxleEditor", "AxleRuntime"} & set(targets[name]["build_dependencies"]):
                raise ValueError(f"{config} {name} unexpectedly builds an application product")

    source_pairs = {}
    for edge in source["source_includes"]:
        key = f"{edge['from_module']} -> {edge['to_module']}"
        item = source_pairs.setdefault(key, dict(include_count=0, files=set()))
        item["include_count"] += 1
        item["files"].add(edge["file"])
    source_pairs = {k: dict(include_count=v["include_count"], files=sorted(v["files"]))
                    for k,v in sorted(source_pairs.items())}
    vendors = []
    for line in git("ls-tree", "HEAD:ThirdParty").splitlines():
        mode, kind, revision, name = line.split(None, 3)
        item = dict(path="ThirdParty/"+name, storage=kind, pinned_revision_or_tree=revision)
        if mode == "160000":
            path = ROOT/"ThirdParty"/name
            item["checkout_revision"] = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
            item["checkout_dirty"] = bool(subprocess.check_output(["git", "-C", str(path), "status", "--porcelain"], text=True).strip())
            item["matches_pin"] = item["checkout_revision"] == revision
        vendors.append(item)
    digest = hashlib.sha256()
    for rel in source["cmake_files"]:
        digest.update(rel.encode()+b"\0"+(ROOT/rel).read_bytes())
    return dict(
        capture_head=git("rev-parse", "HEAD"), production_baseline="95a6c9d; no Engine source changes in baseline corrections",
        working_tree_has_changes=bool(git("status", "--porcelain")),
        vendor_revisions=vendors, cmake_version=index["cmake"]["version"]["string"],
        generator=index["cmake"]["generator"], available_configurations=[c["name"] for c in model["configurations"]],
        configured_graph_scope="Debug/Release Axle targets, actual File API build prerequisites (including transitive) and compile includes",
        configured_graph=configurations, cmake_source_digest_sha256=digest.hexdigest(),
        source_graph_scope=source["coverage"], source_files_scanned=source["source_files_scanned"],
        scanned_cmake_files=source["cmake_files"], source_target_links=source["target_links"],
        source_include_module_graph=source_pairs,
        baseline_allowances=[dict(id=a["id"], owning_package=a["owning_package"],
                                  expiration_condition=a["expiration_condition"], exact_edges=a["edges"])
                             for a in rules["baseline_allowances"]])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT/"build")
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--no-configure", action="store_true", help="Use an already refreshed File API reply")
    args = parser.parse_args()
    data = capture(args.build, configure=not args.no_configure)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(data, indent=2)+"\n", encoding="utf-8")
    print(f"Captured {len(data['source_target_links'])} source link edges and actual Debug/Release graph: {args.report}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
