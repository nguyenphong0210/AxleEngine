"""Regression tests for the deterministic M15 image comparator."""

from pathlib import Path
import tempfile
import unittest

from compare_images import compare_images


def write_ppm(path: Path, width: int, height: int, pixels: bytes) -> None:
    path.write_bytes(f"P6\n{width} {height}\n255\n".encode("ascii") + pixels)


class ImageCompareTests(unittest.TestCase):
    def test_exact_match_is_deterministic_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pixels = bytes([0, 0, 0, 255, 128, 64, 32, 255, 255, 255, 255, 10])
            write_ppm(root / "expected.ppm", 2, 2, pixels)
            write_ppm(root / "actual.ppm", 2, 2, pixels)
            result = compare_images(root / "expected.ppm", root / "actual.ppm")
            self.assertTrue(result.passed)
            self.assertEqual(result.max_channel_error, 0.0)
            self.assertEqual(result.ssim, 1.0)

    def test_large_channel_change_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_ppm(root / "expected.ppm", 1, 1, bytes([128, 128, 128]))
            write_ppm(root / "actual.ppm", 1, 1, bytes([255, 128, 128]))
            result = compare_images(root / "expected.ppm", root / "actual.ppm")
            self.assertFalse(result.passed)
            self.assertEqual(result.differing_channels, 1)
            self.assertGreater(result.max_channel_error, 0.0)

    def test_dimension_mismatch_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_ppm(root / "expected.ppm", 1, 1, bytes([0, 0, 0]))
            write_ppm(root / "actual.ppm", 2, 1, bytes([0, 0, 0, 0, 0, 0]))
            with self.assertRaisesRegex(ValueError, "dimensions differ"):
                compare_images(root / "expected.ppm", root / "actual.ppm")

    def test_linear_and_srgb_spaces_are_explicit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_ppm(root / "expected.ppm", 1, 1, bytes([64, 64, 64]))
            write_ppm(root / "actual.ppm", 1, 1, bytes([96, 64, 64]))
            linear = compare_images(root / "expected.ppm", root / "actual.ppm", comparison_space="linear")
            srgb = compare_images(root / "expected.ppm", root / "actual.ppm", comparison_space="srgb")
            self.assertNotEqual(linear.mse, srgb.mse)


if __name__ == "__main__":
    unittest.main()
