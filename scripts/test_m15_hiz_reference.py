"""Independent CPU reference for the M15 Hi-Z reduction contract.

This test deliberately does not import or call the production shader, RHI or
renderer code. GPU/odd-dimension coverage belongs to later U0-06.2 leaves.
"""

from math import isfinite
import sys


def build_hiz(depth: list[float], width: int, height: int) -> list[tuple[int, int, list[float]]]:
    if width <= 0 or height <= 0 or len(depth) != width * height:
        raise ValueError("depth dimensions and sample count must agree")

    levels = [(width, height, list(depth))]
    source_width, source_height, source = width, height, list(depth)
    while source_width > 1 or source_height > 1:
        destination_width = max(1, source_width // 2)
        destination_height = max(1, source_height // 2)
        destination: list[float] = []
        for y in range(destination_height):
            for x in range(destination_width):
                samples = []
                for offset_y in (0, 1):
                    for offset_x in (0, 1):
                        sample_x = min(x * 2 + offset_x, source_width - 1)
                        sample_y = min(y * 2 + offset_y, source_height - 1)
                        samples.append(source[sample_y * source_width + sample_x])
                destination.append(min(samples))
        levels.append((destination_width, destination_height, destination))
        source_width, source_height, source = destination_width, destination_height, destination
    return levels


def main() -> int:
    # Canonical even-dimension fixture. Odd dimensions are a separate leaf.
    depth = [
        0.90, 0.80, 0.70, 0.60,
        0.50, 0.40, 0.30, 0.20,
        0.15, 0.25, 0.35, 0.45,
        0.05, 0.55, 0.65, 0.75,
    ]
    levels = build_hiz(depth, 4, 4)
    assert [(width, height) for width, height, _ in levels] == [(4, 4), (2, 2), (1, 1)]
    assert levels[0][2] == depth
    assert levels[1][2] == [0.40, 0.20, 0.05, 0.35]
    assert levels[2][2] == [0.05]
    assert all(isfinite(value) for _, _, samples in levels for value in samples)
    # U0-06.2.3 edge fixtures. These intentionally remain in the reference
    # script so their expected dimensions and edge values are explicit.
    one = build_hiz([0.37], 1, 1)
    assert [(width, height) for width, height, _ in one] == [(1, 1)]
    assert one[0][2] == [0.37]

    odd_depth = [
        0.91, 0.82, 0.73,
        0.64, 0.55, 0.46,
        0.37, 0.28, 0.19,
        0.10, 0.20, 0.30,
        0.40, 0.50, 0.60,
    ]
    odd = build_hiz(odd_depth, 3, 5)
    assert [(width, height) for width, height, _ in odd] == [(3, 5), (1, 2), (1, 1)]
    assert odd[1][2] == [0.55, 0.10]
    assert odd[2][2] == [0.10]

    large_width, large_height = 1919, 1079
    large_depth = [float((index % 997) + 1) / 998.0 for index in range(large_width * large_height)]
    large = build_hiz(large_depth, large_width, large_height)
    assert [(width, height) for width, height, _ in large] == [
        (1919, 1079), (959, 539), (479, 269), (239, 134), (119, 67),
        (59, 33), (29, 16), (14, 8), (7, 4), (3, 2), (1, 1),
    ]
    assert all(isfinite(value) for _, _, samples in large for value in samples)
    print("M15 Hi-Z CPU reference: PASS (4x4 canonical; 1x1, 3x5 and 1919x1079 edge cases)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
