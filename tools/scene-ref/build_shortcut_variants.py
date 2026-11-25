#!/usr/bin/env python3
"""Writes the 7 kept keycap variants as Smart Keycaps scenes (Titles > Keyboard Shortcuts):

  13B_A1..A4  Stage13B dynamic-width cases, derived from 13_A_Shortcut_Keys (same art, no `lock`,
              Stage13B timing: keyStep 0.22, all keys leave at 4.69, "+" at the neighbours' midpoint)
  13D_U1..U3  Stage13D English versions of 13C_U1..U3 (same actors and timing, English text only)

Nothing here redesigns anything: it copies the approved scenes and changes only the data the
original scripts changed (keys, texts, ids, source). Run: python3 -I build_shortcut_variants.py
"""
from __future__ import annotations

import copy
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCENES = HERE / 'scenes'
STATUS = 'ART_EXPERIMENT_PENDING_CREATOR_VERDICT'  # the ledger's status; not an approval

# SHA-256 of the approved-for-review MP4 each scene reproduces (Stage13B / Stage13D packages)
MP4_SHA = {
    '13B_A1_Single_Function_Key': 'ab522b493e4a0d022fbda06146977313b19daea6751ffde1622175394f19382c',
    '13B_A2_Two_Mixed_Keys': '14d53171ec82b6794903e5e98565088955c873147cbcea4bcf689a458189b562',
    '13B_A3_Four_Mixed_Keys': '68aec2e346f5f6a19734b86f353ad0ee24d58b3535542f3b8a78a91f1cb3a304',
    '13B_A4_Wide_Spacebar': '00a209f3b856c3ba8557acc975c54afcdad71247a9d1ace3ebdcf2acf7630f43',
    '13D_U1_Universal_Horizontal_Copy_EN': '0a3fcb43d4c901ab08f9d622ee002210ca35bd5ee8a57f1b3516a997489d845a',
    '13D_U2_Universal_Vertical_Paste_EN': 'e36b3a7454efb2edfa495d61871aa1a21d5f7e4f7db488ccda5e6e65ce8b0910',
    '13D_U3_Mac_Dual_Names_Force_Quit_EN': 'e50de43fa8caf459934657dfacd7042cebf340e75371c5de4b643e89d32be2e5',
}

# (file stem, id suffix, label, Stage13B case, eyebrow, title, shortcut)
B_CASES = [
    ('13B_A1_Single_Function_Key', '13b-a1-single-function-key', 'Single Function Key',
     'A1_Single_Function_Key', 'COMANDĂ RAPIDĂ', 'Redenumește', 'F2'),
    ('13B_A2_Two_Mixed_Keys', '13b-a2-two-mixed-keys', 'Two Mixed Keys',
     'A2_Two_Mixed_Keys', 'COMANDĂ RAPIDĂ', 'Copiază', 'Ctrl+C'),
    ('13B_A3_Four_Mixed_Keys', '13b-a3-four-mixed-keys', 'Four Mixed Keys',
     'A3_Four_Mixed_Keys', 'EXEMPLU EDITABIL', 'Comandă personalizată', 'Ctrl+Alt+Shift+S'),
    ('13B_A4_Wide_Spacebar', '13b-a4-wide-spacebar', 'Wide Spacebar',
     'A4_Wide_Spacebar', 'COMANDĂ RAPIDĂ', 'Redare / pauză', 'Space'),
]

# 13C file stem -> (13D stem, id suffix, English strings replacing the Romanian literals, params)
D_CASES = [
    ('13C_U1_Universal_Horizontal_Copy', '13D_U1_Universal_Horizontal_Copy_EN',
     '13d-u1-universal-horizontal-copy-en', 'U1_Universal_Horizontal_Copy_EN',
     'UNIVERSAL SHORTCUT', 'Copy', {'O singură acțiune, două sisteme': 'One action, two systems'}),
    ('13C_U2_Universal_Vertical_Paste', '13D_U2_Universal_Vertical_Paste_EN',
     '13d-u2-universal-vertical-paste-en', 'U2_Universal_Vertical_Paste_EN',
     'UNIVERSAL SHORTCUT', 'Paste',
     {'Denumirea tastei poate fi afișată sub simbol.': 'The key name can appear beneath the symbol.'}),
    ('13C_U3_Mac_Dual_Names_Force_Quit', '13D_U3_Mac_Dual_Names_Force_Quit_EN',
     '13d-u3-mac-dual-names-force-quit-en', 'U3_Mac_Dual_Names_Force_Quit_EN',
     'macOS SHORTCUT', 'Force Quit',
     {'APLICAȚII CARE NU RĂSPUND': 'UNRESPONSIVE APPS',
      'Option și Alt sunt două denumiri pentru aceeași tastă.': 'Option and Alt are two names for the same key.'}),
]


