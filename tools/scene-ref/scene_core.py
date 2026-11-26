"""Scene Title v1: loading, expressions, timing and layout (no pixels here).

Spec: docs/scene-titles.md. Everything in this module is deterministic and pure Python, so the
native Dafin implementation can be checked against it value by value.
"""
from __future__ import annotations

import ast
import copy
import json
import math
import operator
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

FORMAT_ID = 'dafin.scene-title'
FORMAT_VERSION = 1
ACTOR_TYPES = {'text', 'rect', 'roundRect', 'line', 'route', 'ellipse', 'ring', 'circle', 'group', 'repeat', 'keycaps', 'pin'}
RESERVED_EFFECTS = {'sdfMorph', 'maskedLight', 'focusMix'}
PROFILES = {'sprite2x', 'mask1x', 'pil1x', 'cov1x', 'fx1x', 'gold1x'}


class SceneError(ValueError):
    pass


# ----------------------------------------------------------------------------------------------
# Easing. Every curve takes u in [0,1] (clamped) and returns [0,1] with exact endpoints.

def clamp01(u: float) -> float:
    return max(0.0, min(1.0, float(u)))


def cubic_bezier(a, b, c, d, t):
    mt = 1.0 - t
    return mt * mt * mt * a + 3.0 * mt * mt * t * b + 3.0 * mt * t * t * c + t * t * t * d


def bezier_parameter_for_x(x1, x2, x):
    """Same bisection as src/core/base/Bezier.h (24 halvings)."""
    lo, hi = 0.0, 1.0
    for _ in range(24):
        mid = 0.5 * (lo + hi)
        if cubic_bezier(0.0, x1, x2, 1.0, mid) < x:
            lo = mid
        else:
            hi = mid
    return 0.5 * (lo + hi)


def bezier_ease(x1, y1, x2, y2):
    def f(u):
        u = clamp01(u)
        if u in (0.0, 1.0):
            return u
        return cubic_bezier(0.0, y1, y2, 1.0, bezier_parameter_for_x(x1, x2, u))
    return f


# Named curves. The first two are exact cubic beziers in Dafin's keyframe model:
#   smoothstep   = cubic-bezier(1/3, 0, 2/3, 1)  -> 3u^2 - 2u^3
#   easeOutCubic = cubic-bezier(1/3, 1, 2/3, 1)  -> 1 - (1-u)^3
# easeOutQuart has no exact cubic-bezier form (native needs a polynomial easing function).
EASES = {
    'linear': lambda u: clamp01(u),
    'smoothstep': lambda u: (lambda x: x * x * (3 - 2 * x))(clamp01(u)),
    'easeOutCubic': lambda u: 1 - (1 - clamp01(u)) ** 3,
    'easeOutQuart': lambda u: 1 - (1 - clamp01(u)) ** 4,
    'easeInCubic': lambda u: clamp01(u) ** 3,
    # Stage03b C revision's ease5: x*x*x*(x*(6*x-15)+10), evaluated in that order
    'smootherstep': lambda u: (lambda x: x * x * x * (x * (6 * x - 15) + 10))(clamp01(u)),
}
EASE_BEZIER = {'smoothstep': (1 / 3, 0, 2 / 3, 1), 'easeOutCubic': (1 / 3, 1, 2 / 3, 1)}


def ease_fn(spec):
    if spec is None:
        return EASES['smoothstep']
    if isinstance(spec, str):
        if spec in EASES:
            return EASES[spec]
        m = re.fullmatch(r'cubic-bezier\(([^)]*)\)', spec.replace(' ', ''))
        if m:
            v = [float(x) for x in m.group(1).split(',')]
            if len(v) == 4:
                return bezier_ease(*v)
    raise SceneError(f'unknown ease {spec!r}')


def phase(t, start, dur, ease, delay=0.0):
    """Progress of one timed block: 0 before start, 1 after start+dur. `delay` (a per-copy stagger,
    Stage11 Arc Resolve) is taken off after the start, as ((t − start) − delay) / dur."""
    if delay:
        x = (t - start) - delay
        return (1.0 if x >= 0 else 0.0) if dur <= 0 else ease_fn(ease)(x / dur)
    if dur <= 0:
        return 1.0 if t >= start else 0.0
    return ease_fn(ease)((t - start) / dur)


