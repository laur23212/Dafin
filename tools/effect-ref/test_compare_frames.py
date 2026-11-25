"""Tests for compare_frames.py. Every picture is generated here; no outside material is used.

  python3 -m unittest tools/effect-ref/test_compare_frames.py
"""
import contextlib
import csv
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

import cv2
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compare_frames as cf  # noqa: E402


def scene(w=192, h=128, seed=1):
    """A gradient with a few hard-edged shapes: enough structure for SSIM and phase correlation."""
    rng = np.random.default_rng(seed)
    x = np.linspace(0, 255, w, dtype=np.float32)[None, :].repeat(h, 0)
    y = np.linspace(0, 255, h, dtype=np.float32)[:, None].repeat(w, 1)
    img = np.stack([x, y, (x + y) / 2], axis=2).astype(np.uint8)
    for _ in range(6):
        cx, cy = int(rng.integers(20, w - 20)), int(rng.integers(20, h - 20))
        colour = tuple(int(c) for c in rng.integers(0, 255, 3))
        cv2.circle(img, (cx, cy), int(rng.integers(8, 18)), colour, -1)
        cv2.rectangle(img, (cx - 12, cy - 6), (cx + 12, cy + 6), colour[::-1], 2)
    return img


def run(*argv):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = cf.main([str(a) for a in argv])
    return code, out.getvalue(), err.getvalue()


