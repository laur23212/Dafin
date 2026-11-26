#!/usr/bin/env python3
"""Reference renderer for Scene Title v1 (docs/scene-titles.md). PIL + numpy + OpenCV only.

It reproduces the maths of the approved ChatGPT title scripts, so a scene ported from one of them
renders the same pixels the script did. Outputs: PNG frames with alpha, and an MP4 preview over
the proof's own background (BASE gradient or the proof's background video).

  python3 render.py render scene.json --out out/ [--frames 0,10,20] [--no-mp4] [--param k=v]
  python3 render.py anchor --profile sprite2x --font F.ttf --size 26 --text "STEP" --at 352 291
  python3 render.py validate scene.json
"""
from __future__ import annotations

import argparse
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
from functools import lru_cache
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parent))
import scene_core as core  # noqa: E402
from scene_core import SceneError, legacy_round  # noqa: E402

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
SS = 2  # sprite2x supersampling factor

# ----------------------------------------------------------------------------------------------
# Fonts: a scene names files, never bundles them. Looked up in --font-dir, the repo's
# title-fonts/ and the usual system folders.

# System folders come before the repo's title-fonts/: the approved proofs were drawn with the
# Debian/Ubuntu packages (fonts-noto-core, fonts-noto-extra, fonts-inter), and a file of the same
# name in title-fonts/ can be a different cut (NotoSerif-Regular.ttf is). A "sha256" in the
# scene's font entry picks the exact file.
FONT_DIRS = [Path('/usr/share/fonts'), Path('/usr/local/share/fonts'), Path.home() / '.fonts',
             Path.home() / '.local/share/fonts', REPO / 'title-fonts']


@lru_cache(maxsize=None)
def _font_index(dirs: tuple) -> dict:
    idx = {}
    for d in dirs:
        d = Path(d)
        if not d.is_dir():
            continue
        for root, _, files in sorted(os.walk(d)):
            for f in sorted(files):
                if f.lower().endswith(('.ttf', '.otf', '.ttc')):
                    idx.setdefault(f, []).append(str(Path(root) / f))
    return idx


@lru_cache(maxsize=None)
def _sha256(path: str) -> str:
    import hashlib
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def font_path(sc, key: str, extra_dirs=()) -> str:
    spec = sc.raw.get('fonts', {}).get(key)
    if spec is None:
        raise SceneError(f'unknown font key {key!r}')
    name = spec['file']
    cands = [name] if os.path.isabs(name) and os.path.exists(name) else \
        _font_index(tuple(str(d) for d in (*extra_dirs, *FONT_DIRS))).get(name, [])
    if not cands:
        raise SceneError(f'font file {name} ({spec.get("family")}) not found; pass --font-dir'
                         + (f' (package: {spec["package"]})' if spec.get('package') else ''))
    want = spec.get('sha256')
    if want:
        for c in cands:
            if _sha256(c) == want:
                return c
        raise SceneError(f'font {name}: no file with sha256 {want[:12]}… (found {len(cands)} other cut(s));'
                         + (f' install {spec["package"]}' if spec.get('package') else ' pass --font-dir'))
    return cands[0]


@lru_cache(maxsize=256)
def _truetype(path: str, size: int, raqm: bool):
    if raqm:
        return ImageFont.truetype(path, size, layout_engine=ImageFont.Layout.RAQM)
    return ImageFont.truetype(path, size)


# ----------------------------------------------------------------------------------------------
# Backgrounds

def base_matte(w=1920, h=1080, dark=(9, 12, 17)):
    """The BASE matte of the Stage13+ proofs (frozen_stage13_approved_renderer_reference.py)."""
    out = np.zeros((h, w, 3), np.uint8)
    y = np.linspace(0, 1, h, dtype=np.float32)[:, None, None]
    x = np.linspace(0, 1, w, dtype=np.float32)[None, :, None]
    colors = np.array([dark], np.float32)
    out[:] = np.uint8(np.clip(colors + 7.2 * x + 3.2 * (1 - y), 0, 255))
    return out


# ----------------------------------------------------------------------------------------------
# sprite2x profile: Stage13/14/15 maths. Text and rounded shapes are drawn at 2x in PIL, reduced
# with Lanczos, and laid on the frame with an opacity, an integer offset and a horizontal wipe.

class Sprite:
    def __init__(self, img: Image.Image):
        rgba = np.array(img.convert('RGBA'))
        self.rgb = rgba[:, :, :3].astype(np.float32)
        self.alpha = rgba[:, :, 3:4].astype(np.float32) / 255.
        self.w, self.h = img.size

    def _clip(self, W, H, xy, dx, dy, wipe):
        xx, yy = int(round(xy[0] + dx)), int(round(xy[1] + dy))
        limit = int(round(self.w * min(1, wipe)))
        x0 = max(0, xx); y0 = max(0, yy); x1 = min(W, xx + limit); y1 = min(H, yy + self.h)
        if x1 <= x0 or y1 <= y0:
            return None
        return x0, y0, x1, y1, x0 - xx, y0 - yy, x1 - xx, y1 - yy

    def draw(self, canvas, xy, opacity=1, dx=0, dy=0, wipe=1):
        """Exactly Sprite.draw of the frozen Stage13 renderer (direct RGB, truncating)."""
        if opacity <= 0 or wipe <= 0:
            return
        H, W = canvas.shape[:2]
        c = self._clip(W, H, xy, dx, dy, wipe)
        if not c:
            return
        x0, y0, x1, y1, sx0, sy0, sx1, sy1 = c
        mask = self.alpha[sy0:sy1, sx0:sx1] * opacity
        canvas[y0:y1, x0:x1] = np.uint8(np.clip(canvas[y0:y1, x0:x1] * (1 - mask) + self.rgb[sy0:sy1, sx0:sx1] * mask, 0, 255))

    def draw_premul(self, acc, xy, opacity=1, dx=0, dy=0, wipe=1):
        """Same placement, true alpha: acc is float32 premultiplied RGBA in 0..1."""
        if opacity <= 0 or wipe <= 0:
            return
        H, W = acc.shape[:2]
        c = self._clip(W, H, xy, dx, dy, wipe)
        if not c:
            return
        x0, y0, x1, y1, sx0, sy0, sx1, sy1 = c
        a = self.alpha[sy0:sy1, sx0:sx1] * opacity
        src = np.concatenate([self.rgb[sy0:sy1, sx0:sx1] / 255. * a, a], axis=2)
        acc[y0:y1, x0:x1] = src + acc[y0:y1, x0:x1] * (1 - a)


def _rgba(color, alpha=255):
    return (*color[:3], color[3] if len(color) == 4 else alpha)