# ----------------------------------------------------------------------------------------------
# Expressions: {"expr": "..."} anywhere a number, colour or string is expected. A tiny, safe
# subset of Python expression syntax: numbers, names, + - * / // % **, comparisons, and/or/not,
# "a if c else b", and the calls below. Names: params, palette entries, repeat variables.

_BIN = {ast.Add: operator.add, ast.Sub: operator.sub, ast.Mult: operator.mul,
        ast.Div: operator.truediv, ast.FloorDiv: operator.floordiv, ast.Mod: operator.mod,
        ast.Pow: operator.pow}
_CMP = {ast.Lt: operator.lt, ast.LtE: operator.le, ast.Gt: operator.gt, ast.GtE: operator.ge,
        ast.Eq: operator.eq, ast.NotEq: operator.ne}
_FUNCS = {'min': min, 'max': max, 'abs': abs, 'round': round, 'int': int, 'float': float,
          'floor': math.floor, 'ceil': math.ceil, 'sin': math.sin, 'cos': math.cos}


def eval_expr(src: str, env: dict, funcs: dict | None = None):
    funcs = {**_FUNCS, **(funcs or {})}

    def ev(n):
        if isinstance(n, ast.Expression):
            return ev(n.body)
        if isinstance(n, ast.Constant) and isinstance(n.value, (int, float, str)):
            return n.value
        if isinstance(n, ast.Name):
            if n.id in env:
                return env[n.id]
            raise SceneError(f'unknown name {n.id!r} in expression {src!r}')
        if isinstance(n, ast.BinOp) and type(n.op) in _BIN:
            return _BIN[type(n.op)](ev(n.left), ev(n.right))
        if isinstance(n, ast.UnaryOp) and isinstance(n.op, (ast.USub, ast.UAdd, ast.Not)):
            v = ev(n.operand)
            return -v if isinstance(n.op, ast.USub) else (+v if isinstance(n.op, ast.UAdd) else not v)
        if isinstance(n, ast.Compare):
            left = ev(n.left)
            for op, right in zip(n.ops, n.comparators):
                r = ev(right)
                if type(op) not in _CMP or not _CMP[type(op)](left, r):
                    return False
                left = r
            return True
        if isinstance(n, ast.BoolOp):
            vals = [ev(v) for v in n.values]
            return all(vals) if isinstance(n.op, ast.And) else any(vals)
        if isinstance(n, ast.IfExp):
            return ev(n.body) if ev(n.test) else ev(n.orelse)
        if isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and n.func.id in funcs and not n.keywords:
            return funcs[n.func.id](*[ev(a) for a in n.args])
        raise SceneError(f'unsupported expression {src!r}')

    try:
        tree = ast.parse(src, mode='eval')
    except SyntaxError as e:
        raise SceneError(f'bad expression {src!r}: {e}') from None
    return ev(tree)


_FMT = re.compile(r'\{([A-Za-z_][A-Za-z0-9_]*)(?::([^{}]*))?\}')


def format_text(template: str, env: dict) -> str:
    """'STEP {currentStep:02}' -> 'STEP 02'. Only plain names; '{{' and '}}' are literal braces."""
    out, i = [], 0
    while i < len(template):
        if template.startswith('{{', i):
            out.append('{'); i += 2; continue
        if template.startswith('}}', i):
            out.append('}'); i += 2; continue
        m = _FMT.match(template, i)
        if m:
            name, spec = m.group(1), m.group(2) or ''
            if name not in env:
                raise SceneError(f'unknown name {name!r} in text {template!r}')
            out.append(format(env[name], spec))
            i = m.end(); continue
        out.append(template[i]); i += 1
    return ''.join(out)


# ----------------------------------------------------------------------------------------------
# Colours

def parse_color(v, palette) -> tuple:
    if isinstance(v, str):
        if v in palette:
            return parse_color(palette[v], palette)
        s = v.lstrip('#')
        if len(s) in (6, 8) and all(c in '0123456789abcdefABCDEF' for c in s):
            return tuple(int(s[k:k + 2], 16) for k in range(0, len(s), 2))
        raise SceneError(f'unknown colour {v!r}')
    if isinstance(v, (list, tuple)) and len(v) in (3, 4):
        return tuple(int(x) for x in v)
    raise SceneError(f'bad colour {v!r}')


