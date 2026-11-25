"""Smart Keycaps: the keyboard-shortcut generator of Scene Title v1 (docs/scene-keycaps.md).

A `keycaps` actor is one block of keys: a shortcut written as text ("Ctrl+K THEN Ctrl+C",
"Ctrl+Y OR Ctrl+Shift+Z") turned into keycaps, "+" signs and "THEN" / "OR" words. It is a
generator, not a primitive: it expands into the actors the format already has (a flattened group
per keycap, a text per sign), in two phases, so a scene that holds one still loads without any
font.

  Phase A, expand_block(): at load time, no fonts. Which keys, which legends, which actors and
                           ids, and every timing. The number of actors never depends on the text
                           widths (a "+" that falls at the start of a wrapped row is hidden, not
                           removed).
  Phase B, layout():       once the text can be measured. Key widths, rows, x/y and the geometry
                           of each keycap's children.

Both phases are mirrored value for value by src/core/scene/SceneKeycaps.cpp. The only thing the
layout needs from a renderer is ink(font, px, text): PIL's getbbox(text, anchor="ls") at that
exact pixel size, the ink box relative to the baseline origin.
"""
from __future__ import annotations

import copy
import json
import math
import re
from functools import lru_cache
from pathlib import Path

from scene_core import SceneError, eval_expr, format_text

HERE = Path(__file__).resolve().parent
DICTIONARY_PATH = HERE / 'keycaps' / 'key-dictionary.json'
SS = 2  # the text sprites of sprite2x (a separator is a plain text actor)
GLYPHS = {'⌘', '⌥', '⇧', '⌃'}
PLATFORMS = ('windows', 'macos')
POLICIES = ('names', 'glyph', 'glyphPlusNames', 'physical')


@lru_cache(maxsize=1)
def dictionary() -> dict:
    d = json.loads(DICTIONARY_PATH.read_text(encoding='utf-8'))
    if d.get('format') != 'dafin.keycap-dictionary' or d.get('version') != 1:
        raise SceneError('keycaps: not a keycap dictionary v1')
    return d


@lru_cache(maxsize=1)
def _alias_index() -> dict:
    idx = {}
    for kid, k in dictionary()['keys'].items():
        for a in k.get('aliases', []):
            idx[a.lower()] = kid
        idx[kid] = kid
    return idx


# ----------------------------------------------------------------------------------------------
# Shortcut text -> alternatives (OR) -> sequence of chords (THEN) -> keys (+)

_OR = re.compile(r'\s+OR\s+', re.I)
_THEN = re.compile(r'\s+THEN\s+', re.I)


def parse_shortcut(text: str) -> list:
    """[[ [key, key], [key] ], ...]: alternatives of sequences of chords. Keys are raw tokens."""
    text = (text or '').strip()
    if not text:
        raise SceneError('keycaps: the shortcut is empty')
    alts = []
    for alt in _OR.split(text):
        seq = []
        for chord in _THEN.split(alt):
            keys = [k.strip() for k in chord.split('+')]
            if not keys or any(not k for k in keys):
                raise SceneError(f'keycaps: empty key in {chord!r} (a "+" key is written "plus")')
            seq.append(keys)
        alts.append(seq)
    return alts


class Key:
    """One resolved key: identity + what it prints on this platform with this policy."""
    __slots__ = ('ident', 'primary', 'secondary', 'glyph', 'wide', 'custom')

    def __init__(self, ident, primary, secondary, glyph, wide, custom):
        self.ident, self.primary, self.secondary = ident, primary, secondary
        self.glyph, self.wide, self.custom = glyph, wide, custom


