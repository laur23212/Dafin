"""Run single-pass Dafin GPU packages on Mesa (llvmpipe) the way GpuEffectExecutor binds them,
check behaviour, and render thumbnails / preview strips.

Not Dafin: same .frag source, same uniform names/types, top-left uv; no compositor, no
premultiply handling, no real clip layers. Results are PRELIMINARY.

usage: python3 -I gl_harness.py <out_dir> <kind>:<package_dir>[:<thumb_overrides_json>] ...
"""
import json, os, sys
import numpy as np
import moderngl
from PIL import Image

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
ctx = moderngl.create_standalone_context(backend='egl', require=330)
VS = """#version 330 core
in vec2 pos; out vec2 v_texCoord;
void main(){ v_texCoord = pos * 0.5 + 0.5; gl_Position = vec4(pos, 0.0, 1.0); }"""
quad = ctx.buffer(np.array([-1, -1, 1, -1, -1, 1, 1, 1], dtype='f4'))


def scene(w, h, variant):
    """Procedural, licence-free test picture (row 0 = top)."""
    y, x = np.mgrid[0:h, 0:w].astype('f4')
    u, v = x / w, y / h
    img = np.zeros((h, w, 4), 'f4'); img[..., 3] = 1
    if variant == 0:
        sky = np.stack([0.95 - 0.35 * v, 0.62 - 0.2 * v, 0.38 + 0.25 * v], -1)
        ground = np.stack([0.22 + 0 * v, 0.30 + 0.1 * u, 0.16 + 0 * v], -1)
    else:
        sky = np.stack([0.10 + 0.15 * v, 0.28 + 0.25 * v, 0.55 + 0.25 * u], -1)
        ground = np.stack([0.40 + 0.1 * u, 0.36 + 0 * v, 0.50 + 0 * v], -1)
    horizon = 0.62
    img[..., :3] = np.where((v < horizon)[..., None], sky, ground)
    grid = ((np.abs((u * 8) % 1 - 0.5) > 0.47) | (np.abs((v * 8) % 1 - 0.5) > 0.47))
    img[..., :3] = np.where(grid[..., None], img[..., :3] * 0.55 + 0.45 * np.array([1, 1, 1], 'f4'), img[..., :3])
    patches = [(0.18, 0.75, (0.85, 0.12, 0.10)), (0.40, 0.75, (0.35, 0.55, 0.52)), (0.62, 0.75, (0.88, 0.66, 0.52)), (0.84, 0.75, (0.30, 0.30, 0.75))]
    if variant == 1:
        patches = [(p[0], 0.3, p[2][::-1]) for p in patches]
    for cx, cy, col in patches:
        m = (np.abs(u - cx) < 0.08) & (np.abs(v - cy) < 0.08)
        img[m, :3] = col
    cx, cy = (0.5, 0.38) if variant == 0 else (0.5, 0.7)
    disc = ((u - cx) * w / h) ** 2 + (v - cy) ** 2 < 0.12 ** 2
    img[disc, :3] = (0.98, 0.92, 0.70) if variant == 0 else (0.15, 0.85, 0.75)
    return img


def tex(img):
    h, w = img.shape[:2]
    t = ctx.texture((w, h), 4, (np.clip(img, 0, 1) * 255 + 0.5).astype('u1').tobytes())
    t.filter = (moderngl.LINEAR, moderngl.LINEAR)
    t.repeat_x = t.repeat_y = False
    return t


def defaults(params, overrides=None):
    vals = {}
    for p in params:
        k = p.get('identifier') or p.get('key'); t = p.get('type', 'float')
        vals[k] = (t, p.get('defaultValue', p.get('default')))
    for k, v in (overrides or {}).items():
        vals[k] = (vals[k][0], v)
    return vals


def synth_maps(w, h):
    """Stand-in subject data for card art only: the sun disc is the subject/face, lower = nearer."""
    y, x = np.mgrid[0:h, 0:w].astype('f4'); u, v = x / w, y / h
    d = np.sqrt(((u - 0.5) * w / h) ** 2 + (v - 0.38) ** 2)
    m = np.clip((0.16 - d) / 0.03, 0, 1)
    dep = np.maximum(m * 0.95, np.clip(v, 0, 1) * 0.7)
    def g(a):
        o = np.zeros((h, w, 4), 'f4'); o[..., 0] = o[..., 1] = o[..., 2] = a; o[..., 3] = 1; return o
    return g(m), g(dep)