# ----------------------------------------------------------------------------------------------
# Scene loading

@dataclass
class Scene:
    raw: dict
    path: Path | None
    params: dict
    palette: dict
    actors: list = field(default_factory=list)  # resolved, flat, draw order
    keycap_blocks: list = field(default_factory=list)  # Smart Keycaps blocks, see keycaps.py

    @property
    def width(self): return int(self.raw['canvas']['width'])

    @property
    def height(self): return int(self.raw['canvas']['height'])

    @property
    def profile(self): return self.raw.get('render', {}).get('profile', 'sprite2x')

    @property
    def design_duration(self): return float(self.raw['timing']['duration'])

    @property
    def design_fps(self): return float(self.raw['timing'].get('fps', 25))


def _check_param(p, value):
    t = p.get('type', 'text')
    if t == 'int':
        if type(value) is not int:
            raise SceneError(f'param {p["id"]} must be an integer')
    elif t == 'number':
        if not isinstance(value, (int, float)):
            raise SceneError(f'param {p["id"]} must be a number')
    elif t == 'bool':
        if not isinstance(value, bool):
            raise SceneError(f'param {p["id"]} must be true/false')
    elif t in ('text', 'color', 'enum'):
        if not isinstance(value, (str, list)):
            raise SceneError(f'param {p["id"]} has a bad value')
        if t == 'enum' and value not in p.get('options', []):
            raise SceneError(f'param {p["id"]} must be one of {p.get("options")}')
    else:
        raise SceneError(f'param {p["id"]}: unknown type {t}')


def resolve_params(raw: dict, overrides: dict | None) -> dict:
    overrides = dict(overrides or {})
    spec = raw.get('params', [])
    known = {p['id'] for p in spec}
    for k in overrides:
        if k not in known:
            raise SceneError(f'unknown param {k!r}')
    values = {}
    for p in spec:  # declared order: later params may bound by earlier ones
        v = overrides.get(p['id'], p.get('default'))
        _check_param(p, v)
        values[p['id']] = v
    for p in spec:  # bounds may refer to any param
        v = values[p['id']]
        for key, cmp in (('min', operator.lt), ('max', operator.gt)):
            if key in p:
                b = p[key]
                b = eval_expr(b, values) if isinstance(b, str) else b
                if cmp(v, b):
                    if p.get('clamp', False):
                        values[p['id']] = v = b
                    else:
                        raise SceneError(f'param {p["id"]}={v} is outside {key} {b}')
    return values


def load_scene(path_or_dict, overrides: dict | None = None, measure=None) -> Scene:
    if isinstance(path_or_dict, dict):
        raw, path = copy.deepcopy(path_or_dict), None
    else:
        path = Path(path_or_dict)
        raw = json.loads(path.read_text(encoding='utf-8'))
    if raw.get('format') != FORMAT_ID:
        raise SceneError(f'not a scene title (format must be {FORMAT_ID!r})')
    if raw.get('version') != FORMAT_VERSION:
        raise SceneError(f'unsupported version {raw.get("version")!r}')
    for key in ('id', 'canvas', 'timing', 'actors'):
        if key not in raw:
            raise SceneError(f'missing {key!r}')
    prof = raw.get('render', {}).get('profile', 'sprite2x')
    if prof not in PROFILES:
        raise SceneError(f'unknown render profile {prof!r}')
    params = resolve_params(raw, overrides)
    palette = dict(raw.get('palette', {}))
    sc = Scene(raw=raw, path=path, params=params, palette=palette)
    sc.actors = expand_actors(raw['actors'], sc)
    ids = [a['id'] for a in sc.actors]
    by_id = {a['id']: a for a in sc.actors}
    for a in sc.actors:  # "sync": take in/out from another actor (a text never outlives its strap)
        s = a.get('sync')
        if s:
            if s not in by_id:
                raise SceneError(f'{a["id"]}: sync with unknown actor {s!r}')
            for k in ('in', 'out'):
                if k in by_id[s]:
                    a[k] = copy.deepcopy(by_id[s][k])
    resolve_align(sc.actors, by_id)
    dup = {i for i in ids if ids.count(i) > 1}
    if dup:
        raise SceneError(f'duplicate actor ids {sorted(dup)}')
    if measure is not None and sc.keycap_blocks:
        import keycaps  # phase B of the Smart Keycaps generator needs text metrics
        keycaps.layout(sc, measure)
    for a in sc.actors:
        for d in a.get('dependsOn', []):
            if d not in ids:
                raise SceneError(f'{a["id"]}: dependsOn unknown actor {d!r}')
    return sc


