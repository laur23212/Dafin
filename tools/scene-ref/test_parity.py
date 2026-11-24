#!/usr/bin/env python3
"""Parity of the ported scenes with the approved scripts that made them, frame by frame, before
any encoding. Needs the scripts from the design archive (not in the repo):

  python3 -I test_parity.py --stage15 <.../Stage15_InstructionalTitles_Delta_v6.1.6/source> \
                            --stage03b <.../Stage03b_Alternative_LowerThird_Families_v1/source>

                            [--stage13 <folder with frozen_stage13_approved_renderer_reference.py>]

Exit code 0 only if every compared frame is identical. Skips an example whose script is not given.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import render  # noqa: E402
import scene_core as core  # noqa: E402


def load_module(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, str(path))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def diff(a, b):
    d = np.abs(a.astype(np.int16) - b.astype(np.int16))
    return int(d.max()), int((d > 0).sum())


def check_stage15(src: Path, frames):
    m = load_module(src / 'render_stage15.py', 'stage15_original')
    sc = core.load_scene(HERE / 'scenes' / '15_A_Step_Focus.scene.json')
    job = render.Job(sc)
    bad = []
    for i in frames:
        a = m.frame(m.scene_a, i / 25)
        b = job.direct_frame(i)
        mx, n = diff(a, b)
        if mx:
            bad.append({'frame': i, 'max': mx, 'pixels': n})
    return {'example': '15_A_Step_Focus', 'frames': len(frames), 'mismatched': bad}


def check_stage03b(src: Path, frames):
    m = load_module(src / 'render.py', 'stage03b_original')
    sc = core.load_scene(HERE / 'scenes' / '03b_LT_B_25.scene.json')
    job = render.Job(sc)
    x, y, w, h = sc.raw['preview']['legacy']['overlayRect']
    bad = []
    for i in frames:
        a = np.array(m.design_b(m.D['16x9'], i / 25))
        b = job.alpha_frame(i)
        outside = b.copy(); outside[y:y + h, x:x + w] = 0
        mx, n = diff(a, b[y:y + h, x:x + w])
        if mx or outside[:, :, 3].any():
            bad.append({'frame': i, 'max': mx, 'pixels': n, 'inkOutsideLayer': bool(outside[:, :, 3].any())})
    return {'example': '03b_LT_B_25', 'frames': len(frames), 'mismatched': bad}


def check_stage13_primitives(src: Path):
    """Shapes the two ports do not use, against the frozen Stage13 helpers (and Stage15's plate)."""
    b = load_module(src / 'frozen_stage13_approved_renderer_reference.py', 'stage13_frozen')
    s15 = load_module(src / 'render_stage15.py', 'stage15_original') if (src / 'render_stage15.py').exists() else None
    OFF, AQUA = list(b.OFF), list(b.AQUA)
    raw = {'format': core.FORMAT_ID, 'version': 1, 'id': 'prims', 'canvas': {'width': 1920, 'height': 1080},
           'timing': {'duration': 6, 'fps': 25}, 'render': {'profile': 'sprite2x'},
           'fonts': {'semi': {'file': Path(b.FONT_SEMI).name}},
           'actors': [
               {'id': 'key', 'type': 'group', 'flatten': True, 'at': [347, 568], 'size': [213, 107], 'children': [
                   {'id': 'cap', 'type': 'roundRect', 'box': [2, 2, 210, 104], 'radius': 14, 'fill': [23, 32, 39, 244],
                    'strokeColor': AQUA + [255], 'stroke': 2},
                   {'id': 'label', 'type': 'text', 'text': 'CTRL', 'font': 'semi', 'size': 43, 'color': OFF,
                    'align': {'h': 'center', 'v': 'center', 'dy': -2}}]},
               {'id': 'ring', 'type': 'ring', 'box': [402, 440, 452, 490], 'color': AQUA, 'stroke': 2},
               {'id': 'plate', 'type': 'group', 'flatten': True, 'at': [382, 703], 'size': [1112, 217], 'children': [
                   {'id': 'body', 'type': 'roundRect', 'box': [2, 2, 1109, 214], 'radius': 22, 'fill': [27, 36, 44, 245],
                    'strokeColor': [80, 99, 107, 232], 'stroke': 2},
                   {'id': 'lip', 'type': 'line', 'from': [28, 8], 'to': [1084, 8], 'color': [171, 188, 190], 'alpha': 52, 'width': 2}]},
               {'id': 'check', 'type': 'route', 'points': [[414, 464], [424, 473], [440, 454]], 'color': AQUA, 'width': 4,
                'in': {'start': 1, 'dur': 0.38, 'from': {'progress': 0}}},
           ]}
    sc = core.load_scene(raw)
    r = render.Sprite2x(sc)
    by = {a['id']: a for a in sc.actors}
    res = {}
    pairs = [('key', r.group_sprite(by['key'], [by['cap'], by['label']]), b.keycap_sprite('CTRL', 213)),
             ('ring', r.shape_sprite(by['ring'])[0], b.check_ring_sprite())]
    if s15:
        pairs.append(('plate', r.group_sprite(by['plate'], [by['body'], by['lip']]), s15.rounded_plate(1112, 217)))
    for name, mine, theirs in pairs:
        res[name] = bool(np.array_equal(mine.rgb, theirs.rgb) and np.array_equal(mine.alpha, theirs.alpha))
    ok = True
    for t in (1.0, 1.1, 1.2, 1.3, 1.5):
        c1 = b.BASE.copy(); b.route(c1, [(414, 464), (424, 473), (440, 454)], b.enter(t, 1, .38), b.AQUA, 4)
        c2 = b.BASE.copy(); r._draw(by['check'], core.channels(by['check'], t), c2, None)
        ok &= bool(np.array_equal(c1, c2))
    res['route'] = ok
    return {'example': 'stage13 primitives', 'frames': 0, 'mismatched': [k for k, v in res.items() if not v], 'checked': sorted(res)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--stage15', type=Path)
    ap.add_argument('--stage03b', type=Path)
    ap.add_argument('--stage13', type=Path, help='folder with frozen_stage13_approved_renderer_reference.py')
    ap.add_argument('--frames', default='all')
    o = ap.parse_args()
    frames = list(range(150)) if o.frames == 'all' else [int(v) for v in o.frames.split(',')]
    res = []
    if o.stage15:
        res.append(check_stage15(o.stage15, frames))
    if o.stage03b:
        res.append(check_stage03b(o.stage03b, frames))
    if o.stage13:
        res.append(check_stage13_primitives(o.stage13))
    print(json.dumps(res, indent=1))
    return 1 if any(r['mismatched'] for r in res) or not res else 0


if __name__ == '__main__':
    sys.exit(main())
