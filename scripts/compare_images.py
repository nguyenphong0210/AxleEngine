"""Deterministic RGB PPM comparison for M15 image evidence."""

from __future__ import annotations

import argparse
import json
import math
from dataclasses import asdict, dataclass
from pathlib import Path


DEFAULT_MIN_PSNR = 40.0
DEFAULT_MIN_SSIM = 0.985
DEFAULT_MAX_CHANNEL_ERROR = 4.0 / 255.0


@dataclass(frozen=True)
class ImageComparison:
    width: int
    height: int
    comparison_space: str
    mse: float
    psnr_db: float
    ssim: float
    max_channel_error: float
    differing_channels: int
    total_channels: int
    min_psnr_db: float
    min_ssim: float
    allowed_max_channel_error: float
    passed: bool


def _token(data: bytes, offset: int) -> tuple[bytes, int]:
    length = len(data)
    while offset < length:
        if data[offset] in b" \t\r\n":
            offset += 1
        elif data[offset] == ord("#"):
            newline = data.find(b"\n", offset)
            offset = length if newline < 0 else newline + 1
        else:
            break
    start = offset
    while offset < length and data[offset] not in b" \t\r\n#":
        offset += 1
    if start == offset:
        raise ValueError("PPM header is truncated")
    return data[start:offset], offset


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    magic, offset = _token(data, 0)
    width_token, offset = _token(data, offset)
    height_token, offset = _token(data, offset)
    max_value_token, offset = _token(data, offset)
    if magic != b"P6":
        raise ValueError(f"{path}: expected binary P6 PPM")
    try:
        width, height, max_value = int(width_token), int(height_token), int(max_value_token)
    except ValueError as error:
        raise ValueError(f"{path}: invalid PPM dimensions/max value") from error
    if width <= 0 or height <= 0 or max_value != 255:
        raise ValueError(f"{path}: only positive 8-bit PPM images are supported")
    while offset < len(data) and data[offset] in b" \t\r\n":
        offset += 1
    expected = width * height * 3
    pixels = data[offset:]
    if len(pixels) != expected:
        raise ValueError(f"{path}: expected {expected} RGB bytes, found {len(pixels)}")
    return width, height, pixels


def _srgb_to_linear(value: float) -> float:
    return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4


def _values(pixels: bytes, comparison_space: str) -> list[float]:
    if comparison_space not in {"linear", "srgb"}:
        raise ValueError("comparison_space must be 'linear' or 'srgb'")
    values = [channel / 255.0 for channel in pixels]
    if comparison_space == "linear":
        values = [_srgb_to_linear(value) for value in values]
    return values


def _global_ssim(expected: list[float], actual: list[float]) -> float:
    channels = len(expected) // 3
    c1, c2 = 0.01**2, 0.03**2
    scores = []
    for channel in range(3):
        a = expected[channel::3]
        b = actual[channel::3]
        mean_a = sum(a) / channels
        mean_b = sum(b) / channels
        variance_a = sum((value - mean_a) ** 2 for value in a) / channels
        variance_b = sum((value - mean_b) ** 2 for value in b) / channels
        covariance = sum((x - mean_a) * (y - mean_b) for x, y in zip(a, b)) / channels
        numerator = (2 * mean_a * mean_b + c1) * (2 * covariance + c2)
        denominator = (mean_a**2 + mean_b**2 + c1) * (variance_a + variance_b + c2)
        scores.append(numerator / denominator if denominator else 1.0)
    return sum(scores) / len(scores)


def compare_images(
    expected_path: Path,
    actual_path: Path,
    *,
    comparison_space: str = "linear",
    min_psnr_db: float = DEFAULT_MIN_PSNR,
    min_ssim: float = DEFAULT_MIN_SSIM,
    max_channel_error: float = DEFAULT_MAX_CHANNEL_ERROR,
) -> ImageComparison:
    expected_width, expected_height, expected_pixels = read_ppm(expected_path)
    actual_width, actual_height, actual_pixels = read_ppm(actual_path)
    if (expected_width, expected_height) != (actual_width, actual_height):
        raise ValueError(
            f"image dimensions differ: expected {expected_width}x{expected_height}, "
            f"actual {actual_width}x{actual_height}"
        )
    expected = _values(expected_pixels, comparison_space)
    actual = _values(actual_pixels, comparison_space)
    differences = [abs(a - b) for a, b in zip(expected, actual)]
    mse = sum((a - b) ** 2 for a, b in zip(expected, actual)) / len(expected)
    psnr = math.inf if mse == 0.0 else 10.0 * math.log10(1.0 / mse)
    ssim = _global_ssim(expected, actual)
    maximum = max(differences, default=0.0)
    return ImageComparison(
        width=expected_width,
        height=expected_height,
        comparison_space=comparison_space,
        mse=mse,
        psnr_db=psnr,
        ssim=ssim,
        max_channel_error=maximum,
        differing_channels=sum(error != 0.0 for error in differences),
        total_channels=len(differences),
        min_psnr_db=min_psnr_db,
        min_ssim=min_ssim,
        allowed_max_channel_error=max_channel_error,
        passed=psnr >= min_psnr_db and ssim >= min_ssim and maximum <= max_channel_error,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("expected", type=Path)
    parser.add_argument("actual", type=Path)
    parser.add_argument("--space", choices=("linear", "srgb"), default="linear")
    parser.add_argument("--min-psnr", type=float, default=DEFAULT_MIN_PSNR)
    parser.add_argument("--min-ssim", type=float, default=DEFAULT_MIN_SSIM)
    parser.add_argument("--max-channel-error", type=float, default=DEFAULT_MAX_CHANNEL_ERROR)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    result = compare_images(
        args.expected,
        args.actual,
        comparison_space=args.space,
        min_psnr_db=args.min_psnr,
        min_ssim=args.min_ssim,
        max_channel_error=args.max_channel_error,
    )
    serialized = json.dumps(asdict(result), indent=2, sort_keys=True, allow_nan=True) + "\n"
    if args.report:
        args.report.write_text(serialized, encoding="utf-8")
    print(serialized, end="")
    return 0 if result.passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
