#!/usr/bin/env python3
"""Headless tests of the Smart Keycaps generator (docs/scene-keycaps.md). No archive needed.

  python3 -I tools/scene-ref/test_keycaps.py

The proofs' Regular/SemiBold files live in scene-fonts/. NotoSansSymbols2 (the glyphs of the Mac
keys) is not in the repo; without it the tests that draw Mac keys use DejaVu Sans as a stand-in.
That proves the mechanics (widths, rows, timing, placement), never the glyph shapes.
Parity with the approved scripts: test_parity.py --stage13 --stage13c.
"""
from __future__ import annotations

import copy
import json
import sys
import unittest
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import keycaps  # noqa: E402
import render  # noqa: E402
import scene_core as core  # noqa: E402
from scene_core import SceneError  # noqa: E402

SCENES = HERE / 'scenes'
STAND_IN = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'


def load_raw(name, **param_overrides):
    raw = json.loads((SCENES / name).read_text(encoding='utf-8'))
    if 'symbols' in raw.get('fonts', {}):
        probe = core.load_scene({**raw, 'actors': []})
        try:
            render.font_path(probe, 'symbols')
        except SceneError:
            if not Path(STAND_IN).exists():
                raise unittest.SkipTest('no symbol font')
            raw['fonts']['symbols'] = {'family': 'DejaVu Sans (stand-in)', 'file': STAND_IN}
    return raw


def fonts_ok():
    try:
        sc = core.load_scene(load_raw('13_A_Shortcut_Keys.scene.json'))
        for k in sc.raw['fonts']:
            render.font_path(sc, k)
        return True
    except (SceneError, unittest.SkipTest):
        return False


def laid_out(name, **params):
    raw = load_raw(name)
    sc = core.load_scene(raw, params)
    r = render.Sprite2x(sc)
    return sc, r


def single(shortcut, platform='windows', **block):
    """A one-block scene around a shortcut, for layout tests."""
    b = {'id': 'k', 'type': 'keycaps', 'platform': platform, 'shortcut': shortcut, 'at': [100, 300],
         'skin': 'classic-pc' if platform == 'windows' else 'classic-mac', **block}
    raw = {'format': 'dafin.scene-title', 'version': 1, 'id': 't', 'canvas': {'width': 1920, 'height': 1080},
           'timing': {'duration': 6, 'fps': 25}, 'render': {'profile': 'sprite2x'},
           'palette': {'WHITE': [242, 243, 240], 'SOFT': [165, 177, 187], 'AQUA': [123, 209, 192], 'SILVER': [184, 194, 200]},
           'fonts': {k: json.loads((SCENES / '13_A_Shortcut_Keys.scene.json').read_text())['fonts'][k] for k in ('semi', 'regular')},
           'actors': [b]}
    raw['fonts']['symbols'] = {'family': 'stand-in', 'file': STAND_IN}
    return raw


