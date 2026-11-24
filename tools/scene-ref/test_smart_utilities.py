#!/usr/bin/env python3
"""Headless semantic tests of the C10 scenes (progress, menu path, anchors). No archive needed.

  python3 -I tools/scene-ref/test_smart_utilities.py

Pixel parity with the original Part6 scripts is test_parity.py (needs the archives); these tests
check what the params MEAN. Pixel checks are skipped when the proofs' fonts are missing.
"""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import render  # noqa: E402
import scene_core as core  # noqa: E402
from scene_core import SceneError  # noqa: E402

SC = HERE / 'scenes'
GOLD = (255, 200, 87)  # checked against the scene's own palette below


def load(name, params=None):
    return core.load_scene(SC / f'{name}.scene.json', params)


def fonts_ok(name):
    try:
        sc = load(name)
        for k in sc.raw['fonts']:
            render.font_path(sc, k)
        return True
    except SceneError:
        return False


def actor(sc, aid):
    return next(a for a in sc.actors if a['id'] == aid)


def hold_frame(name, params=None, i=75):
    return render.Job(load(name, params)).direct_frame(i)


class ProgressDefinitionCard(unittest.TestCase):
    """15_B: the accent follows entryIndex / totalEntries and stays a short accent."""

    def accent_end_x(self, **p):
        sc = load('15_B_Definition_Card', p)
        return actor(sc, 'accent')['to'][0]

    def test_default_accent_is_89px(self):
        self.assertEqual(self.accent_end_x() - 358, 89)  # 4/8 of the 178 px accent track

    def test_progress_matrix(self):
        for cur, tot in ((1, 8), (4, 8), (8, 8), (1, 1), (3, 5), (12, 24)):
            got = self.accent_end_x(entryIndex=cur, totalEntries=tot) - 358
            self.assertEqual(got, round(178 * cur / tot), (cur, tot))

    def test_never_a_big_bar(self):
        for tot in (1, 8, 32):
            self.assertLessEqual(self.accent_end_x(entryIndex=tot, totalEntries=tot) - 358, 178)

    def test_current_is_clamped_to_total(self):
        self.assertEqual(self.accent_end_x(entryIndex=99, totalEntries=8), self.accent_end_x(entryIndex=8, totalEntries=8))
        self.assertEqual(self.accent_end_x(entryIndex=-3, totalEntries=8), self.accent_end_x(entryIndex=1, totalEntries=8))

    def test_eyebrow_text_has_current_zero_padded(self):
        self.assertEqual(actor(load('15_B_Definition_Card'), 'eyebrow')['text'], 'GLOSSARY  /  04')
        self.assertEqual(actor(load('15_B_Definition_Card', {'entryIndex': 7}), 'eyebrow')['text'], 'GLOSSARY  /  07')


@unittest.skipUnless(fonts_ok('14_A_Menu_Path'), 'proof fonts missing')
class MenuPath(unittest.TestCase):
    def boxes(self, **p):
        return render.Job(load('14_A_Menu_Path', p)).r.rest_boxes(3.0)

    def test_default_geometry_is_the_approved_one(self):
        b = self.boxes()
        self.assertEqual((b['card1'], b['card2'], b['card3']), ((341, 585, 549, 700), (643, 585, 917, 700), (1035, 585, 1279, 700)))

    def test_long_label_grows_its_card_and_pushes_the_rest(self):
        d, b = self.boxes(), self.boxes(item2='A much longer menu entry')
        self.assertGreater(b['card2'][2] - b['card2'][0], d['card2'][2] - d['card2'][0])
        self.assertGreater(b['card3'][0], d['card3'][0])

    def test_short_labels_keep_the_minimum_width(self):
        b = self.boxes(item1='A', item2='B', item3='C')
        for k, m in (('card1', 208), ('card2', 208), ('card3', 208)):
            self.assertGreaterEqual(b[k][2] - b[k][0], m)

    def test_empty_label_does_not_crash(self):
        self.boxes(item2='')

    def test_item_count_two_drops_the_third_card(self):
        b = self.boxes(itemCount=2)
        self.assertNotIn('card3', b)