class Sprite2x:
    """Renders resolved actors the way the Stage13+ scripts did."""

    LINE_OPACITY = 'mixWith'  # direct RGB has no alpha: the scripts mixed the colour with DARK

    def __init__(self, sc, font_dirs=()):
        self.sc, self.font_dirs = sc, font_dirs
        self.cache = {}
        r = sc.raw.get('render', {})
        self.mix_color = core.parse_color(r.get('mixColor', [9, 12, 17]), sc.palette)

    # -- text ---------------------------------------------------------------------------------
    def font(self, key, size):
        return _truetype(font_path(self.sc, key, self.font_dirs), int(size) * SS, False)

    def text_sprite(self, a):
        key = ('text', a['text'], a['font'], a['size'], tuple(self.color(a)))
        if key not in self.cache:
            f = self.font(a['font'], a['size'])
            value = a['text']
            box = f.getbbox(value)
            width = max(1, box[2] - box[0] + 14 * SS)
            height = max(1, box[3] - box[1] + 16 * SS)
            cv = Image.new('RGBA', (width, height))
            d = ImageDraw.Draw(cv)
            d.text((7 * SS - box[0], 8 * SS - box[1]), value, font=f, fill=(*self.color(a)[:3], 255), stroke_width=0)
            cv = cv.resize((width // SS, height // SS), Image.Resampling.LANCZOS)
            self.cache[key] = (Sprite(cv), box, f.getmetrics()[0])
        return self.cache[key]

    def text_origin(self, a):
        """Sprite top-left from the actor's anchor (docs/scene-titles.md, Coordinates)."""
        spr, box, asc = self.text_sprite(a)
        at = a['at']
        if 'sprite' in at:
            return tuple(at['sprite'])
        if 'baseline' in at:
            bx, by = at['baseline']
            return (bx - (14 - box[0]) / SS, by - (16 - box[1] + asc) / SS)
        raise SceneError(f'{a["id"]}: text needs at.baseline or at.sprite')

    # -- shapes -------------------------------------------------------------------------------
    def color(self, a, key='color'):
        return core.parse_color(a[key], self.sc.palette)

    def shape_sprite(self, a):
        """roundRect / ellipse / ring drawn at 2x; rect is a plain 1x sprite (slim_bar)."""
        t = a['type']
        x0, y0, x1, y1 = a['box']
        if t == 'rect':
            w, h = x1 - x0 + 1, y1 - y0 + 1
            key = ('rect', w, h, self.color(a))
            if key not in self.cache:
                self.cache[key] = Sprite(Image.new('RGBA', (int(w), int(h)), _rgba(self.color(a))))
            return self.cache[key], (x0, y0)
        pad = (2, 3) if t == 'roundRect' else (8, 8)
        w, h = x1 - x0 + pad[0] + pad[1], y1 - y0 + pad[0] + pad[1]
        key = (t, w, h, json.dumps({k: a.get(k) for k in ('color', 'fill', 'stroke', 'strokeColor', 'radius')}))
        if key not in self.cache:
            f = SS
            im = Image.new('RGBA', (int(w) * f, int(h) * f), (0, 0, 0, 0))
            d = ImageDraw.Draw(im)
            bb = (pad[0] * f, pad[0] * f, (pad[0] + x1 - x0) * f, (pad[0] + y1 - y0) * f)
            self._pil_shape(d, a, bb, f)
            self.cache[key] = Sprite(im.resize((int(w), int(h)), Image.Resampling.LANCZOS))
        return self.cache[key], (x0 - pad[0], y0 - pad[0])

    def _pil_shape(self, d, a, bb, f):
        t = a['type']
        fill = _rgba(core.parse_color(a['fill'], self.sc.palette)) if a.get('fill') is not None else None
        stroke = a.get('stroke', 0)
        sc = _rgba(core.parse_color(a['strokeColor'], self.sc.palette)) if a.get('strokeColor') is not None else None
        if t == 'roundRect':
            d.rounded_rectangle(bb, radius=a.get('radius', 0) * f, fill=fill, outline=sc, width=int(stroke * f) if sc else 1)
        elif t == 'ellipse':
            d.ellipse(bb, fill=fill, outline=sc, width=int(stroke * f) if sc else 1)
        elif t == 'ring':
            d.ellipse(bb, outline=_rgba(self.color(a)), width=int(a.get('stroke', 2) * f))
        elif t == 'line':
            d.line(bb, fill=_rgba(self.color(a), a.get('alpha', 255)), width=int(a.get('width', 1) * f))
        else:
            raise SceneError(f'{a["id"]}: {t} cannot be drawn inside a flattened group')

    def group_sprite(self, g, children):
        key = ('group', g['id'], json.dumps([{k: v for k, v in c.items() if not k.startswith('_')} for c in children], sort_keys=True, default=str))
        if key not in self.cache:
            f = int(g.get('supersample', SS))  # Stage14's tags were drawn at 3x
            gw, gh = g['size']
            im = Image.new('RGBA', (gw * f, gh * f))
            d = ImageDraw.Draw(im)
            for c in children:
                if 'in' in c or 'out' in c:
                    raise SceneError(f'{c["id"]}: children of a flattened group cannot animate on their own')
                if c['type'] == 'text':
                    fo = _truetype(font_path(self.sc, c['font'], self.font_dirs), int(c['size']) * f, False)
                    al = c.get('align', {})
                    if al.get('h') == 'center' and al.get('v') == 'center':
                        bb = d.textbbox((0, 0), c['text'], font=fo)
                        tx = (gw * f - (bb[2] - bb[0])) / 2 - bb[0]
                        ty = (gh * f - (bb[3] - bb[1])) / 2 - bb[1] + al.get('dy', 0) * f
                        d.text((tx, ty), c['text'], font=fo, fill=_rgba(self.color(c)))
                    elif al.get('h') == 'center' and al.get('v') == 'top':
                        # Ink top at `top` (group px): the Stage13C Mac keys' symbol and name rows.
                        bb = d.textbbox((0, 0), c['text'], font=fo)
                        tx = (gw * f - (bb[2] - bb[0])) / 2 - bb[0]
                        d.text((tx, al['top'] * f - bb[1]), c['text'], font=fo, fill=_rgba(self.color(c)))
                    else:
                        bx, by = c['at']['baseline']
                        d.text((bx * f, by * f), c['text'], font=fo, fill=_rgba(self.color(c)), anchor='ls')
                elif c['type'] == 'line':
                    (ax, ay), (bx, by) = c['from'], c['to']
                    self._pil_shape(d, c, (ax * f, ay * f, bx * f, by * f), f)
                else:
                    x0, y0, x1, y1 = c['box']
                    self._pil_shape(d, c, (x0 * f, y0 * f, x1 * f, y1 * f), f)
            self.cache[key] = Sprite(im.resize((gw, gh), Image.Resampling.LANCZOS))
        return self.cache[key]

    # -- frame --------------------------------------------------------------------------------
    def _line_points(self, a, progress):
        q = max(0., min(1., progress))
        (ax, ay), (bx, by) = a['from'], a['to']
        return (int(ax), int(ay)), (int(round(ax + (bx - ax) * q)), int(round(ay + (by - ay) * q)))

    def _route_segments(self, a, progress):
        pts = [tuple(int(v) for v in p) for p in a['points']]
        segs = [math.dist(p, q) for p, q in zip(pts, pts[1:])]
        remain = sum(segs) * max(0., min(1., progress))
        out = []
        for p, q, L in zip(pts, pts[1:], segs):
            if remain <= 0:
                break
            end = tuple(round(p[k] + (q[k] - p[k]) * min(1, remain / L)) for k in (0, 1))
            out.append((p, end))
            remain -= L
        return out

    def walk(self, t):
        """Yields (actor, combined channels) in draw order, groups folded into their children."""
        acts = self.sc.actors
        by_id = {a['id']: a for a in acts}
        for a in acts:
            ch = core.channels(a, t)
            p = a.get('_parent')
            skip = False
            while p:
                g = by_id[p]
                if g.get('flatten'):
                    skip = True
                    break
                gc = core.channels(g, t)
                for k in ('opacity', 'wipe', 'progress', 'scale'):
                    ch[k] *= gc[k]
                ch['dx'] += gc['dx'] + g.get('at', [0, 0])[0]
                ch['dy'] += gc['dy'] + g.get('at', [0, 0])[1]
                p = g.get('_parent')
            if skip:
                continue
            if a['type'] == 'group' and not a.get('flatten'):
                continue
            yield a, ch

    def _snap(self, v):
        return legacy_round(v)

    def draw_direct(self, canvas, t):
        """Into an opaque RGB frame (the proof's own compositing, bit-exact)."""
        for a, ch in self.walk(t):
            self._draw(a, ch, canvas, None)

    def draw_alpha(self, t):
        acc = np.zeros((self.sc.height, self.sc.width, 4), np.float32)
        for a, ch in self.walk(t):
            self._draw(a, ch, None, acc)
        rgb = np.where(acc[:, :, 3:4] > 0, acc[:, :, :3] / np.maximum(acc[:, :, 3:4], 1e-6), 0)
        out = np.concatenate([rgb, acc[:, :, 3:4]], axis=2)
        return np.uint8(np.clip(np.round(out * 255), 0, 255))

    def _draw(self, a, ch, canvas, acc):
        if ch['scale'] != 1:
            raise SceneError(f'{a["id"]}: scale is not implemented in the sprite2x profile yet')
        if a.get('offsetSnap') == 'trunc':  # Stage13 keycaps: dy=int(...)
            dx, dy = int(ch['dx']), int(ch['dy'])
        else:
            dx, dy = self._snap(ch['dx']), self._snap(ch['dy'])
        t = a['type']
        if t in ('text', 'rect', 'roundRect', 'ellipse', 'ring', 'group'):
            if t == 'text':
                spr = self.text_sprite(a)[0]
                xy = self.text_origin(a)
            elif t == 'group':
                kids = [c for c in self.sc.actors if c.get('_parent') == a['id']]
                spr, xy = self.group_sprite(a, kids), tuple(a['at'])
            else:
                spr, xy = self.shape_sprite(a)
            if canvas is not None:
                spr.draw(canvas, xy, ch['opacity'], dx, dy, ch['wipe'])
            else:
                spr.draw_premul(acc, xy, ch['opacity'], dx, dy, ch['wipe'])
            return
        if t in ('line', 'route'):
            op, prog = ch['opacity'], ch['progress']
            if op <= 0 or prog <= 0:
                return
            color = self.color(a)
            segs = [self._line_points(a, prog)] if t == 'line' else self._route_segments(a, prog)
            segs = [((p[0] + dx, p[1] + dy), (q[0] + dx, q[1] + dy)) for p, q in segs]
            if canvas is not None:
                if op < 1 or a.get('opacityMode') == 'mixWith':
                    m = min(1, op)
                    color = tuple(int(round(self.mix_color[i] * (1 - m) + color[i] * m)) for i in range(3))
                for p, q in segs:
                    cv2.line(canvas, p, q, color[:3], int(a.get('width', 1)), cv2.LINE_AA)
            else:
                m = np.zeros(acc.shape[:2], np.uint8)
                for p, q in segs:
                    cv2.line(m, p, q, 255, int(a.get('width', 1)), cv2.LINE_AA)
                al = (m.astype(np.float32) / 255. * min(1, op))[:, :, None]
                src = np.concatenate([np.array(color[:3], np.float32) / 255. * al, al], axis=2)
                acc[:] = src + acc * (1 - al)
            return
        raise SceneError(f'{a["id"]}: {t} is not drawn by the sprite2x profile')

    # -- geometry for compare.py --------------------------------------------------------------
    def rest_boxes(self, t):
        boxes = {}
        for a, ch in self.walk(t):
            tp = a['type']
            if tp == 'text':
                spr = self.text_sprite(a)[0]; x, y = self.text_origin(a)
                boxes[a['id']] = (x, y, x + spr.w, y + spr.h)
            elif tp in ('line', 'route'):
                pts = [a['from'], a['to']] if tp == 'line' else a['points']
                w = a.get('width', 1) + 2
                xs, ys = [p[0] for p in pts], [p[1] for p in pts]
                boxes[a['id']] = (min(xs) - w, min(ys) - w, max(xs) + w + 1, max(ys) + w + 1)
            elif tp == 'group':
                gx, gy = a['at']; boxes[a['id']] = (gx, gy, gx + a['size'][0], gy + a['size'][1])
            else:
                spr, (x, y) = self.shape_sprite(a)
                boxes[a['id']] = (x, y, x + spr.w, y + spr.h)
        return boxes


# ----------------------------------------------------------------------------------------------
# mask1x profile: Stage03b maths. Full-size 8-bit text masks drawn 1x with RAQM ('lt' anchor),
# shifted with a bicubic affine, clipped by rectangles, composited with alpha_composite on an
# RGBA canvas; rectangles are written straight into the canvas (later shapes replace earlier).

class Mask1x:
    def __init__(self, sc, font_dirs=()):
        self.sc, self.font_dirs = sc, font_dirs
        self.by_id = {a['id']: a for a in sc.actors}
        self._widths = {}
        self._mask_cache = {}

    def font(self, key, size):
        return _truetype(font_path(self.sc, key, self.font_dirs), max(10, round(size)), True)

    def color(self, a, key='color'):
        return core.parse_color(a[key], self.sc.palette)

    def text_xy(self, a):
        at = a['at']
        if 'lt' in at:
            return tuple(at['lt'])
        if 'baseline' in at:
            f = self.font(a['font'], a['size'])
            k = f.getbbox(a['text'], anchor='lt')[1] - f.getbbox(a['text'], anchor='ls')[1]
            bx, by = at['baseline']
            return (bx, by - k)
        raise SceneError(f'{a["id"]}: text needs at.baseline or at.lt')

    def text_mask(self, a):
        W, H = self.sc.width, self.sc.height
        f = self.font(a['font'], a['size'])
        m = Image.new('L', (W, H)); d = ImageDraw.Draw(m)
        xy = self.text_xy(a)
        sp = a.get('letterSpacing', 0)
        if not sp:
            d.text(xy, a['text'], font=f, fill=255, anchor='lt', stroke_width=0)
        else:
            x, y = xy
            for c in a['text']:
                d.text((x, y), c, font=f, fill=255, anchor='lt'); x += d.textlength(c, font=f) + sp
        return m

    def mask(self, a):
        key = (a['id'], a['text'], a['font'], a['size'], json.dumps(a['at']))
        if key not in self._mask_cache:
            self._mask_cache[key] = self.text_mask(a)
        return self._mask_cache[key]

    def rest_width(self, a):
        """Width of a rect at rest: its box, or fitTo another actor's ink plus padding."""
        if a['id'] in self._widths:
            return self._widths[a['id']]
        x0, y0, x1, y1 = a['box']
        w = x1 - x0
        fit = a.get('fitTo')
        if fit:
            tgt = self.by_id[fit['actor']]
            ink_right = self.mask(tgt).getbbox()[2]
            w = max(fit.get('min', 0), ink_right - x0 + fit.get('pad', 0))
            if 'max' in fit:  # Stage03b A: min(w-55, max(155, nb[2]+8))
                w = min(fit['max'], w)
        if 'widthOf' in a:
            w = min(self.rest_width(self.by_id[i]) for i in a['widthOf'])
        self._widths[a['id']] = w
        return w

    def current_box(self, a, ch, origin=(0, 0)):
        """The box as it is now. `origin` makes it relative to a layer (see the rect draw)."""
        ox, oy = origin
        x0, y0, x1, y1 = a['box']
        x0, y0, x1, y1 = x0 - ox, y0 - oy, x1 - ox, y1 - oy
        if a.get('wipeDir') == 'down':  # Stage03b C stem: the height grows
            return (x0, y0, x0 + self.rest_width(a), y0 + (y1 - y0) * ch['wipe'])
        return (x0, y0, x0 + self.rest_width(a) * ch['wipe'], y1)

    def draw(self, t):
        W, H = self.sc.width, self.sc.height
        c = Image.new('RGBA', (W, H), (0, 0, 0, 0))
        for a in self.sc.actors:
            ch = core.channels(a, t)
            tp = a['type']
            if tp in ('rect', 'roundRect'):
                d = ImageDraw.Draw(c, 'RGBA')
                col = self.color(a)
                A = col[3] if len(col) == 4 else 255
                # The scripts built and truncated the box in their overlay layer's own coordinates;
                # doing it in absolute ones can land a float hair across a whole pixel (Stage03b C
                # frame 15: 18 + 166·a = 100.99999 in the layer, 846.0 on the canvas).
                ox, oy = (self.sc.raw.get('preview', {}).get('legacy', {}).get('overlayRect') or [0, 0])[:2]
                b = tuple(int(v) + o for v, o in zip(self.current_box(a, ch, (ox, oy)), (ox, oy, ox, oy)))
                d.rounded_rectangle(b, radius=a.get('radius', 0), fill=(*col[:3], round(A * ch['opacity'])))
            elif tp == 'text':
                alpha = ch['opacity']
                if alpha <= 0:
                    continue
                m = self.mask(a)
                m = m.transform(m.size, Image.Transform.AFFINE, (1, 0, -ch['dx'], 0, 1, -ch['dy']), resample=Image.Resampling.BICUBIC, fillcolor=0)
                clip = a.get('clipTo')
                if clip:
                    tgt = self.by_id[clip['actor']]
                    bx0, by0, bx1, by1 = self.current_box(tgt, core.channels(tgt, t))
                    l, tp_, r, bt = clip.get('inset', [0, 0, 0, 0])
                    cm = Image.new('L', m.size)
                    cx0, cy0, cx1, cy1 = bx0 + l, by0 + tp_, bx1 - r, by1 - bt
                    if cx1 >= cx0 and cy1 >= cy0:  # an empty clip hides the text
                        ImageDraw.Draw(cm).rectangle((cx0, cy0, cx1, cy1), fill=255)
                    m = Image.composite(m, Image.new('L', m.size), cm)
                cr = a.get('clipRect')
                if cr:  # a fixed window whose top rises with the entrance (Stage03b A/C)
                    blk = (core.blocks(a.get('in')) or [None])[0]
                    p = core.phase(t, float(blk['start']), float(blk.get('dur', 0)), blk.get('ease')) if blk else 1.0
                    rise = (1 - p) * cr.get('rise', 0)
                    x0, y0, x1, y1 = cr['box']
                    y0 = y0 + (round(rise) if cr.get('riseSnap') == 'round' else int(rise))
                    cm = Image.new('L', m.size)
                    if y1 >= y0 and x1 >= x0:
                        ImageDraw.Draw(cm).rectangle((x0, y0, x1, y1), fill=255)
                    m = Image.composite(m, Image.new('L', m.size), cm)
                if alpha < 1:
                    m = m.point(lambda p: round(p * alpha))
                layer = Image.new('RGBA', m.size, (*self.color(a)[:3], 0)); layer.putalpha(m)
                c.alpha_composite(layer)
            else:
                raise SceneError(f'{a["id"]}: {tp} is not drawn by the mask1x profile yet')
        return c

    def rest_boxes(self, t):
        boxes = {}
        for a in self.sc.actors:
            if a['type'] == 'text':
                bb = self.mask(a).getbbox()
                if bb:
                    boxes[a['id']] = bb
            else:
                x0, y0, x1, y1 = self.current_box(a, core.channels(a, t))
                boxes[a['id']] = (int(x0), int(y0), int(x1) + 1, int(y1) + 1)
        return boxes


class Pil1x:
    """The Part5 Stage05-08 scripts: 1x PIL text sprites (8 px padding, basic layout) and shapes,
    each drawn on its own transparent pane and alpha_composited in order; a scene-wide exitWipe."""

    PAD = 8

    def __init__(self, sc, font_dirs=()):
        self.sc, self.font_dirs = sc, font_dirs
        self.by_id = {a['id']: a for a in sc.actors}
        self.cache = {}
        self.mix_color = core.parse_color(sc.raw.get('render', {}).get('mixColor', [9, 12, 17]), sc.palette)
        r = sc.raw.get('render', {})
        # Stage05 composite_sprite vs Stage06 put(): thresholds and the opacity table differ.
        self.min_opacity = float(r.get('minOpacity', 0.001))   # skip at or below (Stage06: <= .002)
        self.min_reveal = float(r.get('minReveal', 0.0))       # Stage06: reveal <= .002 skips
        self.full_opacity = float(r.get('fullOpacity', 1.0))   # table applied below this (Stage06 .999)
        self.alpha_trunc = r.get('alphaTable') == 'trunc'      # int(a·p) instead of round
        self.skip_empty = bool(r.get('skipEmptyRects'))        # pane(): x1 <= x0 or y1 <= y0 draws nothing
        # Stage07 put(): skips below (not at) .002, reveals by cropping int(w·v) px when v < .999
        self.skip_le = r.get('skipCompare', 'le' if self.min_opacity > 0.001 else 'lt') == 'le'
        self.reveal_crop = r.get('revealMode') == 'crop'
        self.full_reveal = float(r.get('fullReveal', 1.0))

    def font(self, key, size):
        return _truetype(font_path(self.sc, key, self.font_dirs), int(size), False)

    def rgba(self, a, key='color'):
        c = core.parse_color(a[key], self.sc.palette)
        return tuple(c) if len(c) == 4 else (*c, 255)

    def chapter_sprite(self, a):
        """Dark Chapter A v2: pen = 4 + Σ(getlength(c) + tracking) at float x, ink rows from
        getbbox; sprite ceil(Σ(adv + tracking) − tracking) + 11 wide, bbox height + 11 tall."""
        key = ('chapter', a['id'], a['text'], a['font'], a['size'], a.get('letterSpacing', 0))
        if key not in self.cache:
            f, txt, trk, pad = self.font(a['font'], a['size']), a['text'], a.get('letterSpacing', 0), 4
            width = int(math.ceil(sum(f.getlength(ch) + trk for ch in txt) - (trk if txt else 0)))
            x1, y1, x2, y2 = f.getbbox(txt)
            im = Image.new('RGBA', (width + 2 * pad + 3, y2 - y1 + 2 * pad + 3), (0, 0, 0, 0))
            d = ImageDraw.Draw(im)
            cur = pad
            for ch in txt:
                d.text((cur, pad - y1), ch, font=f, fill=self.rgba(a))
                cur += f.getlength(ch) + trk
            bx, by = a['at']['baseline']  # pen origin at sprite x + 4, baseline at sprite y + 4 − y1 + ascent
            xy = (bx - pad, by - pad + y1 - f.getmetrics()[0])
            self.cache[key] = (im, xy)
        return self.cache[key]

    def draw_chapter(self, c, a, ch):
        im, (x, y) = self.chapter_sprite(a)
        op, v = ch['opacity'], ch['wipe']
        if op <= 0 or v <= 0:
            return
        cw = max(0, min(im.width, round(im.width * v)))
        if cw < 1:
            return
        sub = im.crop((0, 0, cw, im.height))
        if op < 0.999:
            sub.putalpha(sub.getchannel('A').point(lambda q: round(q * op)))
        c.alpha_composite(sub, dest=(round(x + ch['dx']), round(y + ch['dy'])))

    def sprite(self, a, text=None):
        text = a['text'] if text is None else text
        key = ('sprite', a['id'], text, a['font'], a['size'], json.dumps(a['at']))
        if key not in self.cache:
            f = self.font(a['font'], a['size'])
            d = ImageDraw.Draw(Image.new('RGBA', (20, 20)))
            b = d.textbbox((0, 0), text, font=f)
            px = py = self.PAD
            im = Image.new('RGBA', (max(1, b[2] - b[0] + px * 2), max(1, b[3] - b[1] + py * 2)), (0, 0, 0, 0))
            ImageDraw.Draw(im).text((px - b[0], py - b[1]), text, font=f, fill=self.rgba(a))
            if 'right' in a['at']:  # Stage06 counter: the sprite's right edge is fixed
                x = a['at']['right'] - im.width
                y = a['at']['top']
            else:
                bx, by = a['at']['baseline']
                x, y = bx - px + b[0], by - f.getmetrics()[0] - py + b[1]
            self.cache[key] = (im, (x, y))
        return self.cache[key]

    def tracked_sprite(self, a):
        """Stage08 Cinematic: glyph by glyph at round(pen), pen += textlength(ch) + tracking, on a
        sprite ceil(Σadvances + tracking·(n−1)) + 16 wide, int(bbox height) + 20 tall."""
        key = ('tracked', a['id'], a['text'], a['font'], a['size'], a['tracking'])
        if key not in self.cache:
            f = self.font(a['font'], a['size'])
            td = ImageDraw.Draw(Image.new('RGBA', (4, 4), (0, 0, 0, 0)))
            text = a['text']
            adv = [td.textlength(ch, font=f) for ch in text]
            bb = td.textbbox((0, 0), text, font=f)
            tw = int(math.ceil(sum(adv) + a['tracking'] * (len(text) - 1))) + 16
            th = int(bb[3] - bb[1]) + 20
            im = Image.new('RGBA', (tw, th), (0, 0, 0, 0))
            d = ImageDraw.Draw(im)
            x = 8.0
            for ch, w in zip(text, adv):
                d.text((round(x), 9 - bb[1]), ch, font=f, fill=self.rgba(a), stroke_width=0)
                x += w + a['tracking']
            self.cache[key] = im
        return self.cache[key]

    def draw_tracked(self, c, a, ch):
        im = self.tracked_sprite(a)
        ent, alpha = ch['wipe'], ch['opacity']  # alpha = 1 − ex (the exit's opacity)
        if ent <= 0 or alpha <= 0:
            return
        cx, cy = a['at']['center']
        x = int(cx - im.width / 2)
        y = int(round(cy - im.height / 2 + ch['dy']))
        vis = int(im.width * ent)
        if vis <= 0:
            return
        src = im.crop((0, 0, vis, im.height))
        if self.alpha_mode or a.get('composite') != 'numpy':
            sp = src.copy()
            sp.putalpha(src.getchannel('A').point(lambda q: round(q * alpha)))
            c.alpha_composite(sp, (x, y), (max(0, -x), max(0, -y))) if x < 0 or y < 0 else c.alpha_composite(sp, (x, y))
            return
        # The script's numpy blend on its BGR frame: trunc(t·(1−a)) + round(c·a), uint8.
        arr = np.array(c)
        sr = np.asarray(src, dtype=np.uint8)
        h, w = sr.shape[:2]
        W, H = self.sc.width, self.sc.height
        x1, y1, x2, y2 = max(0, x), max(0, y), min(W, x + w), min(H, y + h)
        if x2 <= x1 or y2 <= y1:
            return
        s = sr[y1 - y:y2 - y, x1 - x:x2 - x]
        av = (s[:, :, 3:4].astype(np.float32) / 255.0) * alpha
        tgt = arr[y1:y2, x1:x2, :3]
        np.multiply(tgt, 1.0 - av, out=tgt, casting='unsafe')
        tgt += np.round(s[:, :, :3].astype(np.float32) * av).astype(np.uint8)
        c.paste(Image.fromarray(arr, 'RGBA'))

    def counter_text(self, a, t):
        """counter: {start, dur, ease, to}: the text is str(round(to·phase)) (Stage06 Metric)."""
        c = a['counter']
        return str(round(c['to'] * core.phase(t, float(c['start']), float(c['dur']), c.get('ease'))))

    # -- visibility -------------------------------------------------------------------------
    def chan(self, a, t):
        """The actor's channels with its plain groups folded in (as Sprite2x.walk does)."""
        clock = lambda aid, name: self.chan(self.by_id[aid], t)[name]
        ch = core.channels(a, t, clock)
        p = a.get('_parent')
        while p:
            g = self.by_id[p]
            gc = core.channels(g, t, clock)
            for k in ('opacity', 'wipe', 'progress', 'scale', 'vscale'):
                ch[k] *= gc[k]
            ch['dx'] += gc['dx']
            ch['dy'] += gc['dy']
            p = g.get('_parent')
        return ch

    def visible(self, a, t):
        """visibleWhen: the scripts drew some parts only past a threshold (`if p > .01`)."""
        node = a
        while node is not None:
            for c in node.get('visibleWhen', []):
                other = self.by_id[c['actor']] if c.get('actor') else node
                if not self.chan(other, t)[c['channel']] > c['gt']:
                    return False
            node = self.by_id.get(node.get('_parent')) if node.get('_parent') else None
        return True

    # -- drawing ----------------------------------------------------------------------------
    def draw(self, t, canvas=None):
        W, H = self.sc.width, self.sc.height
        c = canvas.copy() if canvas is not None else Image.new('RGBA', (W, H), (0, 0, 0, 0))
        self.alpha_mode = canvas is None
        # layer "base": Stage07's matte composition, drawn first and never wiped
        base = [a for a in self.sc.actors if a.get('layer') == 'base' or
                (a.get('_parent') and self.by_id[a['_parent']].get('layer') == 'base')]
        rest = [a for a in self.sc.actors if a not in base]
        self._draw_list(c, base, t)
        before = c.copy()
        self._draw_list(c, rest, t)
        return self._exits(c, before, t, canvas)

    def _draw_list(self, c, actors, t):
        W, H = self.sc.width, self.sc.height
        for a in actors:
            if a.get('_parent') and self.by_id[a['_parent']].get('pane'):
                continue  # drawn with its pane group
            if a['type'] == 'group' and not a.get('pane'):
                continue
            if not self.visible(a, t):
                if a['type'] == 'group':
                    c.alpha_composite(Image.new('RGBA', (W, H), (0, 0, 0, 0)))
                continue
            ch = self.chan(a, t)
            if a['type'] == 'text' and 'tracking' in a:
                self.draw_tracked(c, a, ch)
            elif a['type'] == 'text' and a.get('sprite') == 'chapter':
                self.draw_chapter(c, a, ch)
            elif a['type'] == 'text':
                self.draw_text(c, a, ch, self.counter_text(a, t) if a.get('counter') else None)
            elif a['type'] == 'group':
                pane = Image.new('RGBA', (W, H), (0, 0, 0, 0))
                d = ImageDraw.Draw(pane)
                for k in [x for x in self.sc.actors if x.get('_parent') == a['id']]:
                    if self.visible(k, t):
                        self.draw_shape(d, k, self.chan(k, t))
                c.alpha_composite(pane)
            elif a.get('draw') == 'direct':  # Stage07: ImageDraw straight on the frame (replaces)
                self.draw_shape(ImageDraw.Draw(c), a, ch, opaque=self.alpha_mode)
            else:
                pane = Image.new('RGBA', (W, H), (0, 0, 0, 0))
                self.draw_shape(ImageDraw.Draw(pane), a, ch)
                c.alpha_composite(pane)

    def _exits(self, c, before, t, canvas):
        W, H = self.sc.width, self.sc.height
        es = self.sc.raw.get('exitSlide')
        if es and t >= float(es['start']):
            # Stage06 rigid exit: the title as it stood at `hold`, cropped to `rect`, slides down.
            if t >= float(es['start']) + float(es['dur']):
                return before
            x, y, w, h = es['rect']
            frozen = self.draw(float(es['hold']), canvas if canvas is not None else None).crop((x, y, x + w, y + h))
            dy = round(es['distance'] * core.phase(t, float(es['start']), float(es['dur']), es.get('ease')))
            out = before.copy()
            vis = max(0, min(h, self.sc.height - (y + dy)))
            if vis > 0:
                out.alpha_composite(frozen.crop((0, 0, w, vis)), (x, y + dy))
            return out
        ew = self.sc.raw.get('exitWipe')
        if ew:
            p = core.phase(t, float(ew['start']), float(ew['dur']), ew.get('ease'))
            if p >= 1:
                return before
            if p > 0:
                m = Image.new('L', (W, H), 0)
                md = ImageDraw.Draw(m)
                mode = ew.get('mode', 'span')
                if mode == 'right':    # Stage07 A: keep x <= round(W·(1 - q))
                    md.rectangle((0, 0, round(W * (1 - p)), H), fill=255)
                elif mode == 'left':   # Stage07 C: keep x >= round(W·q)
                    md.rectangle((round(W * p), 0, W, H), fill=255)
                elif mode == 'top':    # Stage07 B: keep y >= round(H·q)
                    md.rectangle((0, round(H * p), W, H), fill=255)
                else:                  # Stage05: keep x <= round(right - (right - left)·p)
                    md.rectangle((0, 0, max(0, round(ew['right'] - (ew['right'] - ew['left']) * p)), H), fill=255)
                c = Image.composite(c, before, m)
        return c

    def draw_text(self, c, a, ch, text=None):
        im, (x, y) = self.sprite(a, text)
        op = max(0.0, min(1.0, ch['opacity']))
        if op < self.min_opacity or (self.skip_le and op <= self.min_opacity):
            return
        rev = a.get('reveal')
        if rev and self.reveal_crop:  # Stage07 put(): crop to max(1, int(w·v)) px
            v = max(0.0, min(1.0, ch['wipe']))
            if v < self.min_reveal:
                return
            img = im.crop((0, 0, max(1, int(im.width * v)), im.height)) if v < self.full_reveal else im
            if op < self.full_opacity:
                img = img.copy()
                img.putalpha(img.getchannel('A').point(lambda q: round(q * op)))
            c.alpha_composite(img, (round(x + ch['dx']), round(y + ch['dy'])))
            return
        alpha = im.getchannel('A')
        if rev:
            v = max(0.0, min(1.0, ch['wipe']))
            if v == 0 or (self.min_reveal and ch['wipe'] <= self.min_reveal):
                return
            m = Image.new('L', im.size, 0)
            md = ImageDraw.Draw(m)
            if rev == 'x':
                md.rectangle((0, 0, max(0, round(im.width * v) - 1), im.height), fill=255)
            else:
                md.rectangle((0, im.height - max(0, round(im.height * v)), im.width, im.height), fill=255)
            alpha = ImageChops.multiply(alpha, m)
        if op < self.full_opacity:
            if self.alpha_trunc:
                alpha = alpha.point(lambda p: int(p * op))
            else:
                alpha = alpha.point(lambda p: max(0, min(255, round(p * op))))
        src = im.copy(); src.putalpha(alpha)
        c.alpha_composite(src, (round(x + ch['dx']), round(y + ch['dy'])))

    def ink(self, a, ch, key='color'):
        r, g, b, A = self.rgba(a, key)
        return (r, g, b, round(A * ch['opacity']))

    def draw_shape(self, d, a, ch, opaque=False):
        tp = a['type']
        if opaque:  # direct draws ended up opaque in the RGB proofs; the alpha layer keeps them so
            ch = dict(ch, opacity=1.0)
            a = {**a, **{k: list(core.parse_color(a[k], self.sc.palette)[:3]) + [255]
                          for k in ('color', 'fill', 'strokeColor') if k in a}}
        if tp in ('rect', 'roundRect'):
            x0, y0, x1, y1 = a['box']
            w, h = x1 - x0, y1 - y0
            if a.get('wipeSnap') == 'int1':  # Stage05 A: x0 + max(1, int(w·p))
                x1 = x0 + max(1, int(w * ch['wipe']))
            elif a.get('wipeSnap') == 'round':  # Stage06: x0 + round(w·p)
                x1 = x0 + round(w * ch['wipe'])
            elif ch['wipe'] != 1:
                x1 = x0 + w * ch['wipe']
            if ch['vscale'] != 1:  # collapse to the vertical centre
                trim = h * (1 - ch['vscale']) / 2
                y0, y1 = y0 + trim, y1 - trim
            if 'strokeColor' in a and 'color' not in a and 'fill' not in a:  # outline only (Stage07 C frame)
                d.rectangle((x0, y0, x1, y1), outline=self.ink(a, ch, 'strokeColor'), width=int(a.get('stroke', 1)))
                return
            fill = self.ink(a, ch, 'fill' if tp == 'roundRect' else 'color')
            if self.skip_empty and (x1 <= x0 or y1 <= y0):
                return
            if a.get('radius') and 'strokeColor' in a:  # Dark Chapter A v2's plate: fill and hairline
                d.rounded_rectangle((x0, y0, x1, y1), radius=a['radius'], fill=fill,
                                    outline=self.ink(a, ch, 'strokeColor'), width=int(a.get('stroke', 1)))
            elif a.get('radius'):
                d.rounded_rectangle((x0, y0, x1, y1), radius=a['radius'], fill=fill)
            else:
                d.rectangle((x0, y0, x1, y1), fill=fill)
        elif tp == 'line':
            (ax, ay), (bx, by) = a['from'], a['to']
            p, v = ch['progress'], ch['vscale']
            if a.get('collapse') == 'ends':  # Stage05 A: y0+43+L(1-v)/2 … y1-43-L(1-v)/2
                L = by - ay
                ay, by = ay + L * (1 - v) / 2, by - L * (1 - v) / 2
            elif a.get('progressSnap') == 'round':  # Stage06: horizontal, end = base + round(L·p)
                base = a.get('progressBase', ax)
                bx = base + round((bx - base) * p)
            elif p != 1 or v != 1:  # Stage05 C: y + (L·p)(1-v)/2 … y + (L·p)(1+v)/2
                L = by - ay
                ay, by = ay + (L * p) * (1 - v) / 2, ay + (L * p) * (1 + v) / 2
            d.line((ax, ay, bx, by), fill=self.ink(a, ch), width=int(a.get('width', 1)))
        elif tp == 'route':
            pts = [tuple(p) for p in a['points']]
            wts = a.get('segmentWeights')
            p = max(0.0, min(1.0, ch['progress']))
            fill = self.ink(a, ch)
            start = 0.0
            for i, (q0, q1) in enumerate(zip(pts, pts[1:])):
                wgt = wts[i] if wts else 1 / (len(pts) - 1)
                if i == len(pts) - 2 or p <= start + wgt:
                    q = (p - start) / wgt if i else p / wgt
                    d.line((*q0, q0[0] + (q1[0] - q0[0]) * q, q0[1] + (q1[1] - q0[1]) * q), fill=fill, width=int(a.get('width', 1)))
                    break
                d.line((*q0, *q1), fill=fill, width=int(a.get('width', 1)))
                start += wgt
        elif tp == 'pin':  # Stage06 Location: ring, dot and stem, one scaled motif
            cx, cy = a['center']
            s = ch['scale']
            r = round(a['radius'] * s)
            d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=self.ink(a, ch, 'ringColor'), width=int(a.get('stroke', 3)))
            dr = a.get('dotRadius', 4)
            d.ellipse((cx - dr, cy - dr, cx + dr, cy + dr), fill=self.ink(a, ch, 'dotColor'))
            d.line((cx, cy + r, cx, cy + r + round(a['stem'] * s)), fill=self.ink(a, ch, 'stemColor'), width=int(a.get('stroke', 3)))
        elif tp in ('ellipse', 'ring'):
            cx, cy = a['center']
            r = round(a['radius'] * ch['scale'])
            bb = (cx - r, cy - r, cx + r, cy + r)
            if tp == 'ring':
                d.ellipse(bb, outline=self.ink(a, ch), width=int(a.get('stroke', 1)))
            else:
                d.ellipse(bb, fill=self.ink(a, ch, 'fill'))
        else:
            raise SceneError(f'{a["id"]}: {tp} is not drawn by the pil1x profile')

    def draw_alpha(self, t):
        return np.array(self.draw(t))


class Cov1x:
    """Chapter A v4 / B v4 (RECONSTRUCTED_APPROX, dafin_chapters_v4_recon.py): every actor is a float
    coverage map over the frame, composited in order as out·(1 − m) + colour·m.
    Text: advance-rounded glyphs (round(pen), anchor 'la', pen += getlength + tracking) drawn at
    `supersample`× and BOX-reduced. Boxes: a rounded rect drawn at 4× and BOX-reduced. A wipe is a
    crop edge from wipeRange[0] to wipeRange[1] with a 1 px anti-aliased edge (corners are not
    re-rounded); `clipTo` limits an actor to another's wiped coverage."""

    def __init__(self, sc, font_dirs=()):
        self.sc, self.font_dirs = sc, font_dirs
        self.by_id = {a['id']: a for a in sc.actors}
        self.cache = {}
        self.ss = int(sc.raw.get('render', {}).get('supersample', 1))

    def font(self, key, size):
        return _truetype(font_path(self.sc, key, self.font_dirs), size, False)

    def cov(self, a):
        W, H = self.sc.width, self.sc.height
        key = (a['id'], a.get('text'), a.get('size'), json.dumps(a.get('at')), json.dumps(a.get('box')))
        if key in self.cache:
            return self.cache[key]
        if a['type'] == 'text':
            ss = int(a.get('supersample', self.ss))
            f = self.font(a['font'], int(round(a['size'] * ss)))
            im = Image.new('L', (W * ss, H * ss), 0)
            d = ImageDraw.Draw(im)
            x, y = a['at']['ascender']
            pen = float(round(x * ss))
            for ch in a['text']:
                d.text((round(pen), round(y * ss)), ch, font=f, fill=255, anchor='la')
                pen += f.getlength(ch) + a.get('letterSpacing', 0.0) * ss
            if ss > 1:
                im = im.resize((W, H), Image.Resampling.BOX)
        else:
            x0, y0, x1, y1 = a['box']
            r, S = a.get('radius', 0), 4
            im = Image.new('L', (W, H), 0)
            big = Image.new('L', ((x1 - x0 + 1) * S, (y1 - y0 + 1) * S), 0)
            ImageDraw.Draw(big).rounded_rectangle((0, 0, big.width - 1, big.height - 1), radius=r * S, fill=255)
            im.paste(big.resize((x1 - x0 + 1, y1 - y0 + 1), Image.Resampling.BOX), (x0, y0))
        self.cache[key] = np.asarray(im, np.float32) / 255.0
        return self.cache[key]

    def wipe_mask(self, a, ch):
        x0, x1 = a['wipeRange']
        edge = x0 + (x1 - x0) * ch['wipe']
        xs = np.arange(0, self.sc.width, dtype=np.float32)
        return np.clip(edge - (xs + 0.5) + 0.5, 0, 1)[None, :]

    def draw(self, t, canvas=None):
        W, H = self.sc.width, self.sc.height
        out = None if canvas is None else np.asarray(canvas.convert('RGB'), np.float32).copy()
        acc_a = np.zeros((H, W), np.float32)
        acc_c = np.zeros((H, W, 3), np.float32)
        for a in self.sc.actors:
            if a['type'] == 'group':
                continue
            ch = core.channels(a, t)
            op, dy = float(a.get('alpha', 1.0)), ch['dy']
            node = self.by_id.get(a.get('_parent'))
            while node is not None:  # the group exit: op · group · own fade, as the recon multiplied
                g = core.channels(node, t)
                op *= g['opacity']
                dy += g['dy']
                node = self.by_id.get(node.get('_parent'))
            op *= ch['opacity']
            if op <= 0:
                continue
            m = self.cov(a)
            dy = int(round(dy))
            if dy:
                m = np.roll(m, dy, axis=0)
            m = m * op
            if 'wipeRange' in a:
                m = m * self.wipe_mask(a, ch)
            if a.get('clipTo'):
                c = self.by_id[a['clipTo']['actor']]
                m = m * self.wipe_mask(c, core.channels(c, t)) * (self.cov(c) > 0)
            col = np.array(core.parse_color(a['fill'] if a['type'] == 'roundRect' else a['color'], self.sc.palette)[:3], np.float32)
            if out is not None:
                out = out * (1 - m[..., None]) + col * m[..., None]
            acc_c = acc_c * (1 - m[..., None]) + col * m[..., None]
            acc_a = acc_a * (1 - m) + m
        if out is not None:
            return Image.fromarray(out.clip(0, 255).astype(np.uint8), 'RGB')
        rgb = np.where(acc_a[..., None] > 0, acc_c / np.maximum(acc_a[..., None], 1e-6), 0)
        return Image.fromarray(np.dstack([rgb, acc_a * 255]).clip(0, 255).astype(np.uint8), 'RGBA')


def make_renderer(sc, font_dirs=()):
    return {'sprite2x': Sprite2x, 'mask1x': Mask1x, 'pil1x': Pil1x, 'cov1x': Cov1x}[sc.profile](sc, font_dirs)


# ----------------------------------------------------------------------------------------------
# Frames and outputs

class Job:
    def __init__(self, sc, font_dirs=(), fps=None, duration=None, assets=()):
        self.sc = sc
        self.r = make_renderer(sc, font_dirs)
        self.fps = float(fps or sc.design_fps)
        self.duration = float(duration or sc.design_duration)
        self.tmap = core.time_map(sc, self.duration)
        self.assets = [Path(p) for p in assets]
        self.count = int(round(self.duration * self.fps))

    def design_t(self, i):
        t = i / self.fps
        return self.tmap(core.cadence_time(self.sc, t, self.fps))

    def alpha_frame(self, i) -> np.ndarray:
        t = self.design_t(i)
        if isinstance(self.r, Sprite2x):
            return self.r.draw_alpha(t)
        return np.array(self.r.draw(t))

    def direct_frame(self, i) -> np.ndarray:
        """sprite2x over BASE, drawn as the proof drew it."""
        bg = base_matte(self.sc.width, self.sc.height, self.r.mix_color)
        self.r.draw_direct(bg, self.design_t(i))
        return bg

    def find_asset(self, name):
        for d in [*self.assets, *(([self.sc.path.parent]) if self.sc.path else [])]:
            p = d / name
            if p.exists():
                return p
        raise SceneError(f'preview asset {name} not found; pass --assets <folder>')

    def preview_mp4(self, dst: Path):
        pv = self.sc.raw.get('preview', {})
        bg = pv.get('background', {'kind': 'base'})
        enc = pv.get('encode', {})
        x264 = ['-c:v', 'libx264', '-preset', enc.get('preset', 'veryfast'), '-crf', str(enc.get('crf', 18)),
                '-pix_fmt', 'yuv420p']
        W, H, fps = self.sc.width, self.sc.height, self.fps
        fr = str(int(fps)) if float(fps).is_integer() else str(fps)
        if bg.get('kind') == 'base' and isinstance(self.r, Sprite2x):
            cmd = ['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb24',
                   '-s', f'{W}x{H}', '-r', fr, '-i', '-', '-an', *x264, '-movflags', '+faststart', str(dst)]
            p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                for i in range(self.count):
                    p.stdin.write(self.direct_frame(i).tobytes())
            finally:
                p.stdin.close()
            err = p.stderr.read().decode('utf-8', 'replace')
            if p.wait():
                raise RuntimeError(err[-1500:])
            return dst
        # Everything else: the alpha frames laid over the background by ffmpeg's overlay filter,
        # which is how the Stage03b proofs were composited.
        tmp = Path(tempfile.mkdtemp(prefix='scene-ref-'))
        try:
            rect = pv.get('legacy', {}).get('overlayRect')  # [x, y, w, h] of the proof's layer
            for i in range(self.count):
                im = Image.fromarray(self.alpha_frame(i), 'RGBA')
                if rect:
                    x, y, w, h = rect
                    im = im.crop((x, y, x + w, y + h))
                im.save(tmp / f'{i:04d}.png', compress_level=2)
            ox, oy = (rect[0], rect[1]) if rect else (0, 0)
            inputs, chain = [], ''
            if bg.get('kind') == 'video':
                inputs += ['-i', str(self.find_asset(bg['file']))]
            else:
                Image.fromarray(base_matte(W, H)).save(tmp / 'base.png')
                inputs += ['-loop', '1', '-framerate', fr, '-i', str(tmp / 'base.png')]
            n = 1
            last = '[0:v]'
            for ov in bg.get('overlays', []):
                inputs += ['-loop', '1', '-framerate', fr, '-i', str(self.find_asset(ov))]
                chain += f'{last}[{n}:v]overlay=0:0:shortest=1[b{n}];'
                last = f'[b{n}]'; n += 1
            inputs += ['-framerate', fr, '-i', str(tmp / '%04d.png')]
            chain += f'{last}[{n}:v]overlay={ox}:{oy}:shortest=1,format=yuv420p[v]'
            cmd = ['ffmpeg', '-y', '-v', 'error', *inputs, '-filter_complex', chain, '-map', '[v]',
                   '-frames:v', str(self.count), '-r', fr, *x264, '-an', '-movflags', '+faststart', str(dst)]
            subprocess.run(cmd, check=True)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
        return dst


def parse_params(items):
    out = {}
    for it in items or []:
        k, _, v = it.partition('=')
        try:
            out[k] = json.loads(v)
        except json.JSONDecodeError:
            out[k] = v
    return out


def cmd_render(o):
    sc = core.load_scene(o.scene, parse_params(o.param))
    job = Job(sc, o.font_dir, o.fps, o.duration, o.assets)
    out = Path(o.out); out.mkdir(parents=True, exist_ok=True)
    frames = range(job.count) if not o.frames else [int(x) for x in o.frames.split(',')]
    if not o.no_png:
        (out / 'alpha').mkdir(exist_ok=True)
        for i in frames:
            Image.fromarray(job.alpha_frame(i), 'RGBA').save(out / 'alpha' / f'{i:04d}.png', compress_level=2)
    if not o.no_mp4:
        dst = job.preview_mp4(out / f'{sc.raw["id"]}.mp4')
        print(dst)
    print(json.dumps({'scene': sc.raw['id'], 'frames': job.count, 'fps': job.fps, 'params': sc.params}))


def cmd_anchor(o):
    """Converts a script's draw position into the scene's baseline anchor (and prints both)."""
    if o.profile == 'sprite2x':
        f = _truetype(o.font, o.size * SS, False)
        box = f.getbbox(o.text); asc = f.getmetrics()[0]
        bx, by = o.at[0] + (14 - box[0]) / SS, o.at[1] + (16 - box[1] + asc) / SS
    else:
        f = _truetype(o.font, max(10, round(o.size)), True)
        k = f.getbbox(o.text, anchor='lt')[1] - f.getbbox(o.text, anchor='ls')[1]
        bx, by = o.at[0], o.at[1] + k
    print(json.dumps({'baseline': [bx, by]}))


def cmd_validate(o):
    sc = core.load_scene(o.scene, parse_params(o.param))
    hs, he = core.scene_events(sc)
    r = make_renderer(sc, o.font_dir)
    a = Job(sc, o.font_dir)
    i0, i1 = math.ceil(hs * a.fps) + 1, math.floor(he * a.fps) - 1
    still = i1 <= i0 or np.array_equal(a.alpha_frame(i0), a.alpha_frame(i1))
    print(json.dumps({'ok': bool(still), 'actors': len(sc.actors), 'hold': [hs, he], 'holdIsStatic': bool(still),
                      'profile': sc.profile, 'params': sc.params}, indent=1))
    return 0 if still else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    r = sub.add_parser('render'); r.add_argument('scene'); r.add_argument('--out', required=True)
    r.add_argument('--frames'); r.add_argument('--no-mp4', action='store_true'); r.add_argument('--no-png', action='store_true')
    r.add_argument('--fps', type=float); r.add_argument('--duration', type=float)
    for p in (r, sub.add_parser('validate')):
        if p is not r:
            p.add_argument('scene')
        p.add_argument('--param', action='append', help='name=value (JSON value or text)')
        p.add_argument('--font-dir', action='append', default=[])
    r.add_argument('--assets', action='append', default=[], help='folder with the preview background files')
    an = sub.add_parser('anchor'); an.add_argument('--profile', choices=sorted(core.PROFILES), required=True)
    an.add_argument('--font', required=True); an.add_argument('--size', type=int, required=True)
    an.add_argument('--text', required=True); an.add_argument('--at', type=float, nargs=2, required=True)
    o = ap.parse_args(argv)
    try:
        return {'render': cmd_render, 'anchor': cmd_anchor, 'validate': cmd_validate}[o.cmd](o) or 0
    except SceneError as e:
        print(f'scene error: {e}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
