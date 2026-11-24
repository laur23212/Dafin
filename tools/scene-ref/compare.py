#!/usr/bin/env python3
"""Compare a rendering with the approved reference MP4, frame by frame and actor by actor.

  python3 compare.py --ref original.mp4 --test render.mp4 --scene scene.json --out report/ \
                     [--frames 0,10,20,40,65,90,115,135,149] [--ssim 0.97] [--pos 1.0]

For every frame: whole-frame PSNR and SSIM. For every visible actor, on its region (rest box from
the scene, widened by the motion margin): SSIM, PSNR, the largest channel difference and the
position error (sub-pixel phase correlation of the two regions). Writes report.csv,
summary.json and contact.jpg (reference | test | difference x8). Exit code 1 if a threshold fails.
"""
from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import render  # noqa: E402
import scene_core as core  # noqa: E402

DEFAULT_FRAMES = [0, 10, 20, 40, 65, 90, 115, 135, 149]


def decode(path, wanted=None):
    cap = cv2.VideoCapture(str(path))
    out, i = {}, 0
    while True:
        ok, f = cap.read()
        if not ok:
            break
        if wanted is None or i in wanted:
            out[i] = f
        i += 1
    cap.release()
    return out, i


def luma(bgr):
    b, g, r = [bgr[:, :, k].astype(np.float64) for k in range(3)]
    return 0.299 * r + 0.587 * g + 0.114 * b


def ssim(a, b):
    """Gaussian SSIM (Wang et al. 2004: 11x11, sigma 1.5) on luma."""
    C1, C2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2
    x, y = luma(a), luma(b)
    if min(x.shape) < 11:
        return 1.0 if np.array_equal(a, b) else float('nan')
    blur = lambda z: cv2.GaussianBlur(z, (11, 11), 1.5)
    mx, my = blur(x), blur(y)
    sxx, syy, sxy = blur(x * x) - mx * mx, blur(y * y) - my * my, blur(x * y) - mx * my
    m = ((2 * mx * my + C1) * (2 * sxy + C2)) / ((mx * mx + my * my + C1) * (sxx + syy + C2))
    return float(m[5:-5, 5:-5].mean())


def psnr(a, b):
    mse = np.mean((a.astype(np.float64) - b.astype(np.float64)) ** 2)
    return float('inf') if mse == 0 else float(10 * np.log10(255 ** 2 / mse))


def shift(a, b):
    """Sub-pixel (dx, dy) of b relative to a; (0,0) for identical regions."""
    x, y = luma(a).astype(np.float32), luma(b).astype(np.float32)
    if np.array_equal(x, y):
        return 0.0, 0.0
    win = cv2.createHanningWindow((x.shape[1], x.shape[0]), cv2.CV_32F)
    (dx, dy), _ = cv2.phaseCorrelate(x - x.mean(), y - y.mean(), win)
    return float(dx), float(dy)