def resolve_key(token: str, platform: str, policy: str, legends: dict) -> Key:
    dic = dictionary()['keys']
    if token.lower().startswith('custom:'):
        label = token.split(':', 1)[1]
        if not label:
            raise SceneError('keycaps: custom: needs a legend')
        return Key('custom:' + label, label, '', False, False, True)
    ident = _alias_index().get(token.lower()) if len(token) > 1 or token in GLYPHS else None
    if ident is None and len(token) == 1:
        return Key(token.lower(), token.upper(), '', False, False, False)
    if ident is None and re.fullmatch(r'[fF]([1-9]|1[0-9]|2[0-4])', token):
        return Key(token.lower(), token.upper(), '', False, False, False)
    if ident is None:
        raise SceneError(f'keycaps: unknown key {token!r}; use custom:{token} for a legend of your own')
    spec = dic[ident]
    if 'neutral' in spec:                      # "mod": Ctrl on Windows, Command on macOS
        ident = spec['neutral'][platform]
        spec = dic[ident]
    wide = bool(spec.get('wide'))
    if platform == 'windows':
        if spec.get('pc') is None:
            raise SceneError(f'keycaps: {ident!r} is a macOS key and has no place on a Windows block')
        return Key(ident, spec['pc'], '', False, wide, False)
    mac = spec.get('mac')
    if mac is None:
        raise SceneError(f'keycaps: {ident!r} has no macOS legend')
    if policy == 'physical':
        name = legends.get(ident) or spec.get('pc') or mac.get('plain', ident).upper()
        return Key(ident, name, '', False, wide, False)
    glyph = mac.get('glyph')
    if glyph and policy == 'glyph':
        return Key(ident, glyph, '', True, wide, False)
    if glyph and policy == 'glyphPlusNames':
        return Key(ident, glyph, mac.get('dual', ''), True, wide, False)
    return Key(ident, mac.get('plain', ident), '', False, wide, False)


# ----------------------------------------------------------------------------------------------
# Phase A: expansion

def _timed(start, dur, ease, frm, to_start, to_dur):
    return ({'start': round(start, 6), 'dur': dur, 'ease': ease or 'smoothstep', 'from': frm},
            {'start': round(to_start, 6), 'dur': to_dur, 'ease': 'smoothstep', 'to': {'opacity': 0}})


def _round_even(v: float) -> int:
    return int(math.ceil(v / 2) * 2)