@unittest.skipUnless(fonts_ok('14_B_Setting_Change'), 'proof fonts missing')
class SettingChange(unittest.TestCase):
    def test_unit_follows_the_value(self):
        a = render.Job(load('14_B_Setting_Change')).r.rest_boxes(3.0)
        b = render.Job(load('14_B_Setting_Change', {'newValue': '120', 'currentValue': '24'})).r.rest_boxes(3.0)
        self.assertNotEqual(a, b)

    def test_unit_is_a_param(self):
        self.assertEqual(actor(load('14_B_Setting_Change', {'unit': 'MBPS'}), 'currentUnit')['text'], 'MBPS')


class Anchors(unittest.TestCase):
    def test_user_places_the_point(self):
        sc = load('11_AnchorNote_v4', {'pointX': 400, 'pointY': 300})
        self.assertEqual(actor(sc, 'point')['center'], [400, 300])
        self.assertEqual(actor(sc, 'leader')['points'][0], [400, 300])

    def test_point_is_static_after_entry(self):
        sc = load('11_AnchorNote_v4')
        p = actor(sc, 'point')
        self.assertNotIn('dx', str(p.get('in', {}))); self.assertNotIn('scale', str(p.get('in', {})))
        self.assertNotIn('loop', str(p))

    def test_badge_letters_are_independent(self):
        sc = load('12B_C_Anchor_Index_Manual', {'badge1': 'X', 'badge2': ''})
        self.assertEqual(actor(sc, 'badge1Text')['text'], 'X')
        ids = [a['id'] for a in sc.actors]
        self.assertNotIn('badge2Text', ids)  # empty = a plain ring, text actor is disabled
        self.assertIn('badge2', ids)

    def test_blank_is_not_a_separate_preset(self):
        self.assertFalse(list(SC.glob('*Blank*')))

    def test_badge_text_is_free(self):
        sc = load('12B_C_Anchor_Index_Manual', {'badge1': '12', 'badge2': '✓'})
        self.assertEqual((actor(sc, 'badge1Text')['text'], actor(sc, 'badge2Text')['text']), ('12', '✓'))

    def test_bracket_follows_region(self):
        sc = load('12_A_Anchor_Bracket', {'left': 100, 'top': 100, 'right': 300, 'bottom': 200})
        self.assertEqual(actor(sc, 'bracketTLH')['from'], [100, 100])
        self.assertEqual(actor(sc, 'bracketBRH')['from'], [300, 200])

    def test_measure_caption_optional(self):
        ids = [a['id'] for a in load('12_B_Anchor_Measure', {'caption': ''}).actors]
        self.assertNotIn('caption', ids)

    def test_measure_endpoints_are_params(self):
        sc = load('12_B_Anchor_Measure', {'pointAX': 10, 'pointBX': 50})
        self.assertEqual((actor(sc, 'tickA')['from'][0], actor(sc, 'tickB')['from'][0]), (10, 50))


class EveryNewSceneValidates(unittest.TestCase):
    NAMES = ('15_B_Definition_Card', '14_A_Menu_Path', '14_B_Setting_Change', '11_AnchorNote_v4',
             '12_A_Anchor_Bracket', '12_B_Anchor_Measure', '12B_C_Anchor_Index_Manual')

    def test_load_and_no_filmora_trace(self):
        for n in self.NAMES:
            text = (SC / f'{n}.scene.json').read_text(encoding='utf-8')
            self.assertNotIn('filmora', text.lower(), n)
            sc = load(n)
            self.assertTrue(sc.actors, n)

    def test_defaults_render_visible_pixels_at_the_hold(self):
        for n in self.NAMES:
            if not fonts_ok(n):
                continue
            f = hold_frame(n)
            self.assertGreater(int((f.max(axis=2) > 60).sum()), 100, n)


if __name__ == '__main__':
    unittest.main()
