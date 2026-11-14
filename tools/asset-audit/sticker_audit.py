"""Render Lottie stickers with rlottie and check that they really animate.

usage: python3 -I sticker_audit.py <folder with .json stickers> [contact_sheet.png]
Checks: header (v/fr/w/h/op), banned features, slots + sid use, 5 frames rendered, motion between
frames, last frame non-empty, art stays inside the canvas margin. pip install rlottie-python pillow numpy
"""
import sys, glob, os, json, re
import numpy as np
from PIL import Image
from rlottie_python import LottieAnimation

BANNED_LAYER = {0: 'precomp', 2: 'image', 5: 'text', 6: 'audio'}
def walk(o, f):
    if isinstance(o, dict):
        f(o); [walk(v, f) for v in o.values()]
    elif isinstance(o, list): [walk(v, f) for v in o]

def strip_last_easing(o):
    """The final keyframe of an animated property needs no easing handles (they have no segment to
    shape); rlottie renders such a property as nothing, so drop them. Safe for every renderer."""
    if isinstance(o, dict):
        k = o.get('k')
        if o.get('a') == 1 and isinstance(k, list) and k and isinstance(k[-1], dict):
            k[-1].pop('o', None); k[-1].pop('i', None)
        for v in o.values(): strip_last_easing(v)
    elif isinstance(o, list):
        for v in o: strip_last_easing(v)
    return o

def resolve_slots(d, override=None):
    """rlottie ignores slots (Skottie, which Dafin uses, resolves them), so paste each slot's value in."""
    slots = d.get('slots', {})
    def sub(o):
        if isinstance(o, dict):
            sid = o.get('sid')
            if sid in slots and 'k' not in o:
                p = dict(slots[sid]['p'])
                if override and sid in override: p = {'a': 0, 'k': override[sid]}
                o = {k: v for k, v in o.items() if k != 'sid'}; o.update(p)
            return {k: sub(v) for k, v in o.items()}
        if isinstance(o, list): return [sub(v) for v in o]
        return o
    return strip_last_easing(sub(d))

def audit(path):
    d = json.load(open(path, encoding='utf-8')); issues = []
    for k, v in (('fr', 30), ('w', 512), ('h', 512)):
        if d.get(k) != v: issues.append(f'{k}={d.get(k)}')
    if not (30 <= d.get('op', 0) <= 120): issues.append(f"op={d.get('op')}")
    if d.get('assets'): issues.append('assets not empty')
    slots = d.get('slots', {})
    if set(slots) != {'primaryColor', 'accentColor'}: issues.append(f'slots={list(slots)}')
    sids, fx, banned, mm, expr, hard = set(), 0, set(), 0, 0, 0
    def f(o):
        nonlocal fx, mm, expr, hard
        if 'sid' in o: sids.add(o['sid'])
        if 'ef' in o and o['ef']: fx += 1
        if o.get('ty') == 'mm': mm += 1
        if 'x' in o and isinstance(o['x'], str): expr += 1
        if 'tm' in o and not isinstance(o.get('tm'), dict) and o.get('ty') != 'tm': pass
    walk(d, f)
    for l in d['layers']:
        if l.get('ty') in BANNED_LAYER: banned.add(BANNED_LAYER[l['ty']])
        if l.get('tm') is not None and not isinstance(l['tm'], dict): issues.append('time remap')
        if l.get('ddd'): issues.append('3d')
    if fx: issues.append('effects')
    if mm: issues.append('merge paths')
    if expr: issues.append('expressions')
    if banned: issues.append('layers:' + ','.join(banned))
    if not sids: issues.append('no sid use')
    a = LottieAnimation.from_data(json.dumps(resolve_slots(d))); n = a.lottie_animation_get_totalframe()
    idx = [0, n // 4, n // 2, 3 * n // 4, n - 1]
    fr = [np.array(a.render_pillow_frame(i).convert('RGBA').resize((256, 256), Image.LANCZOS)).astype(float) for i in idx]
    alpha = [(f_[..., 3] > 8) for f_ in fr]
    cov = [float(x.mean()) for x in alpha]
    diffs = [float(np.abs(fr[i + 1] - fr[i]).mean()) for i in range(4)]
    total = float(np.abs(fr[-1] - fr[0]).mean())
    if cov[-1] < 0.004: issues.append(f'last frame nearly empty ({cov[-1]:.4f})')
    if sum(diffs) < 0.6: issues.append(f'barely moves (sum diff {sum(diffs):.2f})')
    if total < 0.15: issues.append('first==last')
    m = 6  # 24px margin at 512 -> 12px at 256
    edge = max(float(x[:12].mean()) + float(x[-12:].mean()) + float(x[:, :12].mean()) + float(x[:, -12:].mean()) for x in alpha)
    if edge > 0.0005 * 4: issues.append(f'touches margin ({edge:.4f})')
    # recolour: changing both slots must change the picture
    b = LottieAnimation.from_data(json.dumps(resolve_slots(d, {'primaryColor': [0, 0, 1, 1], 'accentColor': [1, 0, 1, 1]})))
    rc = np.array(b.render_pillow_frame(n - 1).convert('RGBA').resize((256, 256), Image.LANCZOS)).astype(float)
    if float(np.abs(rc - fr[-1]).mean()) < 0.3 and cov[-1] > 0.004: issues.append('slots do not recolour')
    return dict(id=os.path.basename(path), n=n, cov=cov, diffs=diffs, total=total, edge=edge, issues=issues, fr=fr)

if __name__ == '__main__':
    files = sorted(glob.glob(os.path.join(sys.argv[1], '*.json')))
    files = [f for f in files if os.path.basename(f) != 'index_rows.json']
    res = []
    for f in files:
        try: res.append(audit(f))
        except Exception as e: res.append(dict(id=os.path.basename(f), issues=[f'ERROR {e}'], fr=None))
    bad = [r for r in res if r['issues']]
    for r in res:
        print(f"{'BAD ' if r['issues'] else 'ok  '}{r['id']:48s}", '' if r['fr'] is None else f"n={r['n']} cov_last={r['cov'][-1]:.3f} move={sum(r['diffs']):.1f} first-last={r['total']:.1f}", '; '.join(r['issues']))
    print(len(res), 'stickers,', len(bad), 'with issues')
    if len(sys.argv) > 2:
        ok = [r for r in res if r['fr'] is not None]
        cell = 96; cols = 5
        W = Image.new('RGB', (cell * cols * 1 + 0, 1), 'white')
        rows = len(ok)
        sheet = Image.new('RGB', (cell * cols * 2 + 0, cell * ((rows + 1) // 2) * 1), (60, 60, 70))
        # each sticker one row of 5 frames at cell size, two stickers per row
        sheet = Image.new('RGB', (cell * cols * 2, cell * ((rows + 1) // 2)), (70, 70, 80))
        for i, r in enumerate(ok):
            x0 = (i % 2) * cell * cols; y0 = (i // 2) * cell
            for j, f_ in enumerate(r['fr']):
                im = Image.fromarray(f_.astype('uint8'), 'RGBA').resize((cell, cell))
                sheet.paste(im, (x0 + j * cell, y0), im)
        sheet.save(sys.argv[2])
