"""Conservative source dependency checks for the declared Engine module boundary.

CMake policy is a union of source-declared link edges, including conditional branches,
not a configured product graph. Literal/set/list APPEND dependency arguments are
supported; unresolved variables, generator expressions and dynamic dependency
construction fail coverage. All nested CMakeLists/.cmake files inside declared
module roots (plus root CMake/cmake/) are inspected, including inactive files.
"""
from __future__ import annotations
import argparse
from collections import defaultdict
import json
from pathlib import Path
import re
import sys

REPO = Path(__file__).resolve().parents[1]
TOKEN = re.compile(r'\[(=*)\[.*?\]\1\]|"(?:\\.|[^"\\])*"|\#[^\n]*|[()]|[^\s()"]+', re.S)
VARIABLE = re.compile(r"\$\{([^{}]+)\}")


class CoverageError(ValueError):
    pass


def commands(path):
    text = path.read_text(encoding="utf-8-sig")
    tokens = [(m.group(), text.count("\n", 0, m.start())+1) for m in TOKEN.finditer(text)
              if not m.group().startswith("#")]
    index = 0
    while index < len(tokens)-1:
        name, line = tokens[index]
        if not re.fullmatch(r"\w+", name) or tokens[index+1][0] != "(":
            index += 1
            continue
        depth, index, args = 1, index+2, []
        while index < len(tokens) and depth:
            token = tokens[index][0]
            index += 1
            if token == "(":
                depth += 1
            elif token == ")":
                depth -= 1
            if depth:
                if token.startswith('"'):
                    token = token[1:-1].replace('\\\"', '"')
                args.append(token)
        if depth:
            raise CoverageError(f"{path}: unbalanced CMake command")
        yield name.lower(), args, line


def module_for(path, root, config):
    path = path.resolve()
    for prefix, module in config.get("path_module_overrides", {}).items():
        if path.is_relative_to((root/prefix).resolve()):
            return module
    candidates = [(len(Path(rel).parts), module) for module, rel in config["module_roots"].items()
                  if path.is_relative_to((root/rel).resolve())]
    return max(candidates)[1] if candidates else None


def target_modules(root, config, parsed):
    mapping = {f"Axle{name}": name for name in config["module_roots"]}
    mapping.update({f"Axle::{name}": name for name in config["module_roots"]})
    mapping["AxleScripts"] = "Editor"
    mapping.update({"AxleSDK::Runtime": "PluginSDK", "AxleSDK::Editor": "PluginSDK"})
    for path, entries in parsed.items():
        for name, args, _ in entries:
            if name in {"add_library", "add_executable", "axle_add_plugin_module"} and args and "$" not in args[0]:
                module = module_for(path, root, config)
                if module:
                    mapping.setdefault(args[0], module)
    # Aliases must follow their target's ownership, never the file adding a link.
    for _ in range(3):
        for entries in parsed.values():
            for name, args, _ in entries:
                if name in {"add_library", "add_executable"} and len(args) >= 3 and args[1] == "ALIAS":
                    if args[2] in mapping:
                        mapping[args[0]] = mapping[args[2]]
    return mapping