class CompareFrames(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, name, img):
        path = self.dir / name
        path.parent.mkdir(parents=True, exist_ok=True)
        cv2.imwrite(str(path), img)
        return path

    def rows(self, out):
        with open(Path(out) / "report.csv") as fh:
            return list(csv.DictReader(fh))

    def test_identical_pictures_are_perfect(self):
        img = scene()
        m = cf.compare_pair(img, img.copy())
        self.assertEqual(m["psnr"], cf.PSNR_CAP)
        self.assertEqual(m["ssim"], 1.0)
        self.assertEqual(m["max_abs_diff"], 0)
        self.assertEqual(m["pct_over_tolerance"], 0.0)
        self.assertAlmostEqual(m["shift_x"], 0.0, places=2)

    def test_a_brighter_render_shows_as_a_colour_bias(self):
        img = scene()
        brighter = np.clip(img.astype(np.int16) + np.array([0, 0, 20]), 0, 255).astype(np.uint8)  # +20 red (BGR)
        m = cf.compare_pair(img, brighter)
        self.assertGreater(m["bias_r"], 5)
        self.assertEqual(m["bias_g"], 0)
        self.assertEqual(m["bias_b"], 0)
        self.assertLess(m["psnr"], 40)
        self.assertGreater(m["pct_over_tolerance"], 0)

    def test_a_shifted_picture_reports_the_shift(self):
        img = scene()
        moved = np.roll(img, 3, axis=1)  # 3 px to the right
        m = cf.compare_pair(img, moved)
        self.assertAlmostEqual(m["shift_x"], 3.0, delta=0.5)
        self.assertAlmostEqual(m["shift_y"], 0.0, delta=0.5)
        self.assertLess(m["ssim"], 0.95)

    def test_the_worst_tile_is_where_the_damage_is(self):
        img = scene(192, 128)
        bad = img.copy()
        bad[70:120, 130:180] = 255 - bad[70:120, 130:180]
        m = cf.compare_pair(img, bad)
        self.assertGreaterEqual(m["worst_tile_x"], 128)
        self.assertGreaterEqual(m["worst_tile_y"], 64)

    def test_different_sizes_are_refused_not_stretched(self):
        ref = self.write("a/ref.png", scene(192, 128))
        test = self.write("b/test.png", scene(96, 64))
        code, _, _ = run("--ref", ref, "--test", test, "--out", self.dir / "o1")
        self.assertEqual(code, 1)
        self.assertEqual(self.rows(self.dir / "o1")[0]["status"], "SIZE_MISMATCH")

    def test_fit_resizes_the_test_when_asked(self):
        ref = self.write("a/ref.png", scene(192, 128))
        test = self.write("b/test.png", cv2.resize(scene(192, 128), (96, 64), interpolation=cv2.INTER_AREA))
        code, _, _ = run("--ref", ref, "--test", test, "--out", self.dir / "o2", "--fit")
        self.assertEqual(code, 0)
        row = self.rows(self.dir / "o2")[0]
        self.assertEqual(row["status"], "OK")
        self.assertIn("resized", row["note"])

    def test_folders_are_paired_by_name_and_a_missing_one_fails(self):
        for name, seed in (("f1", 1), ("f2", 2)):
            self.write(f"ref/{name}.png", scene(seed=seed))
            self.write(f"test/{name}.png", scene(seed=seed))
        self.write("ref/f3.png", scene(seed=3))  # no counterpart
        self.write("test/f9.png", scene(seed=9))  # no counterpart
        code, _, _ = run("--ref", self.dir / "ref", "--test", self.dir / "test", "--out", self.dir / "o3")
        statuses = {r["frame"]: r["status"] for r in self.rows(self.dir / "o3")}
        self.assertEqual(statuses["f1"], "OK")
        self.assertEqual(statuses["f2"], "OK")
        self.assertEqual(statuses["f3"], "MISSING_TEST")
        self.assertEqual(statuses["f9"], "MISSING_REFERENCE")
        self.assertEqual(code, 1)

    def test_thresholds_decide_the_exit_code(self):
        img = scene()
        noisy = np.clip(img.astype(np.int16) + 6, 0, 255).astype(np.uint8)
        ref = self.write("ref.png", img)
        test = self.write("test.png", noisy)
        self.assertEqual(run("--ref", ref, "--test", test, "--out", self.dir / "o4")[0], 0)  # report only
        self.assertEqual(run("--ref", ref, "--test", test, "--out", self.dir / "o5", "--psnr", 20)[0], 0)
        code, out, _ = run("--ref", ref, "--test", test, "--out", self.dir / "o6", "--psnr", 40)
        self.assertEqual(code, 1)
        self.assertIn("psnr<40", out)
        self.assertEqual(run("--ref", ref, "--test", test, "--out", self.dir / "o7", "--max-diff", 3)[0], 1)

    def test_transparent_pixels_are_flattened_onto_black(self):
        rgba = np.zeros((64, 64, 4), np.uint8)
        rgba[:, :, :3] = 200
        rgba[:, :, 3] = 0
        path = self.write("clear.png", rgba)
        self.assertEqual(int(cf.read_image(path).max()), 0)

    def test_videos_are_compared_on_the_asked_frames(self):
        paths = []
        for name, bump in (("ref.avi", 0), ("test.avi", 12)):
            path = self.dir / name
            writer = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"MJPG"), 25, (192, 128))
            if not writer.isOpened():
                self.skipTest("no MJPG writer in this OpenCV build")
            for i in range(12):
                frame = np.clip(scene(seed=i + 1).astype(np.int16) + bump, 0, 255).astype(np.uint8)
                writer.write(frame)
            writer.release()
            paths.append(path)
        code, _, _ = run("--ref", paths[0], "--test", paths[1], "--out", self.dir / "o8", "--frames", "0,5,11")
        self.assertEqual([r["frame"] for r in self.rows(self.dir / "o8")], ["0", "5", "11"])
        self.assertEqual(code, 0)
        summary = json.loads((self.dir / "o8" / "summary.json").read_text())
        self.assertEqual(summary["compared"], 3)
        self.assertTrue((self.dir / "o8" / "contact.jpg").exists())

    def test_mixing_kinds_is_a_clear_error(self):
        pic = self.write("p.png", scene())
        code, _, err = run("--ref", pic, "--test", self.dir, "--out", self.dir / "o9")
        self.assertEqual(code, 2)
        self.assertIn("same kind", err)


if __name__ == "__main__":
    unittest.main()
