#!/usr/bin/env python3
"""Compare Dafin's native Scene Title renderer with an approved MP4, like for like.

  dafin --render-scene scene.json --frames all --out native/
  python3 native_compare.py --scene scene.json --native native/ --ref approved.mp4 --out report/ \
                            [--assets <preview background folder>] [--ssim 0.97] [--pos 0.5]

The native alpha frames (native/alpha/NNNN.png, every frame) are laid over the scene's own
preview background and encoded exactly the way render.py makes the reference preview (same
matte, same overlay chain, same x264 preset and CRF), then compare.py measures them against the
approved MP4 per actor. Exit code: compare.py's (1 when a gate fails), 2 on a scene error.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import compare  # noqa: E402
import render  # noqa: E402
import scene_core as core  # noqa: E402


class NativeJob(render.Job):
    """render.Job whose frames come from the native renderer's alpha PNGs."""

    def __init__(self, sc, native_dir, assets=()):
        super().__init__(sc, (), None, None, assets)
        self.native = Path(native_dir) / 'alpha'

    def alpha_frame(self, i):
        p = self.native / f'{i:04d}.png'
        if not p.exists():
            raise core.SceneError(f'native frame {p} is missing; render every frame (--frames all)')
        return np.array(Image.open(p).convert('RGBA'))

    def direct_frame(self, i):
        # The proofs' own compositing over BASE: uint8(clip(dst*(1-a) + src*a)), truncating.
        bg = render.base_matte(self.sc.width, self.sc.height, self.r.mix_color).astype(np.float32)
        f = self.alpha_frame(i).astype(np.float32)
        a = f[:, :, 3:4] / 255.
        return np.uint8(np.clip(bg * (1 - a) + f[:, :, :3] * a, 0, 255))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--scene', required=True)
    ap.add_argument('--native', required=True, help='folder written by dafin --render-scene')
    ap.add_argument('--ref', required=True, help='the approved MP4')
    ap.add_argument('--out', required=True)
    ap.add_argument('--assets', action='append', default=[])
    ap.add_argument('--param', action='append')
    ap.add_argument('--ssim', type=float, default=0.97)
    ap.add_argument('--pos', type=float, default=0.5)
    ap.add_argument('--frames')
    o = ap.parse_args(argv)
    try:
        sc = core.load_scene(o.scene, render.parse_params(o.param))
        out = Path(o.out)
        out.mkdir(parents=True, exist_ok=True)
        mp4 = NativeJob(sc, o.native, o.assets).preview_mp4(out / f'{sc.raw["id"]}.native.mp4')
    except core.SceneError as e:
        print(f'scene error: {e}', file=sys.stderr)
        return 2
    args = ['--ref', o.ref, '--test', str(mp4), '--scene', o.scene, '--out', str(out / 'compare'),
            '--ssim', str(o.ssim), '--pos', str(o.pos)]
    for p in o.param or []:
        args += ['--param', p]
    if o.frames:
        args += ['--frames', o.frames]
    return compare.main(args)


if __name__ == '__main__':
    sys.exit(main())