def render(pkg, kind, imgs, values, progress=0.0, frame=0, fps=30.0, synth=False):
    meta = json.load(open(os.path.join(pkg, kind + '.json')))
    passes = meta['pipeline']['passes']
    assert len(passes) == 1, 'harness handles single-pass packages only'
    src = open(os.path.join(pkg, passes[0]['fragmentShader'])).read()
    req = meta.get('requires', [])
    req = req if isinstance(req, list) else [req]
    if 'mask' in req or ('depth' in req and kind == 'effect'):
        import re as _re
        rt = open(os.environ['DAFIN_REPO'] + '/src/engine/gpu/GlRuntime.cpp').read()
        pre = ''
        if 'depth' in req and kind == 'effect':
            pre += _re.search(r'kDepthPrelude = R"\((.*?)\)";', rt, _re.S).group(1)
        if 'mask' in req:
            pre += _re.search(r'kMaskPrelude = R"\((.*?)\)";', rt, _re.S).group(1)
        body = src.split('\n', 1)[1] if src.startswith('#version') else src
        src = '#version 330 core\n' + pre + body
    prog = ctx.program(vertex_shader=VS, fragment_shader=src)
    h, w = imgs[0].shape[:2]
    fbo = ctx.simple_framebuffer((w, h), components=4, dtype='f4')
    fbo.use(); fbo.clear(0, 0, 0, 0)
    texs = [tex(i) for i in imgs]
    names = ['u_currentTexture', 'u_fromTexture', 'u_toTexture', 'u_texture1']
    binds = {'u_currentTexture': 0, 'u_fromTexture': 0, 'u_toTexture': min(1, len(texs) - 1), 'u_texture1': min(1, len(texs) - 1)}
    for i, t in enumerate(texs):
        t.use(i)
    for n in names:
        if n in prog:
            prog[n].value = binds[n]
    eng = {'u_resolution': (float(w), float(h)), 'u_progress': float(progress), 'u_time': frame / fps,
           'u_frameIndex': int(frame), 'u_frameRate': float(fps), 'u_timeUs': int(frame / fps * 1e6)}
    for n, v in eng.items():
        if n in prog:
            prog[n].value = v
    if synth:
        mk, dp = synth_maps(w, h)
        extra = [tex(mk), tex(dp)]; extra[0].use(5); extra[1].use(6); texs += extra
        ar = w / h
        sv = {'u_clipMask': 5, 'u_hasClipMask': 1.0, 'u_depthTexture': 6, 'u_hasDepth': 1.0,
              'u_depthResolution': (float(w), float(h)), 'u_faceValid': 1.0, 'u_faceCenterX': 0.5,
              'u_faceCenterY': 0.38, 'u_faceRx': 0.12 / ar, 'u_faceRy': 0.12, 'u_faceLeftEyeX': 0.5 - 0.05 / ar,
              'u_faceLeftEyeY': 0.35, 'u_faceRightEyeX': 0.5 + 0.05 / ar, 'u_faceRightEyeY': 0.35, 'u_faceEyeRadius': 0.015}
        for n, v in sv.items():
            if n in prog:
                prog[n].value = v
    for k, (t, v) in values.items():
        if k not in prog:
            continue
        if t in ('vec2', 'point'):
            prog[k].value = tuple(float(a) for a in v)
        elif t in ('bool', 'boolean'):
            prog[k].value = 1.0 if v else 0.0
        elif t in ('color', 'colour'):
            hx = str(v).lstrip('#'); c = [int(hx[i:i + 2], 16) / 255 for i in range(0, len(hx), 2)]
            if len(c) == 3: c.append(1.0)
            prog[k].value = tuple(c[:prog[k].dimension])
        else:
            try:
                prog[k].value = float(v)
            except (TypeError, ValueError):
                prog[k].value = 0.0
    vao = ctx.vertex_array(prog, [(quad, '2f', 'pos')])
    vao.render(moderngl.TRIANGLE_STRIP)
    out = np.frombuffer(fbo.read(components=4, dtype='f4'), 'f4').reshape(h, w, 4).copy()
    for t in texs: t.release()
    vao.release(); prog.release(); fbo.release()
    return out