def scan_cmake(root, config):
    files = set()
    for relative in config["module_roots"].values():
        directory = root/relative
        if directory.exists():
            files.update(directory.rglob("CMakeLists.txt"))
            files.update(directory.rglob("*.cmake"))
    if (root/"CMakeLists.txt").is_file():
        files.add(root/"CMakeLists.txt")
    if (root/"cmake").exists():
        files.update((root/"cmake").rglob("*.cmake"))
    parsed = {p: list(commands(p)) for p in sorted(files)}
    mapping = target_modules(root, config, parsed)
    definitions = defaultdict(list)
    for entries in parsed.values():
        for name, args, _ in entries:
            if name == "set" and args:
                values = args[1:]
                if "CACHE" in values:
                    values = values[:values.index("CACHE")]
                definitions[args[0]].extend(values)
            elif name == "list" and len(args) >= 2 and args[0] == "APPEND":
                definitions[args[1]].extend(args[2:])

    def expand(token, stack=()):
        if "$<" in token or "$ENV{" in token:
            raise CoverageError(f"unsupported dynamic dependency: {token}")
        match = VARIABLE.search(token)
        if not match:
            return token.split(";")
        var = match[1]
        if var in stack or var not in definitions or not definitions[var]:
            raise CoverageError(f"unresolved/cyclic dependency variable: {var}")
        output = []
        for value in definitions[var]:
            for expanded in expand(value, stack+(var,)):
                output.extend(expand(token[:match.start()]+expanded+token[match.end():], stack+(var,)))
        if len(output) > 256:
            raise CoverageError("dependency expansion exceeds conservative limit")
        return list(dict.fromkeys(output))

    # Follow literal/set-based includes and subdirectories outside the initial roots too.
    # Vendor/test composition is explicitly outside this Engine policy and pinned separately.
    reach_errors = []
    queue = list(parsed)
    visited = set()
    while queue:
        path = queue.pop()
        if path in visited:
            continue
        visited.add(path)
        for name, args, line in parsed[path]:
            if name == "cmake_language":
                reach_errors.append(dict(kind="CoverageError", file=path.relative_to(root).as_posix(),
                                         line=line, details="dynamic CMake evaluation requires configured-graph support"))
            if name not in {"include", "add_subdirectory"} or not args:
                continue
            try:
                token = args[0].replace("${CMAKE_SOURCE_DIR}", root.as_posix()).replace(
                    "${CMAKE_CURRENT_SOURCE_DIR}", path.parent.as_posix()).replace(
                    "${CMAKE_CURRENT_LIST_DIR}", path.parent.as_posix())
                for value in expand(token):
                    candidate = (path.parent/value).resolve()
                    if not candidate.is_relative_to(root):
                        raise CoverageError(f"dependency composition outside policy root: {value}")
                    relative = candidate.relative_to(root)
                    if relative.parts and relative.parts[0] in {"ThirdParty", "Tests"}:
                        continue
                    candidate = candidate/"CMakeLists.txt" if name == "add_subdirectory" else candidate
                    if name == "include" and not candidate.suffix:
                        candidate = candidate.with_suffix(".cmake")
                    if not candidate.is_file():
                        raise CoverageError(f"unresolved dependency composition: {value}")
                    if candidate not in parsed:
                        parsed[candidate] = list(commands(candidate))
                        for command, values, _ in parsed[candidate]:
                            if command == "set" and values:
                                definitions[values[0]].extend(values[1:])
                        queue.append(candidate)
            except CoverageError as error:
                reach_errors.append(dict(kind="CoverageError", file=path.relative_to(root).as_posix(),
                                         line=line, details=str(error)))
    mapping = target_modules(root, config, parsed)
    edges, errors = [], reach_errors
    for path, entries in parsed.items():
        rel = path.relative_to(root).as_posix()
        scope = []
        for name, args, line in entries:
            if name in {"function", "macro", "foreach", "while"}:
                scope.append(name)
            elif name in {"endfunction", "endmacro", "endforeach", "endwhile"}:
                if scope:
                    scope.pop()
            dependency_property = name in {"set_property", "set_target_properties"} and any(
                a in {"LINK_LIBRARIES", "INTERFACE_LINK_LIBRARIES"} for a in args)
            if name == "axle_add_plugin_module":
                if scope or len(args)<3 or args[0] not in mapping or args[1] not in {"runtime", "editor"}:
                    errors.append(dict(kind="CoverageError",file=rel,line=line,details="unresolved native module declaration"))
                else:
                    for host in (["Runtime", "Editor"] if args[1]=="editor" else ["Runtime"]):
                        edges.append(dict(kind="CMakeLink",file=rel,line=line,from_module=mapping[args[0]],from_target=args[0],
                                          to=f"AxleSDK::{host}",to_module="PluginSDK"))
                continue
            if name != "target_link_libraries" and not dependency_property:
                continue
            try:
                if dependency_property or scope:
                    raise CoverageError("dynamic/property-based dependency construction requires explicit support")
                if not args:
                    raise CoverageError("empty target_link_libraries")
                owners = expand(args[0])
                if len(owners) != 1 or owners[0] not in mapping:
                    raise CoverageError(f"unknown dependency owner: {args[0]}")
                target = owners[0]
                module = mapping[target]
                for token in args[1:]:
                    if token in {"PUBLIC", "PRIVATE", "INTERFACE", "debug", "optimized", "general"}:
                        continue
                    for link in expand(token):
                        if link:
                            edges.append(dict(kind="CMakeLink", file=rel, line=line, from_module=module,
                                              from_target=target, to=link, to_module=mapping.get(link)))
            except CoverageError as error:
                errors.append(dict(kind="CoverageError", file=rel, line=line, details=str(error)))
    return edges, errors, [p.relative_to(root).as_posix() for p in sorted(parsed)], mapping