def _subst(node, env):
    """Evaluate {"expr"} nodes and format strings with repeat variables / params."""
    if isinstance(node, dict):
        if set(node) == {'expr'}:
            return eval_expr(node['expr'], env)
        return {k: (_subst(v, env) if k not in ('actor', 'children') else v) for k, v in node.items()}
    if isinstance(node, list):
        return [_subst(v, env) for v in node]
    return node


def expand_actors(actors, sc: Scene, env=None, parent=None, out=None):
    env = {**sc.palette, **sc.params, **(env or {})}
    out = [] if out is None else out
    for a in actors:
        if a.get('type') not in ACTOR_TYPES:
            raise SceneError(f'actor {a.get("id")!r}: unknown type {a.get("type")!r}')
        if 'id' not in a:
            raise SceneError('every actor needs an id')
        enabled = a.get('enabled', True)
        if isinstance(enabled, dict) or isinstance(enabled, str):
            enabled = eval_expr(enabled['expr'] if isinstance(enabled, dict) else enabled, env)
        if not enabled:
            if not a.get('optional', False):
                raise SceneError(f'actor {a["id"]} is disabled but not optional')
            continue
        if a['type'] == 'repeat' and 'over' in a:  # one copy per character (Stage11 Arc Resolve)
            var, cvar = a.get('as', 'i'), a.get('char', 'c')
            for i, ch in enumerate(format_text(a['over'], env)):
                sub = copy.deepcopy(a['actor'])
                local = {**env, var: i, cvar: ch}
                sub['id'] = format_text(sub['id'], local)
                expand_actors([sub], sc, {var: i, cvar: ch, **{k: v for k, v in (env or {}).items()}}, parent, out)
            continue
        if a['type'] == 'repeat':
            n = a['count']
            n = eval_expr(n['expr'], env) if isinstance(n, dict) else (eval_expr(n, env) if isinstance(n, str) else n)
            var = a.get('as', 'i')
            for i in range(int(n)):
                sub = copy.deepcopy(a['actor'])
                local = {**env, var: i}
                sub['id'] = format_text(sub['id'], local)
                expand_actors([sub], sc, {var: i, **{k: v for k, v in (env or {}).items()}}, parent, out)
            continue
        r = _subst({k: v for k, v in a.items()}, env)
        r['_env'] = env
        r['_parent'] = parent
        if r['type'] == 'keycaps':
            import keycaps  # phase A of the Smart Keycaps generator (docs/scene-keycaps.md)
            if parent is not None:
                raise SceneError(f'{r["id"]}: a keycaps block cannot sit inside a group')
            for k in ('shortcut', 'labelPolicy', 'orWord', 'thenWord'):  # text slots: params bind here
                if isinstance(r.get(k), str):
                    r[k] = format_text(r[k], env)
            if r.get('label'):
                r['label']['text'] = format_text(r['label'].get('text', ''), env)
            out.extend(keycaps.expand_block(r, sc, env))
            continue
        if r['type'] == 'text':
            r['text'] = format_text(r.get('text', ''), env)
        if 'effect' in r and r['effect'].get('type') in RESERVED_EFFECTS \
                and sc.raw.get('render', {}).get('profile') != 'fx1x':
            raise SceneError(f'{r["id"]}: effect {r["effect"]["type"]} is reserved for a later '
                             'version outside the fx1x profile (docs/scene-titles.md, Stage10)')
        if r['type'] == 'group':
            r['_childIds'] = []
            out.append(r)
            before = len(out)
            expand_actors(a.get('children', []), sc, env and {k: v for k, v in env.items()}, r['id'], out)
            r['_childIds'] = [x['id'] for x in out[before:] if x['_parent'] == r['id']]
            continue
        out.append(r)
    return out


