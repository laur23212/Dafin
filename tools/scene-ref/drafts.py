"""draft1x: the Stage 54–67 library titles (DESIGN_DRAFT, creator verdict PASS_EXTERNAL_MP4).

Each title's frozen renderer is kept verbatim in drafts/d<asset>.py; this module only runs it. A
scene names the renderer by its effect type, gives it the two editable strings (the actor's text
and the effect's "secondary") and the exact font files by sha256. A string equal to the script's
default is passed as None, as the script's own command line does, so the default scene is the
approved MP4 frame for frame. Scripts that froze their hold (the 17 written after Stage 60) froze
it on the first hold frame; draw() does the same, so a hold is one still frame here too.
"""
import importlib.util
import os
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

import scene_core as core

HERE = Path(__file__).resolve().parent

# effect type -> asset id (drafts/d<asset>.py)
KINDS = {
    'milestone': '54B', 'keyphrase': '55A', 'linebreak': '55B', 'waypoint': '56A', 'pullquote': '56B',
    'pinpoint': '56D', 'switchword': '57A', 'nameplate': '57B', 'checklist': '57C', 'section': '57D',
    'dualcaption': '57E', 'sidenote': '57F', 'redact': '58A', 'progress': '58B', 'focal': '58C',
    'credit': '58D', 'pricepoint': '58E', 'bracket': '58F', 'horizon': '59A', 'echo': '59C',
    'dateline': '59D', 'capsule': '59E', 'dropcap': '59F', 'interpose': '60A', 'reanchor': '60C',
    'between': '60D', 'gloss': '61A', 'signpost': '61B', 'parallel': '61C', 'cutline': '61D',
    'focuslist': '61E', 'compose': '62A', 'lumen': '62B', 'stack': '62C', 'footnote': '62D',
    'baseline': '63A', 'underscore': '63B', 'ratio': '63C', 'shutter': '63D', 'delta': '64A',
    'compare': '64B', 'duration': '64C', 'share': '64D', 'turn': '65A', 'wash': '65B', 'total': '65C',
    'labelshift': '65D', 'relay': '66A', 'radiance': '66B', 'countdown': '66C', 'rulefit': '66D',
    'matchword': '67A', 'revision': '67B', 'convert': '67C', 'trail': '67D',
}
# The creator's favourite Filmora styles, rebuilt from their thumbnails (RECONSTRUCTED_APPROX):
# static, render(font_bytes, text) -> (RGBA image, metrics).
FAVOURITES = {'art30': '68_Art30', 'art27': '68_Art27'}


def load_script(asset):
    path = HERE / 'drafts' / f'd{asset}.py'
    spec = importlib.util.spec_from_file_location(f'draft_{asset}', path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    m.FREEZES = 'frozen=render' in path.read_text()
    return m


def font_dir(m, find):
    """A folder with the script's fonts under the names it opens; `find(name, sha)` -> path or None."""
    d = Path(tempfile.mkdtemp(prefix='dafin-draft-fonts-'))
    for name, sha in m.FONT_FILES.values():
        p = find(name, sha)
        if p:
            os.symlink(p, d / name)
    return d


class Draft1x:
    def __init__(self, sc, find_font):
        self.sc = sc
        self.items = []
        for a in sc.actors:
            e = a.get('effect') or {}
            if a['type'] == 'text' and e.get('type') in FAVOURITES:
                self.items.append(self.favourite(a, e, find_font))
                continue
            if a['type'] != 'text' or e.get('type') not in KINDS:
                continue
            m = load_script(KINDS[e['type']])
            text = a['text']
            sec = core.format_text(e['secondary'], a.get('_env') or {}) if 'secondary' in e else None
            text = None if text == m.DEFAULT_TEXT else text
            if sec is not None and sec == getattr(m, 'SECONDARY', None):
                sec = None
            fd = font_dir(m, find_font)
            if m.prepare.__code__.co_argcount == 3:
                render, geo = m.prepare(fd, text, sec)
            else:
                render, geo = m.prepare(fd, m.DEFAULT_TEXT if text is None else text)
            hold = None
            if m.FREEZES:
                ha, hb = geo['hold_seconds']
                n0 = next(n for n in range(m.N) if ha <= n / m.FPS)
                hold = (ha, hb, n0 / m.FPS)
            self.items.append((render, hold, {}))

    def favourite(self, a, e, find_font):
        m = load_script(FAVOURITES[e['type']])
        spec = self.sc.raw['fonts'][a['font']]
        data = Path(find_font(spec['file'], spec['sha256'])).read_bytes()
        im, _ = m.render(data, a['text'])
        return (lambda t: im), None, {}

    def frame(self, t):
        out = Image.new('RGBA', (self.sc.width, self.sc.height))
        for render, hold, cache in self.items:
            if hold and hold[0] <= t <= hold[1]:
                if 'f' not in cache:
                    cache['f'] = render(hold[2])
                im = cache['f']
            else:
                im = render(t)
            im = im if isinstance(im, Image.Image) else Image.fromarray(np.asarray(im), 'RGBA')
            out.alpha_composite(im)
        return out

    def draw(self, t, canvas=None):
        im = self.frame(t)
        if canvas is not None:
            return Image.alpha_composite(canvas.convert('RGBA'), im).convert('RGB')
        return im

    def draw_direct(self, canvas, t):
        """Into an RGB uint8 array, as the scripts' previews composite the frame over black."""
        im = Image.alpha_composite(Image.fromarray(canvas).convert('RGBA'), self.frame(t)).convert('RGB')
        canvas[:] = np.asarray(im)