class Dictionary(unittest.TestCase):
    def key(self, token, platform='windows', policy='names', legends=None):
        return keycaps.resolve_key(token, platform, policy, legends or {})

    def test_option_is_alt_on_a_mac_and_has_two_names(self):
        w, m = self.key('Option'), self.key('alt', 'macos', 'glyphPlusNames')
        self.assertEqual((w.ident, w.primary), ('alt', 'ALT'))
        self.assertEqual((m.ident, m.primary, m.secondary), ('alt', '⌥', 'Option / Alt'))
        self.assertEqual(self.key('Alt', 'macos', 'names').primary, 'Option')

    def test_command_is_not_alt_and_never_on_a_pc_block(self):
        self.assertNotEqual(self.key('cmd', 'macos', 'glyph').ident, self.key('alt', 'macos', 'glyph').ident)
        for alias in ('command', 'cmd', '⌘'):
            self.assertEqual(self.key(alias, 'macos', 'glyph').ident, 'command')
            with self.assertRaises(SceneError):
                self.key(alias)
        with self.assertRaises(SceneError):
            self.key('win', 'macos', 'glyph')

    def test_mod_is_ctrl_or_command(self):
        self.assertEqual(self.key('mod').primary, 'CTRL')
        self.assertEqual(self.key('mod', 'macos', 'glyph').primary, '⌘')

    def test_dual_names_only_for_keys_that_have_a_glyph(self):
        self.assertEqual(self.key('Esc', 'macos', 'glyphPlusNames').secondary, '')
        self.assertEqual(self.key('shift', 'macos', 'glyphPlusNames').secondary, 'Shift')

    def test_physical_legends_override_and_fall_back(self):
        self.assertEqual(self.key('alt', 'macos', 'physical', {'alt': 'Alt'}).primary, 'Alt')
        self.assertEqual(self.key('shift', 'macos', 'physical').primary, 'SHIFT')

    def test_single_letters_function_keys_custom_and_unknown(self):
        self.assertEqual(self.key('c').primary, 'C')
        self.assertEqual(self.key('f12').primary, 'F12')
        self.assertEqual(self.key('custom:Macro 7').primary, 'Macro 7')
        for bad in ('f25', 'hyper', 'custom:'):
            with self.assertRaises(SceneError):
                self.key(bad)

    def test_the_dictionary_is_one_file_with_unique_aliases(self):
        seen = {}
        for kid, k in keycaps.dictionary()['keys'].items():
            for a in k.get('aliases', []):
                self.assertNotIn(a.lower(), seen, f'{a} belongs to {seen.get(a.lower())} and {kid}')
                seen[a.lower()] = kid


class Grammar(unittest.TestCase):
    def test_chords_sequences_alternatives(self):
        self.assertEqual(keycaps.parse_shortcut('Ctrl+K THEN Ctrl+C'), [[['Ctrl', 'K'], ['Ctrl', 'C']]])
        self.assertEqual(keycaps.parse_shortcut('ctrl+y or Ctrl+Shift+Z'), [[['ctrl', 'y']], [['Ctrl', 'Shift', 'Z']]])
        self.assertEqual(keycaps.parse_shortcut('F2'), [[['F2']]])

    def test_empty_things_are_errors(self):
        for bad in ('', '   ', 'Ctrl+', '+C', 'Ctrl++C'):
            with self.assertRaises(SceneError):
                keycaps.parse_shortcut(bad)


class Structure(unittest.TestCase):
    """Phase A needs no font: the actors never depend on how wide the text is."""

    def test_loads_without_any_measure(self):
        sc = core.load_scene(single('Ctrl+Shift+S'))
        ids = [a['id'] for a in sc.actors]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertEqual(sum(1 for a in sc.actors if a['role'] == 'keycap'), 3)
        self.assertEqual(sum(1 for a in sc.actors if a['role'] == 'keycapsSeparator'), 2)

    def test_actor_count_depends_on_the_shortcut_not_on_the_widths(self):
        a = core.load_scene(single('Ctrl+Alt+Shift+Win+Esc+Tab+F2+F3', flow={'maxWidth': 400}))
        b = core.load_scene(single('Ctrl+Alt+Shift+Win+Esc+Tab+F2+F3', flow={'maxWidth': 4000}))
        self.assertEqual([x['id'] for x in a.actors], [x['id'] for x in b.actors])

    def test_then_or_and_label_actors(self):
        sc = core.load_scene(single('Ctrl+K THEN Ctrl+C OR Alt+X', label={'text': 'WINDOWS', 'at': [100, 218]}))
        roles = [a['role'] for a in sc.actors]
        self.assertEqual(roles.count('keycapsJoiner'), 2)
        self.assertEqual(roles.count('keycap'), 6)
        self.assertEqual(sum(1 for a in sc.actors if a['role'] == 'keycapsLabel'), 1)
        self.assertEqual([a['text'] for a in sc.actors if a['role'] == 'keycapsJoiner'], ['THEN', 'OR'])

    def test_a_platform_error_names_the_key(self):
        with self.assertRaisesRegex(SceneError, 'command'):
            core.load_scene(single('Command+C', 'windows'))

    def test_timing_is_numbers_in_the_actors(self):
        sc = core.load_scene(single('A+B', timing={'key': {'start': 1.0, 'keyStep': .5, 'in': {'dur': .4, 'dy': 14},
                                                                 'out': {'start': 4.0, 'revStep': .1, 'dur': .3}}}))
        keys = [a for a in sc.actors if a['role'] == 'keycap']
        self.assertEqual([k['in']['start'] for k in keys], [1.0, 1.5])
        self.assertEqual([k['out']['start'] for k in keys], [4.1, 4.0])
        self.assertEqual(keys[0]['in']['from'], {'opacity': 0, 'dy': 14})


