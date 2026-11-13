"""Audit GPU effect/transition packages against Dafin's own loader rules.

Mirrors src/engine/packages/GpuPackageParse.cpp, EffectPackageLoader.cpp and
TransitionPackageLoader.cpp (read 2026-11-13), then compiles every pass with
glslangValidator after the same prelude splicing GlRuntime.cpp does.
This is a stand-in for the real loader, not a substitute for running Dafin.

usage: python3 -I gpu_audit.py <repo> <out.csv> <pkgdir>...
"""
import csv, glob, hashlib, json, os, re, subprocess, sys, tempfile

repo, out_csv, pkg_roots = sys.argv[1], sys.argv[2], sys.argv[3:]

rt = open(os.path.join(repo, 'src/engine/gpu/GlRuntime.cpp'), encoding='utf-8').read()
def prelude(name):
    m = re.search(r'constexpr const char \*' + name + r' = R"\((.*?)\)";', rt, re.S)
    return m.group(1)
DEPTH, MASK = prelude('kDepthPrelude'), prelude('kMaskPrelude')

TYPES = {'bool', 'boolean', 'color', 'colour', 'file', 'clip', 'point', 'vec2', 'choice', 'enum', 'int',
         'integer', 'float', 'number', 'seed', 'region', 'image', 'gradient', 'curve', 'lut', 'track',
         'trackMask', 'audio'}
FEATURES = {'param:int', 'param:point', 'param:choice', 'param:seed', 'param:region', 'param:image',
            'param:gradient', 'param:curve', 'param:lut', 'curve:spline', 'param:track', 'param:trackMask',
            'param:audio', 'param:color-alpha', 'uniform:audio', 'requires:mask', 'requires:depth',
            'requires:face'}
ENGINE = {'u_resolution', 'u_time', 'u_timeUs', 'u_frameIndex', 'u_frameRate', 'u_currentTexture',
          'u_progress', 'u_fromTexture', 'u_toTexture', 'u_hasDepth', 'u_templateBounds', 'u_clipMask',
          'u_hasClipMask', 'u_meshAspect'}
HEX = re.compile(r'^#[0-9a-fA-F]{6}$|^#[0-9a-fA-F]{8}$|^#[0-9a-fA-F]{3}$')

def reserved(name):
    return (name in ENGINE or name.startswith('u_depth') or name.startswith('u_texture')
            or name.startswith('u_face') or name in ('u_audioLevel', 'u_audioBass', 'u_audioBeat'))

def check_params(params):
    errs = []
    keys = []
    for p in params:
        key = p.get('identifier') or p.get('key') or ''
        t = p.get('type', 'float')
        if not key:
            errs.append('parameter missing identifier'); continue
        keys.append(key)
        if t not in TYPES:
            d = str(p.get('defaultValue') or p.get('default') or '')
            if not (d.startswith('#') and HEX.match(d)):
                errs.append(f"parameter '{key}' has unknown type '{t}'")
        if t in ('choice', 'enum') and len(p.get('options', [])) < 2:
            errs.append(f"parameter '{key}' needs at least two options")
        if t in ('color', 'colour'):
            d = str(p.get('defaultValue') or p.get('default') or '')
            if not HEX.match(d[:7] if p.get('alpha') and len(d) == 9 else d):
                errs.append(f"parameter '{key}' has an invalid colour default '{d}'")
            for s in p.get('swatches', []):
                if not HEX.match(str(s)):
                    errs.append(f"parameter '{key}' has an invalid swatch '{s}'")
        if t == 'region' and p.get('shape', 'rect') not in ('rect', 'ellipse'):
            errs.append(f"parameter '{key}' has an unknown region shape")
        if t not in ('file', 'clip') and reserved(key):
            errs.append(f"parameter '{key}' collides with reserved uniform")
        if t in ('float', 'number', 'int', 'integer'):
            lo, hi, dv = p.get('minValue', p.get('min', 0)), p.get('maxValue', p.get('max', 1)), p.get('defaultValue', p.get('default', 0))
            try:
                if not (float(lo) <= float(dv) <= float(hi)):
                    errs.append(f"WARN default of '{key}' ({dv}) outside {lo}..{hi}")
            except (TypeError, ValueError):
                pass
    for p in params:
        en = p.get('enables')
        if en and not any((q.get('identifier') or q.get('key')) == en and q.get('type') in ('bool', 'boolean') for q in params):
            errs.append(f"parameter enables '{en}', which is not a bool parameter")
    if len(set(keys)) != len(keys):
        errs.append('duplicate parameter identifiers')
    return errs, keys

def compile_frag(src, pre):
    body = src
    if body.startswith('#version'):
        body = body.split('\n', 1)[1] if '\n' in body else ''
    text = '#version 330 core\n' + pre + body
    with tempfile.NamedTemporaryFile('w', suffix='.frag', delete=False) as f:
        f.write(text); name = f.name
    r = subprocess.run(['glslangValidator', name], capture_output=True, text=True)
    os.unlink(name)
    msg = ' '.join(l.strip() for l in r.stdout.splitlines() if 'ERROR' in l or 'WARNING' in l)
    return r.returncode == 0, msg.replace(name, '')[:300]

