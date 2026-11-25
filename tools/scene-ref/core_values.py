"""Value dump of scene_core for the native test (tests/tst_scenetitle.cpp, coreMatchesReference).

  python3 -I core_values.py <tools/scene-ref> <scene.json> [params JSON] > tests/data/scene-titles/core_*.txt
  python3 -I core_values.py <tools/scene-ref> <scene.json> [params JSON] --geometry \
          > tests/data/scene-titles/geo_*.txt            (keycapsGeometryMatchesReference)
  python3 -I core_values.py <tools/scene-ref> <scene.json> [params JSON] --layout > tests/data/scene-titles/layout_*.txt
  python3 -I core_values.py <tools/scene-ref> <scene.json> [params JSON] --measures

Prints the hold, every resolved actor, time_map at several clip lengths and the channels of every
actor at fixed times, in a fixed text format the native loader reproduces line for line.

--geometry prints instead what phase B of Smart Keycaps (keycaps.layout, docs/scene-keycaps.md)
gives every generated actor, measured with the reference renderer's ink (PIL getbbox, anchor 'ls',
the scene's own font files). The native test lays the same scene out with the painter's ink and
compares: sizes and flags exactly, positions within 0.5 px.
--ink-table FILE also writes every measurement phase B made ([font, px, text, [l, t, r, b]]), so a
port can be checked against the same numbers without any font engine.
--layout prints what render.py's Sprite2x.layout() resolves (fitBox, after, fillGroup; needs the
scene's fonts), in the format of tst_scenetitle's layoutDump (layoutMatchesReference):
  group <id> <x> <y> <w> <h>      every group
  text <id> <pen x> <pen y>       every text with a baseline anchor
  box <id> <x0> <y0> <x1> <y1>    every fillGroup child
--measures prints the PIL measurements the layout used, as JSON (text id -> ink width and advance
at ss 1, 2 and 3), for checking the layout arithmetic without the native font stack.
"""
import sys, json
args = [a for a in sys.argv[1:]]
geometry = '--geometry' in args
ink_table = None
if '--ink-table' in args:
    i = args.index('--ink-table'); ink_table = args[i + 1]; del args[i:i + 2]
flags = {a for a in args if a in ('--layout', '--measures')}  # C10n's measured-layout dumps
args = [a for a in args if a != '--geometry' and a not in flags]
sys.path.insert(0, args[0])
import scene_core as core
sc = core.load_scene(args[1], json.loads(args[2]) if len(args) > 2 and args[2] else None)

if geometry:
    import render
    r = render.Sprite2x.__new__(render.Sprite2x)  # the renderer's ink, without its eager layout
    r.sc, r.font_dirs, r.cache = sc, (), {}
    seen = []
    def ink(font, px, text):
        box = r.ink(font, px, text)
        seen.append([font, int(px), text, list(box)])
        return box
    import keycaps
    keycaps.layout(sc, ink)
    def p(*v): return ','.join('%.6f' % x for x in v)
    for a in sc.actors:
        if '_kc' not in a:
            continue
        t = a['type']
        if t == 'group':
            print('geo %s group at=%s size=%s' % (a['id'], p(*a['at']), p(*a['size'])))
        elif t == 'line':
            print('geo %s line from=%s to=%s' % (a['id'], p(*a['from']), p(*a['to'])))
        elif t == 'text':
            al = a.get('align', {})
            center = al.get('h') == 'center' and al.get('v') == 'center'
            print('geo %s text size=%s baseline=%s center=%d dy=%s hidden=%d' % (
                a['id'], p(a['size']), p(*a['at']['baseline']), center, p(al.get('dy', 0) if center else 0),
                1 if a.get('_hidden') else 0))
        else:
            print('geo %s box box=%s' % (a['id'], p(*a['box'])))
    if ink_table:
        with open(ink_table, 'w', encoding='utf-8') as f:
            json.dump(seen, f, ensure_ascii=False)
    sys.exit(0)

if '--layout' in flags or '--measures' in flags:
    import render
    from PIL import Image, ImageDraw
    r = render.Sprite2x(sc)
    if '--measures' in flags:
        out = {}
        d = ImageDraw.Draw(Image.new('RGBA', (1, 1)))
        for a in sc.actors:
            if a['type'] != 'text':
                continue
            m = {}
            for ss in (1, 2, 3):
                f = r.font(a['font'], a['size'], ss)
                bb = d.textbbox((0, 0), a['text'], font=f)
                m[f'ink{ss}'] = bb[2] - bb[0]
                m[f'len{ss}'] = f.getlength(a['text'])
            out[a['id']] = m
        print(json.dumps(out, indent=1, sort_keys=True))
        sys.exit(0)
    for a in sc.actors:
        if a['type'] == 'group':
            at, size = a.get('at', [0, 0]), a.get('size', [0, 0])
            print("group %s %.3f %.3f %.3f %.3f" % (a['id'], at[0], at[1], size[0], size[1]))
        elif a['type'] == 'text' and 'baseline' in a['at']:
            bx, by = a['at']['baseline']
            print("text %s %.3f %.3f" % (a['id'], bx, by))
        if 'fillGroup' in a:
            g = next(x for x in sc.actors if x['id'] == a['_parent'])
            l, t, rr, b = a['fillGroup']
            print("box %s %.3f %.3f %.3f %.3f" % (a['id'], l, t, g['size'][0] - rr, g['size'][1] - b))
    sys.exit(0)

hs, he = core.scene_events(sc)
print("hold %.9f %.9f" % (hs, he))
for a in sc.actors: print("actor %s %s text=[%s] w=%.3f" % (a['id'], a['type'], a.get('text',''), a.get('width',1.0)))
for D in (6.0, 4.0, 10.0, 2.0):
    f = core.time_map(sc, D)
    for t in (0.0,0.3,0.9,1.6,3.0,4.9,5.5,5.99): print("tm %.2f %.2f %.9f" % (D, t, f(t)))
for a in sc.actors:
    for t in (0.0,0.2,0.4,0.6,0.8,1.0,1.5,2.0,4.7,4.8,5.0,5.2):
        c = core.channels(a, t); print("ch %s %.2f %.9f %.9f %.9f %.9f %.9f" % (a['id'], t, c['opacity'], c['wipe'], c['progress'], c['dx'], c['dy']))
