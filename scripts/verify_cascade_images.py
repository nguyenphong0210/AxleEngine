"""Compare physical C13-01 D3D12/Vulkan readbacks; missing evidence fails."""
import argparse
import hashlib
import json
from pathlib import Path


def read_ppm(path):
    header, dimensions, maximum, pixels = path.read_bytes().split(b"\n", 3)
    width, height = map(int, dimensions.split())
    if header != b"P6" or maximum != b"255" or len(pixels) != width * height * 3:
        raise ValueError(f"Invalid RGB readback: {path}")
    return (width, height), pixels


def verify(root):
    results = {}
    for name in ("off", "on", "cutout", "sky-transparent"):
        dimensions, dx12 = read_ppm(root / "dx12" / f"{name}.ppm")
        other_dimensions, vulkan = read_ppm(root / "vulkan" / f"{name}.ppm")
        if dimensions != other_dimensions:
            raise ValueError(f"Backend extent mismatch: {name}")
        errors = [abs(a - b) for a, b in zip(dx12, vulkan)]
        result = {
            "extent": dimensions,
            "max_channel_error": max(errors),
            "mean_channel_error": sum(errors) / len(errors),
            "dx12_sha256": hashlib.sha256(dx12).hexdigest(),
            "vulkan_sha256": hashlib.sha256(vulkan).hexdigest(),
        }
        result["pass"] = result["max_channel_error"] <= 2 and result["mean_channel_error"] <= 0.1
        results[name] = result
    shadow_pixels = {}
    for backend in ("dx12", "vulkan"):
        _, off = read_ppm(root / backend / "off.ppm")
        _, on = read_ppm(root / backend / "on.ppm")
        shadow_pixels[backend] = sum(
            sum(off[i:i+3]) - sum(on[i:i+3]) > 30 for i in range(0, len(off), 3)
        )
    return {
        "images": results,
        "shadow_pixels": shadow_pixels,
        "pass": all(r["pass"] for r in results.values()) and all(n > 20 for n in shadow_pixels.values()),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path, help="Configuration folder containing dx12/ and vulkan/")
    args = parser.parse_args()
    try:
        result = verify(args.root)
    except (OSError, ValueError) as error:
        print(f"FAIL: {error}")
        return 1
    (args.root / "parity.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
