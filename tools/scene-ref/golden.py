"""gold1x: the Golden Run 18 effects (render_stage25.py … render_stage30.py), black-key.

Those proofs drew light onto a pure black frame: every part goes in with np.maximum (or straight
assignment on black), so the frame *is* the light. Each text actor carries `effect` with the
scene's own parameters; the formulas below follow the scripts step for step (same dtypes, same
cv2 calls) so the reference stays bit-exact. The alpha layer is black-keyed: alpha = the brightest
channel, colour = frame / alpha, which puts back the same light over black and the same tint over
footage.
"""
import math

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont

W, H = 1920, 1080


def clamp(v):
    return max(0., min(1., v))


def smooth(v):
    v = clamp(v)
    return v * v * (3. - 2. * v)


def outc(v):
    v = clamp(v)
    return 1. - (1. - v) ** 3


# -- shared helpers (render_stage25.py) ---------------------------------------------------------
def textmask25(txt, path, sz, tracking=0, pad=20):
    ft = ImageFont.truetype(path, sz)
    b0, b1, b2, b3 = ft.getbbox(txt, anchor='ls')
    if tracking:
        advs = [ft.getlength(ch) for ch in txt]
        width = int(math.ceil(sum(advs) + (len(txt) - 1) * tracking)) + 2 * pad
        im = Image.new('L', (width, max(1, b3 - b1 + 2 * pad)), 0)
        d = ImageDraw.Draw(im)
        x = pad
        base = pad - b1
        for ch, a in zip(txt, advs):
            d.text((int(round(x)), int(round(base))), ch, fill=255, font=ft, anchor='ls')
            x += a + tracking
    else:
        im = Image.new('L', (int(b2 - b0 + 2 * pad), int(b3 - b1 + 2 * pad)), 0)
        ImageDraw.Draw(im).text((pad - b0, pad - b1), txt, font=ft, fill=255, anchor='ls')
    mask = np.asarray(im, dtype=np.uint8)
    yy, xx = np.nonzero(mask > 0)
    if len(xx) == 0:
        return mask
    l, r = max(0, xx.min() - 4), min(mask.shape[1], xx.max() + 5)
    t, b = max(0, yy.min() - 4), min(mask.shape[0], yy.max() + 5)
    return mask[t:b, l:r].copy()


def scale_mask(mask, w=None, h=None):
    sh, sw = mask.shape
    if w is None:
        w = round(sw * h / sh)
    if h is None:
        h = round(sh * w / sw)
    return cv2.resize(mask, (max(1, int(w)), max(1, int(h))),
                      interpolation=cv2.INTER_AREA if w < sw or h < sh else cv2.INTER_CUBIC)


def put_max(canvas, mask, x, y, color, opacity=1):
    """render_stage25.put(): lighten the mask's light into the frame."""
    if opacity <= 0 or mask is None:
        return
    x, y = int(round(x)), int(round(y))
    mh, mw = mask.shape
    x0, y0, x1, y1 = max(0, x), max(0, y), min(W, x + mw), min(H, y + mh)
    if x1 <= x0 or y1 <= y0:
        return
    sub = mask[y0 - y:y1 - y, x0 - x:x1 - x]
    aa = (sub.astype(np.float32) * (clamp(opacity) / 255))[:, :, None]
    patch = canvas[y0:y1, x0:x1]
    patch[:] = np.maximum(patch, np.clip(aa * np.array(color, dtype=np.float32)[None, None, :], 0, 255).astype('uint8'))


def sdf3(mask):
    inside = (mask > 127).astype(np.uint8)
    return cv2.distanceTransform(inside, cv2.DIST_L2, 3) - cv2.distanceTransform((1 - inside).astype(np.uint8), cv2.DIST_L2, 3)


def mix_rgb(a, b, u):
    return tuple(int((1 - u) * v + u * w) for v, w in zip(a, b))


# -- render_stage26.py helpers ------------------------------------------------------------------
def masktxt26(txt, ftpath, size, pad=35):
    f = ImageFont.truetype(ftpath, size)
    bbox = f.getbbox(txt, anchor='ls')
    pw, ph = int(math.ceil(bbox[2] - bbox[0] + pad * 2)), int(math.ceil(bbox[3] - bbox[1] + pad * 2))
    m = Image.new('L', (pw, ph), 0)
    ImageDraw.Draw(m).text((pad - bbox[0], pad - bbox[1]), txt, fill=255, font=f, anchor='ls')
    m = np.array(m)
    yy, xx = np.where(m > 0)
    if len(xx) == 0:
        return m
    return m[max(0, yy.min() - 3):min(m.shape[0], yy.max() + 4), max(0, xx.min() - 3):min(m.shape[1], xx.max() + 4)].copy()


def resizeto(m, maxw):
    if m.shape[1] <= maxw:
        return m
    newh = round(m.shape[0] * maxw / m.shape[1])
    return cv2.resize(m, (maxw, newh), interpolation=cv2.INTER_AREA)


def blend_over(bg, mask, x, y, color, opacity=1):
    """render_stage26.blend(): normal alpha composition (overlapping glyph actors)."""
    if opacity <= 0:
        return
    opacity = clamp(opacity)
    x, y = int(round(x)), int(round(y))
    h, w = mask.shape
    x0, x1, y0, y1 = max(0, x), min(W, x + w), max(0, y), min(H, y + h)
    if x1 <= x0 or y1 <= y0:
        return
    a = mask[y0 - y:y1 - y, x0 - x:x1 - x].astype(np.float32) * (opacity / 255.0)
    patch = bg[y0:y1, x0:x1]
    patch[:] = np.clip(patch * (1 - a[:, :, None]) + np.array(color, dtype=np.float32)[None, None, :] * a[:, :, None], 0, 255).astype('uint8')


def mix_round(a, b, u):
    return tuple(round(x * (1 - u) + y * u) for x, y in zip(a, b))


# -- render_stage27.py helpers ------------------------------------------------------------------
def masktxt27(txt, fontpath, size, tracking=0, pad=24):
    f = ImageFont.truetype(fontpath, size)
    if not tracking:
        bb = f.getbbox(txt, anchor='ls')
        im = Image.new('L', (int(math.ceil(bb[2] - bb[0] + 2 * pad)), int(math.ceil(bb[3] - bb[1] + 2 * pad))), 0)
        ImageDraw.Draw(im).text((pad - bb[0], pad - bb[1]), txt, font=f, fill=255, anchor='ls')
    else:
        advances = [f.getlength(ch) for ch in txt]
        bbs = [f.getbbox(ch, anchor='ls') for ch in txt]
        width = int(math.ceil(sum(advances) + tracking * (len(txt) - 1) + 2 * pad))
        asc, desc = max(-bb[1] for bb in bbs), max(bb[3] for bb in bbs)
        im = Image.new('L', (width, int(asc + desc + 2 * pad)), 0)
        d = ImageDraw.Draw(im)
        pos, baseline = pad, pad + asc
        for ch, adv in zip(txt, advances):
            d.text((round(pos), baseline), ch, font=f, fill=255, anchor='ls')
            pos += adv + tracking
    ar = np.asarray(im)
    ys, xs = np.where(ar > 0)
    if len(xs) == 0:
        return ar.copy()
    return ar[max(0, ys.min() - 2):min(ar.shape[0], ys.max() + 3), max(0, xs.min() - 2):min(ar.shape[1], xs.max() + 3)].copy()


def within_width(mask, maxw):
    h, w = mask.shape
    if w <= maxw:
        return mask
    return cv2.resize(mask, (int(maxw), int(h * maxw / w)), interpolation=cv2.INTER_AREA)


def blend27(bg, mask, x, y, color, opacity=1.0):
    opacity = clamp(opacity)
    if opacity <= 0:
        return
    x, y = round(x), round(y)
    h, w = mask.shape
    x0, x1, y0, y1 = max(0, x), min(W, x + w), max(0, y), min(H, y + h)
    if x1 <= x0 or y1 <= y0:
        return
    a = (mask[y0 - y:y1 - y, x0 - x:x1 - x].astype(np.float32) / 255.0 * opacity)[:, :, None]
    region = bg[y0:y1, x0:x1]
    region[:] = np.clip(region * (1 - a) + np.asarray(color, np.float32)[None, None, :] * a, 0, 255).astype('uint8')


def blend_variable(bg, mask, x, y, rgb, opacity=1.):
    x, y = round(x), round(y)
    h, w = mask.shape
    x0, x1, y0, y1 = max(0, x), min(W, x + w), max(0, y), min(H, y + h)
    if x1 <= x0 or y1 <= y0:
        return
    a = (mask[y0 - y:y1 - y, x0 - x:x1 - x].astype(np.float32) / 255 * clamp(opacity))[:, :, None]
    subrgb = rgb[y0 - y:y1 - y, x0 - x:x1 - x]
    region = bg[y0:y1, x0:x1]
    region[:] = np.clip(region * (1 - a) + subrgb * a, 0, 255).astype('uint8')


# -- render_stage28.py helpers ------------------------------------------------------------------
def masktxt28(text, path, size, maxw, tracking=0):
    ft = ImageFont.truetype(path, size)
    pad = 54
    if tracking == 0:
        bb = ft.getbbox(text, anchor='ls')
        im = Image.new('L', (math.ceil(bb[2] - bb[0] + 2 * pad), math.ceil(bb[3] - bb[1] + 2 * pad)), 0)
        ImageDraw.Draw(im).text((pad - bb[0], pad - bb[1]), text, font=ft, fill=255, anchor='ls')
    else:
        advances = [ft.getlength(ch) for ch in text]
        boxes = [ft.getbbox(ch, anchor='ls') for ch in text]
        width = int(math.ceil(sum(advances) + tracking * (len(text) - 1) + 2 * pad))
        ascend, descend = max(-b[1] for b in boxes), max(b[3] for b in boxes)
        im = Image.new('L', (width, int(ascend + descend + 2 * pad)), 0)
        p = pad
        d = ImageDraw.Draw(im)
        for ch, adv in zip(text, advances):
            d.text((round(p), pad + ascend), ch, font=ft, fill=255, anchor='ls')
            p += adv + tracking
    a = np.asarray(im)
    ys, xs = np.where(a > 0)
    if len(xs) == 0:
        return a.copy()
    a = a[max(0, ys.min() - 4):ys.max() + 5, max(0, xs.min() - 4):xs.max() + 5].copy()
    if a.shape[1] > maxw:
        nw, nh = round(maxw), round(a.shape[0] * maxw / a.shape[1])
        a = cv2.resize(a, (nw, nh), interpolation=cv2.INTER_AREA)
    return a


def blend28(bg, mask, x, y, col, opacity=1):
    if opacity <= .0001:
        return
    x, y = int(round(x)), int(round(y))
    h, w = mask.shape
    x0, y0, x1, y1 = max(0, x), max(0, y), min(W, x + w), min(H, y + h)
    if x0 >= x1 or y0 >= y1:
        return
    a = mask[y0 - y:y1 - y, x0 - x:x1 - x].astype(np.float32) / 255 * clamp(opacity)
    region = bg[y0:y1, x0:x1].astype(np.float32)
    bg[y0:y1, x0:x1] = np.clip(region * (1 - a[..., None]) + np.array(col, dtype=np.float32)[None, None, :] * a[..., None], 0, 255).astype(np.uint8)


