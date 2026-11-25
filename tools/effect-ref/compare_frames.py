#!/usr/bin/env python3
"""Compare frames Dafin renders with reference frames you exported yourself (D-019).

  python3 compare_frames.py --ref filmora_export.mp4 --test dafin_export.mp4 --out report/ \
                            [--frames 0,10,20] [--fit] [--psnr 35] [--ssim 0.98] [--max-diff 40]

REF and TEST are each one of: an image, a folder of images (paired by file name without its
extension), or a video (frames picked with --frames, default nine spread over the clip).
Both sides must be the same kind. Nothing here knows or needs anything about where a reference
comes from: it is a pixel comparison of two pictures you provide.

Per frame it reports: PSNR, SSIM (on luma), mean and largest channel difference, the share of
pixels that differ by more than --tolerance, the average colour shift per channel (test minus
reference: a brightness or tint error shows up here), the picture shift between the two
(sub-pixel phase correlation) and the worst 64x64 tile. Transparent pixels are flattened onto
black first. Writes report.csv, summary.json and contact.jpg (reference | test | difference x8).

Thresholds are optional: with none given nothing fails and the report is only a report. When
one is given, the exit code is 1 if any frame misses it (or is missing, or has another size
and --fit was not asked for). Effects are not expected to be pixel-identical, so no default is
imposed here; calibrate the numbers on references you trust before relying on them.
"""
from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path

import cv2
import numpy as np

IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff", ".webp"}
VIDEO_SUFFIXES = {".mp4", ".mov", ".mkv", ".avi", ".webm", ".m4v"}
PSNR_CAP = 100.0  # identical pictures: JSON has no infinity
TILE = 64

FIELDS = ["frame", "status", "width", "height", "psnr", "ssim", "mean_abs_diff", "max_abs_diff",
          "pct_over_tolerance", "bias_r", "bias_g", "bias_b", "shift_x", "shift_y",
          "worst_tile_x", "worst_tile_y", "worst_tile_mean_diff", "failed", "note"]


class CompareError(Exception):
    """Something the caller must fix (wrong inputs), as opposed to a frame that differs."""


def kind_of(path: Path) -> str:
    if path.is_dir():
        return "folder"
    suffix = path.suffix.lower()
    if suffix in IMAGE_SUFFIXES:
        return "image"
    if suffix in VIDEO_SUFFIXES:
        return "video"
    raise CompareError(f"{path}: not an image, a video or a folder of images")


def read_image(path: Path) -> np.ndarray:
    """BGR uint8; transparency flattened onto black."""
    data = cv2.imread(str(path), cv2.IMREAD_UNCHANGED)
    if data is None:
        raise CompareError(f"{path}: could not be read as a picture")
    if data.dtype != np.uint8:
        data = cv2.convertScaleAbs(data, alpha=255.0 / (65535.0 if data.dtype == np.uint16 else 1.0))
    if data.ndim == 2:
        return cv2.cvtColor(data, cv2.COLOR_GRAY2BGR)
    if data.shape[2] == 4:
        alpha = data[:, :, 3:4].astype(np.float32) / 255.0
        return (data[:, :, :3].astype(np.float32) * alpha + 0.5).astype(np.uint8)
    return data


def spread_frames(count: int, wanted: int = 9) -> list[int]:
    if count <= wanted:
        return list(range(count))
    return sorted({round(i * (count - 1) / (wanted - 1)) for i in range(wanted)})


def read_video(path: Path, frames: list[int] | None) -> tuple[dict[str, np.ndarray], int]:
    cap = cv2.VideoCapture(str(path))
    if not cap.isOpened():
        raise CompareError(f"{path}: could not be opened as a video")
    total = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    out: dict[str, np.ndarray] = {}
    index = 0
    wanted = set(frames) if frames is not None else None
    if wanted is None and total > 0:
        wanted = set(spread_frames(total))
    while True:
        ok, frame = cap.read()
        if not ok:
            break
        if wanted is None or index in wanted:
            out[str(index)] = frame
        index += 1
        if wanted is not None and index > max(wanted):
            break
    cap.release()
    return out, index


def load_side(path: Path, frames: list[int] | None) -> tuple[str, dict[str, np.ndarray]]:
    kind = kind_of(path)
    if kind == "image":
        return kind, {path.stem: read_image(path)}
    if kind == "folder":
        files = sorted(p for p in path.iterdir() if p.suffix.lower() in IMAGE_SUFFIXES)
        if not files:
            raise CompareError(f"{path}: no pictures in the folder")
        return kind, {p.stem: read_image(p) for p in files}
    pictures, _ = read_video(path, frames)
    if not pictures:
        raise CompareError(f"{path}: none of the requested frames exist in the video")
    return kind, pictures


def luma(bgr: np.ndarray) -> np.ndarray:
    b, g, r = (bgr[:, :, k].astype(np.float64) for k in range(3))
    return 0.299 * r + 0.587 * g + 0.114 * b


