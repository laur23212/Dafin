#!/usr/bin/env python3
"""Headless tests of the Scene Title reference tools (no archive needed).

  python3 -I tools/scene-ref/test_scene_ref.py

Pixel tests need the proofs' fonts (fonts-noto-core, fonts-noto-extra, fonts-inter); without
them they are skipped, the rest still runs. Parity with the original scripts: test_parity.py.
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
import render  # noqa: E402
import scene_core as core  # noqa: E402
from scene_core import SceneError  # noqa: E402

S15 = HERE / 'scenes' / '15_A_Step_Focus.scene.json'
S03 = HERE / 'scenes' / '03b_LT_B_25.scene.json'


def fonts_ok(path):
    try:
        sc = core.load_scene(path)
        for k in sc.raw['fonts']:
            render.font_path(sc, k)
        return True
    except SceneError:
        return False


class Easing(unittest.TestCase):
    def test_named_curves_are_exact_beziers_in_dafins_keyframe_model(self):
        # Dafin evaluates a key segment with cubicBezier + 24-step bisection (core/base/Bezier.h);
        # handles at 1/3 and 2/3 of the segment make x(s) = s.
        for name, (x1, y1, x2, y2) in core.EASE_BEZIER.items():
            f, g = core.EASES[name], core.bezier_ease(x1, y1, x2, y2)
            err = max(abs(f(u) - g(u)) for u in np.linspace(0, 1, 2001))
            self.assertLess(err, 1e-6, name)

    def test_quart_is_not_a_cubic_bezier(self):
        # The best handles still leave a visible error: the native engine needs a polynomial ease.
        f = core.EASES['easeOutQuart']
        g = core.bezier_ease(0.25, 1, 0.5, 1)
        self.assertGreater(max(abs(f(u) - g(u)) for u in np.linspace(0, 1, 501)), 1e-3)

    def test_endpoints_and_clamping(self):
        for f in core.EASES.values():
            self.assertEqual(f(-1), 0); self.assertEqual(f(0), 0); self.assertEqual(f(1), 1); self.assertEqual(f(2), 1)


class Expressions(unittest.TestCase):
    def test_safe_subset(self):
        env = {'i': 2, 'currentStep': 2, 'AQUA': 'aqua', 'FAINT2': 'faint'}
        self.assertEqual(core.eval_expr('590 + i*154', env), 898)
        self.assertEqual(core.eval_expr('AQUA if i < currentStep else FAINT2', env), 'faint')
        self.assertEqual(core.eval_expr('min(3, max(1, i))', env), 2)
        for bad in ('__import__("os")', 'i.real', '[1,2]', 'lambda: 1', 'open("x")'):
            with self.assertRaises(SceneError):
                core.eval_expr(bad, env)

    def test_text_formatting(self):
        self.assertEqual(core.format_text('STEP {a:02}  /  {b:02}', {'a': 2, 'b': 5}), 'STEP 02  /  05')
        self.assertEqual(core.format_text('{{x}}', {}), '{x}')
        with self.assertRaises(SceneError):
            core.format_text('{nope}', {})


class Loading(unittest.TestCase):
    def test_ports_load_and_validate(self):
        for p in (S15, S03):
            sc = core.load_scene(p)
            self.assertTrue(sc.actors)
            hs, he = core.scene_events(sc)
            self.assertLess(hs, he)

    def test_params_bounds_and_clamp(self):
        sc = core.load_scene(S15, {'totalSteps': 8, 'currentStep': 9})
        self.assertEqual(sc.params['currentStep'], 8)  # clamped, as Smart Progress requires
        self.assertEqual(len([a for a in sc.actors if a['role'] == 'progressSegment']), 8)
        self.assertEqual(sc.actors[0]['text'], 'STEP 08  /  08')
        with self.assertRaises(SceneError):
            core.load_scene(S15, {'totalSteps': 33})
        with self.assertRaises(SceneError):
            core.load_scene(S15, {'totalSteps': 2.5})
        with self.assertRaises(SceneError):
            core.load_scene(S15, {'unknown': 1})

    def test_structure_errors(self):
        raw = json.loads(S15.read_text(encoding='utf-8'))
        for mutate, msg in (
            (lambda r: r.update(version=2), 'version'),
            (lambda r: r['actors'].append(copy.deepcopy(r['actors'][0])), 'duplicate'),
            (lambda r: r['actors'][0].update(type='blob'), 'type'),
            (lambda r: r['actors'][0].update(effect={'type': 'sdfMorph'}), 'reserved'),
            (lambda r: r['actors'][0].update(dependsOn=['ghost']), 'dependsOn'),
        ):
            r = copy.deepcopy(raw); mutate(r)
            with self.assertRaises(SceneError, msg=msg):
                core.load_scene(r)

    def test_optional_actor_can_be_switched_off(self):
        raw = json.loads(S15.read_text(encoding='utf-8'))
        for a in raw['actors']:
            if a['id'] == 'description':
                a['enabled'] = False
        sc = core.load_scene(raw)
        self.assertNotIn('description', [a['id'] for a in sc.actors])
        raw['actors'][0]['enabled'] = False  # eyebrow is not optional
        with self.assertRaises(SceneError):
            core.load_scene(raw)


class Align(unittest.TestCase):
    def test_baseline_and_start_alignment(self):
        raw = json.loads(S15.read_text(encoding='utf-8'))
        d = next(a for a in raw['actors'] if a['id'] == 'description')
        d['at'] = {'baseline': [0, 0]}
        d['align'] = {'x': {'to': 'title', 'edge': 'start', 'offset': 3}, 'y': {'to': 'title', 'edge': 'baseline', 'offset': 78.5}}
        sc = core.load_scene(raw)
        self.assertEqual(next(a for a in sc.actors if a['id'] == 'description')['at']['baseline'], [599.0, 587.5])

    def test_cycle_is_an_error(self):
        raw = json.loads(S15.read_text(encoding='utf-8'))
        raw['actors'][0]['align'] = {'y': {'to': 'title', 'edge': 'baseline'}}
        next(a for a in raw['actors'] if a['id'] == 'title')['align'] = {'y': {'to': 'eyebrow', 'edge': 'baseline'}}
        with self.assertRaises(SceneError):
            core.load_scene(raw)


class Timing(unittest.TestCase):
    def setUp(self):
        self.sc = core.load_scene(S15)
        self.hs, self.he = core.scene_events(self.sc)

    def test_identity_at_design_length(self):
        f = core.time_map(self.sc, 6.0)
        self.assertEqual(f(1.234), 1.234)

    def test_longer_clip_keeps_entrances_and_exits(self):
        f = core.time_map(self.sc, 10.0)
        self.assertEqual(f(0.5), 0.5)                       # entrance at the clip start
        self.assertAlmostEqual(f(10.0 - 0.3), 6.0 - 0.3)     # exit at the clip end
        self.assertTrue(self.hs <= f(5.0) <= self.he)        # elastic hold

    def test_shorter_than_minimum_scales_uniformly(self):
        dmin = self.hs + (6.0 - self.he)
        f = core.time_map(self.sc, dmin / 2)
        self.assertAlmostEqual(f(0.5), 1.0)
        self.assertAlmostEqual(f(dmin / 2), 6.0)

    def test_cadence(self):
        raw = json.loads(S15.read_text(encoding='utf-8')); raw['timing']['cadence'] = 25
        sc = core.load_scene(raw)
        self.assertEqual(core.cadence_time(sc, 0.05, 60), 0.04)  # 25 fps steps inside a 60p project
        self.assertEqual(core.cadence_time(self.sc, 0.05, 60), 0.05)

    def test_envelope(self):
        a = {'id': 'x', 'in': {'start': 1, 'dur': 1, 'ease': 'linear', 'from': {'opacity': 0, 'dy': 10}},
             'out': {'start': 4, 'dur': 1, 'ease': 'linear', 'to': {'opacity': 0}}}
        self.assertEqual(core.channels(a, 1.5)['opacity'], 0.5)
        self.assertEqual(core.channels(a, 1.5)['dy'], 5)
        self.assertEqual(core.channels(a, 3)['opacity'], 1)
        self.assertEqual(core.channels(a, 4.25)['opacity'], 0.75)
        self.assertEqual(core.channels(a, 4.25)['dy'], 0)


@unittest.skipUnless(fonts_ok(S15), 'Stage15 fonts not installed')
class Pixels15(unittest.TestCase):
    def test_hold_is_static_and_render_deterministic(self):
        j = render.Job(core.load_scene(S15))
        self.assertTrue(np.array_equal(j.direct_frame(90), j.direct_frame(96)))
        self.assertTrue(np.array_equal(j.direct_frame(40), render.Job(core.load_scene(S15)).direct_frame(40)))

    def test_alpha_over_base_matches_direct(self):
        j = render.Job(core.load_scene(S15))
        for i in (20, 90, 130):
            a = j.alpha_frame(i).astype(float) / 255
            over = a[:, :, :3] * 255 * a[:, :, 3:] + render.base_matte().astype(float) * (1 - a[:, :, 3:])
            self.assertLessEqual(np.abs(over - j.direct_frame(i)).max(), 3.0)

    def test_first_and_last_frames_are_empty(self):
        j = render.Job(core.load_scene(S15))
        self.assertEqual(j.alpha_frame(0)[:, :, 3].max(), 0)
        self.assertEqual(j.alpha_frame(149)[:, :, 3].max(), 0)

    def test_baseline_stays_when_text_changes(self):
        # A title without ascenders/diacritics keeps the same baseline (no jump on edit).
        r = render.make_renderer(core.load_scene(S15, {'title': 'acum'}))
        t = next(a for a in r.sc.actors if a['id'] == 'title')
        spr, box, asc = r.text_sprite(t)
        x, y = r.text_origin(t)
        self.assertAlmostEqual(y + (16 - box[1] + asc) / 2, 509.0)


@unittest.skipUnless(fonts_ok(S03), 'Inter not installed')
class Pixels03b(unittest.TestCase):
    def test_strap_fits_a_longer_name(self):
        short = render.make_renderer(core.load_scene(S03))
        long_ = render.make_renderer(core.load_scene(S03, {'name': 'Ștefania-Alexandra Țăranu Popescu'}))
        ws = short.rest_width(short.by_id['strapMain']); wl = long_.rest_width(long_.by_id['strapMain'])
        self.assertEqual(ws, 590)  # the minimum width wins for the default name
        self.assertGreater(wl, 590)
        ink = long_.mask(long_.by_id['nameText']).getbbox()[2]
        self.assertEqual(127 + wl, ink + 45)

    def test_text_never_outlives_its_strap(self):
        j = render.Job(core.load_scene(S03))
        for i in range(124, 150):
            a = j.alpha_frame(i)
            strap = a[763:871, 127:1500, 3]
            text_only = (a[763:871, 127:1500, :3].astype(int).sum(2) > 600) & (strap > 0)
            if strap.max() == 0:
                self.assertFalse(text_only.any(), i)


if __name__ == '__main__':
    unittest.main(verbosity=1)