def expand_block(a: dict, sc, env: dict) -> list:
    """Phase A. `a` is the generator actor with expressions and text slots already evaluated."""
    bid = a['id']
    platform = a.get('platform', 'windows')
    if platform not in PLATFORMS:
        raise SceneError(f'{bid}: platform must be one of {PLATFORMS}')
    skin_name = a.get('skin') or ('classic-pc' if platform == 'windows' else 'classic-mac')
    skin = dictionary()['skins'].get(skin_name)
    if skin is None:
        raise SceneError(f'{bid}: unknown skin {skin_name!r}')
    policy = a.get('labelPolicy', 'names' if platform == 'windows' else 'glyphPlusNames')
    if policy not in POLICIES:
        raise SceneError(f'{bid}: labelPolicy must be one of {POLICIES}')
    legends = a.get('legends', {})
    sep_style = {**skin['sep'], **a.get('sep', {})}   # per block: Stage13 A signs are 43 px, Stage13C's 41
    parsed = parse_shortcut(a.get('shortcut', ''))
    tm = a.get('timing', {})
    kt, st, jt, lt = tm.get('key', {}), tm.get('sep', {}), tm.get('joiner', {}), tm.get('label', {})
    kin, kout = kt.get('in', {}), kt.get('out', {})
    out, blk = [], {'id': bid, 'a': a, 'skin': skin, 'skinName': skin_name, 'platform': platform,
                    'sep': sep_style, 'alts': [], 'joiners': [], 'label': None}

    def base(actor_id, role, typ, **kw):
        d = {'id': actor_id, 'role': role, 'type': typ, '_env': env, '_parent': None, '_kc': bid}
        d.update(kw)
        return d

    if a.get('label'):
        lb = a['label']
        t = lt.get('start', 0.7)
        fin, fout = _timed(t, lt.get('dur', 0.38), None, {'opacity': 0, 'dy': lt.get('dy', 7)},
                           lt.get('outStart', 4.86), lt.get('outDur', 0.34))
        out.append(base(f'{bid}.label', 'keycapsLabel', 'text', text=lb['text'], font=lb.get('font', 'semi'),
                        size=lb.get('size', 25), color=lb.get('color', 'AQUA'),
                        at={'baseline': [0, 0]}, **{'in': fin, 'out': fout}, _spriteAt=list(lb['at'])))
        blk['label'] = f'{bid}.label'

    for ai, seq in enumerate(parsed):
        alt_meta = []
        if ai > 0:
            jid = f'{bid}.or{ai}'
            t = jt.get('orStart', 1.35) + jt.get('orStep', 0.32) * (ai - 1)
            fin, fout = _timed(t, jt.get('dur', 0.38), None, {'opacity': 0, 'dy': jt.get('dy', 7)},
                               jt.get('outStart', 4.86), jt.get('outDur', 0.34))
            out.append(base(jid, 'keycapsJoiner', 'text', text=a.get('orWord') or dictionary()['joiners']['or'],
                            font=jt.get('font', 'semi'), size=jt.get('size', 23), color=jt.get('color', 'AQUA'),
                            at={'baseline': [0, 0]}, **{'in': fin, 'out': fout}))
            blk['joiners'].append({'id': jid, 'kind': 'or', 'alt': ai})
        for ci, chord in enumerate(seq):
            if ci > 0:
                jid = f'{bid}.a{ai}then{ci}'
                t = jt.get('thenStart', 1.35) + jt.get('thenStep', 0.38) * (ci - 1)
                fin, fout = _timed(t, jt.get('dur', 0.38), None, {'opacity': 0, 'dy': jt.get('dy', 7)},
                                   jt.get('outStart', 4.86), jt.get('outDur', 0.34))
                out.append(base(jid, 'keycapsJoiner', 'text', text=a.get('thenWord') or dictionary()['joiners']['then'],
                                font=jt.get('font', 'semi'), size=jt.get('size', 23), color=jt.get('color', 'AQUA'),
                                at={'baseline': [0, 0]}, **{'in': fin, 'out': fout}))
                blk['joiners'].append({'id': jid, 'kind': 'then', 'alt': ai, 'chord': ci})
            keys = [resolve_key(tok, platform, policy, legends) for tok in chord]
            nk = len(keys)
            key_starts = [kt.get('start', 0.85) + kt.get('altStep', 0.0) * ai + kt.get('chordStep', 0.0) * ci
                          + kt.get('keyStep', 0.17) * ki for ki in range(nk)]
            chord_meta = {'keys': [], 'seps': []}
            for ki, key in enumerate(keys):
                if ki > 0:
                    sid = f'{bid}.a{ai}c{ci}p{ki}'
                    rel = st.get('rel', 'mid')
                    lst = st.get('list')
                    left, right = key_starts[ki - 1], key_starts[ki]
                    if lst and len(lst) >= ki and lst[ki - 1] is not None and nk == st.get('listFor', nk):
                        s0 = lst[ki - 1]
                    else:
                        s0 = {'left': left, 'right': right, 'mid': (left + right) / 2}[rel] + st.get('off', 0.04)
                    sin = st.get('in', {})
                    sout = st.get('out', {})
                    fin, fout = _timed(s0, sin.get('dur', 0.32), None, {'opacity': 0}, sout.get('start', 4.75), sout.get('dur', 0.30))
                    sp = sep_style
                    out.append(base(sid, 'keycapsSeparator', 'text', text='+', font=sp['font'], size=sp['size'],
                                    color=sp['color'], at={'baseline': [0, 0]}, **{'in': fin, 'out': fout}))
                    chord_meta['seps'].append(sid)
                gid = f'{bid}.a{ai}c{ci}k{ki}'
                snap = kin.get('snap')
                frm = {'opacity': 0}
                if kin.get('dy'):
                    frm['dy'] = kin['dy']
                out_start = kout.get('start', 4.72) + kout.get('revStep', 0.0) * (nk - 1 - ki)
                fin, fout = _timed(key_starts[ki], kin.get('dur', 0.38), kin.get('ease'), frm, out_start, kout.get('dur', 0.30))
                grp = base(gid, 'keycap', 'group', flatten=True, at=[0, 0], size=[0, 0], ss=skin['ss'],
                           **{'in': fin, 'out': fout}, _childIds=[])
                if snap:
                    grp['snap'] = snap
                out.append(grp)
                kids = {}
                kids['plate'] = base(f'{gid}.plate', 'keycapPlate', 'roundRect', box=[0, 0, 0, 0], radius=skin['radius'],
                                     fill=list(skin['fill']), strokeColor=skin['stroke'], stroke=skin['strokeWidth'], _parent=gid)
                if skin.get('shine'):
                    sh = skin['shine']
                    kids['shine'] = base(f'{gid}.shine', 'keycapShine', 'line', **{'from': [0, 0], 'to': [0, 0]}, width=sh['width'],
                                         color=list(sh['color']), _parent=gid)
                pm = skin['primary']
                sym = key.primary in GLYPHS
                pfont = pm['symbolFont'] if sym else pm['font']
                kids['text'] = base(f'{gid}.text', 'keycapLabel', 'text', text=key.primary, font=pfont, size=0,
                                    color=skin['textColor'], at={'baseline': [0, 0]}, _parent=gid)
                if key.secondary:
                    kids['text2'] = base(f'{gid}.text2', 'keycapSecondary', 'text', text=key.secondary,
                                         font=skin['dual']['secondaryFont'], size=skin['dual']['secondarySize'],
                                         color=list(skin['dual']['secondaryColor']), at={'baseline': [0, 0]}, _parent=gid)
                for k in kids.values():
                    out.append(k)
                    grp['_childIds'].append(k['id'])
                chord_meta['keys'].append({'gid': gid, 'key': key, 'kids': {n: k['id'] for n, k in kids.items()}})
            alt_meta.append(chord_meta)
        blk['alts'].append(alt_meta)
    blk['ids'] = [x['id'] for x in out]
    sc.keycap_blocks = getattr(sc, 'keycap_blocks', [])
    sc.keycap_blocks.append(blk)
    return out


