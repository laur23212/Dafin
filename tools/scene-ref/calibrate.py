#!/usr/bin/env python3
"""Calibrates compare.py's thresholds: renders the scene as is and with small deliberate errors,
compares each with the reference MP4 and prints the worst actor SSIM / position error per case.
A good threshold passes the faithful render and fails every mutation.

  python3 calibrate.py --scene scenes/15_A_Step_Focus.scene.json --ref 15_A_Step_Focus.mp4 \
                       --out calib/ [--assets <background folder>] --actor title --font-swap semi=regular
"""
from __future__ import annotations

import argparse
import copy
import csv
import json
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import compare  # noqa: E402
import render  # noqa: E402
import scene_core as core  # noqa: E402


def mutations(raw, actor, font_swap):
    def find(r):
        for a in r['actors']:
            if a['id'] == actor:
                return a
        raise SystemExit(f'no actor {actor}')

    def moved(px):
        r = copy.deepcopy(raw); a = find(r)
        at = a['at']; k = next(iter(at)); at[k] = [at[k][0] + px, at[k][1]]
        return r

    yield 'faithful', copy.deepcopy(raw)
    yield f'{actor} +1px right', moved(1)
    yield f'{actor} +2px right', moved(2)
    if font_swap:
        old, new = font_swap.split('=')
        r = copy.deepcopy(raw); a = find(r)
        if a.get('font') == old:
            a['font'] = new
            yield f'{actor} font {old}->{new}', r
    r = copy.deepcopy(raw)
    for a in r['actors']:
        for blk in ('in', 'out'):
            if blk in a and isinstance(a[blk].get('ease', 'smoothstep'), str):
                a[blk]['ease'] = 'linear'
    yield 'all eases linear', r
    r = copy.deepcopy(raw)
    for a in r['actors']:
        if 'in' in a and isinstance(a['in']['start'], (int, float)):
            a['in']['start'] = round(a['in']['start'] + 0.08, 4)
    yield 'entrances 2 frames late', r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--scene', required=True); ap.add_argument('--ref', required=True)
    ap.add_argument('--out', required=True); ap.add_argument('--assets', action='append', default=[])
    ap.add_argument('--actor', required=True); ap.add_argument('--font-swap')
    ap.add_argument('--frames', default=','.join(map(str, compare.DEFAULT_FRAMES)))
    o = ap.parse_args()
    raw = json.loads(Path(o.scene).read_text(encoding='utf-8'))
    out = Path(o.out); out.mkdir(parents=True, exist_ok=True)
    results = []
    for name, r in mutations(raw, o.actor, o.font_swap):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / 'scene.json'
            p.write_text(json.dumps(r, ensure_ascii=False), encoding='utf-8')
            sc = core.load_scene(p)
            job = render.Job(sc, assets=o.assets or [Path(o.scene).parent])
            mp4 = job.preview_mp4(Path(td) / 'test.mp4')
            rep = out / name.replace(' ', '_').replace('>', '').replace('+', 'p')
            compare.main(['--ref', o.ref, '--test', str(mp4), '--scene', str(p), '--out', str(rep), '--frames', o.frames])
            rows = list(csv.DictReader(open(rep / 'report.csv', encoding='utf-8')))
            act = [x for x in rows if x['actor'] != '*frame*' and x['pos_err'] != '']
            results.append({'case': name,
                            'actor_ssim_min': min(float(x['ssim']) for x in act),
                            'actor_pos_err_max': max(float(x['pos_err']) for x in act),
                            'frame_psnr_min': min(float(x['psnr']) for x in rows if x['actor'] == '*frame*')})
    (out / 'calibration.json').write_text(json.dumps(results, indent=1) + '\n', encoding='utf-8')
    print(json.dumps(results, indent=1))


if __name__ == '__main__':
    main()