# ----------------------------------------------------------------------------------------------
# Static alignment: "align": {"x": {"to": id, "edge": e, "offset": n}, "y": {...}}. Moves the
# actor's own anchor (text: at.baseline; shapes: box; lines: both points) so that its edge of the
# same kind lands on the target's edge plus offset. Text edges: start (pen x) and baseline;
# shape edges: left, right, top, bottom. Ink-dependent sizes are fitTo, not align.

_X_EDGES, _Y_EDGES = {'start', 'left', 'right'}, {'baseline', 'top', 'bottom'}


def _edge(a, edge):
    if a['type'] == 'text':
        bx, by = a['at']['baseline']
        return {'start': bx, 'left': bx, 'baseline': by, 'top': by, 'bottom': by}.get(edge)
    if 'box' in a:
        x0, y0, x1, y1 = a['box']
        return {'left': x0, 'start': x0, 'right': x1, 'top': y0, 'baseline': y1, 'bottom': y1}.get(edge)
    if a['type'] == 'line':
        (ax, ay), (bx, by) = a['from'], a['to']
        return {'left': min(ax, bx), 'start': ax, 'right': max(ax, bx), 'top': min(ay, by),
                'baseline': max(ay, by), 'bottom': max(ay, by)}.get(edge)
    return None


def _move(a, dx, dy):
    if a['type'] == 'text':
        bx, by = a['at']['baseline']; a['at'] = {'baseline': [bx + dx, by + dy]}
    elif 'box' in a:
        x0, y0, x1, y1 = a['box']; a['box'] = [x0 + dx, y0 + dy, x1 + dx, y1 + dy]
    elif a['type'] == 'line':
        a['from'] = [a['from'][0] + dx, a['from'][1] + dy]; a['to'] = [a['to'][0] + dx, a['to'][1] + dy]
    else:
        raise SceneError(f'{a["id"]}: align is not supported for {a["type"]}')


def resolve_align(actors, by_id):
    done, busy = set(), set()

    def visit(a):
        if a['id'] in done or 'align' not in a:
            done.add(a['id']); return
        if a['id'] in busy:
            raise SceneError(f'{a["id"]}: align cycle')
        busy.add(a['id'])
        for axis, edges in (('x', _X_EDGES), ('y', _Y_EDGES)):
            rule = a['align'].get(axis)
            if not rule:
                continue
            tgt = by_id.get(rule['to'])
            if tgt is None:
                raise SceneError(f'{a["id"]}: align to unknown actor {rule["to"]!r}')
            if rule['edge'] not in edges:
                raise SceneError(f'{a["id"]}: {rule["edge"]!r} is not an {axis} edge')
            visit(tgt)
            if a['type'] == 'text' and 'baseline' not in a['at']:
                raise SceneError(f'{a["id"]}: align needs at.baseline')
            want = _edge(tgt, rule['edge']) + rule.get('offset', 0)
            own = rule.get('ownEdge', rule['edge'])
            have = _edge(a, own)
            if want is None or have is None:
                raise SceneError(f'{a["id"]}: cannot align {own!r} to {rule["edge"]!r}')
            _move(a, want - have if axis == 'x' else 0, want - have if axis == 'y' else 0)
        busy.discard(a['id']); done.add(a['id'])

    for a in actors:
        visit(a)


# ----------------------------------------------------------------------------------------------
# Timing: stretching a scene to a clip length. Entrances stay anchored to the clip start, exits
# to the clip end, and the hold in between absorbs the difference. Shorter than the minimum
# (all entrances + all exits, no hold) the compacted timeline is scaled uniformly.

def scene_events(sc: Scene):
    ins, outs = [], []
    for a in sc.actors:
        for b in blocks(a.get('in')):
            ins.append(float(b['start']) + float(b.get('dur', 0)))
        for b in blocks(a.get('out')):
            outs.append(float(b['start']))
    for key in ('exitWipe', 'exitSlide'):  # scene-wide exits count too
        if sc.raw.get(key):
            outs.append(float(sc.raw[key]['start']))
    D0 = sc.design_duration
    hold_start = max(ins) if ins else 0.0
    hold_end = min(outs) if outs else D0
    if hold_end < hold_start:
        raise SceneError(f'exits start ({hold_end}s) before the last entrance ends ({hold_start}s)')
    return hold_start, hold_end