# ----------------------------------------------------------------------------------------------
# Phase B: geometry

def _ink(measure, font, px, text):
    l, t, r, b = measure(font, int(px), text)
    return l, t, r, b


def _sprite_wh(ink2x):
    l, t, r, b = ink2x
    return (r - l + 14 * SS) // SS, (b - t + 16 * SS) // SS


def _sprite_to_baseline(sx, sy, ink2x):
    """docs/scene-titles.md, Text anchors, sprite2x: top-left of the sprite -> pen x, baseline y."""
    l, t, r, b = ink2x
    return sx + (14 - l) / SS, sy + (16 - t) / SS


def _set_text_at(actor, measure, sx, sy):
    ink2x = _ink(measure, actor['font'], actor['size'] * SS, actor['text'])
    actor['at'] = {'baseline': [*_sprite_to_baseline(sx, sy, ink2x)]}


def key_width(key: Key, skin: dict, measure) -> int:
    w = skin['width']
    model = w['model']
    prim_px = skin['primary']['size']
    if model == 'ink':
        ink = _ink(measure, w['font'], w['size'], key.primary)
        v = max(w['min'], (ink[2] - ink[0]) + w['pad'])
        if key.wide:
            v = max(v, w.get('wideMin', 355))
        return _round_even(v)
    if model == 'inkMac':
        v = w['min']
        if key.secondary:
            ink = _ink(measure, w['dualFont'], w['dualSize'], key.secondary)
            v = max(v, (ink[2] - ink[0]) + w['dualPad'])
        if key.wide:
            v = max(v, w['space'])
        # overflow guard, not in the proofs (it never fires on them): a legend must clear the plate
        pm = skin['primary']
        px = pm['symbolSize'] if key.glyph else pm['size']
        ink = _ink(measure, pm['symbolFont'] if key.glyph else pm['font'], px, key.primary)
        v = max(v, (ink[2] - ink[0]) + 44)
        return _round_even(v)
    if model == 'semantic':
        spec = dictionary()['keys'].get(key.ident)
        base_w = spec['w'] if spec else w['default']
        pm = skin['primary']
        pt = pm['size'] if len(key.primary) < pm.get('longFrom', 99) else pm.get('longSize', pm['size'])
        font = pm['symbolFont'] if key.glyph else pm['font']
        ink = _ink(measure, font, pt, key.primary)
        textw = ink[2] - ink[0]
        if key.primary in ('Command', 'Option'):
            base_w = max(base_w, w.get('plainCommandMin', 218))
        sec = 0
        if key.secondary:
            si = _ink(measure, w['dualFont'], w['dualSize'], key.secondary)
            sec = si[2] - si[0]
        base_w = max(base_w, textw + w['textPad'], sec + w['dualPad'])
        return _round_even(base_w)
    raise SceneError(f'unknown width model {model!r}')


def _lock_applies(lock, chord_keys, n_alts, n_chords) -> bool:
    return bool(lock) and n_alts == 1 and n_chords == 1 and [k['key'].ident for k in chord_keys] == lock['keys']


def layout(sc, measure) -> None:
    """Phase B for every block of the scene. Idempotent: run it again after any text change."""
    by_id = {x['id']: x for x in sc.actors}
    for blk in getattr(sc, 'keycap_blocks', []):
        _layout_block(blk, by_id, measure)


