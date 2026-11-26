"""Runs the native gold1x effects (gold_check, built from gold_check.cpp) and the Python reference
on frames 10, 20, 40, 65, 135 of each Golden Run scene and prints the SSIM over the ink.

  g++ -std=c++20 -O2 -fPIC -I ../../../src -I <qt6>/QtCore -I <qt6> -I /usr/include/freetype2 \
      gold_check.cpp -o gold_check -lQt6Core -lfreetype -lharfbuzz
  python3 compare.py 25A 25B …

gold_check stands FreeType + HarfBuzz in for Skia, so text layout differs slightly from Dafin's.
"""
import subprocess, sys
from pathlib import Path
import numpy as np
from skimage.metrics import structural_similarity as ssim
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import render, scene_core as core  # noqa: E402

FR = [10, 20, 40, 65, 135]
for name in sys.argv[1:]:
    p = HERE.parent / 'scenes' / f'{name}.scene.json'
    subprocess.run([str(HERE / 'gold_check'), str(p)] + [str(f) for f in FR], check=True)
    r = render.make_renderer(core.load_scene(p))
    res = []
    for f in FR:
        a = np.zeros((1080, 1920, 3), np.uint8)
        r.draw_direct(a, f / 25)
        b = np.fromfile(f'out_{f:04d}.rgb', np.uint8).reshape(1080, 1920, 3)
        m = (a.max(axis=2) > 8) | (b.max(axis=2) > 8)
        if not m.any():
            res.append('empty')
            continue
        ys, xs = np.where(m)
        y0, y1, x0, x1 = max(0, ys.min() - 10), ys.max() + 11, max(0, xs.min() - 10), xs.max() + 11
        res.append(f'{ssim(a[y0:y1, x0:x1].mean(axis=2), b[y0:y1, x0:x1].mean(axis=2), data_range=255):.3f}')
    print(name, ' '.join(res), flush=True)