def actor_regions(sc, t, margin):
    r = render.make_renderer(sc)
    hs, he = core.scene_events(sc)
    boxes = r.rest_boxes((hs + he) / 2)  # every actor at rest in the middle of the hold
    W, H = sc.width, sc.height
    out = {}
    for a in sc.actors:
        if a['id'] not in boxes:
            continue
        ch = core.channels(a, t)
        vis = ch['opacity'] * ch['wipe'] * ch['progress']
        x0, y0, x1, y1 = boxes[a['id']]
        out[a['id']] = (max(0, int(x0) - margin), max(0, int(y0) - margin),
                        min(W, int(np.ceil(x1)) + margin), min(H, int(np.ceil(y1)) + margin), vis)
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--ref', required=True); ap.add_argument('--test', required=True)
    ap.add_argument('--scene', required=True); ap.add_argument('--out', required=True)
    ap.add_argument('--frames', default=','.join(map(str, DEFAULT_FRAMES)))
    ap.add_argument('--ssim', type=float, default=0.97, help='minimum SSIM per actor region')
    ap.add_argument('--pos', type=float, default=0.5, help='largest position error per actor, px')
    ap.add_argument('--margin', type=int, default=20, help='px added around each actor region')
    ap.add_argument('--param', action='append')
    o = ap.parse_args(argv)
    frames = [int(v) for v in o.frames.split(',')]
    sc = core.load_scene(o.scene, render.parse_params(o.param))
    fps = sc.design_fps
    ref, nref = decode(o.ref, set(frames)); tst, ntst = decode(o.test, set(frames))
    out = Path(o.out); out.mkdir(parents=True, exist_ok=True)
    rows, fails, sheet = [], [], []
    for i in frames:
        if i not in ref or i not in tst:
            fails.append(f'frame {i} missing (ref {nref} frames, test {ntst})'); continue
        a, b = ref[i], tst[i]
        rows.append({'frame': i, 'actor': '*frame*', 'visible': 1, 'ssim': round(ssim(a, b), 5),
                     'psnr': round(psnr(a, b), 2), 'maxdiff': int(np.abs(a.astype(int) - b).max()), 'dx': '', 'dy': '', 'pos_err': ''})
        for aid, (x0, y0, x1, y1, vis) in actor_regions(sc, i / fps, o.margin).items():
            ra, rb = a[y0:y1, x0:x1], b[y0:y1, x0:x1]
            s, p = ssim(ra, rb), psnr(ra, rb)
            md = int(np.abs(ra.astype(int) - rb).max())
            row = {'frame': i, 'actor': aid, 'visible': round(vis, 3), 'ssim': round(s, 5), 'psnr': round(p, 2), 'maxdiff': md,
                   'dx': '', 'dy': '', 'pos_err': ''}
            if vis > 0.05:
                dx, dy = shift(ra, rb)
                row.update(dx=round(dx, 3), dy=round(dy, 3), pos_err=round(float(np.hypot(dx, dy)), 3))
                if s < o.ssim:
                    fails.append(f'frame {i} {aid}: SSIM {s:.4f} < {o.ssim}')
                if np.hypot(dx, dy) > o.pos:
                    fails.append(f'frame {i} {aid}: position error {np.hypot(dx, dy):.2f}px > {o.pos}')
            rows.append(row)
        sheet.append((i, a, b))
    with open(out / 'report.csv', 'w', newline='', encoding='utf-8') as fh:
        w = csv.DictWriter(fh, fieldnames=list(rows[0].keys()) if rows else ['frame'])
        w.writeheader(); w.writerows(rows)
    fr = [r for r in rows if r['actor'] == '*frame*']
    act = [r for r in rows if r['actor'] != '*frame*' and r['pos_err'] != '']
    summary = {
        'ref': str(o.ref), 'test': str(o.test), 'scene': sc.raw['id'], 'frames': frames,
        'thresholds': {'ssim': o.ssim, 'pos_px': o.pos},
        'frame_psnr': {r['frame']: r['psnr'] for r in fr}, 'frame_ssim': {r['frame']: r['ssim'] for r in fr},
        'actor_ssim_min': min((r['ssim'] for r in act), default=None),
        'actor_pos_err_max': max((r['pos_err'] for r in act), default=None),
        'pass': not fails, 'failures': fails[:50]}
    (out / 'summary.json').write_text(json.dumps(summary, indent=1) + '\n', encoding='utf-8')
    # contact sheet: reference | test | |difference| x8
    tw, th = 480, 270
    cs = Image.new('RGB', (tw * 3 + 16, (th + 22) * len(sheet) + 4), (236, 236, 232))
    d = ImageDraw.Draw(cs)
    for k, (i, a, b) in enumerate(sheet):
        diff = np.clip(np.abs(a.astype(int) - b.astype(int)) * 8, 0, 255).astype(np.uint8)
        for j, im in enumerate((a, b, diff)):
            p = Image.fromarray(cv2.cvtColor(im, cv2.COLOR_BGR2RGB)).resize((tw, th), Image.Resampling.LANCZOS)
            cs.paste(p, (4 + j * (tw + 4), 4 + k * (th + 22)))
        f = next(r for r in fr if r['frame'] == i)
        d.text((8, 4 + k * (th + 22) + th + 4), f'frame {i}  t={i / fps:.2f}s   PSNR {f["psnr"]} dB   SSIM {f["ssim"]}   '
               'reference | test | difference x8', fill=(20, 20, 20))
    cs.save(out / 'contact.jpg', quality=90)
    print(json.dumps({k: summary[k] for k in ('scene', 'frame_psnr', 'frame_ssim', 'actor_ssim_min', 'actor_pos_err_max', 'pass')}))
    return 0 if not fails else 1


if __name__ == '__main__':
    sys.exit(main())