def ssim_luma(a: np.ndarray, b: np.ndarray) -> float:
    """Mean SSIM of two luma planes (11x11 Gaussian window, sigma 1.5, the usual constants)."""
    h, w = a.shape
    if min(h, w) < 16:
        raise CompareError("pictures smaller than 16 pixels can't be compared")
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2

    def blur(x):
        return cv2.GaussianBlur(x, (11, 11), 1.5)

    mu_a, mu_b = blur(a), blur(b)
    var_a = blur(a * a) - mu_a * mu_a
    var_b = blur(b * b) - mu_b * mu_b
    cov = blur(a * b) - mu_a * mu_b
    ssim_map = ((2 * mu_a * mu_b + c1) * (2 * cov + c2)) / ((mu_a**2 + mu_b**2 + c1) * (var_a + var_b + c2))
    return float(ssim_map[5:-5, 5:-5].mean())


def picture_shift(a: np.ndarray, b: np.ndarray) -> tuple[float, float]:
    """How far b is moved from a, in pixels (x right, y down), by phase correlation."""
    window = cv2.createHanningWindow((a.shape[1], a.shape[0]), cv2.CV_64F)
    (dx, dy), _ = cv2.phaseCorrelate(a, b, window)
    return float(dx), float(dy)


def worst_tile(diff: np.ndarray) -> tuple[int, int, float]:
    """Top-left corner and mean difference of the 64x64 tile that differs most."""
    h, w = diff.shape
    best = (0, 0, -1.0)
    for y in range(0, h, TILE):
        for x in range(0, w, TILE):
            value = float(diff[y:y + TILE, x:x + TILE].mean())
            if value > best[2]:
                best = (x, y, value)
    return best


def compare_pair(ref: np.ndarray, test: np.ndarray, tolerance: int = 8) -> dict:
    """Metrics for two same-sized BGR pictures."""
    if ref.shape != test.shape:
        raise CompareError("the two pictures differ in size")
    delta = test.astype(np.float64) - ref.astype(np.float64)
    absd = np.abs(delta)
    mse = float((delta**2).mean())
    psnr = PSNR_CAP if mse == 0 else min(PSNR_CAP, 10.0 * np.log10(255.0**2 / mse))
    per_pixel = absd.max(axis=2)
    ref_l, test_l = luma(ref), luma(test)
    # Phase correlation on two flat pictures has nothing to lock onto: no shift is reported.
    flat = ref_l.std() < 1e-6 or test_l.std() < 1e-6
    dx, dy = (0.0, 0.0) if flat else picture_shift(ref_l, test_l)
    tx, ty, tv = worst_tile(per_pixel)
    return {
        "psnr": round(psnr, 3),
        "ssim": round(ssim_luma(ref_l, test_l), 5),
        "mean_abs_diff": round(float(absd.mean()), 4),
        "max_abs_diff": int(per_pixel.max()),
        "pct_over_tolerance": round(float((per_pixel > tolerance).mean() * 100.0), 3),
        "bias_r": round(float(delta[:, :, 2].mean()), 3),
        "bias_g": round(float(delta[:, :, 1].mean()), 3),
        "bias_b": round(float(delta[:, :, 0].mean()), 3),
        "shift_x": round(dx, 3),
        "shift_y": round(dy, 3),
        "worst_tile_x": tx,
        "worst_tile_y": ty,
        "worst_tile_mean_diff": round(tv, 3),
    }


def judge(row: dict, args) -> list[str]:
    failed = []
    if row["status"] != "OK":
        return [row["status"].lower()]
    if args.psnr is not None and row["psnr"] < args.psnr:
        failed.append(f"psnr<{args.psnr}")
    if args.ssim is not None and row["ssim"] < args.ssim:
        failed.append(f"ssim<{args.ssim}")
    if args.max_diff is not None and row["max_abs_diff"] > args.max_diff:
        failed.append(f"max_diff>{args.max_diff}")
    if args.max_shift is not None and max(abs(row["shift_x"]), abs(row["shift_y"])) > args.max_shift:
        failed.append(f"shift>{args.max_shift}")
    return failed


def pair_keys(ref_kind: str, ref: dict, test: dict) -> list[str]:
    # A frame or picture only one side has is reported as missing, never silently dropped.
    if ref_kind == "video":
        return sorted(set(ref) | set(test), key=int)
    return list(ref) + [k for k in test if k not in ref]


