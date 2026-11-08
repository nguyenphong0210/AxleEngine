"""Generate a standalone SDK plugin under a project's Plugins directory."""
from __future__ import annotations
import argparse
import json
import re
import sys
from pathlib import Path


def create(project: Path, plugin_id: str, module: str, name: str) -> Path:
    if not re.fullmatch(r"[a-z][a-z0-9]*(?:[.-][a-z0-9]+)*", plugin_id):
        raise ValueError("Plugin ID must be a lowercase stable ID, for example studio.mover")
    if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", module):
        raise ValueError("Module must be a C++/CMake identifier")
    if not name or any(c in name for c in '\n\r"\\') or "@" in name:
        raise ValueError("Display name must be plain text without quotes, backslashes or placeholders")
    project = project.resolve(strict=True)
    if project.suffix not in (".axleproj", ".axleproject") or not project.is_file():
        raise ValueError("An existing project file is required")
    destination = project.parent / "Plugins" / module
    if destination.exists():
        raise FileExistsError(f"Refusing to overwrite {destination}")
    root = Path(__file__).resolve().parents[1] / "Templates" / "ProjectPlugin"
    replacements = {"@PLUGIN_ID@": plugin_id, "@MODULE@": module, "@PLUGIN_NAME@": name}
    # Prepare and validate all source content before creating any authored file.
    files = []
    for source in sorted(root.rglob("*")):
        if not source.is_file():
            continue
        text = source.read_text(encoding="utf-8")
        for key, value in replacements.items():
            text = text.replace(key, value)
        relative = source.relative_to(root)
        if relative.name == "Plugin.axleplugin.in":
            relative = relative.with_name(module + ".axleplugin")
            json.loads(text)
        files.append((relative, text))
    destination.mkdir(parents=True, exist_ok=False)
    for relative, text in files:
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open("x", encoding="utf-8", newline="\n") as stream:
            stream.write(text)
    return destination


def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=Path)
    parser.add_argument("--id", required=True, dest="plugin_id")
    parser.add_argument("--module", required=True)
    parser.add_argument("--name", required=True)
    args = parser.parse_args()
    try:
        print(create(args.project, args.plugin_id, args.module, args.name))
        return 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"Plugin generation failed: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