def scan_includes(root, config, reference_root):
    headers = defaultdict(set)
    # Reference headers make minimal mutation fixtures resolve existing public includes.
    for tree in {reference_root, root}:
        for module, relative in config["module_roots"].items():
            for directory in (tree/relative/"include",tree/relative/"Runtime/include",tree/relative/"Editor/include"):
                if directory.exists():
                    for path in directory.rglob("*"):
                        if path.is_file() and path.suffix in {".h", ".hpp", ".inl"}:
                            headers[path.relative_to(directory).as_posix()].add(module_for(path, tree, config))
    files = set()
    for relative in config["module_roots"].values():
        directory = root/relative
        if directory.exists():
            files.update(p for p in directory.rglob("*") if p.is_file() and
                         p.suffix in {".h", ".hpp", ".cpp", ".cc", ".cxx", ".inl"})
    edges, errors = [], []
    for path in sorted(files):
        module = module_for(path, root, config)
        rel = path.relative_to(root).as_posix()
        content = path.read_text(encoding="utf-8-sig")
        # Preserve line numbers while discarding block comments.
        content = re.sub(r"/\*.*?\*/", lambda m: "\n"*m[0].count("\n"), content, flags=re.S)
        for line, text in enumerate(content.splitlines(), 1):
            match = re.match(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', text)
            if not match:
                if re.match(r"^\s*#\s*include\b", text):
                    errors.append(dict(kind="CoverageError", file=rel, line=line, details="macro include requires explicit support"))
                continue
            spelling = match[1].replace("\\", "/")
            relative_header = (path.parent/spelling).resolve()
            target = module_for(relative_header, root, config) if relative_header.is_file() else None
            if not target:
                owners = headers.get(spelling, set())
                if len(owners) == 1:
                    target = next(iter(owners))
                elif len(owners) > 1 or spelling.startswith(("Axle/", "AxleSpline/", "AxleTerrain/")):
                    errors.append(dict(kind="CoverageError", file=rel, line=line, details=f"unresolved/ambiguous project include: {spelling}"))
            if target and target != module:
                edges.append(dict(kind="SourceInclude", file=rel, line=line, from_module=module, to=spelling, to_module=target))
    return edges, errors, len(files)


def edge_key(edge):
    return (edge["kind"], edge["file"], edge.get("from_target", ""), edge["to"])


def inspect(root, config, reference_root=REPO, closed_packages=()):
    root, reference_root = Path(root).resolve(), Path(reference_root).resolve()
    links, errors, cmake_files, mapping = scan_cmake(root, config)
    includes, include_errors, count = scan_includes(root, config, reference_root)
    errors.extend(include_errors)
    forbidden = {r["from"]: set(r["forbidden_to"]) for r in config["forbidden_include_rules"]}
    allowances = {}
    for allow in config["baseline_allowances"]:
        if not allow.get("owning_package") or not allow.get("expiration_condition") or not allow.get("edges"):
            raise CoverageError(f"invalid/unmigrated allowance: {allow.get('id')}")
        if allow["owning_package"] not in closed_packages:
            for edge in allow["edges"]:
                allowances[(allow["from_module"], allow["to_module"], edge_key(edge))] = allow["id"]
    violations = list(errors)
    for edge in links+includes:
        module, target = edge["from_module"], edge["to_module"]
        if edge["kind"] == "CMakeLink":
            allowed = config["allowed_target_links"].get(module, [])
            allowed_modules = {mapping[a] for a in allowed if a in mapping}
            bad = edge["to"] not in allowed and (target is None or target not in allowed_modules)
        else:
            bad = target in forbidden.get(module, set())
        allowance = allowances.get((module, target, edge_key(edge)))
        edge["allowance"] = allowance if bad else None
        if bad and not allowance:
            violations.append(dict(edge, details=f"{module} -> {edge['to']} is outside the declared boundary"))
    return dict(coverage="conservative source-declared union; conditional branches included; dynamic dependencies fail coverage",
                source_files_scanned=count, cmake_files=cmake_files, target_links=links, source_includes=includes,
                violations=violations)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--fixture", type=Path)
    parser.add_argument("--config", type=Path, default=REPO/"scripts/architecture_rules.json")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--closed-package", action="append", default=[])
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--expect-violation", action="store_true", help="Validate the forbidden-edge fixture; errors are never success")
    args = parser.parse_args()
    root = (args.fixture or args.root).resolve()
    try:
        if not root.is_dir() or not any((root/p).exists() for p in json.loads(args.config.read_text(encoding="utf-8"))["module_roots"].values()):
            raise CoverageError("root contains no declared Engine/product modules")
        config = json.loads(args.config.read_text(encoding="utf-8"))
        report = inspect(root, config, REPO, args.closed_package)
    except (OSError, ValueError, KeyError) as error:
        print(f"AXLE_ARCH_CHECK ERROR\n{error}", file=sys.stderr)
        return 2
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    print(f"Files scanned: {report['source_files_scanned']}; CMake files: {len(report['cmake_files'])}")
    for violation in report["violations"]:
        print(f"{violation['kind']} {violation['file']}:{violation['line']}: {violation['details']}")
    # Coverage errors are infrastructure failures, not valid forbidden-edge proofs.
    if any(v["kind"] == "CoverageError" for v in report["violations"]):
        print("AXLE_ARCH_CHECK ERROR")
        return 2
    if report["violations"]:
        print("AXLE_ARCH_CHECK VIOLATIONS")
        return 0 if args.expect_violation else 1
    print("AXLE_ARCH_CHECK PASS")
    return 1 if args.expect_violation else 0


if __name__ == "__main__":
    raise SystemExit(main())