@unittest.skipUnless(fonts_ok(), 'scene-fonts missing')
class Layout(unittest.TestCase):
    def boxes(self, shortcut, platform='windows', **block):
        sc = core.load_scene(single(shortcut, platform, **block))
        render.Sprite2x(sc)
        keys = [a for a in sc.actors if a['role'] == 'keycap']
        return sc, [(a['at'][0], a['at'][1], a['size'][0], a['size'][1]) for a in keys]

    def test_widths_follow_the_label(self):
        _, b = self.boxes('Ctrl+Shift+S+F2')
        w = [x[2] for x in b]
        self.assertEqual(w[0], 188)   # CTRL, Stage13C formula: ink(43 px) + 90, even
        self.assertGreater(w[1], w[0])
        self.assertEqual(w[2], 114)   # one letter: the minimum
        self.assertTrue(all(x % 2 == 0 for x in w))

    def test_spacebar_is_wide(self):
        _, b = self.boxes('Shift+Space')
        self.assertGreaterEqual(b[1][2], 355)
        _, m = self.boxes('Command+Space', 'macos', labelPolicy='glyph')
        self.assertGreaterEqual(m[1][2], 360)

    def test_n_keys_one_row_never_overlap(self):
        for n in range(1, 9):
            _, b = self.boxes('+'.join(['Ctrl', 'Alt', 'Shift', 'Win', 'Esc', 'Tab', 'F2', 'F3'][:n]))
            self.assertEqual(len(b), n)
            for left, right in zip(b, b[1:]):
                self.assertGreater(right[0], left[0] + left[2])
            self.assertEqual({x[1] for x in b}, {300})

    def test_rows_wrap_at_the_width_and_the_wrapped_sign_is_hidden(self):
        sc, b = self.boxes('Ctrl+Alt+Shift+Win+Esc+Tab+F2+F3', flow={'maxWidth': 600, 'gap': 48, 'plusDx': 19, 'plusDy': 32})
        self.assertGreater(len({x[1] for x in b}), 1)
        for x in b:
            self.assertLessEqual(x[0] + x[2], 100 + 600)
        seps = [a for a in sc.actors if a['role'] == 'keycapsSeparator']
        starts = [i + 1 for i in range(len(b) - 1) if b[i + 1][1] > b[i][1]]
        self.assertEqual([i for i, s in enumerate(seps, 1) if s.get('_hidden')], starts)
        self.assertEqual(len(seps), len(b) - 1)

    def test_then_and_or_stack_blocks_downwards(self):
        _, b = self.boxes('Ctrl+K THEN Ctrl+C OR Alt+X', flow={'gap': 48, 'plusDx': 19, 'plusDy': 32})
        ys = [x[1] for x in b]
        self.assertEqual(ys, sorted(ys))
        self.assertEqual(len(set(ys)), 3)

    def test_editing_text_relayouts_without_changing_ids(self):
        a = core.load_scene(single('Ctrl+C')); render.Sprite2x(a)
        b = core.load_scene(single('Ctrl+Shift+Alt+C')); render.Sprite2x(b)
        self.assertNotEqual([x['id'] for x in a.actors], [x['id'] for x in b.actors])
        c = core.load_scene(single('Ctrl+C')); render.Sprite2x(c); render.Sprite2x(c)  # idempotent
        self.assertEqual(json.dumps(a.actors, default=str, sort_keys=True), json.dumps(c.actors, default=str, sort_keys=True))

    def test_locked_geometry_only_applies_to_the_approved_default(self):
        raw = load_raw('13_A_Shortcut_Keys.scene.json')
        sc = core.load_scene(raw); render.Sprite2x(sc)
        w = [a['size'][0] for a in sc.actors if a['role'] == 'keycap']
        self.assertEqual(w, [213, 250, 112])
        sc = core.load_scene(raw, {'shortcut': 'Ctrl+C'}); render.Sprite2x(sc)
        w = [a['size'][0] for a in sc.actors if a['role'] == 'keycap']
        self.assertEqual(w, [188, 116])      # auto widths: the lock is for Ctrl+Shift+S only

    def test_mac_dual_keys_use_the_taller_plate_and_two_texts(self):
        sc, b = self.boxes('Option+Command+Esc', 'macos', labelPolicy='glyphPlusNames', height=142)
        self.assertEqual({x[3] for x in b}, {142})
        self.assertEqual(sum(1 for a in sc.actors if a['role'] == 'keycapSecondary'), 2)
        self.assertEqual(sum(1 for a in sc.actors if a['role'] == 'keycapShine'), 3)


