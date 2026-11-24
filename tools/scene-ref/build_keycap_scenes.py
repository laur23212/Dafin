#!/usr/bin/env python3
"""Writes the Smart Keycaps scenes into scenes/: the four approved static titles
(13_A_Shortcut_Keys, 13C_U1/U2/U3) and the 18 Keyboard Shortcuts templates T01-T18.

Everything here is data for the generator (docs/scene-keycaps.md): the numbers are the ones of the
approved scripts (Stage13 `render_stage13_practical_overlays.py`, Stage13C
`render_stage13C_universal_shortcut.py`) and of the Stage13F proofs. Run it with the repo's
scene-fonts as the font folder:

  python3 -I build_keycap_scenes.py [--only 13_A,T05] [--templates DIR_WITH_13G_TEMPLATES]
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
SCENES = HERE / 'scenes'
FONT_DIR = REPO / 'scene-fonts'
sys.path.insert(0, str(HERE))

from PIL import ImageFont  # noqa: E402

FONTS = {
    'regular': {'family': 'Noto Sans Display', 'weight': 400, 'file': 'NotoSansDisplay-Regular.ttf',
                'package': 'fonts-noto-core 20201225-2',
                'sha256': '3dd456595e9226472ab8adaf9408400c671b97dbba241d883d05e2ab70ceadac'},
    'semi': {'family': 'Noto Sans Display', 'weight': 600, 'file': 'NotoSansDisplay-SemiBold.ttf',
             'package': 'fonts-noto-extra 20201225-2',
             'sha256': '9fa1f6b9fc7d95e0aa49ec75a90ce858e0a4c55b839811eb64abb7798afc94fd'},
    'symbols': {'family': 'Noto Sans Symbols 2', 'weight': 400, 'file': 'NotoSansSymbols2-Regular.ttf',
                'package': 'fonts-noto-core 20201225-2'},  # sha256 to be added with the file (see scene-fonts/README.md)
}
PALETTE = {'DARK': [9, 12, 17], 'WHITE': [242, 243, 240], 'SOFT': [165, 177, 187], 'AQUA': [123, 209, 192],
           'SILVER': [184, 194, 200], 'FAINT': [58, 69, 74]}


def sprite_baseline(font_key: str, size: int, text: str, sx: float, sy: float) -> list:
    """Top-left of the PIL sprite of a script -> canonical baseline anchor (docs/scene-titles.md)."""
    f = ImageFont.truetype(str(FONT_DIR / FONTS[font_key]['file']), size * 2)
    l, t, r, b = f.getbbox(text, anchor='ls')
    return [sx + (14 - l) / 2, sy + (16 - t) / 2]


def blocks(start, dur, end, exit_dur, dy=None):
    frm = {'opacity': 0}
    if dy:
        frm['dy'] = dy
    return {'in': {'start': start, 'dur': dur, 'ease': 'smoothstep', 'from': frm},
            'out': {'start': end, 'dur': exit_dur, 'ease': 'smoothstep', 'to': {'opacity': 0}}}


def text(aid, role, value, font, size, color, sprite_xy, *, start, dur, end, exit_dur, dy=None, slot=None, **extra):
    """`value` is the default string the baseline is measured on; `slot` binds the text to a param."""
    a = {'id': aid, 'role': role, 'type': 'text', 'text': '{%s}' % slot if slot else value, 'font': font, 'size': size,
         'color': color, 'at': {'baseline': sprite_baseline(font, size, value, *sprite_xy)}}
    a.update(blocks(start, dur, end, exit_dur, dy))
    a.update(extra)
    return a


def rule(aid, a, b, color, *, start, dur, end, exit_dur, width=1):
    return {'id': aid, 'role': 'rule', 'type': 'line', 'from': list(a), 'to': list(b), 'width': width, 'color': color,
            'in': {'start': start, 'dur': dur, 'ease': 'smoothstep', 'from': {'progress': 0}},
            'out': {'start': end, 'dur': exit_dur, 'ease': 'smoothstep', 'to': {'progress': 0}}}


def scene(sid, label, asset_id, sha, script, fonts, actors, params=None, *, category='Keyboard Shortcuts',
          status='VISUAL_APPROVED_CONDITIONAL_BACKEND', canvas=(1920, 1080), extra_palette=None):
    d = {'format': 'dafin.scene-title', 'version': 1, 'id': sid, 'label': label, 'category': category,
         'source': {'asset': asset_id, 'sha256': sha, 'script': script, 'status': status},
         'canvas': {'width': canvas[0], 'height': canvas[1]},
         'timing': {'duration': 6.0, 'fps': 25, 'cadence': 'match'},
         'render': {'profile': 'sprite2x', 'mixColor': 'DARK'},
         'preview': {'background': {'kind': 'base'}, 'encode': {'preset': 'veryfast', 'crf': 19}},
         'palette': {**PALETTE, **(extra_palette or {})},
         'fonts': {k: FONTS[k] for k in fonts}}
    if params:
        d['params'] = params
    d['actors'] = actors
    return d


# ----------------------------------------------------------------------------------------------
# Stage13 A: Shortcut Keys (frozen original, hand-set geometry kept as the block's `lock`)

def scene_13a():
    A13 = {
        'platform': 'windows', 'skin': 'classic-pc', 'labelPolicy': 'names',
        'at': [347, 568], 'shortcut': '{shortcut}', 'sep': {'size': 43},
        'lock': {'keys': ['control', 'shift', 's'], 'x': [347, 617, 940], 'w': [213, 250, 112],
                 'sep': [[568, 585], [889, 585]]},
        'timing': {
            'key': {'start': 0.85, 'keyStep': 0.30,
                    'in': {'dur': 0.38, 'dy': 18, 'snap': 'trunc'},
                    'out': {'start': 4.66, 'revStep': 0.12, 'dur': 0.26}},
            'sep': {'list': [1.05, 1.33], 'listFor': 3, 'rel': 'mid', 'off': 0.04,
                    'in': {'dur': 0.32}, 'out': {'start': 4.75, 'dur': 0.30}}},
    }
    actors = [
        text('eyebrow', 'eyebrow', 'COMANDĂ RAPIDĂ', 'semi', 25, 'AQUA', (347, 337), start=.18, dur=.32, end=4.65, exit_dur=.25, dy=10, slot='eyebrow'),
        text('title', 'title', 'Salvează ca…', 'semi', 70, 'WHITE', (338, 401), start=.30, dur=.48, end=4.68, exit_dur=.30, dy=13, slot='title'),
        rule('rule', (347, 513), (1381, 513), 'SOFT', start=.52, dur=.55, end=4.93, exit_dur=.40),
        {'id': 'keys', 'role': 'shortcut', 'type': 'keycaps', **A13},
    ]
    # the sprites of the script were measured from the literal strings; the baselines above are for them
    params = [
        {'id': 'eyebrow', 'type': 'text', 'default': 'COMANDĂ RAPIDĂ', 'label': 'Eyebrow'},
        {'id': 'title', 'type': 'text', 'default': 'Salvează ca…', 'label': 'Title'},
        {'id': 'shortcut', 'type': 'text', 'default': 'Ctrl+Shift+S', 'label': 'Shortcut'},
    ]
    # eyebrow/title baselines are for the default strings (canonical anchor: editing never moves the line)
    return scene('dafin.shortcut.13a-shortcut-keys', 'Shortcut Keys', '13_A_Shortcut_Keys',
                 '19055804163e2a13fe4dd6c7c5d2cbd10a9d2a1451fb804d54433dab2fbe110f',
                 'Dafin_TitleDesign_Part6_Stage13_PracticalOverlays_Delta_v6.0.4 :: source/render_stage13_practical_overlays.py scene_a',
                 ['semi', 'regular'], actors, params)


# ----------------------------------------------------------------------------------------------
# Stage13C: Universal shortcuts (frozen original)

def kc_13c(bid, platform, shortcut, at, start, *, policy, skin, height=None):
    d = {'id': bid, 'role': 'shortcut', 'type': 'keycaps', 'platform': platform, 'skin': skin, 'labelPolicy': policy,
         'at': list(at), 'shortcut': shortcut,
         'flow': {'gap': 32, 'plusDx': 16, 'plusDy': 'center'}, 'sep': {'size': 41},
         'timing': {'key': {'start': start, 'keyStep': 0.17, 'in': {'dur': 0.40, 'dy': 14},
                            'out': {'start': 4.72, 'dur': 0.30}},
                    'sep': {'rel': 'left', 'off': 0.17, 'in': {'dur': 0.38}, 'out': {'start': 4.75, 'dur': 0.28}}}}
    if height:
        d['height'] = height
    return d


def head_13c(eye_xy, head_xy, line_y, *, eyebrow_default, title_default):
    return [
        text('eyebrow', 'eyebrow', eyebrow_default, 'semi', 25, 'AQUA', eye_xy, start=.18, dur=.35, end=4.79, exit_dur=.32, dy=7, slot='eyebrow'),
        text('title', 'title', title_default, 'semi', 73, 'WHITE', head_xy, start=.32, dur=.44, end=4.75, exit_dur=.33, dy=11, slot='title'),
        rule('rule', (350, line_y), (1561, line_y), 'SOFT', start=.55, dur=.60, end=4.90, exit_dur=.40),
    ]


def label_13c(aid, value, color, xy, j):
    return text(aid, 'platformLabel', value, 'semi', 25, color, xy, start=.70 + j * .18, dur=.37, end=4.73, exit_dur=.32)


def note_13c(aid, value, size, xy, start, end_dur, dur_in, color='SOFT'):
    return text(aid, 'note', value, 'regular', size, color, xy, start=start, dur=dur_in, end=end_dur[0], exit_dur=end_dur[1])


def scene_13c(kind):
    src = ('Dafin_TitleDesign_Part6_Stage13C_UniversalShortcut_Delta_v6.0.6 :: '
           'source/render_stage13C_universal_shortcut.py UniversalScene')
    if kind == 'U1':
        a = head_13c((348, 254), (339, 323), 474, eyebrow_default='SCURTĂTURĂ UNIVERSALĂ', title_default='Copiază')
        a += [rule('divider', (948, 551), (948, 824), 'FAINT', start=.70, dur=.50, end=4.84, exit_dur=.35),
              label_13c('labelWindows', 'WINDOWS / LINUX', 'AQUA', (352, 556), 0),
              label_13c('labelMac', 'macOS', 'SILVER', (1023, 556), 1),
              kc_13c('keysWindows', 'windows', '{shortcutWindows}', (350, 638), .88, policy='names', skin='classic-pc'),
              kc_13c('keysMac', 'macos', '{shortcutMac}', (1022, 638), 1.34, policy='glyph', skin='classic-mac'),
              note_13c('macName', 'Command / Cmd', 25, (1024, 775), 1.80, (4.66, .30), .34, color='SILVER'),
              note_13c('note', 'O singură acțiune, două sisteme', 26, (352, 905), 2.05, (4.64, .30), .40)]
        params = [('eyebrow', 'SCURTĂTURĂ UNIVERSALĂ', 'Eyebrow'), ('title', 'Copiază', 'Title'),
                  ('shortcutWindows', 'Ctrl+C', 'Windows / Linux shortcut'), ('shortcutMac', 'Command+C', 'macOS shortcut')]
        name, label, aid, sha = 'u1-universal-horizontal-copy', 'Universal Shortcut, side by side', '13C_U1_Universal_Horizontal_Copy', None
    elif kind == 'U2':
        a = head_13c((348, 160), (339, 223), 367, eyebrow_default='SCURTĂTURĂ UNIVERSALĂ', title_default='Lipește')
        a += [label_13c('labelWindows', 'WINDOWS / LINUX', 'AQUA', (350, 438), 0),
              label_13c('labelMac', 'macOS', 'SILVER', (350, 681), 1),
              kc_13c('keysWindows', 'windows', '{shortcutWindows}', (350, 505), .91, policy='names', skin='classic-pc'),
              kc_13c('keysMac', 'macos', '{shortcutMac}', (350, 744), 1.42, policy='glyphPlusNames', skin='classic-mac', height=132),
              note_13c('note', 'Denumirea tastei poate fi afișată sub simbol.', 25, (350, 947), 2.10, (4.64, .30), .36)]
        params = [('eyebrow', 'SCURTĂTURĂ UNIVERSALĂ', 'Eyebrow'), ('title', 'Lipește', 'Title'),
                  ('shortcutWindows', 'Ctrl+V', 'Windows / Linux shortcut'), ('shortcutMac', 'Command+V', 'macOS shortcut')]
        name, label, aid, sha = 'u2-universal-vertical-paste', 'Universal Shortcut, stacked', '13C_U2_Universal_Vertical_Paste', None
    else:
        a = head_13c((348, 254), (339, 320), 474, eyebrow_default='SCURTĂTURĂ macOS', title_default='Închidere forțată')
        a += [label_13c('labelMac', 'APLICAȚII CARE NU RĂSPUND', 'SILVER', (352, 553), 0),
              kc_13c('keysMac', 'macos', '{shortcutMac}', (350, 644), .88, policy='glyphPlusNames', skin='classic-mac', height=142),
              note_13c('note', 'Option și Alt sunt două denumiri pentru aceeași tastă.', 26, (350, 860), 1.78, (4.64, .30), .39)]
        params = [('eyebrow', 'SCURTĂTURĂ macOS', 'Eyebrow'), ('title', 'Închidere forțată', 'Title'),
                  ('shortcutMac', 'Option+Command+Esc', 'macOS shortcut')]
        name, label, aid, sha = 'u3-mac-dual-names-force-quit', 'macOS Shortcut, dual names', '13C_U3_Mac_Dual_Names_Force_Quit', None
    plist = [{'id': i, 'type': 'text', 'default': v, 'label': lb} for i, v, lb in params]
    fonts = ['semi', 'regular'] + (['symbols'] if kind != 'U0' else [])
    return scene(f'dafin.shortcut.13c-{name}', label, aid, sha, src, fonts, a, plist)


RECEIPTS = None  # --receipts: audits/84_EXACT_MEDIA_RECEIPTS.json of the design archive


def receipt(asset_id):
    if not RECEIPTS:
        return {}
    for r in json.loads(Path(RECEIPTS).read_text(encoding='utf-8')):
        if r['id'] == asset_id:
            return r
    return {}


def write(d):
    r = receipt(d['source']['asset'])
    if r:
        d['source']['sha256'] = r['sha256']
        d['source']['status'] = r['original_status']
    path = SCENES / (d['source']['asset'] + '.scene.json')
    path.write_text(json.dumps(d, ensure_ascii=False, indent=1) + '\n', encoding='utf-8')
    return path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--only')
    ap.add_argument('--receipts', help='audits/84_EXACT_MEDIA_RECEIPTS.json of the design archive (fills source.sha256 and status)')
    o = ap.parse_args()
    global RECEIPTS
    RECEIPTS = o.receipts
    only = set(o.only.split(',')) if o.only else None
    out = []
    if not only or '13_A' in only:
        out.append(write(scene_13a()))
    for k in ('U1', 'U2', 'U3'):
        if not only or k in only:
            out.append(write(scene_13c(k)))
    for p in out:
        print('wrote', p.relative_to(REPO))


if __name__ == '__main__':
    main()
