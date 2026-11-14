"""Convert the handoff's title designs to installable Dafin title templates (docs/template-authoring.md).

Changes only what Dafin's validator rejects or the master prompt asks for: chatgpt. id prefix,
`content`->`text` and `fontFamily`->`font.family` bindings, Liberation Mono -> Cousine (same design,
same metrics), the unknown top-level `timing` block removed. Titles whose font Dafin cannot ship
are skipped."""
import glob, json, os, re, sys
H, OUT, FONTDIR = sys.argv[1], sys.argv[2], sys.argv[3]
ESSENTIALS = {'Inter', 'Montserrat', 'Poppins', 'DM Sans', 'Outfit', 'Plus Jakarta Sans', 'League Spartan', 'Ubuntu',
              'Anton', 'Archivo Black', 'Bebas Neue', 'Oswald', 'Rubik One', 'Fredoka', 'Pacifico',
              'Playfair Display', 'Bodoni Moda', 'Cormorant Garamond', 'Cinzel'}
SHIPPED = {json.load(open(f))['family'] for f in glob.glob(os.path.join(FONTDIR, '*/family.json'))}
MAP = {'Liberation Mono': 'Cousine'}
CAP200 = set(os.environ.get('CAP200', '').split(','))
SKIP = set(sys.argv[4].split(',')) if len(sys.argv) > 4 and sys.argv[4] else set()
files = sorted(set(glob.glob(H + '/packs/v*/0*title*/*.json') + glob.glob(H + '/packs/v*/0*_native_kinetic_titles/*.json')))
done, skipped = [], []
for f in files:
    d = json.load(open(f))
    font = MAP.get(d['style']['fontFamily'], d['style']['fontFamily'])
    new_id = 'chatgpt.' + re.sub(r'^dafin[.-]', '', d['id'])
    if new_id in SKIP:
        skipped.append((d['id'], 'fails a Dafin check')); continue
    if font not in ESSENTIALS and font not in SHIPPED:
        skipped.append((d['id'], 'font ' + d['style']['fontFamily'])); continue
    d['id'] = new_id
    d['style']['fontFamily'] = font
    req = d.setdefault('requires', {})
    req['fonts'] = [MAP.get(x, x) for x in req.get('fonts', [])] or [font]
    if not req.get('effects'):
        req.pop('effects', None)
    # the author's readability cap, written where Dafin reads it (style.animation.timing.maxReadMs)
    # Dafin's own rule (tst_core): readable within 200 ms on a 2 s clip, captions 250 ms
    limit = 250 if d.get('kind') == 'caption' else 200
    cap = min((d.get('timing') or {}).get('maxTimeToReadMs') or limit, limit)
    if cap and isinstance(d['style'].get('animation'), dict):
        d['style']['animation'].setdefault('timing', {})['maxReadMs'] = cap
    for k in ('timing', 'previewContext', 'fontProvenance'):
        d.pop(k, None)  # not template fields: Dafin ignores them with a warning
    if d.get('kind') == 'caption':  # captions take their words from the cues: a text control does nothing
        d['controls'] = [c for c in d.get('controls', []) if c['bind'] != 'content']
    for c in d.get('controls', []):
        c['bind'] = {'content': 'text', 'fontFamily': 'font.family'}.get(c['bind'], c['bind'])
    name = re.sub(r'[^a-z0-9-]+', '-', new_id.split('.', 1)[1].lower()).strip('-') + '.json'
    assert not os.path.exists(os.path.join(OUT, name)) or name in [x[1] for x in done], name
    json.dump(d, open(os.path.join(OUT, name), 'w'), indent=2, ensure_ascii=False)
    open(os.path.join(OUT, name), 'a').write('\n')
    done.append((new_id, name, font))
print('converted', len(done), 'skipped', len(skipped))
for s in skipped: print('  skip', *s)
json.dump({'converted': done, 'skipped': skipped}, open('/home/claude/work/titles_converted.json', 'w'), indent=1)