def sdf_precise(a):
    fg = (a >= 128).astype('uint8')
    inside = cv2.distanceTransform(fg, cv2.DIST_L2, cv2.DIST_MASK_PRECISE)
    outside = cv2.distanceTransform(1 - fg, cv2.DIST_L2, cv2.DIST_MASK_PRECISE)
    return inside - outside


def put_centered(mask, width, height, fill=0):
    canvas = np.full((height, width), fill, np.uint8)
    y, x = (height - mask.shape[0]) // 2, (width - mask.shape[1]) // 2
    canvas[y:y + mask.shape[0], x:x + mask.shape[1]] = mask
    return canvas


# -- render_stage29.py helpers ------------------------------------------------------------------
def masktext29(text, fontpath, size, max_width, tracking=0):
    ft = ImageFont.truetype(fontpath, size)
    pad = 35
    bb = ft.getbbox(text, anchor='ls')
    if not tracking:
        im = Image.new('L', (math.ceil(bb[2] - bb[0] + pad * 2), math.ceil(bb[3] - bb[1] + pad * 2)), 0)
        ImageDraw.Draw(im).text((pad - bb[0], pad - bb[1]), text, font=ft, fill=255, anchor='ls')
    else:
        advances = [ft.getlength(ch) for ch in text]
        boxes = [ft.getbbox(ch, anchor='ls') for ch in text]
        asc, des = max(-b[1] for b in boxes), max(b[3] for b in boxes)
        im = Image.new('L', (math.ceil(sum(advances) + (len(text) - 1) * tracking + pad * 2), math.ceil(asc + des + pad * 2)), 0)
        d = ImageDraw.Draw(im)
        p = pad
        for ch, adv in zip(text, advances):
            d.text((round(p), pad + asc), ch, font=ft, fill=255, anchor='ls')
            p += adv + tracking
    ar = np.array(im)
    ys, xs = np.where(ar > 0)
    if len(xs) == 0:
        return ar
    ar = ar[max(0, ys.min() - 4):ys.max() + 5, max(0, xs.min() - 4):xs.max() + 5].copy()
    if ar.shape[1] > max_width:
        nw, nh = int(max_width), round(ar.shape[0] * max_width / ar.shape[1])
        ar = cv2.resize(ar, (nw, nh), interpolation=cv2.INTER_AREA)
    return ar


def blend29(canvas, mask, x, y, color, opacity=1):
    a = clamp(opacity)
    if a <= .0001:
        return
    x, y = int(round(x)), int(round(y))
    h, w = mask.shape
    xx0, yy0, xx1, yy1 = max(0, x), max(0, y), min(W, x + w), min(H, y + h)
    if xx1 <= xx0 or yy1 <= yy0:
        return
    m = mask[yy0 - y:yy1 - y, xx0 - x:xx1 - x].astype(np.float32) / 255 * a
    canvas[yy0:yy1, xx0:xx1] = np.clip(canvas[yy0:yy1, xx0:xx1].astype(np.float32) * (1 - m[..., None]) + np.array(color, np.float32)[None, None, :] * m[..., None], 0, 255).astype(np.uint8)


# -- render_stage30.py helpers (a 1660 × 450 title region at 130, 315) --------------------------
def quint(t):
    t = clamp(t)
    return t * t * t * (t * (t * 6 - 15) + 10)


def mask_text30(txt, fontpath, pt, region, baseline, tracking=0):
    RW, RH = region[2], region[3]
    font = ImageFont.truetype(fontpath, pt)
    width = font.getlength(txt) + max(0, len(txt) - 1) * tracking
    xx = (RW - width) / 2
    b = Image.fromarray(np.zeros((RH, RW), np.uint8))
    d = ImageDraw.Draw(b)
    pos = []
    for i, ch in enumerate(txt):
        x = xx + font.getlength(txt[:i]) + tracking * i
        m = Image.new('L', (RW, RH), 0)
        ImageDraw.Draw(m).text((int(round(x)), baseline), ch, fill=255, font=font, anchor='ls', stroke_width=0)
        pos.append(np.asarray(m))
        d.text((int(round(x)), baseline), ch, fill=255, font=font, anchor='ls')
    return np.asarray(b), pos, font, xx


def comp30(masks, region):
    X0, Y0, RW, RH = region
    patch = np.zeros((RH, RW, 3), np.float32)
    for alpha, color in masks:
        if alpha is None:
            continue
        a = np.clip(alpha, 0, 1)
        patch = patch * (1 - a[..., None]) + a[..., None] * color[None, None, :]
    return np.uint8(np.clip(patch + 0.5, 0, 255))


def move_unclipped(gm, dy):
    RH = gm.shape[0]
    dy = int(round(dy))
    out = np.zeros_like(gm)
    if dy >= 0:
        if dy < RH:
            out[dy:] = gm[:RH - dy]
    else:
        dy = -dy
        if dy < RH:
            out[:RH - dy] = gm[dy:]
    return out


def ease30(t):
    t = float(np.clip(t, 0, 1))
    return t * t * (3 - 2 * t)


# ================================================================================================
# B2: later stages (render_stage32.py … render_stage49.py). The numbers stay in the code as the
# scripts had them; the word, font, size, tracking and colours come from the actor.
# ================================================================================================
def np_smooth(t):
    t = np.clip(t, 0., 1.)
    return t * t * (3 - 2 * t)


def fsm(t):
    t = float(np.clip(t, 0, 1))
    return t * t * (3 - 2 * t)


def f_out(t, k):
    t = float(np.clip(t, 0, 1))
    return 1 - (1 - t) ** k


def cyc(seq, i):
    return seq[i % len(seq)]


