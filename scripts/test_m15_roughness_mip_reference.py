"""Independent roughness-to-prefilter-mip reference for M15 U0-06.4."""

from math import isfinite
import sys


def roughness_to_mip(roughness: float, mip_count: int) -> float:
    if mip_count <= 0:
        raise ValueError("mip_count must be positive")
    clamped = min(max(roughness, 0.0), 1.0)
    return clamped * max(0, mip_count - 1)


def main() -> int:
    mip_count = 5
    sweep = [0.0, 0.04, 0.12, 0.25, 0.50, 0.80, 1.0]
    mapped = [roughness_to_mip(value, mip_count) for value in sweep]
    expected = [0.0, 0.16, 0.48, 1.0, 2.0, 3.2, 4.0]
    assert all(isfinite(value) for value in mapped)
    assert all(abs(actual - wanted) <= 1.0e-6 for actual, wanted in zip(mapped, expected))
    assert all(mapped[index] <= mapped[index + 1] for index in range(len(mapped) - 1))
    assert roughness_to_mip(-1.0, mip_count) == 0.0
    assert roughness_to_mip(2.0, mip_count) == 4.0
    assert roughness_to_mip(0.5, 1) == 0.0
    print("M15 roughness-to-mip reference: PASS (5 mips; monotonic linear mapping)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