def time_map(sc: Scene, clip_duration: float | None):
    """Returns f(t_clip) -> t_design."""
    D0 = sc.design_duration
    if clip_duration is None or abs(clip_duration - D0) < 1e-9:
        return lambda t: t
    hs, he = scene_events(sc)
    D = float(clip_duration)
    tail = D0 - he
    dmin = hs + tail
    if D >= dmin:
        def f(t):
            if t <= hs:
                return t
            if t >= D - tail:
                return he + (t - (D - tail))
            # inside the hold: every actor is at rest, so any hold time is equivalent
            return hs + (t - hs) * ((he - hs) / max(1e-9, D - tail - hs)) if D - tail > hs else hs
        return f
    k = dmin / D if D > 0 else 1.0

    def g(t):
        u = t * k
        return u if u <= hs else he + (u - hs)
    return g


def cadence_time(sc: Scene, t: float, project_fps: float) -> float:
    c = sc.raw['timing'].get('cadence', 'match')
    if c == 'match':
        return t
    c = float(c)
    if c >= project_fps:
        return t
    return math.floor(t * c + 1e-9) / c


# ----------------------------------------------------------------------------------------------
# Animated channels of one actor at design time t.

REVEAL = ('opacity', 'wipe', 'progress', 'vscale')  # multiplicative, rest value 1 (vscale: collapse to the vertical centre)
OFFSET = ('dx', 'dy')                     # additive, rest value 0
SCALE = ('scale',)                        # multiplicative, rest value 1


def blocks(v):
    """`in` / `out` is one timed block or a list of them (one curve per channel group)."""
    if not v:
        return []
    return list(v) if isinstance(v, list) else [v]


def channels(actor: dict, t: float, clock=None) -> dict:
    """`clock(actor_id, channel)` resolves blocks timed by another actor's channel instead of t
    (`"clock": {"actor": id, "channel": "wipe"}`: Stage07's answer tag eases on the block's reveal)."""
    ch = {'opacity': 1.0, 'wipe': 1.0, 'progress': 1.0, 'dx': 0.0, 'dy': 0.0, 'scale': 1.0, 'vscale': 1.0}
    for blk_in in blocks(actor.get('in')):
        _apply_in(ch, actor, blk_in, _clock_t(blk_in, t, clock))
    for blk_out in blocks(actor.get('out')):
        _apply_out(ch, actor, blk_out, _clock_t(blk_out, t, clock))
    return ch


def _clock_t(blk, t, clock):
    c = blk.get('clock') if blk else None
    if not c:
        return t
    if clock is None:
        raise SceneError('a clock-timed block needs the scene (pil1x only)')
    return clock(c['actor'], c['channel'])


def _apply_in(ch, actor, blk_in, t):
    if blk_in:
        p = phase(t, float(blk_in['start']), float(blk_in.get('dur', 0)), blk_in.get('ease'), float(blk_in.get('delay', 0)))
        frm = blk_in.get('from', {'opacity': 0})
        for k, v in frm.items():
            if k in REVEAL or k in SCALE:
                ch[k] *= v + (1 - v) * p
            elif k in OFFSET:
                ch[k] += v * (1 - p)
            else:
                raise SceneError(f'{actor["id"]}: unknown channel {k!r}')
        if 'arc' in blk_in:  # Stage11 Arc Resolve: a lift of arc·sin(πp)·(1 − p) on the way in
            ch['dy'] += float(blk_in['arc']) * math.sin(math.pi * p) * (1 - p)


def _apply_out(ch, actor, blk_out, t):
    if blk_out:
        p = phase(t, float(blk_out['start']), float(blk_out.get('dur', 0)), blk_out.get('ease'), float(blk_out.get('delay', 0)))
        to = blk_out.get('to', {'opacity': 0})
        for k, v in to.items():
            if k in REVEAL or k in SCALE:
                ch[k] *= 1 - (1 - v) * p
            elif k in OFFSET:
                ch[k] += v * p
            else:
                raise SceneError(f'{actor["id"]}: unknown channel {k!r}')


def legacy_round(v: float) -> int:
    """Python's round(): half to even. The approved scripts snap offsets this way."""
    return int(round(v))