class B2:
    """Mixin: the B2 effects; self.fp(font key) gives the font file."""

    # -- 32A Vector: each glyph split by one tilted cut, the halves close the seam --------------
    def _glyphs32(self, a, e):
        def make():
            f = ImageFont.truetype(self.fp(a['font']), int(a['size']))
            word, tracking, size = a['text'], e['tracking'], int(a['size'])
            d = ImageDraw.Draw(Image.new('L', (1, 1)))
            advances = [d.textlength(c, font=f) for c in word]
            total = sum(advances) + (len(word) - 1) * tracking
            x, cy, out = (W - total) / 2, H / 2, []
            for j, (c, adv) in enumerate(zip(word, advances)):
                im = Image.new('L', (W, H))
                ImageDraw.Draw(im).text((round(x), round(cy - size * .43)), c, font=f, fill=255)
                ar = np.asarray(im)
                xx, yy = np.nonzero(ar)[1], np.nonzero(ar)[0]
                if len(xx):
                    x0, x1 = max(int(xx.min()) - 90, 0), min(int(xx.max()) + 91, W)
                    y0, y1 = max(int(yy.min()) - 100, 0), min(int(yy.max()) + 101, H)
                    out.append((ar[y0:y1, x0:x1].copy(), x0, y0, j, c))
                x += adv + tracking
            return out
        return self.get(('g32', a['id'], a['text']), make)

    def split_register(self, img, a, e, t):
        C1 = np.array(a['color'][:3], dtype=np.uint8)
        fadein = np_smooth((t - .06) / .35)
        fadeout = 1 - np_smooth((t - 5.27) / .52)
        for g, x0, y0, j, c in self._glyphs32(a, e):
            hh, ww = g.shape
            yy, xx = np.indices((hh, ww), dtype=np.float32)
            line = hh * .50 + (xx - ww * .5) * .17
            upper = (yy < line).astype(np.float32)
            lower = 1 - upper
            lock = np_smooth((t - .21 - j * .083) / 1.15)
            depart = np_smooth((t - 4.37 - j * .043) / .86)
            aa = 1 - lock + depart
            d, dx = 51 * aa, 21 * aa
            ui = cv2.remap(g * upper, (xx - dx).astype(np.float32), (yy + d).astype(np.float32), cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT, borderValue=0)
            li = cv2.remap(g * lower, (xx + dx).astype(np.float32), (yy - d).astype(np.float32), cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT, borderValue=0)
            alpha = np.maximum(ui, li) * fadein * fadeout
            sub = img[y0:y0 + hh, x0:x0 + ww]
            sub[:] = np.maximum(sub, (alpha[:, :, None] / 255 * C1).astype(np.uint8))

    # -- 32B Sovereign: a twist inside each glyph relaxes to the clean letter --------------------
    def glyph_torsion(self, img, a, e, t):
        fadein = np_smooth((t - .04) / .38)
        fadeout = 1 - np_smooth((t - 5.28) / .5)
        c0 = a['color'][:3]
        for g, x0, y0, j, c in self._glyphs32(a, e):
            hh, ww = g.shape
            yy, xx = np.indices((hh, ww), dtype=np.float32)
            cx, cy = ww / 2., hh / 2.
            settled = np_smooth((t - .17 - j * .075) / 1.36)
            out = np_smooth((t - 4.45 - j * .038) / .82)
            aa = (1 - settled) + out
            normalized = (yy - cy) / max(hh, 1)
            warp = aa * (36 * np.sin(normalized * math.pi * 2.05 + j * .34) * (0.68 + 0.32 * np.cos((xx - cx) / max(ww, 1) * math.pi)))
            compression = (1 - 0.18 * aa)
            mx = ((xx - cx - warp) / compression + cx).astype(np.float32)
            my = (yy - aa * 14 * np.sin((xx - cx) / max(ww, 1) * math.pi * 1.3)).astype(np.float32)
            alpha = cv2.remap(g, mx, my, cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT, borderValue=0)
            ridge = np.clip((np.cos(normalized * math.pi * 2.05 + j * .34) + 1) * .5, 0, 1)
            rgb = np.stack([c0[0] + 10 * ridge * aa, c0[1] + 7 * ridge * aa, c0[2] + 4 * ridge * aa], axis=-1).astype(np.uint8)
            sub = img[y0:y0 + hh, x0:x0 + ww]
            k = (alpha.astype(np.float32) * fadein * fadeout / 255.)[:, :, None]
            sub[:] = np.maximum(sub, (k * rgb).astype(np.uint8))

    # -- 33A Imprint: the word pressed into a material, then ink fills from the walls in ---------
    def imprint_press(self, frame, a, e, t):
        def make():
            word, size, tracking = a['text'], int(a['size']), e['tracking']
            font = ImageFont.truetype(self.fp(a['font']), size)
            adv = [font.getlength(ch) for ch in word]
            total = sum(adv) + tracking * (len(word) - 1)
            im = Image.new('L', (int(math.ceil(total)) + 200, size + 250), 0)
            di = ImageDraw.Draw(im)
            x, baseline = 100., size + 60
            for ch, ad in zip(word, adv):
                di.text((round(x), baseline), ch, anchor='ls', font=font, fill=255)
                x += ad + tracking
            bb = im.getbbox()
            mask = np.array(im.crop((max(bb[0] - 22, 0), max(bb[1] - 22, 0), min(bb[2] + 22, im.width), min(bb[3] + 22, im.height))))
            m = np.where(mask > 100, 1, 0).astype(np.uint8)
            dist = cv2.distanceTransform(m, cv2.DIST_L2, 5)
            dd = cv2.GaussianBlur(dist, (0, 0), 2.8)
            dy, dx = np.gradient(dd)
            length = np.maximum(.001, np.sqrt(dx * dx + dy * dy))
            nx, ny = dx / length, dy / length
            rim = np.clip((14 - dist) / 13.0, 0, 1) * np.clip(dist / 1.5, 0, 1)
            inside = mask.astype(np.float32) / 255
            xx, yy = np.meshgrid(np.arange(mask.shape[1], dtype=np.float32), np.arange(mask.shape[0], dtype=np.float32))
            sh = np.clip(nx * .68 + ny * (-.72) + .58, 0, 1)
            pm = np.stack([np.clip(51 + 148 * rim * (.48 + .52 * sh), 0, 255), np.clip(57 + 154 * rim * (.48 + .52 * sh), 0, 255),
                           np.clip(64 + 162 * rim * (.48 + .52 * sh), 0, 255)], axis=-1)
            return mask, dist, rim, inside, xx, pm
        mask, dist, rim, inside, xx, pm = self.get(('imprint', a['id'], a['text']), make)
        h, w = mask.shape
        x0, y0 = (W - w) // 2, (H - h) // 2
        IVORY = np.array(a['color'][:3], np.float32)
        impression = fsm((t - .12) / .64) * (1 - fsm((t - 5.10) / .7))
        gleam_phase = float(np.clip((t - .50) / .86, 0, 1))
        travel = (gleam_phase * 1.45 - .22) * w
        gleam = np.exp(-((xx - travel) / 63) ** 2) * rim * (1 - fsm((t - 1.45) / .40))
        material = pm + gleam[:, :, None] * np.array([92, 90, 74], np.float32)
        inc = fsm((t - .91) / .72)
        if t >= 1.70:
            inc = 1.
        ink = inc * (1 - fsm((t - 4.55) / .56))
        if ink < .999:
            distdelay = np.clip(1 - dist / 13.5, 0, 1) * .18
            grow = np.clip((ink - distdelay) / np.maximum(.001, 1 - distdelay), 0, 1)
            grow = grow * grow * (3 - 2 * grow)
        else:
            grow = 1.0
        body = inside[:, :, None]
        ink_rgba = body * (grow[..., None] if not np.isscalar(grow) else grow)
        base_rgba = (body * impression * (1 - ink)) * np.clip(material / 255, 0, 1)
        paint = base_rgba * 255 + ink_rgba * IVORY[None, None, :]
        if ink >= .999:
            paint = body * IVORY[None, None, :]
        frame[y0:y0 + h, x0:x0 + w] = np.clip(paint, 0, 255).astype(np.uint8)

    # -- 33B Cadence: the pairs lock into their kerning one by one, then let go ------------------
    def pair_kerning(self, frame, a, e, t):
        def make():
            ft = ImageFont.truetype(self.fp(a['font']), int(a['size']))
            out = []
            for ch in a['text']:
                bb = ft.getbbox(ch, anchor='ls')
                pad = 10
                w, h = max(1, int(math.ceil(bb[2] - bb[0] + 2 * pad))), max(1, int(math.ceil(bb[3] - bb[1] + 2 * pad)))
                im = Image.new('L', (w, h), 0)
                ImageDraw.Draw(im).text((pad - bb[0], pad - bb[1]), ch, anchor='ls', font=ft, fill=255)
                out.append({'advance': ft.getlength(ch), 'mask': np.array(im), 'xoff': bb[0] - pad, 'yoff': bb[1] - pad})
            return out
        glyphs = self.get(('cadence', a['id'], a['text']), make)
        n = len(glyphs)
        adv = np.array([g['advance'] for g in glyphs], np.float32)
        target = np.array([cyc(e['target'], i) for i in range(n - 1)], np.float32)
        wide = np.array([cyc(e['wide'], i) for i in range(n - 1)], np.float32)
        order = [o for o in e['order'] if o < n - 1] + [i for i in range(n - 1) if i not in e['order']]
        instarts = {idx: .31 + rank * .172 for rank, idx in enumerate(order)}
        outstarts = {idx: 4.63 + rank * .155 for rank, idx in enumerate(reversed(order))}
        baseline = H // 2 + 94
        op = fsm((t - .10) / .25) * (1 - fsm((t - 5.60) / .32))

        def gap(i):
            lock = f_out((t - instarts[i]) / .40, 3)
            release = fsm((t - outstarts[i]) / .38)
            return target[i] + (wide[i] - target[i]) * (1 - lock + release)
        gaps = np.array([gap(i) for i in range(n - 1)], np.float32)
        x = (W - (adv.sum() + gaps.sum())) / 2
        for i, g in enumerate(glyphs):
            blend_paste(frame, g['mask'], x + g['xoff'], baseline + g['yoff'], np.array(a['color'][:3], np.float32), op)
            x += adv[i] + (gaps[i] if i < n - 1 else 0)

    # -- 34A Extract: one solid slab is carved away into the typography ---------------------------
    def solid_extract(self, img, a, e, t):
        def make():
            word, size, tracking, padding = a['text'], int(a['size']), e['tracking'], 100
            f = ImageFont.truetype(self.fp(a['font']), size)
            advances = [f.getlength(ch) for ch in word]
            width = int(math.ceil(sum(advances) + tracking * (len(word) - 1) + 2 * padding))
            im = Image.new('L', (width, size + 2 * padding), 0)
            d = ImageDraw.Draw(im)
            x = padding
            for ch, adv in zip(word, advances):
                d.text((round(x), size + padding * .5), ch, anchor='ls', font=f, fill=255)
                x += adv + tracking
            bb = im.getbbox()
            src = np.asarray(im.crop((max(0, bb[0] - 55), max(0, bb[1] - 55), min(im.width, bb[2] + 55), min(im.height, bb[3] + 55))))
            hh, ww = src.shape
            padx, pady = 92, 45
            h, w = hh + 2 * pady, ww + 2 * padx
            alpha = np.zeros((h, w), np.uint8)
            alpha[pady:pady + hh, padx:padx + ww] = src
            yy, xx = np.indices((h, w), dtype=np.float32)
            m = (alpha > 127).astype(np.uint8)
            sdf = cv2.distanceTransform(1 - m, cv2.DIST_L2, cv2.DIST_MASK_PRECISE) - cv2.distanceTransform(m, cv2.DIST_L2, cv2.DIST_MASK_PRECISE)
            iy, ix = np.where(m > 0)
            gb = (int(ix.min()), int(iy.min()), int(ix.max()), int(iy.max()))
            cx, cy = (ix.min() + ix.max()) * .5, (iy.min() + iy.max()) * .5
            rw, rh = (ix.max() - ix.min()) * .5 + 12, (iy.max() - iy.min()) * .5 + 18
            corner = 23.0
            qx, qy = np.abs(xx - cx) - (rw - corner), np.abs(yy - cy) - (rh - corner)
            slab = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - corner
            yrel = np.clip((yy - cy) / max(rh, 1), -1, 1)
            return alpha, xx, yy, sdf, gb, cx, cy, slab, yrel
        alpha, xx, yy, sdf, gb, cx, cy, slab0, yrel = self.get(('extract', a['id'], a['text']), make)
        h, w = alpha.shape
        x0, y0 = (W - w) // 2, (H - h) // 2
        IVORY, OYSTER = np.array(a['color'][:3], np.float32), np.array(e['slabColor'][:3], np.float32)
        to_sil, to_ink, reverse = fsm((t - .48) / .43), fsm((t - .91) / .63), fsm((t - 4.43) / .83)
        if t < .48:
            grow = f_out((t - .10) / .40, 5)
        elif t > 5.26:
            grow = 1 - fsm((t - 5.26) / .58)
        else:
            grow = 1.0
        ghw = w * .48 * grow
        qx = np.abs(xx - cx) - (ghw - 16)
        qy = np.abs(yy - cy) - ((gb[3] - gb[1]) * .5 + 19 - 16)
        box_now = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - 16
        slab = box_now if (t < .48 or t > 5.26) else slab0
        prep = (1 - to_sil) * slab + to_sil * (sdf - 19.0)
        signed = prep * (1 - to_ink) + sdf * to_ink
        if t > 4.43:
            signed = (1 - reverse) * sdf + reverse * slab0
        if t > 5.26:
            signed = box_now
        body = np.clip(.5 - signed, 0, 1)
        final_ink = (1 - reverse) * to_sil * to_ink
        if final_ink >= .999:
            body = alpha.astype(np.float32) / 255.
        color = OYSTER[None, None, :] * (1 - final_ink) + IVORY[None, None, :] * final_ink
        active = final_ink * (1 - final_ink)
        highlight = np.exp(-(signed / 5.0) ** 2) * active * 26 * np.clip(.48 - .45 * yrel, 0, 1)
        rgb = np.clip(body[:, :, None] * (color + highlight[:, :, None]), 0, 255)
        if t < .22:
            rgb *= fsm((t - .06) / .16)
        if t > 5.67:
            rgb *= 1 - fsm((t - 5.67) / .28)
        img[y0:y0 + h, x0:x0 + w] = rgb.astype(np.uint8)

    # -- 34B Parallel: two displaced print impressions converge into each glyph ------------------
    def print_register(self, img, a, e, t):
        def make():
            f = ImageFont.truetype(self.fp(a['font']), int(a['size']))
            res, x = [], 0
            for ch in a['text']:
                box = f.getbbox(ch, anchor='ls')
                pad = 20
                w, h = math.ceil(box[2] - box[0] + 2 * pad), math.ceil(box[3] - box[1] + 2 * pad)
                im = Image.new('L', (w, h), 0)
                ImageDraw.Draw(im).text((pad - box[0], pad - box[1]), ch, font=f, anchor='ls', fill=255)
                res.append({'coverage': np.asarray(im), 'x': x + box[0] - pad, 'y': box[1] - pad})
                x += f.getlength(ch) + e['tracking']
            return res, x - e['tracking']
        glyphs, total = self.get(('register', a['id'], a['text']), make)
        baseline, startx, n = H // 2 + 77, (W - total) / 2, len(glyphs)
        IVORY, WARM, BLUE = (np.array(c[:3], np.float32) for c in (a['color'], e['warm'], e['blue']))
        appear = fsm((t - .10) / .26) * (1 - fsm((t - 5.68) / .26))
        if appear < .001:
            return
        for i, g in enumerate(glyphs):
            hh, ww = g['coverage'].shape
            x, y = startx + g['x'], baseline + g['y']
            registration = fsm((t - (.24 + i * .064)) / .90)
            separation = fsm((t - (4.52 + (n - 1 - i) * .045)) / .72)
            loose = float(np.clip(1 - registration + separation, 0, 1))
            outward = e['outward'][i] if i < len(e['outward']) else e['outward'][-1]
            dx, dy = float(28 * np.float32(outward)) * loose, 13 * loose  # the script's outward is float32
            ma = np.zeros((hh + 60, ww + 100), np.uint8)
            ma[30:30 + hh, 50:50 + ww] = g['coverage']
            A = cv2.warpAffine(ma, np.float32([[1, 0, -dx], [0, 1, -dy]]), (ww + 100, hh + 60), flags=cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT)
            B = cv2.warpAffine(ma, np.float32([[1, 0, dx], [0, 1, dy]]), (ww + 100, hh + 60), flags=cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT)
            af, bf = A.astype(np.float32) / 255, B.astype(np.float32) / 255
            overlap = np.minimum(af, bf)
            warm, blue = np.clip(af - overlap, 0, 1), np.clip(bf - overlap, 0, 1)
            rgba = warm[:, :, None] * WARM[None, None, :] + blue[:, :, None] * BLUE[None, None, :] + overlap[:, :, None] * IVORY[None, None, :]
            if loose < .0001:
                rgba = ma[:, :, None].astype(np.float32) / 255 * IVORY[None, None, :]
            xx, yy = int(round(x - 50)), int(round(y - 30))
            x0, y0, x1, y1 = max(0, xx), max(0, yy), min(W, xx + ww + 100), min(H, yy + hh + 60)
            if x0 >= x1 or y0 >= y1:
                continue
            dest = img[y0:y1, x0:x1]
            dest[:] = np.maximum(dest, np.uint8(np.clip(rgba[y0 - yy:y1 - yy, x0 - xx:x1 - xx] * appear, 0, 255)))

    # -- 35A Tidal: ink rises in the letters like liquid in a vessel ---------------------------
    def _word35(self, a, e):
        def make():
            word, size, tracking, margin = a['text'], int(a['size']), e['tracking'], e['margin']
            f = ImageFont.truetype(self.fp(a['font']), size)
            advances = [f.getlength(ch) for ch in word]
            width = int(math.ceil(sum(advances) + tracking * (len(word) - 1) + 2 * margin))
            canvas = Image.new('L', (width, size + 2 * margin), 0)
            d = ImageDraw.Draw(canvas)
            x = margin
            for ch, adv in zip(word, advances):
                d.text((round(x), size + margin * .5), ch, font=f, anchor='ls', fill=255)
                x += adv + tracking
            b = canvas.getbbox()
            return np.asarray(canvas.crop((max(0, b[0] - 48), max(0, b[1] - 48), min(width, b[2] + 49), min(size + 2 * margin, b[3] + 49)))).copy()
        return self.get(('w35', a['id'], a['text']), make)

    def tidal_ink(self, frame, a, e, t):
        mask = self._word35(a, e)
        def make():
            av = mask.astype(np.float32) / 255.
            h, w = mask.shape
            yy, xx = np.indices((h, w), dtype=np.float32)
            ys, xs = np.where(av > .1)
            edge = cv2.morphologyEx(mask, cv2.MORPH_GRADIENT, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5)))
            return av, yy, 2 * math.pi * xx / (w * .74), 2 * math.pi * xx / 138, float(ys.min()), float(ys.max()), np.clip(edge.astype(np.float32) / 255., 0, 1)
        av, yy, ph, cap, top, bottom, vessel = self.get(('tidal', a['id'], a['text']), make)
        h, w = mask.shape
        x0, y0 = (W - w) // 2, (H - h) // 2
        p = float(np_smooth((t - .20) / 1.33)) if t <= 4.38 else float(1 - np_smooth((t - 4.38) / 1.30))
        if p <= 0:
            return
        BLUE, IVORY = np.array(e['wet'][:3], np.float32), np.array(a['color'][:3], np.float32)
        ink_height = bottom - top
        wave_amp = 20.0 * (1 - np_smooth((p - .24) / .76))
        wave = wave_amp * (0.82 * np.sin(ph + 2.5 * t) + 0.18 * np.sin(cap - 3.5 * t))
        surface = bottom + 28.0 - (ink_height + 63.0) * p + wave
        fluid = np.clip((yy - surface + 1.2) / 3.0, 0, 1)
        ink = av * fluid
        if p > .999:
            ink = av
        if not np.any(ink > .001):
            return
        crest = np.exp(-((yy - surface) / 2.5) ** 2)
        active = (1 - np_smooth((p - .70) / .30))
        dry = np_smooth((p - .64) / .36)
        color = BLUE * (1 - dry) + IVORY * dry
        refl = (1 - dry) * 7.0 * np.sin(ph * .45 + 1.1 * t)
        inside = (1 - dry) * 8.0 * np.clip((yy - surface) / max(ink_height, 1), 0, 1)
        rgb = ink[:, :, None] * np.clip(color[None, None, :] + refl[:, :, None] + inside[:, :, None] + (crest * active * 35)[:, :, None], 0, 255)
        vo = (1 - np_smooth((p - .68) / .31)) * np_smooth((t - .20) / .19)
        rgb = np.maximum(rgb, vessel[:, :, None] * np.array([81., 106., 114.], np.float32)[None, None, :] * vo)
        if t < .28:
            rgb *= np_smooth((t - .20) / .10)
        if t > 5.63:
            rgb *= 1 - np_smooth((t - 5.63) / .28)
        frame[y0:y0 + h, x0:x0 + w] = rgb.astype(np.uint8)

    # -- 35B Hatch: two engraved line gratings thicken into solid ink ---------------------------
    def hatch_engrave(self, frame, a, e, t):
        mask = self._word35(a, e)
        def make():
            av = mask.astype(np.float32) / 255.
            h, w = mask.shape
            yy, xx = np.indices((h, w), dtype=np.float32)
            pa, pb = 16.2, 17.1
            return (av, np.abs(np.mod((xx + .64 * yy) + pa * .5, pa) - pa * .5), np.abs(np.mod((xx - .78 * yy) + pb * .5, pb) - pb * .5),
                    3.5 * np.sin(2 * math.pi * (xx + .64 * yy) / pa))
        av, da, db, bright = self.get(('hatch', a['id'], a['text']), make)
        h, w = mask.shape
        x0, y0 = (W - w) // 2, (H - h) // 2
        p = float(np_smooth((t - .13) / 1.50)) if t <= 4.42 else float(1 - np_smooth((t - 4.42) / 1.34))
        if p <= 0:
            return
        SILVER, IVORY, WARM = (np.array(c[:3], np.float32) for c in (e['line'], a['color'], e['cross']))
        A = 1 - (1 - float(np.clip((p - .025) / .85, 0, 1))) ** 4
        Bv = 1 - (1 - float(np.clip((p - .28) / .62, 0, 1))) ** 4
        wa = .45 + 8.35 * np_smooth((p - .12) / .84)
        wb = .35 + 9.15 * np_smooth((p - .34) / .62)
        la = np.clip((wa - da + .85) / 1.15, 0, 1) * A
        lb = np.clip((wb - db + .85) / 1.15, 0, 1) * Bv
        coverage = (1 - (1 - la) * (1 - lb)) * av
        solid = np_smooth((p - .77) / .23)
        if solid >= .999:
            coverage = av
        else:
            coverage = coverage * (1 - solid) + av * solid
        tint = SILVER[None, None, :] * (1 - solid) + IVORY[None, None, :] * solid
        inter = (np.minimum(la, lb) * (1 - solid) * av)[:, :, None]
        hl = bright[:, :, None] * (1 - solid) * av[:, :, None]
        rgb = coverage[:, :, None] * tint + (WARM - SILVER)[None, None, :] * inter * .30 + hl
        if t < .21:
            rgb *= np_smooth((t - .13) / .08)
        if t > 5.69:
            rgb *= 1 - np_smooth((t - 5.69) / .24)
        frame[y0:y0 + h, x0:x0 + w] = np.uint8(np.clip(rgb, 0, 255))

    # -- 39A Reflow: the letters leave a two-row grid and settle into the word -------------------
    def grid_reflow(self, out, a, e, t):
        def make():
            word = a['text']
            font = ImageFont.truetype(self.fp(a['font']), int(a['size']))
            dr = ImageDraw.Draw(Image.new('L', (4, 4)))
            advances = [dr.textlength(ch, font=font) for ch in word]
            start = W * .5 - (sum(advances) + e['tracking'] * (len(word) - 1)) * .5
            parts = []
            half = (len(word) + 1) // 2
            for i, (ch, advance) in enumerate(zip(word, advances)):
                glyph = Image.new('L', (400, 410), 0)
                ImageDraw.Draw(glyph).text((80, 326), ch, font=font, fill=255, anchor='ls')
                ar = np.asarray(glyph)
                yy, xx = np.where(ar > 0)
                if len(xx) == 0:
                    start += advance
                    continue
                x0, x1, y0, y1 = max(0, int(xx.min()) - 5), min(400, int(xx.max()) + 6), max(0, int(yy.min()) - 5), min(410, int(yy.max()) + 6)
                am = ar[y0:y1, x0:x1].copy()
                cent = ((xx.min() + xx.max()) * .5, (yy.min() + yy.max()) * .5)
                fx = start + (cent[0] - 80) + e['tracking'] * i
                fy = H * .5 + ((cent[1] - 326) + 106)
                col = i % half if half else 0
                ix = W * .5 + (col - (half - 1) / 2) * 255
                iy = H * .5 + (-151 if i < half else 151)
                parts.append({'glyph': am.astype(np.float32) / 255., 'w': am.shape[1], 'h': am.shape[0], 'initial': (ix, iy), 'final': (fx, fy), 'index': i, 'top': i < half})
                start += advance
            lx = min(p['final'][0] - p['w'] / 2 for p in parts)
            rx = max(p['final'][0] + p['w'] / 2 for p in parts)
            shift = W * .5 - (lx + rx) * .5
            for p in parts:
                p['final'] = (p['final'][0] + shift, p['final'][1])
            return parts
        parts = self.get(('reflow', a['id'], a['text']), make)
        if t < .10 or t >= 5.92:
            return
        IVORY, LIME = np.array(a['color'][:3], np.float32), np.array(e['lower'][:3], np.float32)
        fade = fsm((t - .10) / .2) * (1 - fsm((t - 5.63) / .27))
        if t < 4.5:
            px, py = fsm((t - .18) / .43), fsm((t - .57) / .33)
        else:
            py, px = 1 - fsm((t - 4.49) / .35), 1 - fsm((t - 4.91) / .52)
        for p in parts:
            i = p['index']
            sx, sy = p['initial']
            tx, ty = p['final']
            x, y = sx * (1 - px) + tx * px, sy * (1 - py) + ty * py
            theta = (2.8 if i % 2 else -3.2) * (1 - py)
            small = p['glyph']
            if abs(theta) > .06:
                mat = cv2.getRotationMatrix2D((p['w'] / 2, p['h'] / 2), theta, 1.)
                small = cv2.warpAffine(small, mat, (p['w'], p['h']), flags=cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT)
            xx, yy = int(round(x - p['w'] / 2)), int(round(y - p['h'] / 2))
            xx0, yy0, xx1, yy1 = max(0, xx), max(0, yy), min(W, xx + p['w']), min(H, yy + p['h'])
            if xx1 <= xx0 or yy1 <= yy0:
                continue
            alpha = small[yy0 - yy:yy1 - yy, xx0 - xx:xx1 - xx] * fade
            roi = out[yy0:yy1, xx0:xx1]
            tone = IVORY if p['top'] else (LIME * (1 - py) + IVORY * py)
            np.maximum(roi, (alpha[:, :, None] * tone).astype(np.uint8), out=roi)

    # -- 48A Resonance: one travelling ripple bends the glyphs and tints them as it passes -------
    def ripple_wave(self, out, a, e, t):
        def make():
            font = ImageFont.truetype(self.fp(a['font']), int(a['size']))
            canvas = Image.new('L', (W, H), 0)
            d = ImageDraw.Draw(canvas)
            box = d.textbbox((0, 0), a['text'], font=font, stroke_width=0)
            bw, bh = box[2] - box[0], box[3] - box[1]
            d.text(((W - bw) / 2 - box[0], (H - bh) / 2 - box[1]), a['text'], font=font, fill=255)
            full = np.asarray(canvas)
            yp, xp = np.where(full > 1)
            x0, y0 = max(0, int(xp.min()) - 125), max(0, int(yp.min()) - 130)
            x1, y1 = min(W, int(xp.max()) + 126), min(H, int(yp.max()) + 131)
            mask = full[y0:y1, x0:x1].copy()
            hh, ww = mask.shape
            Y, X = np.indices((hh, ww), dtype=np.float32)
            cx, cy = (W / 2 - x0) + 30.0, (H / 2 - y0) + 1.0
            VX, VY = X - cx, Y - cy
            DIST = np.sqrt(VX * VX + VY * VY + 1e-3)
            return mask, (x0, y0, x1, y1), X, Y, VX / DIST, VY / DIST, DIST
        mask, (x0, y0, x1, y1), X, Y, NX, NY, DIST = self.get(('ripple', a['id'], a['text']), make)
        hh, ww = mask.shape
        WHITE, BLUE = np.array(a['color'][:3], dtype=np.float32), np.array(e['accent'][:3], dtype=np.float32)
        p = (t - 0.42) / 0.75
        if p < 0 or p > 1:
            displ = halo = np.zeros((hh, ww), dtype=np.float32)
        else:
            band = np.exp(-((DIST - 1100 * p) / 100.0) ** 2)
            ignition = fsm(p / 0.065)
            displ = (9.0 * band * ignition).astype(np.float32)
            chroma = 0.48 * band * ignition
            if t < 0.52:
                chroma += 0.18 * max(0.0, 1 - abs(t - 0.47) / 0.052) * np.exp(-(DIST / 95.0) ** 2)
            halo = chroma.astype(np.float32)
        warped = cv2.remap(mask, (X - NX * displ).astype(np.float32), (Y - NY * displ * 0.49).astype(np.float32), cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT, borderValue=0)
        mix = np.clip(halo, 0, 1)[:, :, None]
        color = WHITE[None, None, :] * (1 - mix) + BLUE[None, None, :] * mix
        k = fsm((t - 0.06) / (0.31 - 0.06)) * (1 - fsm((t - 5.38) / (5.87 - 5.38)))
        out[y0:y1, x0:x1] = np.uint8(np.clip(color * (warped[:, :, None] / 255.0) * k, 0, 255))

    # -- 49A Signal: a waveform, then level bars, become the word ---------------------------------
    def signal_wave(self, frame, a, e, t):
        def make():
            word = a['text']
            font = ImageFont.truetype(self.fp(a['font']), int(a['size']))
            d = ImageDraw.Draw(Image.new('L', (8, 8)))
            widths = [d.textlength(c, font=font) for c in word]
            track = e['tracking']
            total = sum(widths) + track * (len(word) - 1)
            x = (W - total) * 0.5
            im = Image.new('L', (W, H), 0)
            pp = ImageDraw.Draw(im)
            for c, wd in zip(word, widths):
                pp.text((round(x), 360), c, font=font, fill=255)
                x += wd + track
            ar = np.asarray(im)
            yy, xx = np.where(ar > 0)
            ar = np.roll(ar, round(H / 2 - (yy.min() + yy.max()) / 2), axis=0)
            yy, xx = np.where(ar > 0)
            bbox = (max(int(xx.min()) - 95, 0), max(int(yy.min()) - 120, 0), min(int(xx.max()) + 96, W), min(int(yy.max()) + 121, H))
            centers = []
            x = (W - total) * 0.5
            for wd in widths:
                centers.append(x + wd / 2 - bbox[0])
                x += wd + track
            mask = ar[bbox[1]:bbox[3], bbox[0]:bbox[2]]
            act = np.where(mask > 10)
            return mask, bbox, centers, int(act[1].min()), int(act[1].max()), (int(act[0].min()) + int(act[0].max())) * 0.5
        mask, box, centers, xmin, xmax, ycenter = self.get(('signal', a['id'], a['text']), make)
        WHITE, INK = np.array(a['color'][:3], np.float32), np.array(e['ink'][:3], np.float32)

        def ramp(u, s, f):
            return fsm((u - s) / (f - s))
        word_alpha = ramp(t, .43, .80) * (1 - ramp(t, 4.84, 5.25))
        bars_alpha = ramp(t, .24, .43) * (1 - ramp(t, .50, .78)) + ramp(t, 5.03, 5.25) * (1 - ramp(t, 5.39, 5.67))
        wave_alpha = ramp(t, .06, .19) * (1 - ramp(t, .25, .43)) + ramp(t, 5.44, 5.65) * (1 - ramp(t, 5.73, 5.93))
        maskf = mask.astype(np.float32) / 255.
        sub = maskf[:, :, None] * word_alpha * WHITE[None, None, :]
        bars = np.zeros_like(mask)
        cy = int(round(ycenter))
        for j, x0 in enumerate(centers):
            if not bars_alpha > 0.005:
                break
            factor = cyc(e['bars'], j)
            pulse = 1 + 0.10 * math.sin(12 * t + j * 1.25)
            hh = int(round((56 + 108 * factor * pulse) * min(1, 0.75 + 0.25 * bars_alpha)))
            xc = int(round(x0))
            cv2.rectangle(bars, (xc - 8, cy - hh // 2), (xc + 8, cy + hh // 2), 255, -1, cv2.LINE_AA)
        sub = np.maximum(sub, (bars.astype(np.float32) / 255. * bars_alpha)[:, :, None] * (INK[None, None, :] * .94 + WHITE[None, None, :] * .06))
        wave = np.zeros_like(mask)
        if wave_alpha > 0.005:
            xc = np.arange(xmin - 50, xmax + 51, 2)
            u = (xc - xmin) / max(1., xmax - xmin)
            env = np.maximum(0, np.sin(np.pi * u)) ** 0.85
            osc = (46 * np.sin(2 * np.pi * (3.25 * u + 0.07)) + 18 * np.sin(2 * np.pi * (8.1 * u + 0.14)) + 6 * np.sin(2 * np.pi * 16 * u)) * env
            pts = np.stack([xc, np.rint(ycenter + osc).astype(np.int32)], axis=1).astype(np.int32)
            cv2.polylines(wave, [pts], False, 255, 2, cv2.LINE_AA)
        sub = np.maximum(sub, (wave.astype(np.float32) / 255. * wave_alpha)[:, :, None] * INK[None, None, :])
        x0, y0, x1, y1 = box
        frame[y0:y1, x0:x1] = np.clip(sub, 0, 255).astype(np.uint8)


def blend_paste(canvas, mask, xpos, ypos, color, alpha=1.0):
    """render_stage33.paste()."""
    alpha = float(np.clip(alpha, 0, 1))
    if alpha < 1e-4:
        return
    ih, iw = mask.shape
    x, y = int(round(xpos)), int(round(ypos))
    x0, y0, x1, y1 = max(0, x), max(0, y), min(W, x + iw), min(H, y + ih)
    if x1 <= x0 or y1 <= y0:
        return
    a = (mask[y0 - y:y1 - y, x0 - x:x1 - x].astype(np.float32) / 255 * alpha)[:, :, None]
    c = np.asarray(color, dtype=np.float32).reshape(1, 1, 3)
    dest = canvas[y0:y1, x0:x1, :]
    dest[:] = np.clip(dest.astype(np.float32) * (1 - a) + a * c, 0, 255).astype(np.uint8)


class Gold1x(B2):
    def __init__(self, sc, font_path):
        self.sc, self.font_path = sc, font_path
        self.cache = {}

    def fp(self, key):
        return self.font_path(key)

    def get(self, key, make):
        if key not in self.cache:
            self.cache[key] = make()
        return self.cache[key]

    # 25A Become: per-glyph SDF morph between two words, cells side by side --------------------
    def contour_morph(self, bg, a, e, t):
        import scene_core as core
        src, dst = a['text'], core.format_text(e['to'], a.get('_env') or {})
        n = max(len(src), len(dst))
        src, dst = src.ljust(n), dst.ljust(n)
        gw, gh, gap = e['cell'][0], e['cell'][1], e['gap']
        sw, shh = e['glyph']
        ox, oy = e['glyphAt']

        def make():
            out = []
            for l, r in zip(src, dst):
                pa = np.zeros((gh, gw), dtype=np.uint8)
                pb = np.zeros_like(pa)
                pa[oy:oy + shh, ox:ox + sw] = scale_mask(textmask25(l, self.fp(a['font']), int(a['size'])), w=sw, h=shh)
                pb[oy:oy + shh, ox:ox + sw] = scale_mask(textmask25(r, self.fp(e['fontTo']), int(e['sizeTo'])), w=sw, h=shh)
                out.append((sdf3(pa), sdf3(pb)))
            return out
        fields = self.get(('morph', a['id'], src, dst), make)
        x0 = int((W - (gw * n + gap * (n - 1))) // 2)
        ent = smooth((t - e['in'][0]) / e['in'][1])
        morph = smooth((t - e['morph'][0]) / e['morph'][1])
        leave = 1 - smooth((t - e['out'][0]) / e['out'][1])
        col = mix_rgb(a['color'][:3], e['colorTo'][:3], morph)
        for i, (old, new) in enumerate(fields):
            f = old * (1 - morph) + new * morph
            m = (255 * np.clip((f + 0.65) / 1.5, 0, 1)).astype('uint8')
            put_max(bg, m, x0 + i * (gw + gap), e['y'], col, ent * leave)

    # 25B Unbound: the whole word on a waving remap grid, tinted left to right ------------------
    def elastic_warp(self, bg, a, e, t):
        def make():
            m = scale_mask(textmask25(a['text'], self.fp(a['font']), int(a['size']), tracking=e['tracking']), w=e['width'])
            m = cv2.copyMakeBorder(m, e['margin'][1], e['margin'][1], e['margin'][0], e['margin'][0], cv2.BORDER_CONSTANT, value=0)
            h, w = m.shape
            xg = np.broadcast_to(np.arange(w, dtype=np.float32), (h, w)).copy()
            yg = np.broadcast_to(np.arange(h, dtype=np.float32)[:, None], (h, w)).copy()
            return m, xg, yg
        base, xg, yg = self.get(('warp', a['id'], a['text']), make)
        bh, bw = base.shape
        bx, by = int((W - bw) / 2), int((H - bh) / 2) + e['dy']
        arrival = 1 - smooth((t - e['settle'][0]) / e['settle'][1])
        departure = smooth((t - e['curl'][0]) / e['curl'][1])
        amplitude = e['amp'][0] * arrival + e['amp'][1] * departure
        phase = e['phase'][0] + e['phase'][1] * t
        wave = np.sin(2 * np.pi * (xg / bw) * e['freq'][0] - phase)
        map_y = yg - amplitude * wave
        map_x = xg - (amplitude * e['shear']) * np.sin(2 * np.pi * (yg / bh) * e['freq'][1] + phase + 1.0)
        m = cv2.remap(base, map_x.astype(np.float32), map_y.astype(np.float32), cv2.INTER_LINEAR,
                      borderMode=cv2.BORDER_CONSTANT, borderValue=0)
        op = smooth((t - e['in'][0]) / e['in'][1]) * (1 - smooth((t - e['out'][0]) / e['out'][1]))
        if op > 0:
            xs = np.linspace(0, 1, bw, dtype=np.float32)[None, :, None]
            start = np.array(a['color'][:3], dtype=np.float32)[None, None, :]
            end = np.array(e['colorTo'][:3], dtype=np.float32)[None, None, :]
            tone = start + (end - start) * xs
            aa = (m.astype(np.float32) / 255 * op)[:, :, None]
            y0, x0 = max(0, by), max(0, bx)
            y1, x1 = min(H, by + bh), min(W, bx + bw)
            bg[y0:y1, x0:x1] = np.clip(aa * tone, 0, 255).astype(np.uint8)[y0 - by:y1 - by, x0 - bx:x1 - bx]

    # 25C Revolve: letters on an arc swing down onto a straight, kerned baseline ---------------
    def arc_to_baseline(self, bg, a, e, t):
        n_cell = e['cell']
        c0 = n_cell // 2

        def make():
            f = ImageFont.truetype(self.fp(a['font']), int(a['size']))
            txt, trk = a['text'], e['tracking']
            total = f.getlength(txt) + trk * (len(txt) - 1)
            left = (W - total) * .5
            out = []
            for i, ch in enumerate(txt):
                bx0, by0, bx1, by1 = f.getbbox(ch, anchor='ls')
                gw, gh = bx1 - bx0, by1 - by0
                cim = Image.new('L', (n_cell, n_cell), 0)
                ImageDraw.Draw(cim).text((c0 - gw * .5 - bx0, c0 - gh * .5 - by0), ch, font=f, fill=255, anchor='ls')
                cx = left + f.getlength(txt[:i]) + trk * i + (bx0 + bx1) * .5
                cy = e['baseline'] + (by0 + by1) * .5
                ix = i - (len(txt) - 1) * .5
                rx = W * .5 + (cx - W * .5) * e['arcScale']
                ry = cy - e['arcHeight'] * math.sin(math.pi * (i + .5) / len(txt)) + ix * e['arcTilt']
                out.append((np.asarray(cim, dtype=np.uint8), cx, cy, rx, ry, -ix * e['angle'], i))
            return out
        for orig, cx, cy, rx, ry, ang0, i in self.get(('arc', a['id'], a['text']), make):
            u = outc((t - (e['land'][0] + i * e['land'][2])) / e['land'][1])
            ex = smooth((t - (e['rise'][0] + i * e['rise'][2])) / e['rise'][1])
            x = rx * (1 - u) + cx * u
            y = ry * (1 - u) + cy * u - e['riseBy'] * ex
            rot = ang0 * (1 - u)
            aa = smooth((t - (e['in'][0] + i * e['in'][2])) / e['in'][1]) * (1 - smooth((t - (e['out'][0] + i * e['out'][2])) / e['out'][1]))
            if aa > .001:
                if abs(rot) < .4:
                    m = orig
                else:
                    mat = cv2.getRotationMatrix2D((c0, c0), rot, 1.0)
                    m = cv2.warpAffine(orig, mat, (n_cell, n_cell), flags=cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT, borderValue=0)
                put_max(bg, m, x - c0, y - c0, a['color'][:3], aa)

    # 26A Beyond: the word on a perspective plane that swings flat ---------------------------
    def perspective_plane(self, bg, a, e, t):
        A = self.get(('persp', a['id'], a['text']), lambda: resizeto(masktxt26(a['text'], self.fp(a['font']), int(a['size'])), e['maxWidth']))
        AH, AW = A.shape
        AX, AY = (W - AW) // 2, (H - AH) // 2 + e['dy']
        src = np.float32([[0, 0], [AW - 1, 0], [AW - 1, AH - 1], [0, AH - 1]])
        u = outc((t - e['swing'][0]) / e['swing'][1])
        alpha = smooth((t - e['in'][0]) / e['in'][1]) * (1 - smooth((t - e['out'][0]) / e['out'][1]))
        k = e['corners']  # left, right, topLeft, bottomLeft, topRight, bottomRight offsets at u = 0
        left = AX + (1 - u) * k[0]
        right = AX + AW - (1 - u) * k[1]
        top_left = AY + (1 - u) * k[2]
        bottom_left = AY + AH - (1 - u) * k[3]
        top_right = AY - (1 - u) * k[4]
        bottom_right = AY + AH + (1 - u) * k[5]
        dst = np.float32([[left, top_left], [right, top_right], [right, bottom_right], [left, bottom_left]])
        M = cv2.getPerspectiveTransform(src, dst)
        warped = cv2.warpPerspective(A, M, (W, H), flags=cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT, borderValue=0)
        col = mix_round(e['colorFrom'][:3], a['color'][:3], u)
        aa = (warped.astype(np.float32) / 255 * alpha)[:, :, None]
        bg[:] = np.clip(bg * (1 - aa) + aa * np.array(col, dtype=np.float32)[None, None, :], 0, 255).astype('uint8')

    # 26B Kinetic: each letter grows from a sliver to full width, then compresses away ---------
    def width_bloom(self, bg, a, e, t):
        def make():
            f = ImageFont.truetype(self.fp(a['font']), int(a['size']))
            out = []
            for ch in a['text']:
                bb = f.getbbox(ch, anchor='ls')
                pad = 14
                cm = Image.new('L', (max(1, bb[2] - bb[0] + pad * 2), max(1, bb[3] - bb[1] + pad * 2)), 0)
                ImageDraw.Draw(cm).text((pad - bb[0], pad - bb[1]), ch, fill=255, font=f, anchor='ls')
                arr = np.array(cm)
                ys, xs = np.where(arr > 0)
                if len(xs):
                    arr = arr[ys.min() - 2:ys.max() + 3, xs.min() - 2:xs.max() + 3]
                out.append((arr, bb))
            txt, trk = a['text'], e['tracking']
            adv = [f.getlength(txt[:i]) + trk * i for i in range(len(txt))]
            total = f.getlength(txt) + trk * (len(txt) - 1)
            return out, adv, (W - total) / 2
        metrics, adv, bx = self.get(('bloom', a['id'], a['text']), make)
        exit_ = smooth((t - e['out'][0]) / e['out'][1])
        for i, (orig, bb) in enumerate(metrics):
            start = e['grow'][0] + e['grow'][2] * i
            p = outc((t - start) / e['grow'][1])
            overshoot = e['overshoot'] * math.sin(math.pi * clamp((t - start) / 1.0)) ** 2 * (1 - p)
            sx = e['from'] + (1 - e['from']) * p + overshoot
            scaled = cv2.resize(orig, (max(1, round(orig.shape[1] * sx)), orig.shape[0]), interpolation=cv2.INTER_LINEAR)
            cx = bx + adv[i] + bb[0] + (bb[2] - bb[0]) / 2
            x = cx - scaled.shape[1] / 2
            y = e['baseline'] + bb[1] - 2
            col = tuple(int(q * (1 - p) + r * p) for q, r in zip(e['colorFrom'][:3], a['color'][:3]))
            op = smooth((t - start + e['in'][0]) / e['in'][1]) * (1 - exit_)
            if exit_ > 0:
                sx2 = max(e['squeeze'][1], 1 - e['squeeze'][0] * exit_)
                scaled = cv2.resize(scaled, (max(1, round(scaled.shape[1] * sx2)), scaled.shape[0]), interpolation=cv2.INTER_LINEAR)
                x = cx - scaled.shape[1] / 2
            blend_over(bg, scaled, x, y, col, op)

    # 26C Awaken: each serif glyph's weight morphs Light → Bold through its distance field -----
    def weight_morph(self, bg, a, e, t):
        def make():
            CT = ImageFont.truetype(self.fp(a['font']), int(a['size']))
            CB = ImageFont.truetype(self.fp(e['fontTo']), int(a['size']))
            word, trk = a['text'], e['tracking']
            total = CB.getlength(word) + trk * (len(word) - 1)
            cx0 = (W - total) // 2
            out = []
            for i, ch in enumerate(word):
                width, height = int(math.ceil(CB.getlength(ch))) + 34, e['cellHeight']
                masks = []
                for font in (CT, CB):
                    bb = font.getbbox(ch, anchor='ls')
                    c = Image.new('L', (width, height), 0)
                    ImageDraw.Draw(c).text(((width - (bb[2] - bb[0])) / 2 - bb[0], e['pen']), ch, font=font, fill=255, anchor='ls')
                    masks.append(np.asarray(c, dtype=np.uint8))
                ink0 = (masks[0] > 125).astype(np.uint8)
                ink1 = (masks[1] > 125).astype(np.uint8)
                f0 = cv2.distanceTransform(ink0, cv2.DIST_L2, 3) - cv2.distanceTransform(1 - ink0, cv2.DIST_L2, 3)
                f1 = cv2.distanceTransform(ink1, cv2.DIST_L2, 3) - cv2.distanceTransform(1 - ink1, cv2.DIST_L2, 3)
                out.append((f0, f1, cx0 + CB.getlength(word[:i]) + trk * i - 17))
            return out
        p = outc((t - e['morph'][0]) / e['morph'][1])
        ent = smooth((t - e['in'][0]) / e['in'][1])
        leave = 1 - smooth((t - e['out'][0]) / e['out'][1])
        col = mix_round(e['colorFrom'][:3], a['color'][:3], p)
        for f0, f1, x in self.get(('weight', a['id'], a['text']), make):
            s_ = f0 * (1 - p) + f1 * p
            m = np.clip((s_ + .65) / 1.3, 0, 1)
            blend_over(bg, (255 * m).astype('uint8'), x, e['baseline'] - e['pen'], col, ent * leave)

    # 27A Spectra: a prism passage of colour runs through the letters as they uncover --------
    def spectral_ink(self, bg, a, e, t):
        def make():
            m = within_width(masktxt27(a['text'], self.fp(a['font']), int(a['size']), tracking=e['tracking']), e['maxWidth'])
            return m
        AMASK = self.get(('spectra', a['id'], a['text']), make)
        AH, AW = AMASK.shape
        AX, AY = int((W - AW) // 2), int((H - AH) // 2 + e['dy'])
        A_X = np.arange(AW, dtype=np.float32)[None, :]
        base = np.array(a['color'][:3], dtype=np.float32)
        reveal = smooth((t - e['reveal'][0]) / e['reveal'][1])
        departure = 1 - smooth((t - e['out'][0]) / e['out'][1])
        travel = outc((t - e['travel'][0]) / e['travel'][1])
        center = (e['pass'][0] + e['pass'][1] * travel) * AW
        d = (A_X - center) / (AW * e['pass'][2])
        rgb = base[None, None, :]
        for (off, wid), vec in zip(e['bands'], e['tints']):
            band = np.exp(-((d + off) / wid) ** 2)
            rgb = rgb + band[:, :, None] * np.array(vec, np.float32)
        boundary = reveal * (AW + e['edge'][0]) - e['edge'][1]
        uncover = np.clip((boundary - A_X) / e['edge'][2], 0, 1)
        mask = (AMASK * uncover).astype('uint8')
        y = AY + round((1 - outc((t - e['rise'][0]) / e['rise'][1])) * e['rise'][2])
        blend_variable(bg, mask, AX, y, np.clip(rgb, 0, 255), departure)

    # 27B Pivot: each glyph turns about its own centre and settles on the baseline -------------
    def glyph_pivot(self, bg, a, e, t):
        def make():
            f = ImageFont.truetype(self.fp(a['font']), int(a['size']))
            word = a['text']
            chars = [(masktxt27(ch, self.fp(a['font']), int(a['size']), pad=e['glyphPad']), f.getlength(word[:i])) for i, ch in enumerate(word)]
            return chars, (W - f.getlength(word)) / 2
        chars, bx = self.get(('pivot', a['id'], a['text']), make)
        leave = 1 - smooth((t - e['out'][0]) / e['out'][1])
        for i, (m, advance) in enumerate(chars):
            onset = e['turn'][0] + i * e['turn'][2]
            p = outc((t - onset) / e['turn'][1])
            angle = (e['angle'][0] if i % 2 == 0 else e['angle'][1]) * (1 - p)
            cyshift = (e['shift'][0] if i % 2 == 0 else e['shift'][1]) * (1 - p)
            h, w = m.shape
            c = (w / 2, h / 2)
            pad = e['pad']
            matrix = cv2.getRotationMatrix2D(c, angle, 1)
            matrix[0, 2] += pad
            matrix[1, 2] += pad
            warped = cv2.warpAffine(m, matrix, (w + 2 * pad, h + 2 * pad), flags=cv2.INTER_CUBIC, borderMode=cv2.BORDER_CONSTANT, borderValue=0)
            targetx = bx + advance + e['nudge'][0]
            targety = e['y'] + e['nudge'][1]
            col = tuple(int(round((1 - p) * q + p * r)) for q, r in zip(e['colorFrom'][:3], a['color'][:3]))
            op = smooth((t - onset) / e['in'][1]) * leave
            blend27(bg, warped, targetx - pad, targety + cyshift - pad, col, op)

    # 27C Mirror: the word unfolds upward from its reflection, which shrinks away --------------
    def mirror_unfold(self, bg, a, e, t):
        def make():
            m = within_width(masktxt27(a['text'], self.fp(a['font']), int(a['size']), tracking=e['tracking']), e['maxWidth'])
            return m, cv2.flip(m, 0)
        CMASK, CREF = self.get(('mirror', a['id'], a['text']), make)
        CH, CW = CMASK.shape
        CX, CY = (W - CW) // 2, e['y']
        unfold = outc((t - e['unfold'][0]) / e['unfold'][1])
        leave = 1 - smooth((t - e['out'][0]) / e['out'][1])
        h = max(1, round(CH * (e['from'] + (1 - e['from']) * unfold)))
        top = cv2.resize(CMASK, (CW, h), interpolation=cv2.INTER_AREA if h < CH else cv2.INTER_LINEAR)
        topy = CY + CH - h
        refl_p = (1 - unfold) ** 1.6
        if refl_p > .008:
            rh = max(1, round(CH * e['reflection'] * refl_p))
            refl = cv2.resize(CREF, (CW, rh), interpolation=cv2.INTER_AREA)
            v = np.linspace(1, 0, rh, dtype=np.float32)[:, None] ** 1.5
            refl = np.clip(refl * v, 0, 255).astype('uint8')
            blend27(bg, refl, CX, CY + CH + 9, e['reflectionColor'][:3], e['reflectionAlpha'] * refl_p * leave * smooth((t - e['rin'][0]) / e['rin'][1]))
        col = tuple(int(round((1 - unfold) * q + unfold * r)) for q, r in zip(e['colorFrom'][:3], a['color'][:3]))
        blend27(bg, top, CX, topy, col, smooth((t - e['in'][0]) / e['in'][1]) * leave)

    # 28A Incline: upright → italic through the two masters' distance fields ------------------
    def slant_morph(self, bg, a, e, t):
        def make():
            reg = masktxt28(a['text'], self.fp(a['font']), int(a['size']), e['maxWidth'])
            ita = masktxt28(a['text'], self.fp(e['fontTo']), int(a['size']), e['maxWidth'])
            ah = max(reg.shape[0], ita.shape[0]) + 48
            aw = max(reg.shape[1], ita.shape[1]) + 60
            m0, m1 = put_centered(reg, aw, ah), put_centered(ita, aw, ah)
            return m0, m1, sdf_precise(m0), sdf_precise(m1)
        m0, m1, s0, s1 = self.get(('slant', a['id'], a['text']), make)
        ah, aw = m0.shape
        ax, ay = (W - aw) // 2, (H - ah) // 2 + e['dy']
        pr = smooth((t - e['morph'][0]) / e['morph'][1])
        if pr <= 0:
            mask = m0
        elif pr >= 1:
            mask = m1
        else:
            mask = np.clip((s0 * (1 - pr) + s1 * pr + 1.05) * 255 / 2.1, 0, 255).astype('uint8')
        entry = smooth((t - e['in'][0]) / e['in'][1])
        if entry < 1:
            yy = np.arange(ah, dtype=np.float32)[:, None]
            mask = (mask * np.clip((entry * (ah + 24) - yy) / 18, 0, 1)).astype('uint8')
        opacity = 1 - smooth((t - e['out'][0]) / e['out'][1])
        c0, c1 = a['color'][:3], e['colorTo'][:3]
        tint = tuple(round(q + (r - q) * pr) for q, r in zip(c0, c1))
        blend28(bg, mask, ax, ay, tint, opacity)

    # 28B Aperture: the counters open from solid forms, each from its own centre --------------
    def counter_release(self, bg, a, e, t):
        def make():
            bm = masktxt28(a['text'], self.fp(a['font']), int(a['size']), e['maxWidth'], tracking=e['tracking'])
            outer = np.zeros_like(bm)
            contours, hier = cv2.findContours((bm >= 120).astype('uint8'), cv2.RETR_TREE, cv2.CHAIN_APPROX_SIMPLE)
            holes = []
            for i, cn in enumerate(contours):
                d, p = 0, hier[0][i][3]
                while p >= 0:
                    d += 1
                    p = hier[0][p][3]
                if d % 2 == 0:
                    cv2.drawContours(outer, contours, i, 255, thickness=-1)
                else:
                    hole = np.zeros_like(bm)
                    cv2.drawContours(hole, contours, i, 255, thickness=-1)
                    ys, xs = np.where(hole > 0)
                    if len(xs) < 25:
                        continue
                    cx, cy = float(xs.mean()), float(ys.mean())
                    R = float(np.sqrt((xs - cx) ** 2 + (ys - cy) ** 2).max())
                    holes.append((cx, cy, R, hole))
            inks = np.maximum(bm, outer)
            fields = []
            for cx, cy, r, hole in sorted(holes, key=lambda z: z[0]):
                yy, xx = np.where(hole > 0)
                patch = (slice(yy.min(), yy.max() + 1), slice(xx.min(), xx.max() + 1))
                Y, X = np.mgrid[yy.min():yy.max() + 1, xx.min():xx.max() + 1]
                fields.append((patch, hole[patch].astype(np.float32) / 255, np.sqrt((X - cx) ** 2 + (Y - cy) ** 2) / max(r, .001)))
            return inks, fields
        inks, fields = self.get(('aperture', a['id'], a['text']), make)
        bh, bw = inks.shape
        mask = inks.astype(np.float32).copy()
        for j, (patch, hole, distance) in enumerate(fields):
            prog = outc((t - (e['open'][0] + e['open'][2] * j)) / e['open'][1])
            reveal = np.clip((prog - distance) / .20, 0, 1)
            mask[patch] *= (1 - hole * reveal)
        mask = mask.astype('uint8')
        entry = smooth((t - e['in'][0]) / e['in'][1]) * (1 - smooth((t - e['out'][0]) / e['out'][1]))
        blend28(bg, mask, (W - bw) // 2, (H - bh) // 2, a['color'][:3], entry)

    # 28C Phase: an ivory swath crosses the word, letters black inside it, ivory behind ---------
    def inverse_transit(self, bg, a, e, t):
        cm = self.get(('phase', a['id'], a['text']), lambda: masktxt28(a['text'], self.fp(a['font']), int(a['size']), e['maxWidth'], tracking=e['tracking']))
        ch, cw = cm.shape
        cx, cy = (W - cw) // 2, (H - ch) // 2
        if t < e['visible'][0] or t >= e['visible'][1]:
            return
        exiting = t > e['exit'][0]
        prog = outc((t - e['exit'][0]) / e['exit'][1]) if exiting else outc((t - e['enter'][0]) / e['enter'][1])
        if not exiting:
            front = -310 + prog * (cw + 590)
        else:
            front = cw + 315 - prog * (cw + 650)
        left, right = front - e['slab'], front
        coords = np.arange(cw, dtype=np.float32)
        band = np.clip((coords - left) / 6, 0, 1) * np.clip((right - coords) / 6, 0, 1)
        uncovered = np.clip((front - coords) / 20, 0, 1)
        y0, y1, x0, x1 = cy - 13, cy + ch + 13, cx, cx + cw
        local = bg[y0:y1, x0:x1]
        ink = np.array(a['color'][:3], np.float32)
        local[:] = (band[None, :, None] * ink).astype(np.uint8)
        mask = cm.astype(np.float32) / 255
        local[13:13 + ch, :, :] = (np.clip(local[13:13 + ch, :, :].astype(np.float32) * (1 - mask[..., None]) + mask[..., None] * ((1 - band)[None, :, None] * uncovered[None, :, None] * ink), 0, 255)).astype(np.uint8)

    # 29A Gravity: an extruded cast of the word retracts into a crisp front face -------------
    def depth_cast(self, bg, a, e, t):
        m = self.get(('cast', a['id'], a['text']), lambda: masktext29(a['text'], self.fp(a['font']), int(a['size']), e['maxWidth'], tracking=e['tracking']))
        ah, aw = m.shape
        ax, ay = (W - aw) // 2, (H - ah) // 2 + e['dy']
        if t < e['visible'][0] or t >= e['visible'][1]:
            return
        inc = smooth((t - e['in'][0]) / e['in'][1])
        retract = smooth((t - e['retract'][0]) / e['retract'][1])
        expand = smooth((t - e['expand'][0]) / e['expand'][1])
        depth = (1 - retract) * 1.0 + expand * e['expand'][2]
        n = e['layers']
        if depth > .005:
            for j in range(n, 0, -1):
                u = j / float(n)
                dx = -int(round(u * e['extrude'][0] * depth))
                dy = int(round(u * e['extrude'][1] * depth))
                col = tuple(int(b + e['castLift'] * (1 - u)) for b in e['castColor'][:3])
                alpha = inc * (1 - smooth((t - e['castOut'][0]) / e['castOut'][1]))
                blend29(bg, m, ax + dx, ay + dy, col, alpha)
        frontop = inc * (1 - smooth((t - e['out'][0]) / e['out'][1]))
        if frontop > 0:
            xx = np.arange(aw, dtype=np.float32)[None, :]
            p = smooth((t - e['light'][0]) / e['light'][1])
            wave = np.clip((p * (aw + 300) - xx + 25) / 160, 0, 1)
            tint = np.maximum(.45, np.minimum(1., .46 + .54 * wave))
            blend29(bg, (m.astype(np.float32) * tint).astype(np.uint8), ax, ay, a['color'][:3], frontop)

    # 29B Unfold: the word opens like a pleated sheet, creases shading as it flattens ----------
    def pleat_unfold(self, bg, a, e, t):
        bm = self.get(('pleat', a['id'], a['text']), lambda: masktext29(a['text'], self.fp(a['font']), int(a['size']), e['maxWidth'], tracking=e['tracking']))
        bh, bw = bm.shape
        by = (H - bh) // 2
        if t < e['visible'][0] or t > e['visible'][1]:
            return
        u = 1 - (1 - clamp((t - e['open'][0]) / e['open'][1])) ** 3
        foldout = smooth((t - e['close'][0]) / e['close'][1])
        flat = u * (1 - foldout)
        scale = e['from'] + (1 - e['from']) * flat
        nw = max(8, int(round(bw * scale)))
        sh = cv2.resize(bm, (nw, bh), interpolation=cv2.INTER_LINEAR).astype(np.float32)
        xs = np.arange(nw, dtype=np.float32)[None, :]
        ys = np.arange(bh, dtype=np.float32)[:, None]
        phase = xs / max(1, nw - 1) * 4 * math.pi
        bend = np.sin(phase) * (1 - flat)
        srcx = np.broadcast_to(xs + 9 * bend, (bh, nw)).astype(np.float32)
        srcy = np.broadcast_to(ys + 18 * bend, (bh, nw)).astype(np.float32)
        sh = cv2.remap(sh, srcx, srcy, cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT)
        light = (.77 + .23 * np.cos(phase + math.pi * .22)) * (1 - flat) + flat
        sh = np.clip(sh * light, 0, 255).astype(np.uint8)
        al = smooth((t - e['in'][0]) / e['in'][1]) * (1 - smooth((t - e['out'][0]) / e['out'][1]))
        blend29(bg, sh, (W - nw) // 2, by, a['color'][:3], al)

    # 29C Coalesce: a seeded dust cloud condenses onto the word, then the fill takes over ------
    def particle_coalesce(self, bg, a, e, t):
        def make():
            m = masktext29(a['text'], self.fp(a['font']), int(a['size']), e['maxWidth'], tracking=e['tracking'])
            rng = np.random.default_rng(int(e['seed']))
            valid = np.column_stack(np.where(m > 180))
            n = int(e['count'])
            v = valid[rng.choice(len(valid), size=n, replace=True)]
            ey, ex = v[:, 0].astype(np.float32), v[:, 1].astype(np.float32)
            ang = rng.uniform(0, 2 * np.pi, n).astype(np.float32)
            rad = rng.uniform(e['radius'][0], e['radius'][1], n).astype(np.float32)
            sx, sy = ex + np.cos(ang) * rad, ey + np.sin(ang) * rad * e['squash']
            radii = rng.choice([1, 1, 1, 2], n)
            shades = rng.integers(e['shade'][0], e['shade'][1], n)
            return m, ex, ey, sx, sy, radii, shades
        m, ex, ey, sx, sy, radii, shades = self.get(('dust', a['id'], a['text']), make)
        ch, cw = m.shape
        cx, cy = (W - cw) // 2, (H - ch) // 2 + e['dy']
        if t < .20:
            return
        if t <= 2.70:
            drift = 1 - smooth((t - .45) / 1.82)
            dot_alpha = (1 - smooth((t - 1.70) / .67)) * smooth((t - .20) / .24)
            fill_alpha = smooth((t - 1.54) / .95)
        elif t < 4.43:
            drift, dot_alpha, fill_alpha = 0, 0, 1
        else:
            drift = smooth((t - 4.43) / .88)
            dot_alpha = smooth((t - 4.48) / .25) * (1 - smooth((t - 5.18) / .54))
            fill_alpha = 1 - smooth((t - 4.44) / .36)
        if dot_alpha > .004:
            xx = np.rint(ex + drift * (sx - ex)).astype('int32') + cx
            yy = np.rint(ey + drift * (sy - ey)).astype('int32') + cy
            for k in range(len(xx)):
                if xx[k] < 0 or xx[k] >= W or yy[k] < 0 or yy[k] >= H:
                    continue
                v = int(shades[k] * dot_alpha)
                cv2.circle(bg, (int(xx[k]), int(yy[k])), int(radii[k]), (v, v, v), -1, lineType=cv2.LINE_AA)
        if fill_alpha > 0:
            blend29(bg, m, cx, cy, a['color'][:3], fill_alpha)

    def _region(self, bg, e, patch):
        X0, Y0, RW, RH = e['region']
        bg[Y0:Y0 + RH, X0:X0 + RW] = patch

    # 30A Etched: each glyph's outline is traced in copper, then the ivory fill comes in -------
    def contour_trace(self, bg, a, e, t):
        def make():
            _, glyphs, _, _ = mask_text30(a['text'], self.fp(a['font']), int(a['size']), e['region'], e['baseline'], e['tracking'])
            cont = []
            for gm in glyphs:
                cs, _ = cv2.findContours((gm > 50).astype('uint8'), cv2.RETR_LIST, cv2.CHAIN_APPROX_NONE)
                cont.append(sorted(cs, key=lambda c: cv2.arcLength(c, True), reverse=True))
            return glyphs, cont
        glyphs, conts = self.get(('etch', a['id'], a['text']), make)
        RW, RH = e['region'][2], e['region'][3]
        fill = np.zeros((RH, RW), np.float32)
        contours = np.zeros((RH, RW), np.uint8)
        exit_amount = ease30((t - e['out'][0]) / e['out'][1])
        for i, (gm, cs) in enumerate(zip(glyphs, conts)):
            start = e['trace'][0] + e['trace'][2] * i
            outline_p = quint((t - start) / e['trace'][1])
            fill_p = ease30((t - start - e['fill'][0]) / e['fill'][1])
            if fill_p > 0:
                np.maximum(fill, (gm / 255.0) * fill_p * (1 - exit_amount), out=fill)
            if outline_p > 0 and fill_p < .98:
                for c in cs:
                    count = max(1, int(len(c) * outline_p))
                    if count > 1:
                        cv2.polylines(contours, [c[:count]], False, 255, 2, cv2.LINE_AA)
        outline_alpha = (contours / 255.0) * (1 - ease30((t - e['tracer'][0]) / e['tracer'][1])) * (1 - exit_amount)
        self._region(bg, e, comp30([(fill, np.array(a['color'][:3], np.float32)), (outline_alpha, np.array(e['tracerColor'][:3], np.float32))], e['region']))

    # 30B Recode: each letter rolls through alternates like a character drum ------------------
    def character_reel(self, bg, a, e, t):
        def make():
            _, glyphs, font, bx = mask_text30(a['text'], self.fp(a['font']), int(a['size']), e['region'], e['baseline'], e['tracking'])
            RW, RH = e['region'][2], e['region'][3]
            reels = []
            for index, ch0 in enumerate(a['text']):
                alts = e['reels'][index] if index < len(e['reels']) else [ch0, ch0, ch0]
                x = bx + font.getlength(a['text'][:index]) + e['tracking'] * index
                row = []
                for ch in alts:
                    canvas = Image.new('L', (RW, RH), 0)
                    ImageDraw.Draw(canvas).text((int(round(x)), e['baseline']), ch, fill=255, font=font, anchor='ls')
                    row.append(np.asarray(canvas))
                reels.append(row)
            return glyphs, reels
        glyphs, reels = self.get(('reel', a['id'], a['text']), make)
        RW, RH = e['region'][2], e['region'][3]
        layer = np.zeros((RH, RW), np.float32)
        exit_p = ease30((t - e['out'][0]) / e['out'][1])
        for i, final_g in enumerate(glyphs):
            start = e['roll'][0] + i * e['roll'][2]
            p = float(np.clip((t - start) / e['roll'][1], 0, 1))
            if p <= 0:
                continue
            phase = p * 3
            step = min(int(phase), 2)
            raw_frac = phase - step if p < 1 else 1.
            change = ease30((raw_frac - .16) / .7)
            seq = reels[i] + [final_g]
            old = move_unclipped(seq[step], -e['drum'] * change)
            new = move_unclipped(seq[step + 1], e['drum'] * (1 - change))
            intensity = ease30((t - start) / .14) * (1 - exit_p)
            blended = (old.astype(np.float32) * (1 - change) + new.astype(np.float32) * change) / 255.0
            np.maximum(layer, blended * intensity, out=layer)
        self._region(bg, e, comp30([(layer, np.array(a['color'][:3], np.float32))], e['region']))

    # 30C Contour: iso-distance rings collapse onto the glyph edges, then the ink fills --------
    def isoline_collapse(self, bg, a, e, t):
        def make():
            cm, _, _, _ = mask_text30(a['text'], self.fp(a['font']), int(a['size']), e['region'], e['baseline'], e['tracking'])
            cb = (cm > 45).astype(np.uint8)
            return cm, cv2.distanceTransform(1 - cb, cv2.DIST_L2, 5) - cv2.distanceTransform(cb, cv2.DIST_L2, 5)
        cm, signed = self.get(('iso', a['id'], a['text']), make)
        RW, RH = e['region'][2], e['region'][3]
        entry = ease30((t - .13) / 1.45)
        leave = ease30((t - 4.48) / 1.28)
        solid = ease30((t - 1.02) / .83) * (1 - ease30((t - 4.56) / .55))
        fill = cm.astype(np.float32) / 255.0 * solid
        rings = np.zeros((RH, RW), np.float32)
        rings_in = ease30((t - .11) / .16) * (1 - ease30((t - 1.48) / .58))
        rings_out = ease30((t - 4.52) / .24) * (1 - ease30((t - 5.45) / .36))
        if rings_in > .001 or rings_out > .001:
            for offset, weight in e['rings']:
                rad_in = 1.4 + offset * (1 - entry)
                rad_out = 1.4 + offset * leave
                if rings_in > .001:
                    np.maximum(rings, np.exp(-.5 * ((signed - rad_in) / 1.35) ** 2) * weight * rings_in, out=rings)
                if rings_out > .001:
                    np.maximum(rings, np.exp(-.5 * ((signed - rad_out) / 1.35) ** 2) * weight * rings_out, out=rings)
        self._region(bg, e, comp30([(rings, np.array(e['ringColor'][:3], np.float32)), (fill, np.array(a['color'][:3], np.float32))], e['region']))

    KINDS = {'contourMorph': 'contour_morph', 'elasticWarp': 'elastic_warp', 'arcToBaseline': 'arc_to_baseline',
             'perspectivePlane': 'perspective_plane', 'widthBloom': 'width_bloom', 'weightMorph': 'weight_morph',
             'spectralInk': 'spectral_ink', 'glyphPivot': 'glyph_pivot', 'mirrorUnfold': 'mirror_unfold',
             'slantMorph': 'slant_morph', 'counterRelease': 'counter_release', 'inverseTransit': 'inverse_transit',
             'depthCast': 'depth_cast', 'pleatUnfold': 'pleat_unfold', 'particleCoalesce': 'particle_coalesce',
             'contourTrace': 'contour_trace', 'characterReel': 'character_reel', 'isolineCollapse': 'isoline_collapse',
             'splitRegister': 'split_register', 'glyphTorsion': 'glyph_torsion', 'imprintPress': 'imprint_press',
             'pairKerning': 'pair_kerning', 'solidExtract': 'solid_extract', 'printRegister': 'print_register',
             'tidalInk': 'tidal_ink', 'hatchEngrave': 'hatch_engrave', 'gridReflow': 'grid_reflow',
             'rippleWave': 'ripple_wave', 'signalWave': 'signal_wave'}

    def draw_direct(self, canvas, t):
        for a in self.sc.actors:
            e = a.get('effect')
            if a['type'] == 'text' and e and e.get('type') in self.KINDS:
                getattr(self, self.KINDS[e['type']])(canvas, a, e, t)

    def draw(self, t, canvas=None):
        if canvas is not None:
            out = np.array(canvas.convert('RGB'))
            self.draw_direct(out, t)
            return Image.fromarray(out, 'RGB')
        k = np.zeros((H, W, 3), np.uint8)
        self.draw_direct(k, t)
        a = k.max(axis=2)
        rgb = np.where(a[..., None] > 0, np.clip(k.astype(np.float32) * 255 / np.maximum(a[..., None], 1), 0, 255), 0)
        return Image.fromarray(np.dstack([rgb, a]).astype(np.uint8), 'RGBA')