def _layout_block(blk, by_id, measure):
    a, skin = blk['a'], blk['skin']
    flow = a.get('flow', {})
    x0, y0 = a['at']
    lock = a.get('lock')
    gap, plus_dx = flow.get('gap', 32), flow.get('plusDx', 16)   # gap: space around the sign, besides its own width
    max_w = flow.get('maxWidth')
    row_pitch, chord_pitch = flow.get('rowPitch', 160), flow.get('chordPitch', 164)
    or_pitch, then_pitch = flow.get('orPitch', 74), flow.get('thenPitch', 72)
    jx, or_dy, then_dy = flow.get('joinerDx', 8), flow.get('orDy', -7), flow.get('thenDy', -4)
    sep_dy_mode = flow.get('plusDy', 'center')
    if blk['label']:
        lab = by_id[blk['label']]
        _set_text_at(lab, measure, *lab['_spriteAt'])
    sp = blk['sep']
    plus_w, plus_h = _sprite_wh(_ink(measure, sp['font'], sp['size'] * SS, '+'))
    adv = gap + plus_w
    y = y0
    joiners = {(j['kind'], j['alt'], j.get('chord')): j['id'] for j in blk['joiners']}
    for ai, alt in enumerate(blk['alts']):
        if ai > 0:
            _set_text_at(by_id[joiners[('or', ai, None)]], measure, x0 + jx, y + or_dy)
            y += or_pitch
        for ci, chord in enumerate(alt):
            if ci > 0:
                _set_text_at(by_id[joiners[('then', ai, ci)]], measure, x0 + jx, y + then_dy)
                y += then_pitch
            keys = chord['keys']
            locked = _lock_applies(lock, keys, len(blk['alts']), len(alt))
            row, end = 0, x0
            for ki, k in enumerate(keys):
                w = lock['w'][ki] if locked else key_width(k['key'], skin, measure)
                h = a.get('height') or (skin['dualHeight'] if k['key'].secondary and skin.get('dualHeight') else skin['height'])
                hidden = False
                if locked:
                    kx, ky = lock['x'][ki], y
                elif ki == 0:
                    kx, ky = x0, y
                elif max_w and end + adv + w > x0 + max_w:
                    row, kx, hidden = row + 1, x0, True
                    ky = y + row * row_pitch
                else:
                    kx, ky = end + adv, y + row * row_pitch
                if ki > 0:
                    sep = by_id[chord['seps'][ki - 1]]
                    sep['_hidden'] = hidden
                    if not hidden:
                        if locked:
                            sx, sy = lock['sep'][ki - 1]
                        else:
                            sx = end + plus_dx
                            sy = ky + (int((h - plus_h) / 2) if sep_dy_mode == 'center' else sep_dy_mode)
                        _set_text_at(sep, measure, sx, sy)
                _place_key(by_id, k, skin, kx, ky, w, h, measure)
                end = kx + w
            y += (row + 1) * chord_pitch


def _place_key(by_id, k, skin, x, y, w, h, measure):
    key, ss = k['key'], skin['ss']
    g = by_id[k['gid']]
    g['at'] = [x, y]
    g['size'] = [w, h]
    pad = skin['pad']
    plate = by_id[k['kids']['plate']]
    plate['box'] = [pad, pad, w - pad - 1, h - pad - 1]
    if 'shine' in k['kids']:
        sh = skin['shine']
        line = by_id[k['kids']['shine']]
        line['from'] = [sh['inset'], sh['y']]
        line['to'] = [w - sh['inset'], sh['y']]
    pm = skin['primary']
    text = by_id[k['kids']['text']]
    if key.secondary:
        d = skin['dual']
        size = d['symbolSize'] if key.glyph else d['primarySize']
        text['size'] = size
        text.pop('align', None)
        l, t, r, b = _ink(measure, text['font'], size * ss, key.primary)
        text['at'] = {'baseline': [((w * ss - (r - l)) / 2 - l) / ss, (d['top'] * ss - t) / ss]}
        t2 = by_id[k['kids']['text2']]
        l, t, r, b = _ink(measure, t2['font'], t2['size'] * ss, key.secondary)
        t2['at'] = {'baseline': [((w * ss - (r - l)) / 2 - l) / ss, ((h - d['secondaryBottom']) * ss - t) / ss]}
    else:
        if key.glyph:
            size = pm['symbolSize']
        else:
            size = pm['size'] if len(key.primary) < pm.get('longFrom', 99) else pm.get('longSize', pm['size'])
        text['size'] = size
        text['at'] = {'baseline': [0, 0]}
        text['align'] = {'h': 'center', 'v': 'center', 'dy': pm['dy']}
