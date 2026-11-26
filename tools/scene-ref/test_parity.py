#!/usr/bin/env python3
"""Parity of the ported scenes with the approved scripts that made them, frame by frame, before
any encoding. Needs the scripts from the design archive (not in the repo):

  python3 -I test_parity.py --stage15 <.../Stage15_InstructionalTitles_Delta_v6.1.6/source> \
                            --stage03b <.../Stage03b_Alternative_LowerThird_Families_v1/source>

                            [--stage13 <folder with frozen_stage13_approved_renderer_reference.py>]
                            [--stage14 <.../Stage14_TutorialUtility_Delta_v6.1.2/source>]
                            [--stage13c <folder with render_stage13C_universal_shortcut.py>]

Exit code 0 only if every compared frame is identical. Skips an example whose script is not given.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import render  # noqa: E402
import scene_core as core  # noqa: E402


def load_module(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, str(path))
    m = importlib.util.module_from_spec(spec)
    sys.modules[name] = m  # Stage13C's dataclasses look their module up
    spec.loader.exec_module(m)
    return m


def diff(a, b):
    d = np.abs(a.astype(np.int16) - b.astype(np.int16))
    return int(d.max()), int((d > 0).sum())


def check_direct(name, draw, frames):
    """`draw(i)` = the original script's opaque frame i; the port's direct frame must equal it."""
    job = render.Job(core.load_scene(HERE / 'scenes' / f'{name}.scene.json'))
    bad = []
    for i in frames:
        mx, n = diff(draw(i), job.direct_frame(i))
        if mx:
            bad.append({'frame': i, 'max': mx, 'pixels': n})
    return {'example': name, 'frames': len(frames), 'mismatched': bad}


def check_stage15(src: Path, frames):
    m = load_module(src / 'render_stage15.py', 'stage15_original')
    return [check_direct(name, lambda i, f=fn: m.frame(f, i / 25), frames)
            for name, fn in (('15_A_Step_Focus', m.scene_a), ('15_B_Definition_Card', m.scene_b), ('15_C_Pro_Tip', m.scene_c))]


def check_stage14(src: Path, frames):
    m = load_module(src / 'render_stage14.py', 'stage14_original')
    return [check_direct(name, lambda i, f=fn: m.frame(f, i / 25), frames)
            for name, fn in (('14_A_Menu_Path', m.scene_a), ('14_B_Setting_Change', m.scene_b))]


def check_stage13_scenes(src: Path, frames):
    b = load_module(src / 'frozen_stage13_approved_renderer_reference.py', 'stage13_scenes')
    return [check_direct(name, lambda i, f=fn: b.frame(f, i / 25), frames)
            for name, fn in (('13_A_Shortcut_Keys', b.scene_a), ('13_B_Source_Credit', b.scene_b),
                             ('13_C_Editorial_Checklist', b.scene_c))]


def check_stage13c(src: Path, frames):
    """Folder with render_stage13C_universal_shortcut.py and its original_render_stage13_v6_0_4_FROZEN_COPY.py."""
    m = load_module(src / 'render_stage13C_universal_shortcut.py', 'stage13c_original')
    out = []
    for case in ('U1_Universal_Horizontal_Copy', 'U2_Universal_Vertical_Paste', 'U3_Mac_Dual_Names_Force_Quit'):
        u = m.UniversalScene(case)
        out.append(check_direct('13C_' + case, lambda i, u=u: u.frame(i / 25), frames))
    return out


def check_layer(name, draw, frames):
    """Stage03b: `draw(t)` = the script's RGBA overlay layer; the port's alpha frame must equal it
    inside the overlay rectangle and be empty outside."""
    sc = core.load_scene(HERE / 'scenes' / f'{name}.scene.json')
    job = render.Job(sc)
    x, y, w, h = sc.raw['preview']['legacy']['overlayRect']
    fps = sc.design_fps
    bad = []
    for i in frames(fps):
        a = np.array(draw(i / fps))
        b = job.alpha_frame(i)
        outside = b.copy(); outside[y:y + h, x:x + w] = 0
        mx, n = diff(a, b[y:y + h, x:x + w])
        if mx or outside[:, :, 3].any():
            bad.append({'frame': i, 'max': mx, 'pixels': n, 'inkOutsideLayer': bool(outside[:, :, 3].any())})
    return {'example': name, 'frames': len(list(frames(fps))), 'mismatched': bad}


def safe_clip(m):
    """render_60fps.py / render_C_typography.py skip a text whose clip rectangle is empty."""
    orig = m.colored

    def colored(*args, **kw):
        clip = kw.get('clip')
        if clip and (clip[2] <= clip[0] or clip[3] <= clip[1]):
            return
        return orig(*args, **kw)
    m.colored = colored
    return m


def check_stage03b(src: Path, frames):
    m = load_module(src / 'render.py', 'stage03b_original')
    m60 = safe_clip(load_module(src / 'render.py', 'stage03b_60fps'))
    d = m.D['16x9']
    return [check_layer('03b_LT_A_25', lambda t: m.design_a(d, t), frames),
            check_layer('03b_LT_B_25', lambda t: m.design_b(d, t), frames),
            check_layer('03b_LT_A_60', lambda t: m60.design_a(d, t), frames),
            check_layer('03b_LT_B_60', lambda t: m60.design_b(d, t), frames)]


def check_stage03b_crev(src: Path, frames):
    """Folder with C_25fps_refined_source.py (the C revision's choreography); the font revision
    swaps in Noto Sans Display SemiCondensed ExtraBold/Medium and Noto Sans Display Medium."""
    m = safe_clip(load_module(src / 'C_25fps_refined_source.py', 'stage03b_c_rev'))
    noto = '/usr/share/fonts/truetype/noto/'
    m.F.update({'bold': noto + 'NotoSansDisplay-SemiCondensedExtraBold.ttf',
                'regular': noto + 'NotoSansDisplay-SemiCondensedMedium.ttf', 'medium': noto + 'NotoSansDisplay-Medium.ttf'})
    d = m.D['16x9']
    return [check_layer(n, lambda t: m.design_c(d, t), frames) for n in ('03b_LT_C_25_REV', '03b_LT_C_60_REV')]


def plate_adapter(script: Path, name: str):
    """Part5 Stage05/06 read a photo plate (Dafin_clean_source_check.png) that is not in the
    archives. TEST-ONLY adapter: the script is loaded with a generated plate instead; both sides
    then draw over the same base."""
    import tempfile
    from PIL import Image
    tmp = Path(tempfile.mkdtemp())
    y, x = np.mgrid[0:1080, 0:1920]
    plate = np.dstack([(40 + x / 1920 * 90 + y / 1080 * 30) % 256, (110 + np.sin(x / 90) * 30) % 256, (70 + y / 1080 * 80) % 256])
    Image.fromarray(plate.astype(np.uint8)).save(tmp / 'plate.png')
    code = script.read_text().replace("SOURCE=Path('/mnt/data/Dafin_clean_source_check.png')", f"SOURCE=Path({str(tmp / 'plate.png')!r})")
    (tmp / f'{name}.py').write_text(code)
    return load_module(tmp / f'{name}.py', name)


def check_pil(m, pairs, frames):
    out = []
    for name, fn in pairs:
        r = render.Pil1x(core.load_scene(HERE / 'scenes' / f'{name}.scene.json'))
        bad = []
        for i in frames:
            mx, n = diff(np.array(fn(i / 25)), np.array(r.draw(i / 25, m.BASE)))
            if mx:
                bad.append({'frame': i, 'max': mx, 'pixels': n})
        out.append({'example': name, 'frames': len(frames), 'mismatched': bad})
    return out


def check_stage05(src: Path, frames):
    m = plate_adapter(src / 'render_stage05.py', 'stage05_adapter')
    return check_pil(m, (('05_TutorialStep', m.a_frame), ('05_PrecisionCallout', m.b_frame), ('05_PullQuote', m.c_frame)), frames)


def check_stage06(src: Path, frames):
    m = plate_adapter(src / 'render_stage06.py', 'stage06_adapter')
    return check_pil(m, (('06_Metric', m.frame_a), ('06_Location', m.frame_c)), frames)


def check_stage07(src: Path, frames):
    """Part5 Stage07 (pil1x) through the plate adapter. Kinetic is frame_a as rendered; Numbered
    Insights v2 and Question & Answer v3 are frame_b/frame_c with the typography passes'
    fonts (render_typography_v2.py, then render_targeted_fixes_v3.py), set here as those scripts
    set them. The frame's matte is the port's base layer; its footage crops are the test start."""
    from PIL import Image, ImageFont
    s7 = plate_adapter(next(src.glob('*render_stage07.py')), 'stage07_adapter')
    PT, f = '/usr/share/fonts/truetype/paratype/', ImageFont.truetype
    s7.B_S.update({'eyebrow': s7.spr('DE ȚINUT MINTE', f(PT + 'PTS75F.ttf', 26), s7.TEAL),
                   'title': s7.spr('3 PRINCIPII', f('/usr/share/fonts/opentype/roboto/slab/RobotoSlab-Bold.otf', 80), s7.PAPER),
                   'sub': s7.spr('Un ghid scurt, fără complicații.', f(PT + 'PTC55F.ttf', 28), (204, 213, 213, 255)),
                   'idxs': [s7.spr(x, f(PT + 'PTC55F.ttf', 28), (153, 195, 195, 255)) for x in ('01', '02', '03')],
                   'rows': [s7.spr(x, f(PT + 'PTS75F.ttf', 62), s7.PAPER) for x in ('CLARITATE', 'RITM', 'CONSECVENȚĂ')]})
    s7.C_S.update({'eyebrow': s7.spr('O ÎNTREBARE ESENȚIALĂ', f(PT + 'PTC75F.ttf', 27), s7.GOLD),
                   'answer_tag': s7.spr('RĂSPUNSUL', f(PT + 'PTC55F.ttf', 27), (27, 31, 33, 255)),
                   'sub': s7.spr('Dincolo de prima impresie.', f(PT + 'PTC55F.ttf', 27), (186, 194, 193, 255)),
                   'line1': s7.spr('CUM FACEM', f(PT + 'PTF55F.ttf', 130), s7.PAPER),
                   'line2': s7.spr('SĂ CONTEZE?', f(PT + 'PTF55F.ttf', 130), s7.PAPER),
                   'answer': s7.spr('PRIN DETALII.', f(PT + 'PTF75F.ttf', 74), s7.PAPER)})
    W, H = 1920, 1080
    a0 = Image.new('RGBA', (W, H), (17, 23, 28, 255)); a0.alpha_composite(s7.photo.crop((520, 0, 1420, 1080)).convert('RGBA'), (1020, 0))
    b0 = Image.new('RGBA', (W, H), (15, 21, 27, 255)); b0.alpha_composite(s7.photo.crop((446, 0, 1496, 1080)).convert('RGBA'), (0, 0))
    c0 = Image.new('RGBA', (W, H), (20, 22, 25, 255))
    out = []
    for name, fn, start in (('07_Kinetic', s7.frame_a, a0), ('07_Insights_v2', s7.frame_b, b0), ('07_QA_v3', s7.frame_c, c0)):
        r = render.Pil1x(core.load_scene(HERE / 'scenes' / f'{name}.scene.json'))
        bad = []
        for i in frames:  # the proofs were encoded from RGB: compare RGB
            mx, n = diff(np.array(fn(i / 25).convert('RGB')), np.array(r.draw(i / 25, start).convert('RGB')))
            if mx:
                bad.append({'frame': i, 'max': mx, 'pixels': n})
        out.append({'example': name, 'frames': len(frames), 'mismatched': bad})
    return out


def check_stage08_cinematic(src: Path, frames):
    """Stage08 Cinematic Statement, four typefaces: overlay_fast on the script's own charcoal BG."""
    m = load_module(src / 'render_font_alternatives.py', 'stage08_cinematic')
    out = []
    for k, key in (('Garamond', 'A_EB_Garamond_Regular'), ('Inter', 'B_Inter_Display_Medium'),
                   ('RobotoSlab', 'C_Roboto_Slab_Bold'), ('Lato', 'D_Lato_Heavy')):
        spr, _ = m.glyph_sprite(m.CHOSEN[key])
        name = '08_Cinematic_' + k
        r = render.Pil1x(core.load_scene(HERE / 'scenes' / f'{name}.scene.json'))
        bad = []
        for i in frames:
            mx, n = diff(m.overlay_fast(m.BACKGROUND, spr, i / 25)[:, :, ::-1], np.array(r.draw(i / 25, m.BG).convert('RGB')))
            if mx:
                bad.append({'frame': i, 'max': mx, 'pixels': n})
        out.append({'example': name, 'frames': len(frames), 'mismatched': bad})
    return out


def check_dark_chapter_a(src: Path, frames):
    """Editorial Chapter A Dark v2 over the script's own backdrop. TEST-ONLY adapter: its QA output
    folder is moved to a temporary one (the script writes text_bounds.json on import)."""
    import tempfile
    tmp = Path(tempfile.mkdtemp())
    code = (src / 'render_a_dark_v2.py').read_text().replace(
        "BASE=Path('/mnt/data/Dafin_Chapter_A_Dark_v2_Corrected')", f"BASE=Path({str(tmp)!r})")
    (tmp / 'render_a_dark_v2_adapter.py').write_text(code)
    m = load_module(tmp / 'render_a_dark_v2_adapter.py', 'dark_a_v2')
    r = render.Pil1x(core.load_scene(HERE / 'scenes' / '04_Dark_Chapter_A_v2.scene.json'))
    bad = []
    for i in frames:
        mx, n = diff(np.array(m.draw_frame(i / 25).convert('RGB')), np.array(r.draw(i / 25, m.BASE_IMAGE).convert('RGB')))
        if mx:
            bad.append({'frame': i, 'max': mx, 'pixels': n})
    return [{'example': '04_Dark_Chapter_A_v2', 'frames': len(frames), 'mismatched': bad}]


def check_stage13_primitives(src: Path):
    """Shapes the two ports do not use, against the frozen Stage13 helpers (and Stage15's plate)."""
    b = load_module(src / 'frozen_stage13_approved_renderer_reference.py', 'stage13_frozen')
    s15 = load_module(src / 'render_stage15.py', 'stage15_original') if (src / 'render_stage15.py').exists() else None
    OFF, AQUA = list(b.OFF), list(b.AQUA)
    raw = {'format': core.FORMAT_ID, 'version': 1, 'id': 'prims', 'canvas': {'width': 1920, 'height': 1080},
           'timing': {'duration': 6, 'fps': 25}, 'render': {'profile': 'sprite2x'},
           'fonts': {'semi': {'file': Path(b.FONT_SEMI).name}},
           'actors': [
               {'id': 'key', 'type': 'group', 'flatten': True, 'at': [347, 568], 'size': [213, 107], 'children': [
                   {'id': 'cap', 'type': 'roundRect', 'box': [2, 2, 210, 104], 'radius': 14, 'fill': [23, 32, 39, 244],
                    'strokeColor': AQUA + [255], 'stroke': 2},
                   {'id': 'label', 'type': 'text', 'text': 'CTRL', 'font': 'semi', 'size': 43, 'color': OFF,
                    'align': {'h': 'center', 'v': 'center', 'dy': -2}}]},
               {'id': 'ring', 'type': 'ring', 'box': [402, 440, 452, 490], 'color': AQUA, 'stroke': 2},
               {'id': 'plate', 'type': 'group', 'flatten': True, 'at': [382, 703], 'size': [1112, 217], 'children': [
                   {'id': 'body', 'type': 'roundRect', 'box': [2, 2, 1109, 214], 'radius': 22, 'fill': [27, 36, 44, 245],
                    'strokeColor': [80, 99, 107, 232], 'stroke': 2},
                   {'id': 'lip', 'type': 'line', 'from': [28, 8], 'to': [1084, 8], 'color': [171, 188, 190], 'alpha': 52, 'width': 2}]},
               {'id': 'check', 'type': 'route', 'points': [[414, 464], [424, 473], [440, 454]], 'color': AQUA, 'width': 4,
                'in': {'start': 1, 'dur': 0.38, 'from': {'progress': 0}}},
           ]}
    sc = core.load_scene(raw)
    r = render.Sprite2x(sc)
    by = {a['id']: a for a in sc.actors}
    res = {}
    pairs = [('key', r.group_sprite(by['key'], [by['cap'], by['label']]), b.keycap_sprite('CTRL', 213)),
             ('ring', r.shape_sprite(by['ring'])[0], b.check_ring_sprite())]
    if s15:
        pairs.append(('plate', r.group_sprite(by['plate'], [by['body'], by['lip']]), s15.rounded_plate(1112, 217)))
    for name, mine, theirs in pairs:
        res[name] = bool(np.array_equal(mine.rgb, theirs.rgb) and np.array_equal(mine.alpha, theirs.alpha))
    ok = True
    for t in (1.0, 1.1, 1.2, 1.3, 1.5):
        c1 = b.BASE.copy(); b.route(c1, [(414, 464), (424, 473), (440, 454)], b.enter(t, 1, .38), b.AQUA, 4)
        c2 = b.BASE.copy(); r._draw(by['check'], core.channels(by['check'], t), c2, None)
        ok &= bool(np.array_equal(c1, c2))
    res['route'] = ok
    return {'example': 'stage13 primitives', 'frames': 0, 'mismatched': [k for k, v in res.items() if not v], 'checked': sorted(res)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--stage15', type=Path)
    ap.add_argument('--stage03b', type=Path)
    ap.add_argument('--stage13', type=Path, help='folder with frozen_stage13_approved_renderer_reference.py')
    ap.add_argument('--stage03b-crev', type=Path, help='folder with C_25fps_refined_source.py')
    ap.add_argument('--stage05', type=Path, help='folder with render_stage05.py')
    ap.add_argument('--stage06', type=Path, help='folder with render_stage06.py')
    ap.add_argument('--stage07', type=Path, help='folder with render_stage07.py')
    ap.add_argument('--dark-chapter-a', type=Path, help='folder with render_a_dark_v2.py')
    ap.add_argument('--stage08-cinematic', type=Path, help='folder with render_font_alternatives.py')
    ap.add_argument('--stage14', type=Path, help='Stage14_TutorialUtility_Delta_v6.1.2/source')
    ap.add_argument('--stage13c', type=Path, help='folder with render_stage13C_universal_shortcut.py + its frozen copy')
    ap.add_argument('--frames', default='all')
    o = ap.parse_args()
    frames = list(range(150)) if o.frames == 'all' else [int(v) for v in o.frames.split(',')]

    def fps_frames(fps):  # the 60 fps proofs have 360 frames
        return range(int(round(6 * fps))) if o.frames == 'all' else frames
    res = []
    if o.stage15:
        res.extend(check_stage15(o.stage15, frames))
    if o.stage14:
        res.extend(check_stage14(o.stage14, frames))
    if o.stage05:
        res.extend(check_stage05(o.stage05, frames))
    if o.stage06:
        res.extend(check_stage06(o.stage06, frames))
    if o.stage07:
        res.extend(check_stage07(o.stage07, frames))
    if o.dark_chapter_a:
        res.extend(check_dark_chapter_a(o.dark_chapter_a, frames))
    if o.stage08_cinematic:
        res.extend(check_stage08_cinematic(o.stage08_cinematic, frames))
    if o.stage13c:
        res.extend(check_stage13c(o.stage13c, frames))
    if o.stage03b:
        res.extend(check_stage03b(o.stage03b, fps_frames))
    if o.stage03b_crev:
        res.extend(check_stage03b_crev(o.stage03b_crev, fps_frames))
    if o.stage13:
        res.append(check_stage13_primitives(o.stage13))
        res.extend(check_stage13_scenes(o.stage13, frames))
    print(json.dumps(res, indent=1))
    return 1 if any(r['mismatched'] for r in res) or not res else 0


if __name__ == '__main__':
    sys.exit(main())
