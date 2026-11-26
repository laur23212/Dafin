"""Native draft1x (draft_check) against the reference (drafts/d<asset>.py): SSIM over the ink,
composited on black and on mid grey (the lower of the two), per frame.  python3 compare_draft.py [f=10,40] 55A …"""
import subprocess, sys
from pathlib import Path
import numpy as np
from skimage.metrics import structural_similarity as ssim
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import render, scene_core as core  # noqa: E402

args = sys.argv[1:]
FR = [int(v) for v in args.pop(0)[2:].split(',')] if args and args[0].startswith('f=') else [10, 20, 40, 65, 120]
for name in args:
    p = HERE.parent / 'scenes' / f'{name}.scene.json'
    subprocess.run([str(HERE / 'draft_check'), str(p)] + [str(f) for f in FR], check=True)
    r = render.make_renderer(core.load_scene(p))
    res = []
    for f in FR:
        a = np.asarray(r.draw(f / 25)).astype(np.float32)
        b = np.fromfile(f'out_{f:04d}.rgba', np.uint8).reshape(1080, 1920, 4).astype(np.float32)
        m = (a[..., 3] > 8) | (b[..., 3] > 8)
        if not m.any():
            res.append('empty')
            continue
        ys, xs = np.where(m)
        y0, y1, x0, x1 = max(0, ys.min() - 10), ys.max() + 11, max(0, xs.min() - 10), xs.max() + 11
        s = 1.0
        for bg in (0.0, 128.0):
            ca = (a[..., :3] * a[..., 3:] / 255 + bg * (1 - a[..., 3:] / 255))[y0:y1, x0:x1].mean(axis=2)
            cb = (b[..., :3] * b[..., 3:] / 255 + bg * (1 - b[..., 3:] / 255))[y0:y1, x0:x1].mean(axis=2)
            s = min(s, ssim(ca, cb, data_range=255))
        res.append(f'{s:.3f}')
    print(name, ' '.join(res), flush=True)