@unittest.skipUnless(fonts_ok(), 'scene-fonts missing')
class Scenes(unittest.TestCase):
    def test_every_keycap_scene_loads_renders_and_holds_still(self):
        files = sorted(SCENES.glob('13*.scene.json')) + sorted(SCENES.glob('T*.scene.json'))
        self.assertEqual(len(files), 4 + 18)
        for f in files:
            raw = load_raw(f.name)
            sc = core.load_scene(raw)
            self.assertEqual(sc.raw['id'].split('.')[1], 'shortcut' if f.name.startswith('13') else 'keyboard-shortcuts')
            job = render.Job(sc)
            a, b = job.direct_frame(90), job.direct_frame(96)
            self.assertTrue(np.array_equal(a, b), f'{f.name}: the hold is not static')
            self.assertGreater(int((a.astype(int) - job.direct_frame(0).astype(int)).any(axis=2).sum()), 1000, f.name)

    def test_the_18_templates_are_distinct_catalog_entries(self):
        cat = json.loads((HERE / 'keycaps' / 'template-catalog.json').read_text(encoding='utf-8'))['templates']
        self.assertEqual([t['id'] for t in cat], [f'T{i:02d}' for i in range(1, 19)])
        ids = set()
        for f in sorted(SCENES.glob('T*.scene.json')):
            d = json.loads(f.read_text(encoding='utf-8'))
            self.assertEqual(d['category'], 'Keyboard Shortcuts')
            self.assertNotIn(d['id'], ids); ids.add(d['id'])
        t16 = json.loads(next(SCENES.glob('T16*.scene.json')).read_text(encoding='utf-8'))
        self.assertEqual((t16['canvas']['width'], t16['canvas']['height']), (1080, 1920))

    def test_every_template_stays_inside_the_safe_area(self):
        for f in sorted(SCENES.glob('T*.scene.json')):
            sc = core.load_scene(load_raw(f.name)); render.Sprite2x(sc)
            W, H = sc.width, sc.height
            for a in sc.actors:
                if a['role'] == 'keycap':
                    x, y = a['at']; w, h = a['size']
                    self.assertGreaterEqual(x, 0, f.name); self.assertLessEqual(x + w, W - 100 + 20, f.name)
                    self.assertLessEqual(y + h, H * 0.885, f.name)

    def test_param_edit_changes_the_keys(self):
        sc = core.load_scene(load_raw('T09_stacked.scene.json'), {'shortcutWindows': 'Ctrl+Shift+V'}); render.Sprite2x(sc)
        self.assertEqual(sum(1 for a in sc.actors if a['id'].startswith('keysWindows') and a['role'] == 'keycap'), 3)
        with self.assertRaises(SceneError):
            core.load_scene(load_raw('T09_stacked.scene.json'), {'shortcutWindows': 'Command+V'})


if __name__ == '__main__':
    unittest.main(verbosity=1)