FONT_DIR = HERE.parent.parent / 'scene-fonts'
FONT_FILES = {'regular': 'NotoSansDisplay-Regular.ttf', 'semi': 'NotoSansDisplay-SemiBold.ttf'}


def _ink(font: str, size: int, text: str):
    from PIL import ImageFont
    f = ImageFont.truetype(str(FONT_DIR / FONT_FILES[font]), size * 2)
    return f.getbbox(text, anchor='ls')


def rebase(actor: dict, old: str, new: str) -> None:
    """A text actor's baseline is the script sprite's top-left corrected by the ink box of the string
    (build_keycap_scenes.sprite_baseline). Another string has another ink box, so go back to the
    sprite position with the old string and forward again with the new one."""
    ol, ot, _, _ = _ink(actor['font'], actor['size'], old)
    nl, nt, _, _ = _ink(actor['font'], actor['size'], new)
    bx, by = actor['at']['baseline']
    sx, sy = bx - (14 - ol) / 2, by - (16 - ot) / 2
    actor['at']['baseline'] = [sx + (14 - nl) / 2, sy + (16 - nt) / 2]


def param_default(doc: dict, pid: str) -> str:
    return next(p['default'] for p in doc['params'] if p['id'] == pid)


def set_texts(doc: dict, **values: str) -> None:
    """Sets param defaults (eyebrow, title) and re-bases the baselines of the actors bound to them."""
    for pid, new in values.items():
        old = param_default(doc, pid)
        for a in doc['actors']:
            if a['type'] == 'text' and a['text'] == '{%s}' % pid:
                rebase(a, old, new)
        set_default(doc, pid, new)


def load(stem: str) -> dict:
    return json.loads((SCENES / f'{stem}.scene.json').read_text(encoding='utf-8'))


def save(stem: str, doc: dict) -> None:
    (SCENES / f'{stem}.scene.json').write_text(json.dumps(doc, indent=1, ensure_ascii=False) + '\n',
                                               encoding='utf-8')


def set_default(doc: dict, pid: str, value: str) -> None:
    for p in doc['params']:
        if p['id'] == pid:
            p['default'] = value
            return
    raise KeyError(pid)


def build_b() -> None:
    for stem, sid, label, case, eyebrow, title, shortcut in B_CASES:
        doc = copy.deepcopy(load('13_A_Shortcut_Keys'))
        doc['id'] = f'dafin.shortcut.{sid}'
        doc['label'] = label
        doc['category'] = 'Keyboard Shortcuts'
        doc['source'] = {
            'asset': stem,
            'sha256': MP4_SHA[stem],
            'script': 'Dafin_TitleDesign_Part6_Stage13B_ShortcutFlex_Delta_v6.0.5 :: '
                      f'source/render_stage13B_shortcut_flex.py {case}',
            'status': STATUS,
        }
        set_texts(doc, eyebrow=eyebrow, title=title)
        set_default(doc, 'shortcut', shortcut)
        for a in doc['actors']:
            if a['type'] == 'keycaps':
                a.pop('lock', None)
                a['flow'] = {'gap': 32, 'plusDx': 16, 'plusDy': 18, 'maxWidth': 1034, 'rowPitch': 132}
                kt = a['timing']['key']
                kt['keyStep'] = 0.22
                kt['out'] = {'start': 4.69, 'revStep': 0.0, 'dur': 0.30}
                if stem.endswith('A4_Wide_Spacebar'):
                    # Stage13B gives the spacebar max(355, ink): an odd 355 that the generator's even
                    # rounding would turn into 356. Hand-set geometry of the approved proof, like 13_A.
                    a['lock'] = {'keys': ['space'], 'x': [347], 'w': [355], 'sep': []}
                a['timing']['sep'] = {'rel': 'mid', 'off': 0.04, 'in': {'dur': 0.32},
                                      'out': {'start': 4.75, 'dur': 0.30}}
        save(stem, doc)


def build_d() -> None:
    for src, stem, sid, case, eyebrow, title, literals in D_CASES:
        doc = copy.deepcopy(load(src))
        doc['id'] = f'dafin.shortcut.{sid}'
        doc['category'] = 'Keyboard Shortcuts'
        doc['source'] = {
            'asset': stem,
            'sha256': MP4_SHA[stem],
            'script': 'Dafin_TitleDesign_Part6_Stage13D_UniversalShortcut_English_Delta_v6.0.7 :: '
                      f'source/render_stage13D_universal_shortcut_english.py {case}',
            'status': STATUS,
        }
        doc['label'] = doc['label'] + ' (English)'
        set_texts(doc, eyebrow=eyebrow, title=title)
        for a in doc['actors']:
            if a['type'] == 'text' and a.get('text') in literals:
                new = literals[a['text']]
                rebase(a, a['text'], new)
                a['text'] = new
        save(stem, doc)


if __name__ == '__main__':
    build_b()
    build_d()
    print('wrote', len(B_CASES) + len(D_CASES), 'scenes')
