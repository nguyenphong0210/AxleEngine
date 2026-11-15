"""Validate the M15 roughness/metal sweep fixture without launching the Editor."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
FIXTURE_DIR = ROOT / "Tests" / "Fixtures" / "M15"
PROJECT = FIXTURE_DIR / "M15_RoughnessSweep.axleproj"
SCENE = FIXTURE_DIR / "M15_RoughnessSweep.axlescene"
EXPECTED = {
    "roughness_004": "0.04",
    "roughness_012": "0.12",
    "roughness_025": "0.25",
    "roughness_050": "0.50",
    "roughness_080": "0.80",
}


def fail(message: str) -> None:
    raise SystemExit(f"M15 roughness fixture: FAIL: {message}")


def main() -> int:
    if not PROJECT.is_file() or not SCENE.is_file():
        fail("project or scene is missing")

    project_text = PROJECT.read_text(encoding="utf-8")
    start_match = re.search(r"^  StartScene:\s*(.+)$", project_text, re.MULTILINE)
    assets_match = re.search(r"^  AssetDirectory:\s*(.+)$", project_text, re.MULTILINE)
    if not start_match or not assets_match:
        fail("project must declare StartScene and AssetDirectory")

    asset_root = (PROJECT.parent / assets_match.group(1).strip()).resolve()
    resolved_scene = (asset_root / start_match.group(1).strip()).resolve()
    if resolved_scene != SCENE.resolve():
        fail(f"StartScene resolves to {resolved_scene}, expected {SCENE.resolve()}")

    text = SCENE.read_text(encoding="utf-8")
    if len(re.findall(r"^  - Entity:\s*\d+", text, re.MULTILINE)) != 8:
        fail("scene must contain sky, ground, light and five sweep entities")

    found = {}
    blocks = re.split(r"(?=^  - Entity:\s*10000[4-8]\s*$)", text, flags=re.MULTILINE)
    for block in blocks:
        tag = re.search(r"^      Tag:\s*(roughness_\d+)$", block, re.MULTILINE)
        roughness = re.search(r"^      Roughness:\s*([0-9.]+)$", block, re.MULTILINE)
        metallic = re.search(r"^      Metallic:\s*([0-9.]+)$", block, re.MULTILINE)
        material = re.search(r"^    MaterialComponent:\s*$([\s\S]*?)(?=^  - Entity:|\Z)", block, re.MULTILINE)
        if tag and roughness and metallic:
            if not material or re.search(r"^      (?:MaterialHandle|AssetPath):", material.group(1), re.MULTILINE):
                fail(f"{tag.group(1)} must use an inline material identity")
            found[tag.group(1)] = (roughness.group(1), metallic.group(1))

    expected = {key: (value, "1.0") for key, value in EXPECTED.items()}
    if found != expected:
        fail(f"sweep values are {found}, expected {expected}")

    print("M15 roughness fixture: PASS (8 entities; five monotonic metallic roughness cases)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