def audit(pkg_dir, kind):
    jf = os.path.join(pkg_dir, 'effect.json' if kind == 'effect' else 'transition.json')
    row = {'kind': kind, 'path': pkg_dir, 'id': '', 'displayName': '', 'category': '', 'requires': '',
           'loader': 'OK', 'loader_errors': '', 'glsl': '', 'glsl_errors': '', 'unbound_uniforms': '',
           'thumbnail': '', 'shader_sha': '', 'params': ''}
    try:
        root = json.load(open(jf, encoding='utf-8'))
    except Exception as e:
        row['loader'] = 'FAIL'; row['loader_errors'] = f'invalid JSON: {e}'; return row
    errs = []
    row['id'] = root.get('id', ''); row['displayName'] = root.get('displayName', '')
    row['category'] = root.get('category', '')
    req = root.get('requires', [])
    req = req if isinstance(req, list) else ([req] if req else [])
    row['requires'] = '+'.join(req)
    if kind == 'effect':
        b = root.get('backend', '')
        if b not in ('gpu', 'libav', 'compositor', 'model3d', 'faceswap'):
            errs.append(f"unsupported backend '{b}'" if b else 'effect.json missing backend')
        for r in req:
            if r not in ('face', 'depth', 'mask'):
                errs.append(f"unsupported requires '{r}'")
    else:
        if not row['id']:
            errs.append('transition.json missing id')
        if root.get('audioCurve', 'crossfade') not in ('crossfade', 'dip', 'hold'):
            errs.append('unknown audioCurve')
    bad = [f for f in root.get('nextFeatures', []) if not f.startswith('node:') and f not in FEATURES]
    if bad:
        errs.append('needs a newer Drift: ' + ','.join(bad))
    perrs, keys = check_params(root.get('parameters', []))
    errs += perrs
    row['params'] = len(keys)
    pipe = root.get('pipeline', {})
    bufs = {b.get('id') for b in pipe.get('intermediateBuffers', [])}
    texs = {t.get('id') for t in pipe.get('textures', [])}
    for t in pipe.get('textures', []):
        if not os.path.exists(os.path.join(pkg_dir, t.get('file', ''))):
            errs.append(f"missing texture file '{t.get('file')}'")
    passes = pipe.get('passes', [])
    gpu = kind == 'transition' or root.get('backend') == 'gpu'
    if gpu and not passes:
        errs.append('pipeline has no passes')
    maxsrc = 0 if kind == 'effect' else 1
    pre = (DEPTH if 'depth' in req and kind == 'effect' else '') + (MASK if 'mask' in req else '')
    glsl_ok, glsl_msgs, shas, unbound = True, [], [], set()
    fixed = set(root.get('fixedParams', {}).keys())
    for i, p in enumerate(passes):
        fs = p.get('fragmentShader', '')
        path = os.path.join(pkg_dir, fs)
        if not fs or not os.path.exists(path):
            errs.append(f'pass {i} missing shader {fs}'); glsl_ok = False; continue
        for inp in p.get('inputs', []):
            t = inp.get('type')
            if t == 'source_texture' and not (0 <= inp.get('index', 0) <= maxsrc):
                errs.append('source_texture index out of range')
            elif t == 'buffer' and inp.get('id') not in bufs:
                errs.append(f"unknown buffer '{inp.get('id')}'")
            elif t == 'texture' and inp.get('id') not in texs:
                errs.append(f"unknown texture '{inp.get('id')}'")
            elif t not in ('source_texture', 'buffer', 'texture'):
                errs.append(f"unknown pass input type '{t}'")
        o = p.get('output') or {}
        if not o and i != len(passes) - 1:
            errs.append(f'pass {i} missing output')
        if o.get('type') == 'buffer' and o.get('id') not in bufs:
            errs.append('pass output references unknown buffer')
        if o and o.get('type') not in ('canvas', 'buffer'):
            errs.append('unknown pass output type')
        geo = p.get('geometry', 'quad')
        if geo not in ('quad', 'face111'):
            errs.append(f'unknown geometry {geo}')
        src = open(path, encoding='utf-8').read()
        shas.append(hashlib.sha256(src.encode()).hexdigest()[:12])
        ok, msg = compile_frag(src, pre)
        glsl_ok &= ok
        if msg:
            glsl_msgs.append(f'{fs}: {msg}')
        for u in re.findall(r'\buniform\s+\w+\s+(\w+)', src):
            if u not in keys and u not in fixed and not reserved(u) and not u.startswith('u_texture'):
                unbound.add(u)
    hard = [e for e in errs if not e.startswith('WARN')]
    row['loader'] = 'FAIL' if hard else ('WARN' if errs else 'OK')
    row['loader_errors'] = '; '.join(errs)
    row['glsl'] = 'OK' if glsl_ok else 'FAIL'
    row['glsl_errors'] = ' | '.join(glsl_msgs)
    row['unbound_uniforms'] = ','.join(sorted(unbound))
    th = root.get('thumbnail') or root.get('previewStrip') or ''
    row['thumbnail'] = th + ('' if not th or os.path.exists(os.path.join(pkg_dir, th)) else ' (MISSING)')
    row['shader_sha'] = '+'.join(shas)
    return row

rows = []
for r in pkg_roots:
    for jf in sorted(glob.glob(os.path.join(r, '**', 'effect.json'), recursive=True)):
        rows.append(audit(os.path.dirname(jf), 'effect'))
    for jf in sorted(glob.glob(os.path.join(r, '**', 'transition.json'), recursive=True)):
        rows.append(audit(os.path.dirname(jf), 'transition'))
with open(out_csv, 'w', newline='', encoding='utf-8') as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
    w.writeheader(); w.writerows(rows)
from collections import Counter
print(len(rows), 'packages')
print('loader', Counter((r['kind'], r['loader']) for r in rows))
print('glsl', Counter((r['kind'], r['glsl']) for r in rows))