def to_img(a):
    # cards show transparency over a checkerboard, so alpha-only effects still read
    h, w = a.shape[:2]; y, x = np.mgrid[0:h, 0:w]
    chk = np.where(((x // 16 + y // 16) % 2)[..., None] == 0, 0.78, 0.55).astype('f4')
    al = np.clip(a[..., 3:4], 0, 1); a = a.copy(); a[..., :3] = a[..., :3] * al + chk * (1 - al); a[..., 3] = 1
    return Image.fromarray((np.clip(a, 0, 1) * 255 + 0.5).astype('u1'), 'RGBA')


results = {}
for spec in sys.argv[2:]:
    kind, pkg, *rest = spec.split(':', 2)
    show = json.loads(rest[0]) if rest else {}
    meta = json.load(open(os.path.join(pkg, kind + '.json')))
    pid = meta['id']
    params = meta.get('parameters', [])
    r = {'id': pid, 'checks': {}}
    A, B = scene(320, 180, 0), scene(320, 180, 1)
    vals = defaults(params, show if kind == 'transition' else None)
    if kind == 'effect':
        o1 = render(pkg, kind, [A], vals, frame=12)
        o2 = render(pkg, kind, [A], vals, frame=12)
        r['checks']['finite'] = bool(np.isfinite(o1).all())
        r['checks']['repeatable_same_frame'] = bool(np.array_equal(o1, o2))
        r['checks']['changes_picture_at_defaults'] = float(np.abs(o1 - A).mean())
        r['checks']['alpha_preserved'] = float(np.abs(o1[..., 3] - A[..., 3]).max())
        o3 = render(pkg, kind, [A], vals, frame=13)
        r['checks']['animates_between_frames'] = float(np.abs(o3 - o1).mean())
        # 24/30/60 fps: same timeline second must give the same picture when the shader uses time
        s24 = render(pkg, kind, [A], vals, frame=24, fps=24); s60 = render(pkg, kind, [A], vals, frame=60, fps=60)
        r['checks']['same_second_24_vs_60fps_diff'] = float(np.abs(s24 - s60).mean())
        Ta = A.copy(); Ta[:, :40, 3] = 0; Ta[:, :40, :3] = 0
        ot = render(pkg, kind, [Ta], vals, frame=12)
        r['checks']['transparent_input_finite'] = bool(np.isfinite(ot).all())
    else:
        p0 = render(pkg, kind, [A, B], vals, progress=0.0)
        p1 = render(pkg, kind, [A, B], vals, progress=1.0)
        q = 1 / 255
        r['checks']['progress0_equals_from'] = float(np.abs(p0 - A).max())
        r['checks']['progress1_equals_to'] = float(np.abs(p1 - B).max())
        mids = [render(pkg, kind, [A, B], vals, progress=p) for p in (0.25, 0.5, 0.75, 0.99)]
        r['checks']['finite'] = bool(all(np.isfinite(m).all() for m in mids))
        r['checks']['progress0.99_left_from_pixels'] = float((np.abs(mids[3] - A).sum(-1) < 0.02).mean())
        r['checks']['repeatable'] = bool(np.array_equal(mids[1], render(pkg, kind, [A, B], vals, progress=0.5)))
    # every float parameter at its min and max stays finite
    sweep = True
    for p in params:
        k = p.get('identifier'); t = p.get('type', 'float')
        if t not in ('float', 'int', 'number', 'integer'):
            continue
        for v in (p.get('minValue', 0), p.get('maxValue', 1)):
            vv = dict(vals); vv[k] = (t, v)
            o = render(pkg, kind, [A] if kind == 'effect' else [A, B], vv, progress=0.5, frame=7)
            sweep &= bool(np.isfinite(o).all())
    r['checks']['param_min_max_finite'] = sweep
    # thumbnails in Dafin's shapes: effects 256x256 card, transitions 12 cells of 128 px
    slug = os.path.basename(pkg.rstrip('/'))
    sv = defaults(params, show)
    if kind == 'effect':
        base = scene(256, 256, 0)
        to_img(render(pkg, kind, [base], sv, frame=9, synth=bool(os.environ.get('SYNTH')))).convert('RGB').save(os.path.join(OUT, slug + '.thumbnail.webp'), 'WEBP', quality=82, method=6)
    else:
        a, b = scene(128, 128, 0), scene(128, 128, 1)
        cells = [render(pkg, kind, [a, b], sv, progress=i / 11, synth=bool(os.environ.get('SYNTH'))) for i in range(12)]
        to_img(np.concatenate(cells, 1)).convert('RGB').save(os.path.join(OUT, slug + '.preview_strip.webp'), 'WEBP', quality=82, method=6)
    results[pid + ('' if not show else ' ' + json.dumps(show))] = r
json.dump(results, open(os.path.join(OUT, 'harness_results.json'), 'w'), indent=1)
print(json.dumps(results, indent=1))