def compare(ref_path: Path, test_path: Path, args) -> tuple[list[dict], dict[str, tuple]]:
    frames = None
    if args.frames:
        try:
            frames = [int(x) for x in args.frames.split(",") if x.strip() != ""]
        except ValueError as exc:
            raise CompareError(f"--frames must be whole numbers separated by commas ({exc})") from exc
    ref_kind, ref = load_side(ref_path, frames)
    test_kind, test = load_side(test_path, frames)
    if ref_kind != test_kind:
        raise CompareError(f"the reference is a {ref_kind} and the test a {test_kind}: give the same kind")
    if ref_kind == "image" and test_kind == "image":
        test = {next(iter(ref)): next(iter(test.values()))}  # two single pictures are a pair
    rows, pictures = [], {}
    for key in pair_keys(ref_kind, ref, test):
        row = {f: "" for f in FIELDS}
        row["frame"] = key
        if key not in ref or key not in test:
            row["status"] = "MISSING_REFERENCE" if key not in ref else "MISSING_TEST"
        else:
            r, t = ref[key], test[key]
            row["height"], row["width"] = r.shape[:2]
            if r.shape != t.shape and args.fit:
                t = cv2.resize(t, (r.shape[1], r.shape[0]), interpolation=cv2.INTER_LANCZOS4)
                row["status"] = "OK"
                row["note"] = "test resized to the reference"
            elif r.shape != t.shape:
                row["status"] = "SIZE_MISMATCH"
            else:
                row["status"] = "OK"
            if row["status"] == "OK":
                row.update(compare_pair(r, t, args.tolerance))
                pictures[key] = (r, t)
        failed = judge(row, args)
        row["failed"] = ";".join(failed)
        rows.append(row)
    return rows, pictures


def contact_sheet(rows, pictures, path: Path, width: int = 480) -> bool:
    lines = []
    for row in rows:
        key = row["frame"]
        if key not in pictures:
            continue
        r, t = pictures[key]
        diff = np.clip(np.abs(t.astype(np.int16) - r.astype(np.int16)) * 8, 0, 255).astype(np.uint8)
        scale = width / r.shape[1]
        cells = [cv2.resize(x, (width, max(1, round(r.shape[0] * scale))), interpolation=cv2.INTER_AREA)
                 for x in (r, t, diff)]
        label = f"{key}  PSNR {row['psnr']}  SSIM {row['ssim']}"
        cv2.putText(cells[0], label, (6, 18), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 2, cv2.LINE_AA)
        cv2.putText(cells[0], label, (6, 18), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 0, 0), 1, cv2.LINE_AA)
        lines.append(np.hstack(cells))
    if not lines:
        return False
    cv2.imwrite(str(path), np.vstack(lines), [cv2.IMWRITE_JPEG_QUALITY, 88])
    return True


def summarize(rows, args) -> dict:
    ok = [r for r in rows if r["status"] == "OK"]
    summary = {
        "frames": len(rows),
        "compared": len(ok),
        "missing_or_mismatched": len(rows) - len(ok),
        "failed": [r["frame"] for r in rows if r["failed"]],
        "thresholds": {"psnr": args.psnr, "ssim": args.ssim, "max_diff": args.max_diff,
                       "max_shift": args.max_shift, "tolerance": args.tolerance},
    }
    if ok:
        summary["worst_psnr"] = min(r["psnr"] for r in ok)
        summary["worst_ssim"] = min(r["ssim"] for r in ok)
        summary["worst_max_abs_diff"] = max(r["max_abs_diff"] for r in ok)
    summary["passed"] = not summary["failed"]
    return summary


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--ref", required=True, type=Path, help="reference: picture, folder of pictures or video")
    p.add_argument("--test", required=True, type=Path, help="what Dafin made, the same kind")
    p.add_argument("--out", required=True, type=Path, help="folder for report.csv, summary.json, contact.jpg")
    p.add_argument("--frames", default="", help="video frame numbers, e.g. 0,10,20 (default: nine spread out)")
    p.add_argument("--fit", action="store_true", help="resize the test pictures to the reference's size")
    p.add_argument("--tolerance", type=int, default=8, help="channel difference counted as a real one (0-255)")
    p.add_argument("--psnr", type=float, help="fail a frame below this PSNR (dB)")
    p.add_argument("--ssim", type=float, help="fail a frame below this SSIM")
    p.add_argument("--max-diff", type=int, help="fail a frame with a channel difference above this")
    p.add_argument("--max-shift", type=float, help="fail a frame shifted by more than this many pixels")
    args = p.parse_args(argv)

    try:
        rows, pictures = compare(args.ref, args.test, args)
    except CompareError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    args.out.mkdir(parents=True, exist_ok=True)
    with open(args.out / "report.csv", "w", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=FIELDS, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)
    summary = summarize(rows, args)
    (args.out / "summary.json").write_text(json.dumps(summary, indent=2))
    contact_sheet(rows, pictures, args.out / "contact.jpg")
    for row in rows:
        mark = "FAIL" if row["failed"] else row["status"]
        extra = f"  {row['failed']}" if row["failed"] else ""
        print(f"{row['frame']:>10}  {mark:<18} PSNR {row['psnr']!s:>8}  SSIM {row['ssim']!s:>8}{extra}")
    print(json.dumps(summary))
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
